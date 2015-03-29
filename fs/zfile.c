/*
 * Copyright (C) 2015 Cyanogen, Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/aio.h>
#include <linux/swap.h>
#include <linux/zlib.h>
#include <linux/lz4.h>
#include <linux/sysfs.h>

#include <linux/zfile.h>

#include <asm/uaccess.h>

#define CM_UNKNOWN	0
#define CM_ZLIB		1
#define CM_GZIP		2
#define CM_LZ4		3

static int zfile_enable = 1;

struct zdata {
	unsigned int		z_refcount;
	struct list_head	z_node;
	char			*z_filename;
	int			z_method;
	loff_t			z_size;
	int			z_dirty;
	void			**z_map;
	size_t			z_mapsize;
};
struct list_head z_data = LIST_HEAD_INIT(z_data);
DEFINE_MUTEX(zdata_lock);

struct zinfo {
	struct list_head		z_node;
	struct zdata			*z_data;
	const struct file_operations	*z_orig_fop;
	void				*z_orig_priv;
};
struct list_head z_info = LIST_HEAD_INIT(z_info);
DEFINE_MUTEX(zinfo_lock);

static ssize_t kernel_write(struct file *file, const char *buf, size_t count,
			loff_t pos)
{
	mm_segment_t old_fs;
	ssize_t res;

	old_fs = get_fs();
	set_fs(get_ds());
	/* The cast to a user pointer is valid due to the set_fs() */
	res = vfs_write(file, (__force const char __user *)buf, count, &pos);
	set_fs(old_fs);

	return res;
}

static int zfile_get_xattr_if_exists(struct inode *inode, struct dentry *dentry,
		const char* name, void *buf, size_t len)
{
	if (!inode || !inode->i_op || !inode->i_op->getxattr)
		return -ENOENT;

	if (!dentry)
		return -ENOENT;

	memset(buf, 0, len);
	return inode->i_op->getxattr(dentry, name, buf, len);
}

static int zfile_realsize(struct file *file, loff_t *off)
{
	char buf[32];
	int rc;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct dentry *dentry = file->f_dentry;

	rc = zfile_get_xattr_if_exists(inode, dentry,
			"user.compression.realsize", buf, sizeof(buf));
	if (rc <= 0)
		return -ENOENT;
	*off = (loff_t)simple_strtoull(buf, NULL, 0);
	return 0;
}

static int zfile_compression_method(struct file *file)
{
	char buf[32];
	int rc;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct dentry *dentry = file->f_dentry;

	rc = zfile_get_xattr_if_exists(inode, dentry,
			"user.compression.method", buf, sizeof(buf)-1);
	if (rc <= 0)
		return -ENOENT;
	if (!strcmp(buf, "zlib"))
		return CM_ZLIB;
	if (!strcmp(buf, "gzip"))
		return CM_GZIP;
	if (!strcmp(buf, "lz4"))
		return CM_LZ4;
	return CM_UNKNOWN;
}

int zfile_is_compressed(struct file *file)
{
	return zfile_compression_method(file) >= CM_UNKNOWN;
}

static int zfile_set_realsize(struct file *file, loff_t off)
{
	char buf[32];
	int rc;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct dentry *dentry = file->f_dentry;

	if (!inode || !inode->i_op || !inode->i_op->setxattr)
		return -EINVAL;
	sprintf(buf, "%lu", (unsigned long)off);
	rc = inode->i_op->setxattr(dentry,
			"user.compression.realsize", buf, sizeof(buf), 0);
	return rc;
}

/* decompression code liberally copied from fs/binfmt_flat.c */

#define ZBUFSIZE (64*1024)

/* gzip flag byte */
#define ASCII_FLAG	0x01 /* bit 0 set: file probably ASCII text */
#define CONTINUATION	0x02 /* bit 1 set: continuation of multi-part gzip file */
#define EXTRA_FIELD	0x04 /* bit 2 set: extra field present */
#define ORIG_NAME	0x08 /* bit 3 set: original file name present */
#define COMMENT		0x10 /* bit 4 set: file comment present */
#define ENCRYPTED	0x20 /* bit 5 set: file is encrypted */
#define RESERVED	0xC0 /* bit 6,7:   reserved */

static int zfile_decompress_zlib(struct file *file, struct zdata *zdata)
{
	fmode_t savemode;
	unsigned char *buf;
	z_stream strm;
	loff_t pos, zpos;
	int ret, zret, retval;
	int wbits;

	size_t curpg;

	savemode = file->f_mode;
	file->f_mode |= FMODE_READ;

	memset(&strm, 0, sizeof(strm));
	strm.workspace = vmalloc(zlib_inflate_workspacesize());
	if (!strm.workspace) {
		printk(KERN_INFO "%s: failed to alloc workspace\n", __func__);
		retval = -ENOMEM;
		goto out;
	}
	buf = vmalloc(ZBUFSIZE);
	if (!buf) {
		printk(KERN_INFO "%s: failed to alloc buf\n", __func__);
		retval = -ENOMEM;
		goto out_free;
	}

	pos = 0;
	zpos = 0;

	ret = kernel_read(file, 0, buf, ZBUFSIZE);
	if (ret == 0) {
		/* XXX? */
		printk(KERN_INFO "%s: read zero bytes, truncated?\n", __func__);
		zdata->z_size = 0;
		retval = 0;
		goto out_free_buf;
	}
	strm.next_in = buf;
	strm.avail_in = ret;
	strm.total_in = 0;
	pos += ret;

	wbits = MAX_WBITS;

	if (zdata->z_method == CM_GZIP) {
		wbits = -MAX_WBITS;
		if (ret < 10) {
			printk(KERN_INFO "%s: failed to do initial read (ret=%d 0x%x)\n", __func__, ret, ret);
			retval = -EINVAL;
			goto out_free_buf;
		}
		if ((buf[0] != 037) || ((buf[1] != 0213) && (buf[1] != 0236))) {
			printk(KERN_INFO "%s: bad magic 1\n", __func__);
			retval = -EINVAL;
			goto out_free_buf;
		}
		if (buf[2] != 8) {
			printk(KERN_INFO "%s: bad magic 2\n", __func__);
			retval = -EINVAL;
			goto out_free_buf;
		}
		if ((buf[3] & ENCRYPTED) || (buf[3] & CONTINUATION) ||
		    (buf[3] & RESERVED)) {
			printk(KERN_INFO "%s: bad magic 3\n", __func__);
			retval = -EINVAL;
			goto out_free_buf;
		}

		ret = 10;
		if (buf[3] & EXTRA_FIELD) {
			ret += 2 + buf[10] + (buf[11] << 8);
			if (unlikely(ZBUFSIZE <= ret)) {
				printk(KERN_INFO "%s: bad extra\n", __func__);
				retval = -EINVAL;
				goto out_free_buf;
			}
		}
		if (buf[3] & ORIG_NAME) {
			while (ret < ZBUFSIZE && buf[ret++] != 0)
				;
			if (unlikely(ZBUFSIZE == ret)) {
				printk(KERN_INFO "%s: bad name\n", __func__);
				retval = -EINVAL;
				goto out_free_buf;
			}
		}
		if (buf[3] & COMMENT) {
			while (ret < ZBUFSIZE && buf[ret++] != 0)
				;
			if (unlikely(ZBUFSIZE == ret)) {
				printk(KERN_INFO "%s: bad comment\n", __func__);
				retval = -EINVAL;
				goto out_free_buf;
			}
		}
		strm.next_in += ret;
		strm.avail_in -= ret;
	}

	curpg = 0;
	strm.next_out = zdata->z_map[curpg];
	strm.avail_out = PAGE_SIZE;
	strm.total_out = 0;

	if (zlib_inflateInit2(&strm, wbits) != Z_OK) {
		printk(KERN_INFO "%s: failed to inflateInit2\n", __func__);
		retval = -EINVAL;
		goto out_free_buf;
	}

	do {
		zret = zlib_inflate(&strm, Z_NO_FLUSH);
		if (zret != Z_OK && zret != Z_STREAM_END)
			break;
		if (strm.avail_out == 0 || zret == Z_STREAM_END) {
			zpos += PAGE_SIZE - strm.avail_out;
			strm.next_out = zdata->z_map[++curpg];
			strm.avail_out = PAGE_SIZE;
		}
		if (strm.avail_in == 0) {
			ret = kernel_read(file, pos, buf, ZBUFSIZE);
			if (ret <= 0)
				break;
			strm.next_in = buf;
			strm.avail_in = ret;
			pos += ret;
		}
	}
	while (zret != Z_STREAM_END);

	if (zret != Z_STREAM_END) {
		printk(KERN_INFO "%s: zlib_inflate failed\n", __func__);
		retval = -EINVAL;
		goto out_zlib;
	}
	if (zpos != zdata->z_size) {
		printk(KERN_INFO "%s: zlib_inflate did not complete (inflated %lu of %lu)\n", __func__,
				(unsigned long)zpos, (unsigned long)zdata->z_size);
		retval = -EINVAL;
		goto out_zlib;
	}

	retval = 0;

out_zlib:
	zlib_inflateEnd(&strm);
out_free_buf:
	vfree(buf);
out_free:
	vfree(strm.workspace);
out:
	file->f_mode = savemode;
	return retval;
}

static int zfile_compress_zlib(struct file *file, struct zdata *zdata)
{
	fmode_t savemode;
	unsigned int saveflags;
	unsigned char *buf;
	z_stream strm;
	loff_t pos, zpos;
	int ret, zret, retval;

	size_t curpg;

	savemode = file->f_mode;
	saveflags = file->f_flags;
	file->f_mode |= FMODE_WRITE;
	file->f_flags &= ~O_APPEND;

	pos = vfs_llseek(file, 0, SEEK_SET);
	if (pos != 0) {
		printk(KERN_INFO "%s: failed to seek\n", __func__);
		retval = -EINVAL;
		goto out;
	}

	memset(&strm, 0, sizeof(strm));
	strm.workspace = vmalloc(zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL));
	if (!strm.workspace) {
		printk(KERN_INFO "%s: failed to alloc workspace\n", __func__);
		retval = -ENOMEM;
		goto out;
	}
	buf = vmalloc(ZBUFSIZE);
	if (!buf) {
		printk(KERN_INFO "%s: failed to alloc buf\n", __func__);
		retval = -ENOMEM;
		goto out_free;
	}

	pos = 0;
	zpos = 0;

	strm.next_out = buf;
	strm.avail_out = ZBUFSIZE;
	strm.total_out = 0;

	curpg = 0;
	strm.next_in = zdata->z_map[curpg];
	strm.avail_in = (zdata->z_size - pos >= PAGE_SIZE ? PAGE_SIZE : zdata->z_size - pos);
	pos += strm.avail_in;
	strm.total_in = 0;

	if (zlib_deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
		printk(KERN_INFO "%s: failed to inflateInit\n", __func__);
		retval = -EINVAL;
		goto out_free_buf;
	}

	do {
		zret = zlib_deflate(&strm, (pos < zdata->z_size ? Z_NO_FLUSH : Z_FINISH));
		if (zret != Z_OK && zret != Z_STREAM_END) {
			printk(KERN_INFO "%s: zlib_deflate failed\n", __func__);
			break;
		}
		if (strm.avail_out == 0 || zret == Z_STREAM_END) {
			printk(KERN_INFO "%s: flush out, size=%lu\n", __func__,
					(unsigned long)(ZBUFSIZE - strm.avail_out));
			ret = kernel_write(file, buf, ZBUFSIZE - strm.avail_out, zpos);
			if (ret <= 0)
				break;
			zpos += ZBUFSIZE - strm.avail_out;
			printk(KERN_INFO "%s: flush out, zpos=%lu\n", __func__,
					(unsigned long)zpos);
			strm.next_out = buf;
			strm.avail_out = ZBUFSIZE;
		}
		if (strm.avail_in == 0) {
			printk(KERN_INFO "%s: read in, pos=%lu\n", __func__,
					(unsigned long)pos);
			strm.next_in = zdata->z_map[++curpg];
			strm.avail_in = (zdata->z_size - pos >= PAGE_SIZE ? PAGE_SIZE : zdata->z_size - pos);
			pos += strm.avail_in;
		}
	}
	while (zret != Z_STREAM_END);

	if (zret != Z_STREAM_END) {
		printk(KERN_INFO "%s: zlib_deflate failed\n", __func__);
		retval = -EINVAL;
		goto out_zlib;
	}

	if (pos != zdata->z_size) {
		printk(KERN_INFO "%s: zlib_deflate did not complete (deflated %lu of %lu)\n", __func__,
				(unsigned long)pos, (unsigned long)zdata->z_size);
		retval = -EINVAL;
		goto out_zlib;
	}

	ret = vfs_truncate(&file->f_path, zpos);
	if (ret != 0) {
		printk(KERN_INFO "%s: vfs_truncate failed\n", __func__);
		retval = -EINVAL;
		goto out_zlib;
	}

	zfile_set_realsize(file, zdata->z_size);
	printk(KERN_INFO "%s: set realsize to %lu\n", __func__, (unsigned long)zdata->z_size);

	retval = 0;

out_zlib:
	zlib_deflateEnd(&strm);
out_free_buf:
	vfree(buf);
out_free:
	vfree(strm.workspace);
out:
	file->f_mode = savemode;
	file->f_flags = saveflags;
	return retval;
}

static const unsigned char lz4magic[] = { 0x02, 0x21, 0x4c, 0x18 };

#define LZ4_BLOCKSIZE (8*1024*1024)

static int zfile_decompress_lz4(struct file *file, struct zdata *zdata)
{
	fmode_t savemode;
	unsigned char *ibuf, *obuf;
	loff_t pos, zpos;
	int ret, zret, retval;
	size_t curpg;
	size_t max_isize, max_osize;

	savemode = file->f_mode;
	file->f_mode |= FMODE_READ;

	max_osize = LZ4_BLOCKSIZE;
	max_isize = lz4_compressbound(max_osize);

	ibuf = vmalloc(max_isize);
	if (!ibuf) {
		printk(KERN_INFO "%s: failed to alloc ibuf\n", __func__);
		retval = -ENOMEM;
		goto out;
	}
	obuf = vmalloc(max_osize);
	if (!obuf) {
		printk(KERN_INFO "%s: failed to alloc obuf\n", __func__);
		retval = -ENOMEM;
		goto out;
	}

	pos = 0;
	zpos = 0;
	curpg = 0;

	ret = kernel_read(file, pos, ibuf, 4);
	if (ret == 0) {
		/* XXX? */
		printk(KERN_INFO "%s: read zero bytes, truncated?\n", __func__);
		zdata->z_size = 0;
		retval = 0;
		goto out_free_buf;
	}
	if (ret != 4) {
		printk(KERN_INFO "%s: short read\n", __func__);
		retval = -EINVAL;
		goto out_free_buf;
	}
	pos += ret;
	if (memcmp(ibuf, lz4magic, 4) != 0) {
		printk(KERN_INFO "%s: bad magic\n", __func__);
		retval = -EINVAL;
		goto out_free_buf;
	}

	do {
		__u32 val;
		size_t isize, osize, opos;

		ret = kernel_read(file, pos, ibuf, 4);
		if (ret <= 0)
			break;
		pos += ret;
		if (ret == 4 && memcmp(ibuf, lz4magic, 4) == 0) {
			ret = kernel_read(file, pos, ibuf, 4);
			if (ret <= 0)
				break;
			pos += ret;
		}
		if (ret != 4) {
			printk(KERN_INFO "%s: short read\n", __func__);
			retval = -EINVAL;
			goto out_free_buf;
		}
		memcpy(&val, ibuf, 4);
		isize = le32_to_cpu(val);
		if (isize > max_isize) {
			printk(KERN_INFO "%s: invalid chunksize\n", __func__);
			retval = -EINVAL;
			goto out_free_buf;
		}
		ret = kernel_read(file, pos, ibuf, isize);
		if (ret != isize) {
			printk(KERN_INFO "%s: short read\n", __func__);
			retval = -EINVAL;
			goto out_free_buf;
		}
		pos += ret;

		osize = min_t(size_t, max_osize, zdata->z_size - zpos);

		zret = lz4_decompress(ibuf, &isize, obuf, osize);
		if (zret != 0) {
			printk(KERN_INFO "%s: lz4_decompress failed with zret=%d\n", __func__, zret);
			break;
		}
		opos = 0;
		while (opos < osize) {
			size_t len = min_t(size_t, PAGE_SIZE, osize - opos);
			memcpy(zdata->z_map[curpg++], obuf+opos, len);
			opos += len;
		}
		zpos += osize;
	}
	while (zpos != zdata->z_size);

	if (zret != 0) {
		printk(KERN_INFO "%s: lz4_decompress failed\n", __func__);
		retval = -EINVAL;
		goto out_free_buf;
	}

	retval = 0;

out_free_buf:
	vfree(obuf);
	vfree(ibuf);
out:
	file->f_mode = savemode;
	return retval;
}

static int zfile_compress_lz4(struct file *file, struct zdata *zdata)
{
	fmode_t savemode;
	unsigned int saveflags;
	unsigned char *workbuf, *ibuf, *obuf;
	loff_t pos, zpos;
	int ret, zret, retval;
	size_t curpg;
	size_t max_isize, max_osize;

	savemode = file->f_mode;
	saveflags = file->f_flags;
	file->f_mode |= FMODE_WRITE;
	file->f_flags &= ~O_APPEND;

	pos = vfs_llseek(file, 0, SEEK_SET);
	if (pos != 0) {
		printk(KERN_INFO "%s: failed to seek\n", __func__);
		retval = -EINVAL;
		goto out;
	}

	max_isize = LZ4_BLOCKSIZE;
	max_osize = lz4_compressbound(max_isize);

	workbuf = vmalloc(LZ4_MEM_COMPRESS);
	if (!workbuf) {
		printk(KERN_INFO "%s: failed to alloc workbuf\n", __func__);
		retval = -ENOMEM;
		goto out;
	}
	ibuf = vmalloc(max_isize);
	if (!ibuf) {
		printk(KERN_INFO "%s: failed to alloc ibuf\n", __func__);
		retval = -ENOMEM;
		goto out;
	}
	obuf = vmalloc(max_osize);
	if (!obuf) {
		printk(KERN_INFO "%s: failed to alloc obuf\n", __func__);
		retval = -ENOMEM;
		goto out;
	}

	pos = 0;
	zpos = 0;
	curpg = 0;

	ret = kernel_write(file, lz4magic, sizeof(lz4magic), zpos);
	if (ret != sizeof(lz4magic)) {
		printk(KERN_INFO "%s: failed to write magic\n", __func__);
		retval = -EINVAL;
		goto out_free_buf;
	}
	zpos += sizeof(lz4magic);

	do {
		__u32 val;
		size_t isize, osize, ipos;

		isize = min_t(size_t, max_isize, zdata->z_size - pos);
		ipos = 0;
		while (ipos < isize) {
			size_t len = min_t(size_t, PAGE_SIZE, isize - ipos);
			memcpy(ibuf + ipos, zdata->z_map[curpg++], len);
			ipos += len;
		};
		pos += isize;

		zret = lz4_compress(ibuf, isize, obuf, &osize, workbuf);
		if (zret != 0) {
			printk(KERN_INFO "%s: lz4_compress failed\n", __func__);
			break;
		}
		val = cpu_to_le32(osize);

		ret = kernel_write(file, (char *)&val, sizeof(val), zpos);
		if (ret != sizeof(val)) {
			printk(KERN_INFO "%s: failed to write frame size\n", __func__);
			break;
		}
		zpos += ret;
		ret = kernel_write(file, obuf, osize, zpos);
		if (ret != osize) {
			printk(KERN_INFO "%s: failed to write frame\n", __func__);
			break;
		}
		zpos += ret;
	}
	while (pos != zdata->z_size);

	if (zret != 0) {
		printk(KERN_INFO "%s: lz4_compress failed\n", __func__);
		retval = -EINVAL;
		goto out_free_buf;
	}

	if (pos != zdata->z_size) {
		printk(KERN_INFO "%s: lz4_compress did not complete (deflated %lu of %lu)\n", __func__,
				(unsigned long)pos, (unsigned long)zdata->z_size);
		retval = -EINVAL;
		goto out_free_buf;
	}

	ret = vfs_truncate(&file->f_path, zpos);
	if (ret != 0) {
		printk(KERN_INFO "%s: vfs_truncate failed\n", __func__);
		retval = -EINVAL;
		goto out_free_buf;
	}

	zfile_set_realsize(file, zdata->z_size);
	printk(KERN_INFO "%s: set realsize to %lu\n", __func__, (unsigned long)zdata->z_size);

	retval = 0;

out_free_buf:
	vfree(workbuf);
	vfree(obuf);
	vfree(ibuf);
out:
	file->f_mode = savemode;
	file->f_flags = saveflags;
	return retval;
}

static int zfile_decompress(struct file *file, struct zdata *zdata)
{
	int ret = -EINVAL;

	switch (zdata->z_method) {
	case CM_ZLIB:
		ret = zfile_decompress_zlib(file, zdata);
		break;
	case CM_GZIP:
		ret = zfile_decompress_zlib(file, zdata);
		break;
	case CM_LZ4:
		ret = zfile_decompress_lz4(file, zdata);
		break;
	}

	return ret;
}

static int zfile_compress(struct file *file, struct zdata *zdata)
{
	int ret = -EINVAL;

	switch (zdata->z_method) {
	case CM_ZLIB:
		ret = zfile_compress_zlib(file, zdata);
		break;
	case CM_GZIP:
		ret = zfile_compress_zlib(file, zdata);
		break;
	case CM_LZ4:
		ret = zfile_compress_lz4(file, zdata);
		break;
	}

	return ret;
}

static struct zdata *zdata_lookup(const char *filename)
{
	struct zdata *cur;

	if (!filename)
		return NULL;

	list_for_each_entry(cur, &z_data, z_node) {
		if (!strcmp(filename, cur->z_filename)) {
			++cur->z_refcount;
			return cur;
		}
	}

	return NULL;
}

static int zdata_realloc(struct zdata *zdata, loff_t size)
{
	size_t mapsz;
	void **map;
	size_t n;

	if (size == 0) {
		for (n = 0; n < zdata->z_mapsize; ++n) {
			free_pages((unsigned long)zdata->z_map[n], 0);
		}
		vfree(zdata->z_map);
		zdata->z_map = NULL;
		zdata->z_mapsize = 0;
		return 0;
	}

	mapsz = DIV_ROUND_UP(size, PAGE_SIZE);
	if (mapsz > zdata->z_mapsize) {
		map = (void **)vmalloc(mapsz * sizeof(void *));
		if (!map)
			goto out;
		if (zdata->z_mapsize)
			memcpy(map, zdata->z_map, zdata->z_mapsize * sizeof(void *));
		for (n = zdata->z_mapsize; n < mapsz; ++n) {
			map[n] = (void *)__get_free_page(GFP_KERNEL);
			if (!map[n])
				goto out;
		}

		vfree(zdata->z_map);
		zdata->z_map = map;
		zdata->z_mapsize = mapsz;
	}

	return 0;

out:
	vfree(map);
	return -ENOMEM;
}

static struct zdata *zdata_init(struct file *file, const char *filename)
{
	loff_t zsize;
	size_t mapsz;
	struct zdata *zdata;
	int rc;

	mutex_lock(&zdata_lock);

	zdata = zdata_lookup(filename);
	if (zdata) {
		mutex_unlock(&zdata_lock);
		return zdata;
	}

	zdata = (struct zdata *)vmalloc(sizeof(struct zdata));
	if (!zdata) {
		printk(KERN_INFO "%s: failed to alloc zdata\n", __func__);
		mutex_unlock(&zdata_lock);
		return ERR_PTR(-ENOMEM);
	}
	memset(zdata, 0, sizeof(struct zdata));

	if (zfile_realsize(file, &zsize) != 0) {
		printk(KERN_INFO "%s: failed to get realsize\n", __func__);
		vfree(zdata);
		mutex_unlock(&zdata_lock);
		return ERR_PTR(-EINVAL);
	}
	mapsz = DIV_ROUND_UP(zsize, PAGE_SIZE);

	zdata->z_refcount = 1;
	zdata->z_filename = kstrdup(filename, GFP_KERNEL);
	zdata->z_method = zfile_compression_method(file);
	zdata->z_size = zsize;
	if (zdata_realloc(zdata, zsize) != 0) {
		printk(KERN_INFO "%s: failed to alloc zmap\n", __func__);
		mutex_unlock(&zdata_lock);
		kfree(zdata->z_filename);
		vfree(zdata);
		return ERR_PTR(-ENOMEM);
	}

	rc = zfile_decompress(file, zdata);
	if (rc != 0) {
		printk(KERN_INFO "%s: failed to decompress\n", __func__);
		mutex_unlock(&zdata_lock);
		kfree(zdata->z_filename);
		vfree(zdata);
		return ERR_PTR(rc);
	}

	list_add(&zdata->z_node, &z_data);
	mutex_unlock(&zdata_lock);

	return zdata;
}

static int zdata_release(struct file *file, struct zdata *zdata)
{
	size_t curpg;

	mutex_lock(&zdata_lock);
	--zdata->z_refcount;
	if (zdata->z_refcount > 0) {
		mutex_unlock(&zdata_lock);
		return 0;
	}
	list_del(&zdata->z_node);
	mutex_unlock(&zdata_lock);

	if (zdata->z_dirty)
		zfile_compress(file, zdata);

	for (curpg = 0; curpg < zdata->z_mapsize; ++curpg)
		free_pages((unsigned long)zdata->z_map[curpg], 0);

	if (zdata->z_map)
		vfree(zdata->z_map);
	kfree(zdata->z_filename);
	vfree(zdata);

	return 0;
}

static struct zinfo *zinfo_init(struct file *file, const char *filename)
{
	struct zinfo *zinfo;
	struct zdata *zdata;

	zinfo = (struct zinfo *)vmalloc(sizeof(struct zinfo));
	if (!zinfo) {
		printk(KERN_INFO "%s: failed to alloc zinfo\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	zdata = zdata_init(file, filename);
	if (IS_ERR(zdata)) {
		printk(KERN_INFO "%s: failed to init zdata\n", __func__);
		vfree(zinfo);
		return (struct zinfo *)zdata;
	}

	zinfo->z_data = zdata;
	zinfo->z_orig_fop = file->f_op;
	zinfo->z_orig_priv = file->private_data;

	mutex_lock(&zinfo_lock);
	list_add(&zinfo->z_node, &z_info);
	mutex_unlock(&zinfo_lock);

	return zinfo;
}

static void zinfo_free(struct file *file, struct zinfo *zinfo)
{
	mutex_lock(&zinfo_lock);
	list_del(&zinfo->z_node);
	mutex_unlock(&zinfo_lock);

	zdata_release(file, zinfo->z_data);

	vfree(zinfo);
}

static ssize_t zfile_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	struct zinfo *zinfo = (struct zinfo *)file->private_data;
	struct zdata *zdata = zinfo->z_data;
	size_t len, remain;
	size_t curpg;

	if (*pos >= zdata->z_size)
		return 0;

	len = count;
	if (*pos + len > zdata->z_size)
		len = zdata->z_size - *pos;
	remain = len;
	curpg = *pos >> PAGE_SHIFT;

	if (*pos & (PAGE_SIZE-1)) {
		size_t pgoff = (*pos & (PAGE_SIZE-1));
		size_t pgrem = PAGE_SIZE - pgoff;
		if (pgrem > remain)
			pgrem = remain;
		if (copy_to_user(buf, zdata->z_map[curpg] + pgoff, pgrem) != 0) {
			printk(KERN_INFO "%s: fault\n", __func__);
			return -EFAULT;
		}
		++curpg;
		buf += pgrem;
		remain -= pgrem;
	}
	while (remain >= PAGE_SIZE) {
		if (copy_to_user(buf, zdata->z_map[curpg], PAGE_SIZE) != 0) {
			printk(KERN_INFO "%s: fault\n", __func__);
			return -EFAULT;
		}
		++curpg;
		buf += PAGE_SIZE;
		remain -= PAGE_SIZE;
	}
	if (remain > 0) {
		if (copy_to_user(buf, zdata->z_map[curpg], remain) != 0) {
			printk(KERN_INFO "%s: fault\n", __func__);
			return -EFAULT;
		}
		++curpg;
		buf += remain;
	}
	*pos += len;
	return len;
}

/*
 * XXX: this is not really correct/complete yet.  we should at least allow
 *      partial copies from userspace.
 */
ssize_t zfile_write(struct file *file, const char __user *buf, size_t len, loff_t *pos)
{
	struct zinfo *zinfo = (struct zinfo *)file->private_data;
	struct zdata *zdata = zinfo->z_data;
	size_t remain;
	size_t curpg;
	loff_t off;

	off = (file->f_flags & O_APPEND) ? zdata->z_size : *pos;

	if (zdata_realloc(zdata, off + len) != 0)
		return -ENOMEM;

	remain = len;
	curpg = off >> PAGE_SHIFT;

	if (off & (PAGE_SIZE-1)) {
		size_t pgoff = (off & (PAGE_SIZE-1));
		size_t pgrem = PAGE_SIZE - pgoff;
		if (pgrem > remain)
			pgrem = remain;
		if (copy_from_user(zdata->z_map[curpg] + pgoff, buf, pgrem) != 0) {
			printk(KERN_INFO "%s: fault\n", __func__);
			return -EFAULT;
		}
		++curpg;
		buf += pgrem;
		remain -= pgrem;
	}
	while (remain >= PAGE_SIZE) {
		if (copy_from_user(zdata->z_map[curpg], buf, PAGE_SIZE) != 0) {
			printk(KERN_INFO "%s: fault\n", __func__);
			return -EFAULT;
		}
		++curpg;
		buf += PAGE_SIZE;
		remain -= PAGE_SIZE;
	}
	if (remain > 0) {
		if (copy_from_user(zdata->z_map[curpg], buf, remain) != 0) {
			printk(KERN_INFO "%s: fault\n", __func__);
			return -EFAULT;
		}
		++curpg;
		buf += remain;
	}
	zdata->z_dirty = 1;
	off += len;
	if (off > zdata->z_size)
		zdata->z_size = off;
	*pos += len;

	return len;
}

static int zfile_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct zinfo *zinfo = (struct zinfo *)file->private_data;
	struct zdata *zdata = zinfo->z_data;
	size_t mapsz;
	unsigned int page_count;
	unsigned int i;
	int rc = 0;

	mapsz = DIV_ROUND_UP(zdata->z_size, PAGE_SIZE);
	page_count = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;

	if (vma->vm_pgoff + page_count > mapsz) {
		/* XXX:
		 * It is valid to mmap() a file beyond its EOF in case it
		 * may be expanded.  We don't handle that (yet).  Instead,
		 * we just truncate the mapping.  If the process tries to
		 * write beyond the EOF, it will get a SIGBUS.
		 */
		page_count = mapsz - vma->vm_pgoff;
	}

	for (i = 0; i < page_count; ++i) {
		unsigned long paddr = __pa(zdata->z_map[vma->vm_pgoff+i]);
		unsigned long pfn = paddr >> PAGE_SHIFT;
		struct page *page = pfn_to_page(pfn);
		rc = vm_insert_page(vma,
				vma->vm_start + i*PAGE_SIZE,
				page);
		if (rc)
			break;
	}

	if (rc) {
		printk(KERN_INFO "%s: failed\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int zfile_release(struct inode *inode, struct file *file)
{
	struct zinfo *zinfo;
	int ret = 0;

	zinfo = (struct zinfo *)file->private_data;

	file->private_data = zinfo->z_orig_priv;
	file->f_op = zinfo->z_orig_fop;

	zinfo_free(file, zinfo);

	if (file->f_op->release)
		ret = file->f_op->release(inode, file);

	return ret;
}

static ssize_t zfile_splice_read(struct file *in, loff_t *ppos, struct pipe_inode_info *pipe, size_t len, unsigned int flags)
{
	return -EINVAL;
}

static long zfile_fallocate(struct file *file, int mode, loff_t offset, loff_t len)
{
	return -EINVAL;
}

static const struct file_operations zfile_operations = {
	.llseek = generic_file_llseek,
	.read = zfile_read,
	.write = zfile_write,
	.mmap = zfile_mmap,
	.release = zfile_release,
	.fsync = noop_fsync,
	.splice_read = zfile_splice_read,
	.splice_write = generic_file_splice_write,
	.fallocate = zfile_fallocate,
};

struct file *zfile_open(struct file *file, const char *filename)
{
	struct zinfo *zinfo;

	zinfo = zinfo_init(file, filename);
	if (IS_ERR(zinfo)) {
		printk(KERN_INFO "%s: failed to init zinfo\n", __func__);
		return (struct file *)zinfo;
	}

	/*
	 * Insert our file into the chain.  Note our file becomes the
	 * "real" file from the view of the rest of the kernel, and the
	 * original file is saved off so that we may use it for various
	 * things, eg. re-writing the file on close.
	 */
	file->f_op = &zfile_operations;
	file->private_data = zinfo;

	/* XXX: use this or custom llseek? */
	file->f_path.dentry->d_inode->i_size = zinfo->z_data->z_size;

	return file;
}

int zpath_realsize(struct dentry *dentry, loff_t *off)
{
	char buf[32];
	int rc;

	if (!zfile_enable)
		return 0;

	rc = zfile_get_xattr_if_exists(dentry->d_inode, dentry,
			"user.compression.realsize", buf, sizeof(buf));
	if (rc <= 0)
		return -ENOENT;
	*off = (loff_t)simple_strtoull(buf, NULL, 0);
	return 0;
}

static struct kobject *zfile_kobj;

static ssize_t enable_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", zfile_enable);
}

static ssize_t enable_store(struct kobject *kobj,
			    struct kobj_attribute *attr, const char *buf, size_t len)
{
	int rc;
	unsigned long val;

	rc = strict_strtoul(buf, 10, &val);
	if ((rc < 0) || (val > 1))
		return -EIO;

	zfile_enable = val;

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

static int __init zfile_init(void)
{
	int rc;

	zfile_kobj = kobject_create_and_add("zfile", fs_kobj);
	if (!zfile_kobj) {
		printk(KERN_ERR "Unable to register with sysfs\n");
		rc = -ENOMEM;
		goto out;
	}
	rc = sysfs_create_group(zfile_kobj, &attr_group);
	if (rc) {
		printk(KERN_ERR "Unable to create zfile sysfs attributes\n");
		kobject_put(zfile_kobj);
	}
out:
	return rc;
}

static void __exit zfile_exit(void)
{
	sysfs_remove_group(zfile_kobj, &attr_group);
	kobject_put(zfile_kobj);
}

MODULE_AUTHOR("Tom Marshall <tdm@cyngn.com>");
MODULE_DESCRIPTION("zfile");
MODULE_LICENSE("GPL");

module_init(zfile_init);
module_exit(zfile_exit);
