/*
 * arch/arm/mach-msm/kcal_ctrl.c
 *
 * Copyright (c) 2013, LGE Inc. All rights reserved
 * Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014 Paul Reioux <reioux@gmail.com>
 * Copyright (c) 2014 savoca <adeddo27@gmail.com>
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
#include "../../../../drivers/video/msm/mdss/mdss_fb.h"

static struct kcal_lut_data lut = {
	.r = 255,
	.g = 255,
	.b = 255,
	.min = 0,
	.stat = 0
};

static int kcal_set_values(int kcal_r, int kcal_g, int kcal_b)
{
	lut.r = kcal_r < lut.min ? lut.min : kcal_r;
	lut.g = kcal_g < lut.min ? lut.min : kcal_g;
	lut.b = kcal_b < lut.min ? lut.min : kcal_b;

	if (kcal_r < lut.min || kcal_g < lut.min || kcal_b < lut.min)
		update_preset_lcdc_lut(lut.r, lut.g, lut.b);

	return 0;
}

static int kcal_get_values(int *kcal_r, int *kcal_g, int *kcal_b)
{
	*kcal_r = lut.r;
	*kcal_g = lut.g;
	*kcal_b = lut.b;
	return 0;
}

static int kcal_set_min(int kcal_min)
{
	lut.min = kcal_min;

	if (lut.min > lut.r || lut.min > lut.g || lut.min > lut.b) {
		lut.r = lut.r < lut.min ? lut.min : lut.r;
		lut.g = lut.g < lut.min ? lut.min : lut.g;
		lut.b = lut.b < lut.min ? lut.min : lut.b;
		update_preset_lcdc_lut(lut.r, lut.b, lut.g);
	}

	return 0;
}

static int kcal_get_min(int *kcal_min)
{
	*kcal_min = lut.min;
	return 0;
}

static int kcal_refresh_values(void)
{
	return update_preset_lcdc_lut(lut.r, lut.g, lut.b);
}

static struct kcal_platform_data kcal_pdata = {
	.set_values = kcal_set_values,
	.get_values = kcal_get_values,
	.refresh_display = kcal_refresh_values,
	.set_min = kcal_set_min,
	.get_min = kcal_get_min
};

static ssize_t kcal_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	int kcal_r = 0;
	int kcal_g = 0;
	int kcal_b = 0;

	if (!count)
		return -EINVAL;

	sscanf(buf, "%d %d %d", &kcal_r, &kcal_g, &kcal_b);

	if (kcal_r < 0 || kcal_r > 255)
		return -EINVAL;

	if (kcal_r < 0 || kcal_r > 255)
		return -EINVAL;

	if (kcal_r < 0 || kcal_r > 255)
		return -EINVAL;

	kcal_pdata.set_values(kcal_r, kcal_g, kcal_b);
	return count;
}

static ssize_t kcal_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	int kcal_r = 0;
	int kcal_g = 0;
	int kcal_b = 0;

	kcal_pdata.get_values(&kcal_r, &kcal_g, &kcal_b);

	return sprintf(buf, "%d %d %d\n", kcal_r, kcal_g, kcal_b);
}

static ssize_t kcal_ctrl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int cmd = 0;

	if (!count)
		return lut.stat = -EINVAL;

	sscanf(buf, "%d", &cmd);

	if (cmd != 1)
		return lut.stat = -EINVAL;

	lut.stat = kcal_pdata.refresh_display();

	if (lut.stat)
		return -EINVAL;
	else
		return count;
}

static ssize_t kcal_ctrl_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	if (lut.stat)
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

	if (kcal_min < 0 || kcal_min > 255)
		return -EINVAL;

	kcal_pdata.set_min(kcal_min);
	return count;
}

static ssize_t kcal_min_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	int kcal_min = 0;

	kcal_pdata.get_min(&kcal_min);
	return sprintf(buf, "%d\n", kcal_min);
}

static DEVICE_ATTR(kcal, 0644, kcal_show, kcal_store);
static DEVICE_ATTR(kcal_ctrl, 0644, kcal_ctrl_show, kcal_ctrl_store);
static DEVICE_ATTR(kcal_min, 0644, kcal_min_show, kcal_min_store);

static struct platform_device kcal_ctrl_device = {
	.name = "kcal_ctrl",
	.dev = {
		.platform_data = &kcal_pdata,
	}
};

static int kcal_ctrl_probe(struct platform_device *pdev)
{
	int rc = 0;

	if(!kcal_pdata.set_values || !kcal_pdata.get_values ||
					!kcal_pdata.refresh_display)
		return -1;

	rc = device_create_file(&pdev->dev, &dev_attr_kcal);
	if (rc != 0)
		return -1;
	rc = device_create_file(&pdev->dev, &dev_attr_kcal_ctrl);
	if (rc != 0)
		return -1;
	rc = device_create_file(&pdev->dev, &dev_attr_kcal_min);
	if (rc != 0)
		return -1;

	return 0;
}

static struct platform_driver kcal_ctrl_driver = {
	.probe  = kcal_ctrl_probe,
	.driver = {
		.name   = "kcal_ctrl",
	},
};

int __init kcal_ctrl_init(void)
{
	int ret;

	ret = platform_driver_register(&kcal_ctrl_driver);
	if (ret) {
		pr_err("%s: driver register failed: %d\n", __func__, ret);
		return ret;
	}

	ret = platform_device_register(&kcal_ctrl_device);
	if (ret) {
		pr_err("%s: device register failed: %d\n", __func__, ret);
		return ret;
	}

	pr_info("%s: driver registered\n", __func__);

	return ret;
}

static void __exit kcal_ctrl_exit(void)
{
	platform_device_unregister(&kcal_ctrl_device);
	platform_driver_unregister(&kcal_ctrl_driver);
}

device_initcall(kcal_ctrl_init);
module_exit(kcal_ctrl_exit);

MODULE_DESCRIPTION("LCD KCAL Driver");
