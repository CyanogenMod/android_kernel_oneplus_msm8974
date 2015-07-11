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

#ifndef MDSS_LIVEDISPLAY_H
#define MDSS_LIVEDISPLAY_H

#include <linux/of.h>
#include <linux/sysfs.h>

#include "mdss_dsi.h"

struct mdss_livedisplay_ctx {
	struct dsi_panel_cmds cabc_off_cmd;
	struct dsi_panel_cmds cabc_ui_cmd;
	struct dsi_panel_cmds cabc_image_cmd;
	struct dsi_panel_cmds cabc_video_cmd;
	struct dsi_panel_cmds cabc_sre_cmd;
	struct dsi_panel_cmds color_enhance_on_cmd;
	struct dsi_panel_cmds color_enhance_off_cmd;

	unsigned int cabc_mode;
	bool sre_enabled;
	bool ce_enabled;

	unsigned int caps;

	struct mutex lock;
};

enum {
	CABC_OFF,
	CABC_UI,
	CABC_IMAGE,
	CABC_VIDEO,
	CABC_MAX
};

enum {
	MODE_CABC = 1,
	MODE_SRE = 2,
	MODE_COLOR_ENHANCE = 4,
    MODE_UPDATE_ALL = MODE_CABC | MODE_SRE | MODE_COLOR_ENHANCE,
};

int mdss_livedisplay_update(struct mdss_dsi_ctrl_pdata *ctrl_pdata, int types);
int mdss_livedisplay_parse_dt(struct device_node *np, struct mdss_panel_info *pinfo);
int mdss_livedisplay_create_sysfs(struct msm_fb_data_type *mfd);

#endif
