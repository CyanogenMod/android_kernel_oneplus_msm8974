/*
 * Copyright (c) 2015 The CyanogenMod Project
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

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "mdss_dsi.h"
#include "mdss_fb.h"
#include "mdss_mdp.h"
#include "mdss_livedisplay.h"


extern void mdss_dsi_panel_cmds_send(struct mdss_dsi_ctrl_pdata *ctrl,
		struct dsi_panel_cmds *pcmds);

extern int mdss_dsi_parse_dcs_cmds(struct device_node *np,
		struct dsi_panel_cmds *pcmds, char *cmd_key, char *link_key);


int mdss_livedisplay_update(struct mdss_dsi_ctrl_pdata *ctrl_pdata,
		int types)
{
	int ret = 0;
	struct mdss_panel_info *pinfo = NULL;
	struct mdss_livedisplay_ctx *mlc = NULL;

	if (ctrl_pdata == NULL)
		return -ENODEV;

	pinfo = &(ctrl_pdata->panel_data.panel_info);
	if (pinfo->panel_power_state != MDSS_PANEL_POWER_ON)
		return 0;

	mlc = pinfo->livedisplay;
	if (mlc == NULL)
		return -ENODEV;

	if (mlc->caps & MODE_CABC && (types & MODE_CABC || types & MODE_SRE)) {

		pr_info("%s: update cabc level=%d  sre=%d\n", __func__,
				mlc->cabc_mode, mlc->sre_enabled);

		if (mlc->caps & MODE_SRE && mlc->sre_enabled) {
			mdss_dsi_panel_cmds_send(ctrl_pdata, &(mlc->cabc_sre_cmd));
		} else {

			switch (mlc->cabc_mode)
			{
				case CABC_OFF:
					mdss_dsi_panel_cmds_send(ctrl_pdata, &(mlc->cabc_off_cmd));
					break;
				case CABC_UI:
					mdss_dsi_panel_cmds_send(ctrl_pdata, &(mlc->cabc_ui_cmd));
					break;
				case CABC_IMAGE:
					mdss_dsi_panel_cmds_send(ctrl_pdata, &(mlc->cabc_image_cmd));
					break;
				case CABC_VIDEO:
					mdss_dsi_panel_cmds_send(ctrl_pdata, &(mlc->cabc_video_cmd));
					break;
				default:
					pr_err("%s: cabc level %d is not supported!\n",__func__, mlc->cabc_mode);
					break;
			}
		}
	}

	if (mlc->caps & MODE_COLOR_ENHANCE && types & MODE_COLOR_ENHANCE) {
		if (mlc->ce_enabled) {
			mdss_dsi_panel_cmds_send(ctrl_pdata, &(mlc->color_enhance_on_cmd));
		} else {
			mdss_dsi_panel_cmds_send(ctrl_pdata, &(mlc->color_enhance_off_cmd));
		}
	}

	return ret;
}

static struct mdss_livedisplay_ctx* get_ctx(struct msm_fb_data_type *mfd)
{
	return mfd->panel_info->livedisplay;
}

static struct mdss_dsi_ctrl_pdata* get_ctrl(struct msm_fb_data_type *mfd)
{
	struct mdss_panel_data *pdata = dev_get_platdata(&mfd->pdev->dev);
	return container_of(pdata, struct mdss_dsi_ctrl_pdata, panel_data);
}

static ssize_t mdss_livedisplay_get_cabc(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_livedisplay_ctx *mlc = get_ctx(mfd);

	return sprintf(buf, "%d\n", mlc->cabc_mode);
}

static ssize_t mdss_livedisplay_set_cabc(struct device *dev,
							   struct device_attribute *attr,
							   const char *buf, size_t count)
{
	int level = 0;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_livedisplay_ctx *mlc = get_ctx(mfd);

	sscanf(buf, "%du", &level);
	if (level >= CABC_OFF && level < CABC_MAX &&
				level != mlc->cabc_mode) {
		mlc->cabc_mode = level;
		if (!mlc->sre_enabled)
			mdss_livedisplay_update(get_ctrl(mfd), MODE_CABC);
	}
	return count;
}

static ssize_t mdss_livedisplay_get_sre(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_livedisplay_ctx *mlc = get_ctx(mfd);

	return sprintf(buf, "%d\n", mlc->sre_enabled);
}

static ssize_t mdss_livedisplay_set_sre(struct device *dev,
							   struct device_attribute *attr,
							   const char *buf, size_t count)
{
	int value = 0;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_livedisplay_ctx *mlc = get_ctx(mfd);

	sscanf(buf, "%du", &value);
	if ((value == 0 || value == 1)
			&& value != mlc->sre_enabled) {
		mlc->sre_enabled = value;
		mdss_livedisplay_update(get_ctrl(mfd), MODE_SRE);
	}
	return count;
}

static ssize_t mdss_livedisplay_get_color_enhance(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_livedisplay_ctx *mlc = get_ctx(mfd);

	return sprintf(buf, "%d\n", mlc->ce_enabled);
}

static ssize_t mdss_livedisplay_set_color_enhance(struct device *dev,
							   struct device_attribute *attr,
							   const char *buf, size_t count)
{
	int value = 0;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_livedisplay_ctx *mlc = get_ctx(mfd);

	sscanf(buf, "%du", &value);
	if ((value == 0 || value == 1)
			&& value != mlc->ce_enabled) {
		mlc->ce_enabled = value;
		mdss_livedisplay_update(get_ctrl(mfd), MODE_COLOR_ENHANCE);
	}
	return count;
}

static ssize_t mdss_livedisplay_get_rgb(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 copyback = 0;
	struct mdp_pcc_cfg_data pcc_cfg;
	unsigned int pcc_r = 32768, pcc_g = 32768, pcc_b = 32768;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;

	if (mfd == NULL)
		return -ENODEV;

	memset(&pcc_cfg, 0, sizeof(struct mdp_pcc_cfg_data));

	pcc_cfg.block = mfd->index + MDP_LOGICAL_BLOCK_DISP_0;
	pcc_cfg.ops = MDP_PP_OPS_READ;

	mdss_mdp_pcc_config(&pcc_cfg, &copyback);

	/* We disable pcc when using default values and reg
	 * are zeroed on pp resume, so ignore empty values.
	 */
	if (pcc_cfg.r.r && pcc_cfg.g.g && pcc_cfg.b.b) {
		pcc_r = pcc_cfg.r.r;
		pcc_g = pcc_cfg.g.g;
		pcc_b = pcc_cfg.b.b;
	}

	return scnprintf(buf, PAGE_SIZE, "%d %d %d\n", pcc_r, pcc_g, pcc_b);
}

/**
 * simple color temperature interface using polynomial color correction
 *
 * input values are r/g/b adjustments from 0-32768 representing 0 -> 1
 *
 * example adjustment @ 3500K:
 * 1.0000 / 0.5515 / 0.2520 = 32768 / 25828 / 17347
 *
 * reference chart:
 * http://www.vendian.org/mncharity/dir3/blackbody/UnstableURLs/bbr_color.html
 */
static ssize_t mdss_livedisplay_set_rgb(struct device *dev,
							struct device_attribute *attr,
							const char *buf, size_t count)
{
	uint32_t r = 0, g = 0, b = 0;
	struct mdp_pcc_cfg_data pcc_cfg;
	u32 copyback = 0;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;

	if (mfd == NULL)
		return -ENODEV;

	if (count > 19)
		return -EINVAL;

	sscanf(buf, "%d %d %d", &r, &g, &b);

	if (r < 0 || r > 32768)
		return -EINVAL;
	if (g < 0 || g > 32768)
		return -EINVAL;
	if (b < 0 || b > 32768)
		return -EINVAL;

	pr_info("%s: r=%d g=%d b=%d", __func__, r, g, b);

	memset(&pcc_cfg, 0, sizeof(struct mdp_pcc_cfg_data));

	pcc_cfg.block = mfd->index + MDP_LOGICAL_BLOCK_DISP_0;
	if (r == 32768 && g == 32768 && b == 32768)
		pcc_cfg.ops = MDP_PP_OPS_DISABLE;
	else
		pcc_cfg.ops = MDP_PP_OPS_ENABLE;
	pcc_cfg.ops |= MDP_PP_OPS_WRITE;
	pcc_cfg.r.r = r;
	pcc_cfg.g.g = g;
	pcc_cfg.b.b = b;

	if (mdss_mdp_pcc_config(&pcc_cfg, &copyback) == 0)
		return count;

	return -EINVAL;
}

static DEVICE_ATTR(cabc, S_IRUGO | S_IWUSR | S_IWGRP, mdss_livedisplay_get_cabc, mdss_livedisplay_set_cabc);
//static DEVICE_ATTR(gamma, S_IRUGO | S_IWUSR | S_IWGRP, mdss_livedisplay_get_gamma_index, mdss_livedisplay_set_gamma_index);
static DEVICE_ATTR(sre, S_IRUGO | S_IWUSR | S_IWGRP, mdss_livedisplay_get_sre, mdss_livedisplay_set_sre);
static DEVICE_ATTR(color_enhance, S_IRUGO | S_IWUSR | S_IWGRP, mdss_livedisplay_get_color_enhance, mdss_livedisplay_set_color_enhance);
static DEVICE_ATTR(rgb, S_IRUGO | S_IWUSR | S_IWGRP, mdss_livedisplay_get_rgb, mdss_livedisplay_set_rgb);

int mdss_livedisplay_parse_dt(struct device_node *np, struct mdss_panel_info *pinfo)
{
	int rc = 0;
	struct mdss_livedisplay_ctx *mlc;

	if (pinfo == NULL)
		return -ENODEV;

	mlc = kzalloc(sizeof(struct mdss_livedisplay_ctx), GFP_KERNEL);

	rc = mdss_dsi_parse_dcs_cmds(np, &(mlc->cabc_off_cmd),
			"cm,mdss-livedisplay-cabc-off", "qcom,mdss-dsi-off-command-state");

	if (rc == 0) {
		rc = mdss_dsi_parse_dcs_cmds(np, &(mlc->cabc_ui_cmd),
				"cm,mdss-livedisplay-cabc-ui", "qcom,mdss-dsi-off-command-state");
		if (rc == 0) {
			mlc->caps |= MODE_CABC;
			mdss_dsi_parse_dcs_cmds(np, &(mlc->cabc_image_cmd),
					"cm,mdss-livedisplay-cabc-image", "qcom,mdss-dsi-off-command-state");
			mdss_dsi_parse_dcs_cmds(np, &(mlc->cabc_video_cmd),
					"cm,mdss-livedisplay-cabc-video", "qcom,mdss-dsi-off-command-state");
			rc = mdss_dsi_parse_dcs_cmds(np, &(mlc->cabc_sre_cmd),
					"cm,mdss-livedisplay-cabc-sre", "qcom,mdss-dsi-off-command-state");
			if (rc == 0)
				mlc->caps |= MODE_SRE;
		}
	}

	rc = mdss_dsi_parse_dcs_cmds(np, &(mlc->color_enhance_on_cmd),
			"cm,mdss-livedisplay-color-enhance-on", "qcom,mdss-dsi-off-command-state");

	if (rc == 0) {
		rc = mdss_dsi_parse_dcs_cmds(np, &(mlc->color_enhance_off_cmd),
				"cm,mdss-livedisplay-color-enhance-off", "qcom,mdss-dsi-off-command-state");
		if (rc == 0)
			mlc->caps |= MODE_COLOR_ENHANCE;
	}

    pinfo->livedisplay = mlc;
	return 0;
}

int mdss_livedisplay_create_sysfs(struct msm_fb_data_type *mfd)
{
	int rc = 0;
	struct mdss_livedisplay_ctx *mlc = get_ctx(mfd);

	if (mlc == NULL)
		goto sysfs_err;

	rc = sysfs_create_file(&mfd->fbi->dev->kobj, &dev_attr_rgb.attr);
	if (rc)
		goto sysfs_err;

	if (mlc->caps & MODE_CABC) {
		rc = sysfs_create_file(&mfd->fbi->dev->kobj, &dev_attr_cabc.attr);
		if (rc)
			goto sysfs_err;
	}

	if (mlc->caps & MODE_SRE) {
		rc = sysfs_create_file(&mfd->fbi->dev->kobj, &dev_attr_sre.attr);
		if (rc)
			goto sysfs_err;
	}

	if (mlc->caps & MODE_COLOR_ENHANCE) {
		rc = sysfs_create_file(&mfd->fbi->dev->kobj, &dev_attr_color_enhance.attr);
		if (rc)
			goto sysfs_err;
	}

	return rc;

sysfs_err:
	pr_err("%s: sysfs creation failed, rc=%d", __func__, rc);
	return rc;
}

