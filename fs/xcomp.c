/*
 * linux/fs/xcomp.c
 *
 * Copyright (C) 2015 Cyanogen, Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/fs-xcomp.h>
#include <linux/highmem.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/aio.h>
#include <linux/swap.h>
#include <linux/zlib.h>
#include <linux/lz4.h>
#include <linux/sysfs.h>
#include <linux/backing-dev.h>
#include <linux/workqueue.h>

#include <asm/uaccess.h>

#define XCOMP_HDRLEN	12

#define CM_UNKNOWN	0
#define CM_ZLIB		1
#define CM_LZ4		2

/* Header flags */
#define XFLAG_PADDING	0x10

/* Cluster flags: static */
#define ZFLAG_UNCOMP	0x0001

/* Cluster flags: dynamic */
#define ZFLAG_READING	0x0100
#define ZFLAG_DIRTY	0x0200

#define XCOMP_METHOD(param1) ((param1) & 0x0f)
#define XCOMP_FLAGS(param1) ((param1) & 0xf0)

struct cluster {
	loff_t			off;
	size_t			len;
	struct mutex		lock;
	unsigned int		flags;
};

struct cluster_queue_entry {
	struct inode		*inode;
	struct xcomp_inode_info	*info;
	size_t			cidx;
	struct address_space	*mapping;
	struct work_struct	work;
};

static const unsigned char xcomp_magic[] = { 'z', 'z', 'z', 'z' };

static int xcomp_enable = 1;

static struct workqueue_struct *kxblockd_workqueue;

static struct page *find_alloc_page(struct address_space *mapping, pgoff_t index)
{
	struct page *page;
	int ret;

find_page:
	page = find_get_page(mapping, index);
	if (!page) {
		page = page_cache_alloc_cold(mapping);
		if (!page)
			return ERR_PTR(-ENOMEM);
		ret = add_to_page_cache_lru(page, mapping, index, GFP_KERNEL);
		if (ret) {
			page_cache_release(page);
			if (ret == -EEXIST)
				goto find_page;
			return ERR_PTR(ret);
		}
	}
	ClearPageError(page);

	return page;
}

static int wait_page_io_complete(struct page *page)
{
	int ret = 0;

	if (!PageUptodate(page)) {
		ret = lock_page_killable(page);
		if (ret) {
			return ret;
		}
		if (!PageUptodate(page)) {
			if (page->mapping == NULL) {
				unlock_page(page);
				return -EAGAIN;
			}
			unlock_page(page);
			ret = -EIO;
		}
	}

	return ret;
}

static int xcomp_inode_read(struct inode *inode, struct xcomp_inode_info *info,
		loff_t off, void *buf, size_t len)
{
	struct page *page;
	void *pageaddr;
	size_t index;
	size_t pgoff, pgrem;
	int ret = -EINVAL;

	while (len > 0) {
		index = off >> PAGE_SHIFT;
		pgoff = off - (index << PAGE_SHIFT);
		pgrem = min_t(size_t, len, PAGE_SIZE - pgoff);

again:
		page = find_alloc_page(&info->i_mapping, index);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto out;
		}
		ret = info->i_lower_readpage(page);
		if (ret) {
			page_cache_release(page);
			goto out;
		}
		ret = wait_page_io_complete(page);
		if (ret) {
			page_cache_release(page);
			if (ret == -EAGAIN) {
				goto again;
			}
			goto out;
		}

		pageaddr = kmap_atomic(page);
		memcpy(buf, pageaddr + pgoff, pgrem);
		kunmap_atomic(pageaddr);
		page_cache_release(page);

		buf += pgrem;
		off += pgrem;
		len -= pgrem;
	}

	ret = 0;

out:
	return ret;
}

/*** cluster map ***/

static int decompress_zlib(const void *ibuf, size_t ilen,
		void *obuf, size_t olen)
{
	z_stream strm;
	int zret;
	int retval = -EINVAL;

	memset(&strm, 0, sizeof(strm));
	strm.workspace = vmalloc(zlib_inflate_workspacesize());
	if (!strm.workspace) {
		printk(KERN_INFO "%s: failed to alloc workspace\n", __func__);
		return -ENOMEM;
	}

	strm.next_in = ibuf;
	strm.avail_in = ilen;
	strm.total_in = 0;

	strm.next_out = obuf;
	strm.avail_out = olen;
	strm.total_out = 0;

	if (zlib_inflateInit2(&strm, MAX_WBITS) != Z_OK) {
		printk(KERN_INFO "%s: failed to inflateInit2\n", __func__);
		retval = -EINVAL;
		goto out_free;
	}

	zret = zlib_inflate(&strm, Z_FINISH);
	if (zret != Z_STREAM_END) {
		printk(KERN_INFO "%s: zlib_inflate failed with zret=%d\n", __func__, zret);
		retval = -EINVAL;
		goto out_zlib;
	}
	if (strm.total_out != olen) {
		printk(KERN_INFO "%s: zlib_inflate failed to produce output (%u != %u)\n", __func__,
			(unsigned int)strm.total_out, (unsigned int)olen);
		retval = -EINVAL;
		goto out_zlib;
	}

	retval = 0;

out_zlib:
	zlib_inflateEnd(&strm);
out_free:
	vfree(strm.workspace);
	return retval;
}

static int decompress_lz4(const void *ibuf, size_t ilen,
		void *obuf, size_t olen)
{
	int zret;

	zret = lz4_decompress(ibuf, &ilen, obuf, olen);
	if (zret != 0) {
		printk(KERN_INFO "%s: lz4_decompress failed with zret=%d\n", __func__, zret);
		return -EINVAL;
	}

	return 0;
}

static int cluster_decompress(struct cluster *clst, int method,
		unsigned char *ibuf, size_t ilen,
		unsigned char *obuf, size_t olen)
{
	int ret = -EINVAL;
	int comp;
	int retval;
	int (*decompress)(const void *ibuf, size_t ilen, void *obuf, size_t olen);

	comp = !(clst->flags & ZFLAG_UNCOMP);

	switch (method) {
	case CM_ZLIB:
		decompress = decompress_zlib;
		break;
	case CM_LZ4:
		decompress = decompress_lz4;
		break;
	default:
		return -EINVAL;
	}

	if (comp) {
		ret = decompress(ibuf, clst->len, obuf, olen);
		if (ret != 0) {
			printk(KERN_INFO "%s: decompress failed\n", __func__);
			retval = -EINVAL;
			goto out;
		}
	}
	else {
		memcpy(obuf, ibuf, olen);
	}

	retval = 0;

out:
	return retval;
}

static void map_pages(struct address_space *mapping, loff_t off,
		unsigned char *obuf, size_t olen)
{
	size_t pidx, poff, prem;
	struct page *page;
	void *pageaddr;

	while (olen > 0) {
		pidx = off >> PAGE_SHIFT;
		poff = off - (pidx << PAGE_SHIFT);
		prem = min_t(size_t, olen, PAGE_SIZE - poff);
		page = find_get_page(mapping, pidx);
		if (!page)
			page = grab_cache_page(mapping, pidx);
		if (!page)
			goto no_page;

		if (PageUptodate(page))
			goto skip_page;

		pageaddr = kmap_atomic(page);
		if (obuf) {
			memcpy(pageaddr, obuf, prem);
		}
		else {
			SetPageError(page);
			memset(pageaddr, 0, PAGE_SIZE);
		}
		flush_dcache_page(page);
		kunmap_atomic(pageaddr);
		SetPageUptodate(page);
skip_page:
		unlock_page(page);
		page_cache_release(page);

no_page:
		obuf += prem;
		off += prem;
		olen -= prem;
	}
}

static int cluster_map_init(struct inode *inode, struct xcomp_inode_info *info)
{
	loff_t off;
	size_t nr_clusters;
	u32 entlen, clstmaplen;
	unsigned char *clstmap;
	int pad;
	size_t n;
	int ret;

	off = XCOMP_HDRLEN;

	nr_clusters = DIV_ROUND_UP((size_t)(inode->i_size >> PAGE_SHIFT),
				   (size_t)(info->i_cluster_size >> PAGE_SHIFT));

	entlen = info->i_cluster_size > (1 << 16) ? 4 : 2;
	clstmaplen = nr_clusters * entlen;

	info->i_clusters = (struct cluster *)vmalloc(nr_clusters * sizeof(struct cluster));
	if (!info->i_clusters) {
		printk(KERN_INFO "%s: out of memory\n", __func__);
		return -ENOMEM;
	}
	memset(info->i_clusters, 0, nr_clusters * sizeof(struct cluster));

	clstmap = vmalloc(clstmaplen);
	if (!clstmap) {
		printk(KERN_INFO "%s: out of memory\n", __func__);
		vfree(info->i_clusters);
		info->i_clusters = NULL;
		return -ENOMEM;
	}

	if (inode->i_compressed_size < XCOMP_HDRLEN + clstmaplen) {
		printk(KERN_INFO "%s: file too short\n", __func__);
		vfree(clstmap);
		vfree(info->i_clusters);
		info->i_clusters = NULL;
		return -EINVAL;
	}
	off = XCOMP_HDRLEN;
	ret = xcomp_inode_read(inode, info, off, clstmap, clstmaplen);
	if (ret) {
		printk(KERN_INFO "%s: failed read\n", __func__);
		vfree(clstmap);
		vfree(info->i_clusters);
		info->i_clusters = NULL;
		return -EIO;
	}
	off += clstmaplen;
	pad = XCOMP_FLAGS(info->i_param1) & XFLAG_PADDING;
	for (n = 0; n < nr_clusters; ++n) {
		struct cluster *clst = info->i_clusters + n;
		__le32 leval;
		u32 val;

		clst->off = off;
		memset(&leval, 0, sizeof(leval));
		memcpy(&leval, clstmap + entlen * n, entlen);
		val = le32_to_cpu(leval);
		if (val) {
			clst->len = val;
		}
		else {
			clst->len = min_t(size_t,
					 info->i_cluster_size,
					 inode->i_size - (info->i_cluster_size * n));
			clst->flags |= ZFLAG_UNCOMP;
		}
		mutex_init(&clst->lock);

		if (pad) {
			off += round_up(clst->len, 1 << inode->i_blkbits);
		}
		else {
			off += clst->len;
		}
	}
	vfree(clstmap);

	return 0;
}

/*** cluster workqueue ***/

static void cluster_wait(struct work_struct *work)
{
	struct cluster_queue_entry *entry;
	struct inode *inode;
	struct xcomp_inode_info *info;
	size_t cidx;
	struct address_space *mapping;
	struct cluster *clst;
	unsigned char *ibuf = NULL;
	size_t ilen;
	unsigned char *obuf = NULL;
	size_t olen;
	loff_t off;
	size_t len;
	size_t boff;
	struct page *page;
	void *pageaddr;

	entry = container_of(work, struct cluster_queue_entry, work);
	inode = entry->inode;
	info = entry->info;
	cidx = entry->cidx;
	mapping = entry->mapping;
	vfree(entry);

	clst = info->i_clusters + cidx;
	BUG_ON(!(clst->flags & ZFLAG_READING));

	ilen = clst->len;
	olen = min_t(size_t, info->i_cluster_size, inode->i_size - (info->i_cluster_size * cidx));

	ibuf = vmalloc(ilen);
	/* Failure to alloc will be noticed below */

	off = clst->off;
	len = clst->len;
	boff = 0;
	while (len > 0) {
		size_t pidx = off >> PAGE_SHIFT;
		loff_t poff = off - (pidx << PAGE_SHIFT);
		size_t prem = min_t(size_t, len, PAGE_SIZE - poff);
		int err;
		page = find_get_page(&info->i_mapping, pidx);
		if (!page) {
			goto out_err;
		}
		page_cache_release(page); /* Release ref from cluster_read() */
		err = wait_page_io_complete(page);
		if (err) {
			page_cache_release(page);
			goto out_err;
		}

		/* XXX: For PageError, is the page uptodate or not? */
		pageaddr = kmap_atomic(page);
		memcpy(ibuf + boff, pageaddr + poff, prem);
		kunmap_atomic(pageaddr);
		unlock_page(page);
		page_cache_release(page);

		boff += prem;
		off += prem;
		len -= prem;
	}

	if (!ibuf)
		goto out_err;

	obuf = vmalloc(olen);
	if (!obuf)
		goto out_err;

	cluster_decompress(clst, XCOMP_METHOD(info->i_param1), ibuf, ilen, obuf, olen);

	goto out;

out_err:
	printk(KERN_INFO "%s: out of memory or lower page error\n", __func__);
	vfree(obuf);
	obuf = NULL;

out:
	mutex_lock(&clst->lock);
	clst->flags &= ~ZFLAG_READING;
	mutex_unlock(&clst->lock);
	map_pages(mapping, cidx * info->i_cluster_size, obuf, olen);
	vfree(obuf);
	vfree(ibuf);
}

static int cluster_read(struct inode *inode, struct xcomp_inode_info *info, size_t cidx, struct address_space *mapping)
{
	int ret = -EINVAL;
	struct cluster *clst = info->i_clusters + cidx;
	loff_t off;
	size_t len;
	struct cluster_queue_entry *entry;

	mutex_lock(&clst->lock);
	if (clst->flags & ZFLAG_READING) {
		mutex_unlock(&clst->lock);
		return 0;
	}
	clst->flags |= ZFLAG_READING;
	mutex_unlock(&clst->lock);

	off = clst->off;
	len = clst->len;
	while (len > 0) {
		size_t index = off >> PAGE_SHIFT;
		size_t page_off = off - (index << PAGE_SHIFT);
		size_t page_rem = min_t(size_t, len, PAGE_SIZE - page_off);
		struct page *page;

		page = find_alloc_page(&info->i_mapping, index);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto out;
		}
		ret = info->i_lower_readpage(page);
		if (ret) {
			page_cache_release(page);
			goto out;
		}

		off += page_rem;
		len -= page_rem;
	}

	entry = (struct cluster_queue_entry *)vmalloc(sizeof(struct cluster_queue_entry));
	if (!entry) {
		printk(KERN_INFO "%s: out of memory\n", __func__);
		ret = -ENOMEM;
		goto out;
	}
	entry->inode = inode;
	entry->info = info;
	entry->cidx = cidx;
	entry->mapping = mapping;
	INIT_WORK(&entry->work, cluster_wait);
	schedule_work(&entry->work);

	ret = 0;

out:
	return ret;
}

int xcomp_readpage(struct xcomp_inode_info *info, struct page *page)
{
	struct inode *inode = page->mapping->host;
	size_t ppc;
	size_t cidx;
	void *pageaddr;

	if (page->index * PAGE_SIZE >= inode->i_size) {
		pageaddr = kmap_atomic(page);
		memset(pageaddr, 0, PAGE_SIZE);
		kunmap_atomic(pageaddr);
		flush_dcache_page(page);
		SetPageUptodate(page);
		unlock_page(page);
		return 0;
	}

	ppc = info->i_cluster_size / PAGE_SIZE;
	cidx = page->index / ppc;

	return cluster_read(inode, info, cidx, page->mapping);
}
EXPORT_SYMBOL_GPL(xcomp_readpage);

/*
 * VFS readahead is disabled for now, as decompressing clusters provide
 * automatic readahead.  Performance impact should be analyzed.
 */
int xcomp_readpages(struct xcomp_inode_info *info, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	return 0;
}
EXPORT_SYMBOL_GPL(xcomp_readpages);

/*** xcomp api ***/

int xcomp_enabled(void)
{
	return xcomp_enable;
}
EXPORT_SYMBOL_GPL(xcomp_enabled);

int xcomp_inode_info_init(struct inode *inode,
		struct xcomp_inode_info *info, readpage_t lower_readpage)
{
	loff_t off;
	unsigned char hdr[XCOMP_HDRLEN];
	__le64 orig_size;
	size_t nr_clusters;
	u32 entlen, clstmaplen;
	int ret;

	memset(info, 0, sizeof(struct xcomp_inode_info));

	info->i_lower_readpage = lower_readpage;
	address_space_init_once(&info->i_mapping);
	info->i_mapping.a_ops = &empty_aops;
	info->i_mapping.host = inode;
	mapping_set_gfp_mask(&info->i_mapping, GFP_HIGHUSER_MOVABLE);
	info->i_mapping.backing_dev_info = &default_backing_dev_info;

	if (inode->i_compressed_size < sizeof(hdr)) {
		printk(KERN_INFO "%s: file too short\n", __func__);
		return -EINVAL;
	}

	ret = xcomp_inode_read(inode, info, 0, hdr, sizeof(hdr));
	if (ret) {
		printk(KERN_INFO "%s: failed to read header\n", __func__);
		return ret;
	}
	off = XCOMP_HDRLEN;
	if (memcmp(hdr, xcomp_magic, 4) != 0) {
		printk(KERN_INFO "%s: bad magic\n", __func__);
		return -EINVAL;
	}
	memset(&orig_size, 0, sizeof(orig_size));
	memcpy(&orig_size, hdr+6, 6);

	inode->i_size = le64_to_cpu(orig_size);

	info->i_param1 = hdr[4];
	info->i_cluster_size = 1 << hdr[5];

	nr_clusters = DIV_ROUND_UP(inode->i_size, info->i_cluster_size);

	entlen = info->i_cluster_size > (1 << 16) ? 4 : 2;
	clstmaplen = nr_clusters * entlen;

	ret = cluster_map_init(inode, info);
	if (ret) {
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xcomp_inode_info_init);

int xcomp_inode_info_free(struct xcomp_inode_info *info)
{
	if (info->i_mapping.nrpages)
		truncate_inode_pages(&info->i_mapping, 0);
	might_sleep();
	spin_lock_irq(&info->i_mapping.tree_lock);
	BUG_ON(info->i_mapping.nrpages);
	spin_unlock_irq(&info->i_mapping.tree_lock);
	BUG_ON(!list_empty(&info->i_mapping.private_list));

	vfree(info->i_clusters);
	memset(info, 0, sizeof(struct xcomp_inode_info));
	return 0;
}
EXPORT_SYMBOL_GPL(xcomp_inode_info_free);

/*** sysfs ***/

static struct kobject *xcomp_kobj;

static ssize_t enable_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", xcomp_enable);
}

static ssize_t enable_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t len)
{
	int rc;
	unsigned long val;

	rc = strict_strtoul(buf, 10, &val);
	if ((rc < 0) || (val > 1))
		return -EIO;

	xcomp_enable = val;

	return len;
}

static struct kobj_attribute enable_attr =
	__ATTR(enable, S_IRUGO|S_IWUSR, enable_show, enable_store);

static struct attribute *attributes[] = {
	&enable_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attributes,
};

/*** init ***/

static int __init xcomp_init(void)
{
	int rc;

	kxblockd_workqueue = alloc_workqueue("kxcompd", WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	if (!kxblockd_workqueue) {
		printk(KERN_ERR "Unable to create xcomp workqueue\n");
		rc = -ENOMEM;
		goto out;
	}

	xcomp_kobj = kobject_create_and_add("xcomp", fs_kobj);
	if (!xcomp_kobj) {
		printk(KERN_ERR "Unable to register with sysfs\n");
		rc = -ENOMEM;
		goto out;
	}
	rc = sysfs_create_group(xcomp_kobj, &attr_group);
	if (rc) {
		printk(KERN_ERR "Unable to create xcomp sysfs attributes\n");
		goto out_put;
	}

	return 0;

out_put:
	kobject_put(xcomp_kobj);
out:
	return rc;
}

static void __exit xcomp_exit(void)
{
	sysfs_remove_group(xcomp_kobj, &attr_group);
	kobject_put(xcomp_kobj);
	destroy_workqueue(kxblockd_workqueue);
}

MODULE_AUTHOR("Tom Marshall <tdm@cyngn.com>");
MODULE_DESCRIPTION("xcomp");
MODULE_LICENSE("GPL");

module_init(xcomp_init);
module_exit(xcomp_exit);
