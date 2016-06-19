/*
 * arch/arm/mach-msm/kcal_ctrl.c
 *
 * Copyright (c) 2013, LGE Inc. All rights reserved
 * Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014 Paul Reioux <reioux@gmail.com>
 * Copyright (c) 2014 Alex Deddo <adeddo27@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <mach/kcal.h>

#ifdef CONFIG_LCD_KCAL

static struct kcal_platform_data *kcal_pdata;
static int last_status_kcal_ctrl;

static ssize_t kcal_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	int kcal_r = 0;
	int kcal_g = 0;
	int kcal_b = 0;

	if (!count)
		return -EINVAL;

	sscanf(buf, "%d %d %d", &kcal_r, &kcal_g, &kcal_b);
	kcal_pdata->set_values(kcal_r, kcal_g, kcal_b);
	return count;
}

static ssize_t kcal_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	int kcal_r = 0;
	int kcal_g = 0;
	int kcal_b = 0;

	kcal_pdata->get_values(&kcal_r, &kcal_g, &kcal_b);

	return sprintf(buf, "%d %d %d\n", kcal_r, kcal_g, kcal_b);
}

static ssize_t kcal_ctrl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int cmd = 0;

	if (!count)
		return last_status_kcal_ctrl = -EINVAL;

	sscanf(buf, "%d", &cmd);

	if(cmd != 1)
		return last_status_kcal_ctrl = -EINVAL;

	last_status_kcal_ctrl = kcal_pdata->refresh_display();

	if(last_status_kcal_ctrl)
	{
		return -EINVAL;
	}
	else
	{
		return count;
	}
}

static ssize_t kcal_ctrl_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	if(last_status_kcal_ctrl)
		return sprintf(buf, "NG\n");
	else
		return sprintf(buf, "OK\n");
}

static ssize_t kcal_min_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	int kcal_min = 0;

	if (!count)
		return -EINVAL;

	sscanf(buf, "%d", &kcal_min);
	kcal_pdata->set_min(kcal_min);
	return count;
}

static ssize_t kcal_min_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	int kcal_min = 0;

	kcal_pdata->get_min(&kcal_min);
	return sprintf(buf, "%d\n", kcal_min);
}

static DEVICE_ATTR(kcal, 0644, kcal_show, kcal_store);
static DEVICE_ATTR(kcal_ctrl, 0644, kcal_ctrl_show, kcal_ctrl_store);
static DEVICE_ATTR(kcal_min, 0644, kcal_min_show, kcal_min_store);

static int kcal_ctrl_probe(struct platform_device *pdev)
{
	int rc = 0;

	kcal_pdata = pdev->dev.platform_data;

	if(!kcal_pdata->set_values || !kcal_pdata->get_values ||
					!kcal_pdata->refresh_display) {
		return -1;
	}

	rc = device_create_file(&pdev->dev, &dev_attr_kcal);
	if(rc !=0)
		return -1;
	rc = device_create_file(&pdev->dev, &dev_attr_kcal_ctrl);
	if(rc !=0)
		return -1;
	rc = device_create_file(&pdev->dev, &dev_attr_kcal_min);
	if(rc !=0)
		return -1;

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = kcal_ctrl_probe,
	.driver = {
		.name   = "kcal_ctrl",
	},
};

int __init kcal_ctrl_init(void)
{
	return platform_driver_register(&this_driver);
}

device_initcall(kcal_ctrl_init);
#endif

MODULE_DESCRIPTION("LCD KCAL Driver");
MODULE_LICENSE("GPL v2");
