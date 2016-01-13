/*
 *  c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * /dev/nvram driver for PPC64
 *
 * This perhaps should live in drivers/char
 */


#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/kmsg_dump.h>
#include <linux/ctype.h>
#include <linux/zlib.h>
#include <asm/uaccess.h>
#include <asm/nvram.h>
#include <asm/rtas.h>
#include <asm/prom.h>
#include <asm/machdep.h>

/* Max bytes to read/write in one go */
#define NVRW_CNT 0x20

static unsigned int nvram_size;
static int nvram_fetch, nvram_store;
static char nvram_buf[NVRW_CNT];	/* assume this is in the first 4GB */
static DEFINE_SPINLOCK(nvram_lock);

struct err_log_info {
	int error_type;
	unsigned int seq_num;
};

struct nvram_os_partition {
	const char *name;
	int req_size;	/* desired size, in bytes */
	int min_size;	/* minimum acceptable size (0 means req_size) */
	long size;	/* size of data portion (excluding err_log_info) */
	long index;	/* offset of data portion of partition */
};

static struct nvram_os_partition rtas_log_partition = {
	.name = "ibm,rtas-log",
	.req_size = 2079,
	.min_size = 1055,
	.index = -1
};

static struct nvram_os_partition oops_log_partition = {
	.name = "lnx,oops-log",
	.req_size = 4000,
	.min_size = 2000,
	.index = -1
};

static const char *pseries_nvram_os_partitions[] = {
	"ibm,rtas-log",
	"lnx,oops-log",
	NULL
};

static void oops_to_nvram(struct kmsg_dumper *dumper,
			  enum kmsg_dump_reason reason);

static struct kmsg_dumper nvram_kmsg_dumper = {
	.dump = oops_to_nvram
};

/* See clobbering_unread_rtas_event() */
#define NVRAM_RTAS_READ_TIMEOUT 5		/* seconds */
static unsigned long last_unread_rtas_event;	/* timestamp */

/*
 * For capturing and compressing an oops or panic report...

 * big_oops_buf[] holds the uncompressed text we're capturing.
 *
 * oops_buf[] holds the compressed text, preceded by a prefix.
 * The prefix is just a u16 holding the length of the compressed* text.
 * (*Or uncompressed, if compression fails.)  oops_buf[] gets written
 * to NVRAM.
 *
 * oops_len points to the prefix.  oops_data points to the compressed text.
 *
 * +- oops_buf
 * |		+- oops_data
 * v		v
 * +------------+-----------------------------------------------+
 * | length	| text                                          |
 * | (2 bytes)	| (oops_data_sz bytes)                          |
 * +------------+-----------------------------------------------+
 * ^
 * +- oops_len
 *
 * We preallocate these buffers during init to avoid kmalloc during oops/panic.
 */
static size_t big_oops_buf_sz;
static char *big_oops_buf, *oops_buf;
static u16 *oops_len;
static char *oops_data;
static size_t oops_data_sz;

/* Compression parameters */
#define COMPR_LEVEL 6
#define WINDOW_BITS 12
#define MEM_LEVEL 4
static struct z_stream_s stream;

static ssize_t pSeries_nvram_read(char *buf, size_t count, loff_t *index)
{
	unsigned int i;
	unsigned long len;
	int done;
	unsigned long flags;
	char *p = buf;


	if (nvram_size == 0 || nvram_fetch == RTAS_UNKNOWN_SERVICE)
		return -ENODEV;

	if (*index >= nvram_size)
		return 0;

	i = *index;
	if (i + count > nvram_size)
		count = nvram_size - i;

	spin_lock_irqsave(&nvram_lock, flags);

	for (; count != 0; count -= len) {
		len = count;
		if (len > NVRW_CNT)
			len = NVRW_CNT;
		
		if ((rtas_call(nvram_fetch, 3, 2, &done, i, __pa(nvram_buf),
			       len) != 0) || len != done) {
			spin_unlock_irqrestore(&nvram_lock, flags);
			return -EIO;
		}
		
		memcpy(p, nvram_buf, len);

		p += len;
		i += len;
	}

	spin_unlock_irqrestore(&nvram_lock, flags);
	
	*index = i;
	return p - buf;
}

static ssize_t pSeries_nvram_write(char *buf, size_t count, loff_t *index)
{
	unsigned int i;
	unsigned long len;
	int done;
	unsigned long flags;
	const char *p = buf;

	if (nvram_size == 0 || nvram_store == RTAS_UNKNOWN_SERVICE)
		return -ENODEV;

	if (*index >= nvram_size)
		return 0;

	i = *index;
	if (i + count > nvram_size)
		count = nvram_size - i;

	spin_lock_irqsave(&nvram_lock, flags);

	for (; count != 0; count -= len) {
		len = count;
		if (len > NVRW_CNT)
			len = NVRW_CNT;

		memcpy(nvram_buf, p, len);

		if ((rtas_call(nvram_store, 3, 2, &done, i, __pa(nvram_buf),
			       len) != 0) || len != done) {
			spin_unlock_irqrestore(&nvram_lock, flags);
			return -EIO;
		}
		
		p += len;
		i += len;
	}
	spin_unlock_irqrestore(&nvram_lock, flags);
	
	*index = i;
	return p - buf;
}

static ssize_t pSeries_nvram_get_size(void)
{
	return nvram_size ? nvram_size : -ENODEV;
}


/* nvram_write_os_partition, nvram_write_error_log
 *
 * We need to buffer the error logs into nvram to ensure that we have
 * the failure information to decode.  If we have a severe error there
 * is no way to guarantee that the OS or the machine is in a state to
 * get back to user land and write the error to disk.  For example if
 * the SCSI device driver causes a Machine Check by writing to a bad
 * IO address, there is no way of guaranteeing that the device driver
 * is in any state that is would also be able to write the error data
 * captured to disk, thus we buffer it in NVRAM for analysis on the
 * next boot.
 *
 * In NVRAM the partition containing the error log buffer will looks like:
 * Header (in bytes):
 * +-----------+----------+--------+------------+------------------+
 * | signature | checksum | length | name       | data             |
 * |0          |1         |2      3|4         15|16        length-1|
 * +-----------+----------+--------+------------+------------------+
 *
 * The 'data' section would look like (in bytes):
 * +--------------+------------+-----------------------------------+
 * | event_logged | sequence # | error log                         |
 * |0            3|4          7|8                  error_log_size-1|
 * +--------------+------------+-----------------------------------+
 *
 * event_logged: 0 if event has not been logged to syslog, 1 if it has
 * sequence #: The unique sequence # for each event. (until it wraps)
 * error log: The error log from event_scan
 */
int nvram_write_os_partition(struct nvram_os_partition *part, char * buff,
		int length, unsigned int err_type, unsigned int error_log_cnt)
{
	int rc;
	loff_t tmp_index;
	struct err_log_info info;
	
	if (part->index == -1) {
		return -ESPIPE;
	}

	if (length > part->size) {
		length = part->size;
	}

	info.error_type = err_type;
	info.seq_num = error_log_cnt;

	tmp_index = part->index;

	rc = ppc_md.nvram_write((char *)&info, sizeof(struct err_log_info), &tmp_index);
	if (rc <= 0) {
		pr_err("%s: Failed nvram_write (%d)\n", __FUNCTION__, rc);
		return rc;
	}

	rc = ppc_md.nvram_write(buff, length, &tmp_index);
	if (rc <= 0) {
		pr_err("%s: Failed nvram_write (%d)\n", __FUNCTION__, rc);
		return rc;
	}
	
	return 0;
}

int nvram_write_error_log(char * buff, int length,
                          unsigned int err_type, unsigned int error_log_cnt)
{
	int rc = nvram_write_os_partition(&rtas_log_partition, buff, length,
						err_type, error_log_cnt);
	if (!rc)
		last_unread_rtas_event = get_seconds();
	return rc;
}

/* nvram_read_error_log
 *
 * Reads nvram for error log for at most 'length'
 */
int nvram_read_error_log(char * buff, int length,
                         unsigned int * err_type, unsigned int * error_log_cnt)
{
	int rc;
	loff_t tmp_index;
	struct err_log_info info;
	
	if (rtas_log_partition.index == -1)
		return -1;

	if (length > rtas_log_partition.size)
		length = rtas_log_partition.size;

	tmp_index = rtas_log_partition.index;

	rc = ppc_md.nvram_read((char *)&info, sizeof(struct err_log_info), &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_read_error_log: Failed nvram_read (%d)\n", rc);
		return rc;
	}

	rc = ppc_md.nvram_read(buff, length, &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_read_error_log: Failed nvram_read (%d)\n", rc);
		return rc;
	}

	*error_log_cnt = info.seq_num;
	*err_type = info.error_type;

	return 0;
}

/* This doesn't actually zero anything, but it sets the event_logged
 * word to tell that this event is safely in syslog.
 */
int nvram_clear_error_log(void)
{
	loff_t tmp_index;
	int clear_word = ERR_FLAG_ALREADY_LOGGED;
	int rc;

	if (rtas_log_partition.index == -1)
		return -1;

	tmp_index = rtas_log_partition.index;
	
	rc = ppc_md.nvram_write((char *)&clear_word, sizeof(int), &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_clear_error_log: Failed nvram_write (%d)\n", rc);
		return rc;
	}
	last_unread_rtas_event = 0;

	return 0;
}

/* pseries_nvram_init_os_partition
 *
 * This sets up a partition with an "OS" signature.
 *
 * The general strategy is the following:
 * 1.) If a partition with the indicated name already exists...
 *	- If it's large enough, use it.
 *	- Otherwise, recycle it and keep going.
 * 2.) Search for a free partition that is large enough.
 * 3.) If there's not a free partition large enough, recycle any obsolete
 * OS partitions and try again.
 * 4.) Will first try getting a chunk that will satisfy the requested size.
 * 5.) If a chunk of the requested size cannot be allocated, then try finding
 * a chunk that will satisfy the minum needed.
 *
 * Returns 0 on success, else -1.
 */
static int __init pseries_nvram_init_os_partition(struct nvram_os_partition
									*part)
{
	loff_t p;
	int size;

	/* Scan nvram for partitions */
	nvram_scan_partitions();

	/* Look for ours */
	p = nvram_find_partition(part->name, NVRAM_SIG_OS, &size);

	/* Found one but too small, remove it */
	if (p && size < part->min_size) {
		pr_info("nvram: Found too small %s partition,"
					" removing it...\n", part->name);
		nvram_remove_partition(part->name, NVRAM_SIG_OS, NULL);
		p = 0;
	}

	/* Create one if we didn't find */
	if (!p) {
		p = nvram_create_partition(part->name, NVRAM_SIG_OS,
					part->req_size, part->min_size);
		if (p == -ENOSPC) {
			pr_info("nvram: No room to create %s partition, "
				"deleting any obsolete OS partitions...\n",
				part->name);
			nvram_remove_partition(NULL, NVRAM_SIG_OS,
						pseries_nvram_os_partitions);
			p = nvram_create_partition(part->name, NVRAM_SIG_OS,
					part->req_size, part->min_size);
		}
	}

	if (p <= 0) {
		pr_err("nvram: Failed to find or create %s"
		       " partition, err %d\n", part->name, (int)p);
		return -1;
	}

	part->index = p;
	part->size = nvram_get_partition_size(p) - sizeof(struct err_log_info);
	
	return 0;
}

static void __init nvram_init_oops_partition(int rtas_partition_exists)
{
	int rc;

	rc = pseries_nvram_init_os_partition(&oops_log_partition);
	if (rc != 0) {
		if (!rtas_partition_exists)
			return;
		pr_notice("nvram: Using %s partition to log both"
			" RTAS errors and oops/panic reports\n",
			rtas_log_partition.name);
		memcpy(&oops_log_partition, &rtas_log_partition,
						sizeof(rtas_log_partition));
	}
	oops_buf = kmalloc(oops_log_partition.size, GFP_KERNEL);
	if (!oops_buf) {
		pr_err("nvram: No memory for %s partition\n",
						oops_log_partition.name);
		return;
	}
	oops_len = (u16*) oops_buf;
	oops_data = oops_buf + sizeof(u16);
	oops_data_sz = oops_log_partition.size - sizeof(u16);

	/*
	 * Figure compression (preceded by elimination of each line's <n>
	 * severity prefix) will reduce the oops/panic report to at most
	 * 45% of its original size.
	 */
	big_oops_buf_sz = (oops_data_sz * 100) / 45;
	big_oops_buf = kmalloc(big_oops_buf_sz, GFP_KERNEL);
	if (big_oops_buf) {
		stream.workspace = kmalloc(zlib_deflate_workspacesize(
				WINDOW_BITS, MEM_LEVEL), GFP_KERNEL);
		if (!stream.workspace) {
			pr_err("nvram: No memory for compression workspace; "
				"skipping compression of %s partition data\n",
				oops_log_partition.name);
			kfree(big_oops_buf);
			big_oops_buf = NULL;
		}
	} else {
		pr_err("No memory for uncompressed %s data; "
			"skipping compression\n", oops_log_partition.name);
		stream.workspace = NULL;
	}

	rc = kmsg_dump_register(&nvram_kmsg_dumper);
	if (rc != 0) {
		pr_err("nvram: kmsg_dump_register() failed; returned %d\n", rc);
		kfree(oops_buf);
		kfree(big_oops_buf);
		kfree(stream.workspace);
	}
}

static int __init pseries_nvram_init_log_partitions(void)
{
	int rc;

	rc = pseries_nvram_init_os_partition(&rtas_log_partition);
	nvram_init_oops_partition(rc == 0);
	return 0;
}
machine_arch_initcall(pseries, pseries_nvram_init_log_partitions);

int __init pSeries_nvram_init(void)
{
	struct device_node *nvram;
	const unsigned int *nbytes_p;
	unsigned int proplen;

	nvram = of_find_node_by_type(NULL, "nvram");
	if (nvram == NULL)
		return -ENODEV;

	nbytes_p = of_get_property(nvram, "#bytes", &proplen);
	if (nbytes_p == NULL || proplen != sizeof(unsigned int)) {
		of_node_put(nvram);
		return -EIO;
	}

	nvram_size = *nbytes_p;

	nvram_fetch = rtas_token("nvram-fetch");
	nvram_store = rtas_token("nvram-store");
	printk(KERN_INFO "PPC64 nvram contains %d bytes\n", nvram_size);
	of_node_put(nvram);

	ppc_md.nvram_read	= pSeries_nvram_read;
	ppc_md.nvram_write	= pSeries_nvram_write;
	ppc_md.nvram_size	= pSeries_nvram_get_size;

	return 0;
}

/*
 * Are we using the ibm,rtas-log for oops/panic reports?  And if so,
 * would logging this oops/panic overwrite an RTAS event that rtas_errd
 * hasn't had a chance to read and process?  Return 1 if so, else 0.
 *
 * We assume that if rtas_errd hasn't read the RTAS event in
 * NVRAM_RTAS_READ_TIMEOUT seconds, it's probably not going to.
 */
static int clobbering_unread_rtas_event(void)
{
	return (oops_log_partition.index == rtas_log_partition.index
		&& last_unread_rtas_event
		&& get_seconds() - last_unread_rtas_event <=
						NVRAM_RTAS_READ_TIMEOUT);
}

/* Derived from logfs_compress() */
static int nvram_compress(const void *in, void *out, size_t inlen,
							size_t outlen)
{
	int err, ret;

	ret = -EIO;
	err = zlib_deflateInit2(&stream, COMPR_LEVEL, Z_DEFLATED, WINDOW_BITS,
						MEM_LEVEL, Z_DEFAULT_STRATEGY);
	if (err != Z_OK)
		goto error;

	stream.next_in = in;
	stream.avail_in = inlen;
	stream.total_in = 0;
	stream.next_out = out;
	stream.avail_out = outlen;
	stream.total_out = 0;

	err = zlib_deflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END)
		goto error;

	err = zlib_deflateEnd(&stream);
	if (err != Z_OK)
		goto error;

	if (stream.total_out >= stream.total_in)
		goto error;

	ret = stream.total_out;
error:
	return ret;
}

/* Compress the text from big_oops_buf into oops_buf. */
static int zip_oops(size_t text_len)
{
	int zipped_len = nvram_compress(big_oops_buf, oops_data, text_len,
								oops_data_sz);
	if (zipped_len < 0) {
		pr_err("nvram: compression failed; returned %d\n", zipped_len);
		pr_err("nvram: logging uncompressed oops/panic report\n");
		return -1;
	}
	*oops_len = (u16) zipped_len;
	return 0;
}

/*
 * This is our kmsg_dump callback, called after an oops or panic report
 * has been written to the printk buffer.  We want to capture as much
 * of the printk buffer as possible.  First, capture as much as we can
 * that we think will compress sufficiently to fit in the lnx,oops-log
 * partition.  If that's too much, go back and capture uncompressed text.
 */
static void oops_to_nvram(struct kmsg_dumper *dumper,
			  enum kmsg_dump_reason reason)
{
	static unsigned int oops_count = 0;
	static bool panicking = false;
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;
	size_t text_len;
	unsigned int err_type = ERR_TYPE_KERNEL_PANIC_GZ;
	int rc = -1;

	switch (reason) {
	case KMSG_DUMP_RESTART:
	case KMSG_DUMP_HALT:
	case KMSG_DUMP_POWEROFF:
		/* These are almost always orderly shutdowns. */
		return;
	case KMSG_DUMP_OOPS:
		break;
	case KMSG_DUMP_PANIC:
		panicking = true;
		break;
	case KMSG_DUMP_EMERG:
		if (panicking)
			/* Panic report already captured. */
			return;
		break;
	default:
		pr_err("%s: ignoring unrecognized KMSG_DUMP_* reason %d\n",
						__FUNCTION__, (int) reason);
		return;
	}

	if (clobbering_unread_rtas_event())
		return;

	if (!spin_trylock_irqsave(&lock, flags))
		return;

	if (big_oops_buf) {
		kmsg_dump_get_buffer(dumper, false,
				     big_oops_buf, big_oops_buf_sz, &text_len);
		rc = zip_oops(text_len);
	}
	if (rc != 0) {
		kmsg_dump_rewind(dumper);
		kmsg_dump_get_buffer(dumper, true,
				     oops_data, oops_data_sz, &text_len);
		err_type = ERR_TYPE_KERNEL_PANIC;
		*oops_len = (u16) text_len;
	}

	(void) nvram_write_os_partition(&oops_log_partition, oops_buf,
		(int) (sizeof(*oops_len) + *oops_len), err_type, ++oops_count);

	spin_unlock_irqrestore(&lock, flags);
}
