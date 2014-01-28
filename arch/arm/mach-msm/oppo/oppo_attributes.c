/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include <linux/boot_mode.h>
#include <linux/pcb_version.h>

static ssize_t startup_mode_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%s", get_start_reason());
}

static ssize_t startup_mode_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	return 0;
}

struct kobj_attribute startup_mode_attr = {
	.attr = { "startup_mode", 0644 },
	.show = &startup_mode_show,
	.store = &startup_mode_store,
};

static ssize_t app_boot_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%s", get_boot_mode_str());
}

static ssize_t app_boot_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	return 0;
}

struct kobj_attribute app_boot_attr = {
	.attr = { "app_boot", 0644 },
	.show = &app_boot_show,
	.store = &app_boot_store,
};

static ssize_t pcb_version_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", get_pcb_version());
}

static struct kobj_attribute pcb_version_attr = {
	.attr = {"pcb_version", 0444},
	.show = pcb_version_show,
};

static ssize_t rf_version_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", get_rf_version());
}

static struct kobj_attribute rf_version_attr = {
	.attr = {"rf_version", 0444},
	.show = rf_version_show,
};

static ssize_t closemodem_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	//writing '1' to close and '0' to open
	//pr_err("closemodem buf[0] = 0x%x",buf[0]);
	switch (buf[0]) {
		case 0x30:
			break;
		case 0x31:
			//pr_err("closemodem now");
			gpio_direction_output(27, 0);
			mdelay(4000);
			break;
		default:
			break;
	}

	return count;
}

struct kobj_attribute closemodem_attr = {
	.attr = {"closemodem", 0644},
	//.show = &closemodem_show,
	.store = &closemodem_store
};

static ssize_t ftmmode_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", get_boot_mode());
}

struct kobj_attribute ftmmode_attr = {
	.attr = {"ftmmode", 0644},
	.show = &ftmmode_show,
};

static struct attribute *systeminfo_attr_list[] = {
	&startup_mode_attr.attr,
	&app_boot_attr.attr,
	&pcb_version_attr.attr,
	&rf_version_attr.attr,
	&ftmmode_attr.attr,
	&closemodem_attr.attr,
	NULL,
};

static struct attribute_group systeminfo_attr_group = {
	.attrs = systeminfo_attr_list,
};

static struct kobject *systeminfo_kobj;

static int __init oppo_attributes_init(void)
{
	int rc;

	systeminfo_kobj = kobject_create_and_add("systeminfo", NULL);
	if (systeminfo_kobj == NULL) {
		pr_err("%s: Failed to create system info kobject", __func__);
		return -ENOMEM;
	}
	rc = sysfs_create_group(systeminfo_kobj, &systeminfo_attr_group);
	if (rc != 0) {
		pr_err("%s: Failed to create system info sysfs node: %d",
				__func__, rc);
		return rc;
	}

	return 0;
}

static void __exit oppo_attributes_exit(void)
{
	sysfs_remove_group(systeminfo_kobj, &systeminfo_attr_group);
	kobject_del(systeminfo_kobj);
}

module_init(oppo_attributes_init);
module_exit(oppo_attributes_exit);
MODULE_DESCRIPTION("Oppo system attributes module");
MODULE_AUTHOR("The CyanogenMod Project");
MODULE_LICENSE("GPL");
