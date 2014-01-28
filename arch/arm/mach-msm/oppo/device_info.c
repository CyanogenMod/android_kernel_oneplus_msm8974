/**
 * Copyright 2008-2013 OPPO Mobile Comm Corp., Ltd, All rights reserved.
 * VENDOR_EDIT:
 * FileName:devinfo.c
 * ModuleName:devinfo
 * Author: wangjc
 * Create Date: 2013-10-23
 * Description:add interface to get device information.
 * History:
   <version >  <time>  <author>  <desc>
   1.0		2013-10-23	wangjc	init
*/

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <mach/device_info.h>



static struct proc_dir_entry *parent = NULL;

static int device_proc_output (char *buf, struct manufacture_info *priv)
{
	char *p = buf;

	p += sprintf(p, "Device version:\t\t%s\n",
		     priv->version);

	p += sprintf(p, "Device manufacture:\t\t%s\n",
			priv->manufacture);

	return p - buf;
}

static int device_read_proc(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	struct manufacture_info *priv = data;
	
	int len = device_proc_output (page, priv);
	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

int register_device_proc(char *name, char *version, char *manufacture)
{
	struct proc_dir_entry *d_entry;
	struct manufacture_info *info;

	if(!parent) {
		parent =  proc_mkdir ("devinfo", NULL);
		if(!parent) {
			pr_err("can't create devinfo proc\n");
			return -ENOENT;
		}
	}

	info = kzalloc(sizeof *info, GFP_KERNEL);
	info->version = version;
	info->manufacture = manufacture;

	d_entry = create_proc_read_entry (name, S_IRUGO, parent, device_read_proc, info);
	if(!d_entry) {
		pr_err("create %s proc failed.\n", name);
		kfree(info);
		return -ENOENT;
	}
	return 0;
}

static int __init device_info_init(void)
{
	int ret = 0;
	
	parent =  proc_mkdir ("devinfo", NULL);
	if(!parent) {
		pr_err("can't create devinfo proc\n");
		ret = -ENOENT;
	}
	
	return ret;
}

static void __exit device_info_exit(void)
{

	remove_proc_entry("devinfo", NULL);

}

module_init(device_info_init);
module_exit(device_info_exit);


MODULE_DESCRIPTION("OPPO device info");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wangjc <wjc@oppo.com>");


