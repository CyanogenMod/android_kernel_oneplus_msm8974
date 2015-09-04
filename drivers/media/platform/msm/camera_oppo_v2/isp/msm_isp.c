/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <mach/board.h>
#include <mach/vreg.h>
#include <mach/iommu.h>

#include "msm_isp.h"
#include "msm_isp_util.h"
#include "msm_isp_axi_util.h"
#include "msm_isp_stats_util.h"
#include "msm_sd.h"
#include "msm_isp40.h"
#include "msm_isp32.h"

static struct msm_sd_req_vb2_q vfe_vb2_ops;

static const struct of_device_id msm_vfe_dt_match[] = {
	{
		.compatible = "qcom,vfe40",
		.data = &vfe40_hw_info,
	},
	{
		.compatible = "qcom,vfe32",
		.data = &vfe32_hw_info,
	},
	{}
};

MODULE_DEVICE_TABLE(of, msm_vfe_dt_match);

static const struct platform_device_id msm_vfe_dev_id[] = {
	{"msm_vfe32", (kernel_ulong_t) &vfe32_hw_info},
	{}
};

#define MAX_OVERFLOW_COUNTERS  15
#define OVERFLOW_LENGTH 512
#define OVERFLOW_BUFFER_LENGTH 32
static struct msm_isp_buf_mgr vfe_buf_mgr;
static int msm_isp_enable_debugfs(struct msm_isp_statistics *stats);
static char *stats_str[MAX_OVERFLOW_COUNTERS] = {
	"imgmaster0_overflow_cnt",
	"imgmaster1_overflow_cnt",
	"imgmaster2_overflow_cnt",
	"imgmaster3_overflow_cnt",
	"imgmaster4_overflow_cnt",
	"imgmaster5_overflow_cnt",
	"imgmaster6_overflow_cnt",
	"be_overflow_cnt",
	"bg_overflow_cnt",
	"bf_overflow_cnt",
	"awb_overflow_cnt",
	"rs_overflow_cnt",
	"cs_overflow_cnt",
	"ihist_overflow_cnt",
	"skinbhist_overflow_cnt",
};
static int vfe_debugfs_statistics_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t vfe_debugfs_statistics_read(struct file *t_file, char *t_char,
	size_t t_size_t, loff_t *t_loff_t)
{
	int i;
	char name[OVERFLOW_LENGTH] = {0};
	int *ptr;
	char buffer[OVERFLOW_BUFFER_LENGTH] = {0};
	struct msm_isp_statistics  *stats = (struct msm_isp_statistics *)
		t_file->private_data;
	ptr = (int *)(stats);
	for (i = 0; i < MAX_OVERFLOW_COUNTERS; i++) {
		strlcat(name, stats_str[i], sizeof(name));
		strlcat(name, "     ", sizeof(name));
		snprintf(buffer, sizeof(buffer), "%d", ptr[i]);
		strlcat(name, buffer, sizeof(name));
		strlcat(name, "\r\n", sizeof(name));
	}
	return simple_read_from_buffer(t_char, t_size_t,
		t_loff_t, name, strlen(name));
}

static ssize_t vfe_debugfs_statistics_write(struct file *t_file,
	const char *t_char, size_t t_size_t, loff_t *t_loff_t)
{
	struct msm_isp_statistics *stats = (struct msm_isp_statistics *)
		t_file->private_data;
	memset(stats, 0, sizeof(struct msm_isp_statistics));

	return sizeof(struct msm_isp_statistics);
}

static const struct file_operations vfe_debugfs_error = {
	.open = vfe_debugfs_statistics_open,
	.read = vfe_debugfs_statistics_read,
	.write = vfe_debugfs_statistics_write,
};

static int msm_isp_enable_debugfs(struct msm_isp_statistics *stats)
{
	struct dentry *debugfs_base;
	debugfs_base = debugfs_create_dir("msm_isp", NULL);
	if (!debugfs_base)
		return -ENOMEM;
	if (!debugfs_create_file("stats", S_IRUGO | S_IWUSR, debugfs_base,
		stats, &vfe_debugfs_error))
		return -ENOMEM;
	return 0;
}
static int __devinit vfe_probe(struct platform_device *pdev)
{
	struct vfe_device *vfe_dev;
	/*struct msm_cam_subdev_info sd_info;*/
	const struct of_device_id *match_dev;
	int rc = 0;

	struct msm_iova_partition vfe_partition = {
		.start = SZ_128K,
		.size = SZ_2G - SZ_128K,
	};
	struct msm_iova_layout vfe_layout = {
		.partitions = &vfe_partition,
		.npartitions = 1,
		.client_name = "vfe",
		.domain_flags = 0,
	};

	vfe_dev = kzalloc(sizeof(struct vfe_device), GFP_KERNEL);
	vfe_dev->stats = kzalloc(sizeof(struct msm_isp_statistics), GFP_KERNEL);
	if (!vfe_dev) {
		pr_err("%s: no enough memory\n", __func__);
		return -ENOMEM;
	}

	if (pdev->dev.of_node) {
		of_property_read_u32((&pdev->dev)->of_node,
			"cell-index", &pdev->id);
		match_dev = of_match_device(msm_vfe_dt_match, &pdev->dev);
		vfe_dev->hw_info =
			(struct msm_vfe_hardware_info *) match_dev->data;
	} else {
		vfe_dev->hw_info = (struct msm_vfe_hardware_info *)
			platform_get_device_id(pdev)->driver_data;
	}

	if (!vfe_dev->hw_info) {
		pr_err("%s: No vfe hardware info\n", __func__);
		return -EINVAL;
	}
	ISP_DBG("%s: device id = %d\n", __func__, pdev->id);

	vfe_dev->pdev = pdev;
	rc = vfe_dev->hw_info->vfe_ops.core_ops.get_platform_data(vfe_dev);
	if (rc < 0) {
		pr_err("%s: failed to get platform resources\n", __func__);
		kfree(vfe_dev);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&vfe_dev->tasklet_q);
	tasklet_init(&vfe_dev->vfe_tasklet,
		msm_isp_do_tasklet, (unsigned long)vfe_dev);

	v4l2_subdev_init(&vfe_dev->subdev.sd, vfe_dev->hw_info->subdev_ops);
	vfe_dev->subdev.sd.internal_ops =
		vfe_dev->hw_info->subdev_internal_ops;
	snprintf(vfe_dev->subdev.sd.name,
		ARRAY_SIZE(vfe_dev->subdev.sd.name),
		"vfe");
	vfe_dev->subdev.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	vfe_dev->subdev.sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS;
	v4l2_set_subdevdata(&vfe_dev->subdev.sd, vfe_dev);
	platform_set_drvdata(pdev, &vfe_dev->subdev.sd);
	mutex_init(&vfe_dev->realtime_mutex);
	mutex_init(&vfe_dev->core_mutex);
	spin_lock_init(&vfe_dev->tasklet_lock);
	spin_lock_init(&vfe_dev->shared_data_lock);
	media_entity_init(&vfe_dev->subdev.sd.entity, 0, NULL, 0);
	vfe_dev->subdev.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	vfe_dev->subdev.sd.entity.group_id = MSM_CAMERA_SUBDEV_VFE;
	vfe_dev->subdev.sd.entity.name = pdev->name;
	vfe_dev->subdev.close_seq = MSM_SD_CLOSE_1ST_CATEGORY | 0x2;
	rc = msm_sd_register(&vfe_dev->subdev);
	if (rc != 0) {
		pr_err("%s: msm_sd_register error = %d\n", __func__, rc);
		kfree(vfe_dev);
		goto end;
	}

	vfe_dev->buf_mgr = &vfe_buf_mgr;
	v4l2_subdev_notify(&vfe_dev->subdev.sd,
		MSM_SD_NOTIFY_REQ_CB, &vfe_vb2_ops);
	rc = msm_isp_create_isp_buf_mgr(vfe_dev->buf_mgr,
		&vfe_vb2_ops, &vfe_layout);
	if (rc < 0) {
		pr_err("%s: Unable to create buffer manager\n", __func__);
		msm_sd_unregister(&vfe_dev->subdev);
		kfree(vfe_dev);
		return -EINVAL;
	}
	msm_isp_enable_debugfs(vfe_dev->stats);
	vfe_dev->buf_mgr->ops->register_ctx(vfe_dev->buf_mgr,
		&vfe_dev->iommu_ctx[0], vfe_dev->hw_info->num_iommu_ctx);
	vfe_dev->vfe_open_cnt = 0;
end:
	return rc;
}

static struct platform_driver vfe_driver = {
	.probe = vfe_probe,
	.driver = {
		.name = "msm_vfe",
		.owner = THIS_MODULE,
		.of_match_table = msm_vfe_dt_match,
	},
	.id_table = msm_vfe_dev_id,
};

static int __init msm_vfe_init_module(void)
{
	return platform_driver_register(&vfe_driver);
}

static void __exit msm_vfe_exit_module(void)
{
	platform_driver_unregister(&vfe_driver);
}

module_init(msm_vfe_init_module);
module_exit(msm_vfe_exit_module);
MODULE_DESCRIPTION("MSM VFE driver");
MODULE_LICENSE("GPL v2");
