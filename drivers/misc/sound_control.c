/*
 * Copyright 2014 Francisco Franco
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

extern void update_headphones_volume_boost(int vol_boost);
extern void update_speaker_gain(int vol_boost);
extern void update_mic_gain(int vol_boost);

/*
 * Volume boost value
 */
int headphones_boost = 0;
int headphones_boost_limit = 20;
int headphones_boost_limit_min = -20;

/*
 * Speaker boost value
 */
int speaker_boost = 0;
int speaker_boost_limit = 20;
int speaker_boost_limit_min = -20;

/*
 * Mic boost value
 */
int mic_boost = 0;
int mic_boost_limit = 20;
int mic_boost_limit_min = -20;

/*
 * Sysfs get/set entries
 */

static ssize_t headphones_boost_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", headphones_boost);
}

static ssize_t headphones_boost_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != headphones_boost) {
		if (new_val <= headphones_boost_limit_min)
			new_val = headphones_boost_limit_min;

		else if (new_val >= headphones_boost_limit)
			new_val = headphones_boost_limit;

		pr_info("New headphones_boost: %d\n", new_val);

		headphones_boost = new_val;
		update_headphones_volume_boost(headphones_boost);
	}

    return size;
}

static ssize_t speaker_boost_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", speaker_boost);
}

static ssize_t speaker_boost_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != speaker_boost) {
		if (new_val <= speaker_boost_limit_min)
			new_val = speaker_boost_limit_min;

		else if (new_val >= speaker_boost_limit)
			new_val = speaker_boost_limit;

		pr_info("New speaker_boost: %d\n", new_val);

		speaker_boost = new_val;
		update_speaker_gain(speaker_boost);
	}

    return size;
}

static ssize_t mic_boost_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", mic_boost);
}

static ssize_t mic_boost_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
    int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != mic_boost) {
		if (new_val <= mic_boost_limit_min)
			new_val = mic_boost_limit_min;

		else if (new_val >= mic_boost_limit)
			new_val = mic_boost_limit;

		pr_info("New mic_boost: %d\n", new_val);

		mic_boost = new_val;
		update_mic_gain(mic_boost);
	}

    return size;
}

static DEVICE_ATTR(volume_boost, 0664, headphones_boost_show, 
	headphones_boost_store);
static DEVICE_ATTR(speaker_boost, 0664, speaker_boost_show, 
	speaker_boost_store);
static DEVICE_ATTR(mic_boost, 0664, mic_boost_show, mic_boost_store);

static struct attribute *soundcontrol_attributes[] =
{
	&dev_attr_volume_boost.attr,
	&dev_attr_speaker_boost.attr,
	&dev_attr_mic_boost.attr,
	NULL
};

static struct attribute_group soundcontrol_group =
{
	.attrs  = soundcontrol_attributes,
};

static struct miscdevice soundcontrol_device =
{
	.minor = MISC_DYNAMIC_MINOR,
	.name = "soundcontrol",
};

static int __init soundcontrol_init(void)
{
    int ret;

    pr_info("%s misc_register(%s)\n", __FUNCTION__, soundcontrol_device.name);

    ret = misc_register(&soundcontrol_device);

    if (ret) {
	    pr_err("%s misc_register(%s) fail\n", __FUNCTION__, 
	    	soundcontrol_device.name);
	    return 1;
	}

	if (sysfs_create_group(&soundcontrol_device.this_device->kobj, 
    		&soundcontrol_group) < 0) {
	    pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
	    pr_err("Failed to create sysfs group for device (%s)!\n", 
	    	soundcontrol_device.name);
	}

    return 0;
}
late_initcall(soundcontrol_init);
