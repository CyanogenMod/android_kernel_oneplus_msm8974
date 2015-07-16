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
#include "mdss_fb.h"

#define MAX_PRESETS 10

struct mdss_livedisplay_ctx {
	uint8_t cabc_ui_value;
	uint8_t cabc_image_value;
	uint8_t cabc_video_value;
	uint8_t sre_weak_value;
	uint8_t sre_medium_value;
	uint8_t sre_strong_value;
	uint8_t aco_value;

	const uint8_t *ce_off_cmds;
	const uint8_t *ce_on_cmds;
	unsigned int ce_off_cmds_len;
	unsigned int ce_on_cmds_len;

	const uint8_t *presets[MAX_PRESETS];
	unsigned int presets_len[MAX_PRESETS];

	const uint8_t *cabc_cmds;
	unsigned int cabc_cmds_len;

	const uint8_t *post_cmds;
	unsigned int post_cmds_len;

	unsigned int preset;
	unsigned int cabc_level;
	unsigned int sre_level;
	bool aco_enabled;
	bool ce_enabled;

	unsigned int num_presets;
	unsigned int caps;

	uint32_t r, g, b;
	struct msm_fb_data_type *mfd;

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
	SRE_OFF,
	SRE_WEAK,
	SRE_MEDIUM,
	SRE_STRONG,
	SRE_MAX
};

enum {
	MODE_CABC		= 0x01,
	MODE_SRE		= 0x02,
	MODE_AUTO_CONTRAST	= 0x04,
	MODE_COLOR_ENHANCE	= 0x08,
	MODE_PRESET		= 0x10,
	MODE_UPDATE_ALL		= 0xFF,
};

int mdss_livedisplay_update(struct mdss_dsi_ctrl_pdata *ctrl_pdata, int types);
int mdss_livedisplay_parse_dt(struct device_node *np, struct mdss_panel_info *pinfo);
int mdss_livedisplay_create_sysfs(struct msm_fb_data_type *mfd);

static inline bool is_cabc_cmd(unsigned int value)
{
    return (value & MODE_CABC) || (value & MODE_SRE) || (value & MODE_AUTO_CONTRAST);
}

static inline struct mdss_livedisplay_ctx* get_ctx(struct msm_fb_data_type *mfd)
{
    return mfd->panel_info->livedisplay;
}

static inline struct mdss_dsi_ctrl_pdata* get_ctrl(struct msm_fb_data_type *mfd)
{
    struct mdss_panel_data *pdata = dev_get_platdata(&mfd->pdev->dev);
    return container_of(pdata, struct mdss_dsi_ctrl_pdata, panel_data);
}

#endif
