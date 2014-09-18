/*
 * Author: andip71, 18.09.2014
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/charge_level.h>


/* sysfs interface for charge levels */

static ssize_t charge_level_ac_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{

	// print current value
	return sprintf(buf, "%d mA", ac_level);

}


static ssize_t charge_level_ac_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	unsigned int ret = -EINVAL;
	int val;

	// read value from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;
		
	// check whether value is within the valid ranges and adjust accordingly
	if (val > AC_CHARGE_LEVEL_MAX)
		val = AC_CHARGE_LEVEL_MAX;

	if (val < AC_CHARGE_LEVEL_MIN)
		val = AC_CHARGE_LEVEL_MIN;

	// store value
	ac_level = val;

	return count;
}


static ssize_t charge_level_usb_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{

	// print current value
	return sprintf(buf, "%d mA", usb_level);

}


static ssize_t charge_level_usb_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	unsigned int ret = -EINVAL;
	int val;

	// read value from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;
		
	// check whether value is within the valid ranges and adjust accordingly
	if (val > USB_CHARGE_LEVEL_MAX)
		val = USB_CHARGE_LEVEL_MAX;

	if (val < USB_CHARGE_LEVEL_MIN)
		val = USB_CHARGE_LEVEL_MIN;

	// store value
	usb_level = val;

	return count;
}


static ssize_t charge_info_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{

	// print charge info
	return sprintf(buf, "%s / %d mA", charge_info_text, charge_info_level);

}


/* Initialize charge level sysfs folder */

static struct kobj_attribute charge_level_ac_attribute =
__ATTR(charge_level_ac, 0666, charge_level_ac_show, charge_level_ac_store);

static struct kobj_attribute charge_level_usb_attribute =
__ATTR(charge_level_usb, 0666, charge_level_usb_show, charge_level_usb_store);

static struct kobj_attribute charge_info_attribute =
__ATTR(charge_info, 0666, charge_info_show, NULL);

static struct attribute *charge_level_attrs[] = {
&charge_level_ac_attribute.attr,
&charge_level_usb_attribute.attr,
&charge_info_attribute.attr,
NULL,
};

static struct attribute_group charge_level_attr_group = {
.attrs = charge_level_attrs,
};

static struct kobject *charge_level_kobj;


int charge_level_init(void)
{
	int charge_level_retval;

        charge_level_kobj = kobject_create_and_add("charge_levels", kernel_kobj);

        if (!charge_level_kobj)
	{
		printk("Charger-Control: failed to create kernel object for charge level interface.\n");
                return -ENOMEM;
        }

        charge_level_retval = sysfs_create_group(charge_level_kobj, &charge_level_attr_group);

        if (charge_level_retval)
	{
			kobject_put(charge_level_kobj);
		printk("Charger-Control: failed to create fs object for charge level interface.\n");
	        return (charge_level_retval);
	}

	// print debug info
	printk("Charger-Control: charge level interface started.\n");

        return (charge_level_retval);
}


void charge_level_exit(void)
{
	kobject_put(charge_level_kobj);

	// print debug info
	printk("Charger-Control: charge level interface stopped.\n");
}


/* define driver entry points */
module_init(charge_level_init);
module_exit(charge_level_exit);
