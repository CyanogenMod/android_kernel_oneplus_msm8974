/* Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/msm_mdp.h>
#include <linux/memblock.h>
#include <linux/sw_sync.h>

#include <mach/iommu_domains.h>
#include <mach/event_timer.h>
#include <mach/msm_bus.h>
#include <mach/scm.h>
#include "mdss.h"
#include "mdss_debug.h"
#include "mdss_fb.h"
#include "mdss_mdp.h"
#include "mdss_mdp_rotator.h"

#define VSYNC_PERIOD 16
#define BORDERFILL_NDX	0x0BF000BF
#define CHECK_BOUNDS(offset, size, max_size) \
	(((size) > (max_size)) || ((offset) > ((max_size) - (size))))

#define IS_RIGHT_MIXER_OV(flags, dst_x, left_lm_w)	\
	((flags & MDSS_MDP_RIGHT_MIXER) || (dst_x >= left_lm_w))

#define PP_CLK_CFG_OFF 0
#define PP_CLK_CFG_ON 1

#define OVERLAY_MAX 10
#define BUF_POOL_SIZE 32

static int mdss_mdp_overlay_free_fb_pipe(struct msm_fb_data_type *mfd);
static int mdss_mdp_overlay_fb_parse_dt(struct msm_fb_data_type *mfd);
static int mdss_mdp_overlay_off(struct msm_fb_data_type *mfd);
static void __overlay_pipe_cleanup(struct msm_fb_data_type *mfd,
		struct mdss_mdp_pipe *pipe);
static void __overlay_kickoff_requeue(struct msm_fb_data_type *mfd);
static void __vsync_retire_signal(struct msm_fb_data_type *mfd, int val);
static int __vsync_set_vsync_handler(struct msm_fb_data_type *mfd);

static inline u32 left_lm_w_from_mfd(struct msm_fb_data_type *mfd)
{
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);
	return ctl->mixer_left->width;
}

static struct mdss_mdp_pipe *__overlay_find_pipe(
		struct msm_fb_data_type *mfd, u32 ndx)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_pipe *tmp, *pipe = NULL;

	mutex_lock(&mdp5_data->list_lock);
	list_for_each_entry(tmp, &mdp5_data->pipes_used, list) {
		if (tmp->ndx == ndx) {
			pipe = tmp;
			break;
		}
	}
	mutex_unlock(&mdp5_data->list_lock);

	return pipe;
}

static int mdss_mdp_overlay_get(struct msm_fb_data_type *mfd,
				struct mdp_overlay *req)
{
	struct mdss_mdp_pipe *pipe;

	pipe = __overlay_find_pipe(mfd, req->id);
	if (!pipe) {
		pr_err("invalid pipe ndx=%x\n", req->id);
		return pipe ? PTR_ERR(pipe) : -ENODEV;
	}

	*req = pipe->req_data;

	return 0;
}

/*
 * This function is modified from mainline version. Source-split
 * change is too large to port over onto certain code bases.
 * Source-split patch added a new way to determine if layer
 * is intended for right panel, by using x offset >= left LM
 * This function corrects the x offset.
 * Additionally, patches that fix other issues assumes that checks
 * and corrections in this function are in place.
 */
static int mdss_mdp_ov_xres_check(struct msm_fb_data_type *mfd,
	struct mdp_overlay *req)
{
	u32 xres = 0;
	u32 left_lm_w = left_lm_w_from_mfd(mfd);
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);

	if (IS_RIGHT_MIXER_OV(req->flags, req->dst_rect.x, left_lm_w)) {
		if (req->dst_rect.x >= left_lm_w) {
			/*
			 * this is a step towards removing a reliance on
			 * MDSS_MDP_RIGHT_MIXER flags. With the new src split
			 * code, some clients of non-src-split chipsets have
			 * stopped sending MDSS_MDP_RIGHT_MIXER flag and
			 * modified their xres relative to full panel
			 * dimensions. In such cases, we need to deduct left
			 * layer mixer width before we programm this HW.
			 */
			req->dst_rect.x -= left_lm_w;
			req->flags |= MDSS_MDP_RIGHT_MIXER;
		}

		if (ctl->mixer_right) {
			xres += ctl->mixer_right->width;
		} else {
			pr_err("ov cannot be placed on right mixer\n");
			return -EPERM;
		}
	} else {
		if (ctl->mixer_left) {
			xres = ctl->mixer_left->width;
		} else {
			pr_err("ov cannot be placed on left mixer\n");
			return -EPERM;
		}
	}

	if (CHECK_BOUNDS(req->dst_rect.x, req->dst_rect.w, xres)) {
		pr_err("dst_xres is invalid. dst_x:%d, dst_w:%d, xres:%d\n",
			req->dst_rect.x, req->dst_rect.w, xres);
		return -EOVERFLOW;
	}

	return 0;
}

int mdss_mdp_overlay_req_check(struct msm_fb_data_type *mfd,
			       struct mdp_overlay *req,
			       struct mdss_mdp_format_params *fmt)
{
	u32 yres;
	u32 min_src_size, min_dst_size;
	int content_secure;
	struct mdss_data_type *mdata = mfd_to_mdata(mfd);
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);

	yres = mfd->fbi->var.yres;

	content_secure = (req->flags & MDP_SECURE_OVERLAY_SESSION);
	if (!ctl->is_secure && content_secure &&
				 (mfd->panel.type == WRITEBACK_PANEL)) {
		pr_debug("return due to security concerns\n");
		return -EPERM;
	}
	if (mdata->mdp_rev >= MDSS_MDP_HW_REV_102) {
		min_src_size = fmt->is_yuv ? 2 : 1;
		min_dst_size = 1;
	} else {
		min_src_size = fmt->is_yuv ? 10 : 5;
		min_dst_size = 2;
	}

	if (req->z_order >= MDSS_MDP_MAX_STAGE) {
		pr_err("zorder %d out of range\n", req->z_order);
		return -ERANGE;
	}

	if (req->src.width > MAX_IMG_WIDTH ||
	    req->src.height > MAX_IMG_HEIGHT ||
	    req->src_rect.w < min_src_size || req->src_rect.h < min_src_size ||
	    CHECK_BOUNDS(req->src_rect.x, req->src_rect.w, req->src.width) ||
	    CHECK_BOUNDS(req->src_rect.y, req->src_rect.h, req->src.height)) {
#if 0
		pr_err("invalid source image img wh=%dx%d rect=%d,%d,%d,%d\n",
		       req->src.width, req->src.height,
		       req->src_rect.x, req->src_rect.y,
		       req->src_rect.w, req->src_rect.h);
#endif
		return -EOVERFLOW;
	}

	if (req->dst_rect.w < min_dst_size || req->dst_rect.h < min_dst_size) {
		pr_err("invalid destination resolution (%dx%d)",
		       req->dst_rect.w, req->dst_rect.h);
		return -EOVERFLOW;
	}

	if (req->horz_deci || req->vert_deci) {
		if (!mdata->has_decimation) {
			pr_err("No Decimation in MDP V=%x\n", mdata->mdp_rev);
			return -EINVAL;
		} else if ((req->horz_deci > MAX_DECIMATION) ||
				(req->vert_deci > MAX_DECIMATION))  {
			pr_err("Invalid decimation factors horz=%d vert=%d\n",
					req->horz_deci, req->vert_deci);
			return -EINVAL;
		} else if (req->flags & MDP_BWC_EN) {
			pr_err("Decimation can't be enabled with BWC\n");
			return -EINVAL;
		} else if (fmt->tile) {
			pr_err("Decimation can't be enabled with MacroTile format\n");
			return -EINVAL;
		}
	}

	if (!(req->flags & MDSS_MDP_ROT_ONLY)) {
		u32 src_w, src_h, dst_w, dst_h;

		if (CHECK_BOUNDS(req->dst_rect.y, req->dst_rect.h, yres)) {
			pr_err("invalid destination rect=%d,%d,%d,%d\n",
			       req->dst_rect.x, req->dst_rect.y,
			       req->dst_rect.w, req->dst_rect.h);
			return -EOVERFLOW;
		}

		if (req->flags & MDP_ROT_90) {
			dst_h = req->dst_rect.w;
			dst_w = req->dst_rect.h;
		} else {
			dst_w = req->dst_rect.w;
			dst_h = req->dst_rect.h;
		}

		src_w = DECIMATED_DIMENSION(req->src_rect.w, req->horz_deci);
		src_h = DECIMATED_DIMENSION(req->src_rect.h, req->vert_deci);

		if (src_w > MAX_MIXER_WIDTH) {
			pr_err("invalid source width=%d HDec=%d\n",
					req->src_rect.w, req->horz_deci);
			return -EINVAL;
		}

		if ((src_w * MAX_UPSCALE_RATIO) < dst_w) {
			pr_err("too much upscaling Width %d->%d\n",
			       req->src_rect.w, req->dst_rect.w);
			return -EINVAL;
		}

		if ((src_h * MAX_UPSCALE_RATIO) < dst_h) {
			pr_err("too much upscaling. Height %d->%d\n",
			       req->src_rect.h, req->dst_rect.h);
			return -EINVAL;
		}

		if (src_w > (dst_w * MAX_DOWNSCALE_RATIO)) {
			pr_err("too much downscaling. Width %d->%d H Dec=%d\n",
			       src_w, req->dst_rect.w, req->horz_deci);
			return -EINVAL;
		}

		if (src_h > (dst_h * MAX_DOWNSCALE_RATIO)) {
			pr_err("too much downscaling. Height %d->%d V Dec=%d\n",
			       src_h, req->dst_rect.h, req->vert_deci);
			return -EINVAL;
		}

		if (req->flags & MDP_BWC_EN) {
			if ((req->src.width != req->src_rect.w) ||
			    (req->src.height != req->src_rect.h)) {
				pr_err("BWC: unequal src img and rect w,h\n");
				return -EINVAL;
			}

			if (req->flags & MDP_DECIMATION_EN) {
				pr_err("Can't enable BWC decode && decimate\n");
				return -EINVAL;
			}
		}

		if ((req->flags & MDP_DEINTERLACE) &&
					!req->scale.enable_pxl_ext) {
			if (req->flags & MDP_SOURCE_ROTATED_90) {
				if ((req->src_rect.w % 4) != 0) {
					pr_err("interlaced rect not h/4\n");
					return -EINVAL;
				}
			} else if ((req->src_rect.h % 4) != 0) {
				pr_err("interlaced rect not h/4\n");
				return -EINVAL;
			}
		}
	} else {
		if (req->flags & MDP_DEINTERLACE) {
			if ((req->src_rect.h % 4) != 0) {
				pr_err("interlaced rect h not multiple of 4\n");
				return -EINVAL;
			}
		}
	}

	if (fmt->is_yuv) {
		if ((req->src_rect.x & 0x1) || (req->src_rect.y & 0x1) ||
		    (req->src_rect.w & 0x1) || (req->src_rect.h & 0x1)) {
			pr_err("invalid odd src resolution or coordinates\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int __mdp_pipe_tune_perf(struct mdss_mdp_pipe *pipe)
{
	struct mdss_data_type *mdata = pipe->mixer->ctl->mdata;
	struct mdss_mdp_perf_params perf;
	int rc;

	for (;;) {
		rc = mdss_mdp_perf_calc_pipe(pipe, &perf, NULL, true);

		if (!rc && (perf.mdp_clk_rate <= mdata->max_mdp_clk_rate))
			break;

		/*
		 * if decimation is available try to reduce minimum clock rate
		 * requirement by applying vertical decimation and reduce
		 * mdp clock requirement
		 */
		if (mdata->has_decimation && (pipe->vert_deci < MAX_DECIMATION)
			&& !pipe->bwc_mode && !pipe->src_fmt->tile &&
			!pipe->scale.enable_pxl_ext)
			pipe->vert_deci++;
		else
			return -EPERM;
	}

	return 0;
}

static int __mdss_mdp_validate_pxl_extn(struct mdss_mdp_pipe *pipe)
{
	int plane;

	for (plane = 0; plane < MAX_PLANES; plane++) {
		u32 hor_req_pixels, hor_fetch_pixels;
		u32 hor_ov_fetch, vert_ov_fetch;
		u32 vert_req_pixels, vert_fetch_pixels;
		u32 src_w = DECIMATED_DIMENSION(pipe->src.w, pipe->horz_deci);
		u32 src_h = DECIMATED_DIMENSION(pipe->src.h, pipe->vert_deci);

		/*
		 * plane 1 and 2 are for chroma and are same. While configuring
		 * HW, programming only one of the chroma components is
		 * sufficient.
		 */
		if (plane == 2)
			continue;

		/*
		 * For chroma plane, width is half for the following sub sampled
		 * formats. Except in case of decimation, where hardware avoids
		 * 1 line of decimation instead of downsampling.
		 */
		if (plane == 1 && !pipe->horz_deci &&
		    ((pipe->src_fmt->chroma_sample == MDSS_MDP_CHROMA_420) ||
		     (pipe->src_fmt->chroma_sample == MDSS_MDP_CHROMA_H2V1))) {
			src_w >>= 1;
		}

		if (plane == 1 && !pipe->vert_deci &&
		    ((pipe->src_fmt->chroma_sample == MDSS_MDP_CHROMA_420) ||
		     (pipe->src_fmt->chroma_sample == MDSS_MDP_CHROMA_H1V2)))
			src_h >>= 1;

		hor_req_pixels = pipe->scale.roi_w[plane] +
			pipe->scale.num_ext_pxls_left[plane] +
			pipe->scale.num_ext_pxls_right[plane];

		hor_fetch_pixels = src_w +
			(pipe->scale.left_ftch[plane] >> pipe->horz_deci) +
			pipe->scale.left_rpt[plane] +
			(pipe->scale.right_ftch[plane] >> pipe->horz_deci) +
			pipe->scale.right_rpt[plane];

		hor_ov_fetch = src_w +
			(pipe->scale.left_ftch[plane] >> pipe->horz_deci)+
			(pipe->scale.right_ftch[plane] >> pipe->horz_deci);

		vert_req_pixels = pipe->scale.num_ext_pxls_top[plane] +
			pipe->scale.num_ext_pxls_btm[plane];

		vert_fetch_pixels =
			(pipe->scale.top_ftch[plane] >> pipe->vert_deci) +
			pipe->scale.top_rpt[plane] +
			(pipe->scale.btm_ftch[plane] >> pipe->vert_deci)+
			pipe->scale.btm_rpt[plane];

		vert_ov_fetch = src_h +
			(pipe->scale.top_ftch[plane] >> pipe->vert_deci)+
			(pipe->scale.btm_ftch[plane] >> pipe->vert_deci);

		if ((hor_req_pixels != hor_fetch_pixels) ||
			(hor_ov_fetch > pipe->img_width) ||
			(vert_req_pixels != vert_fetch_pixels) ||
			(vert_ov_fetch > pipe->img_height)) {
			pr_err("err: plane=%d h_req:%d h_fetch:%d v_req:%d v_fetch:%d\n",
					plane,
					hor_req_pixels, hor_fetch_pixels,
					vert_req_pixels, vert_fetch_pixels);
			pr_err("roi_w[%d]=%d, src_img:[%d, %d]\n",
					plane, pipe->scale.roi_w[plane],
					pipe->img_width, pipe->img_height);
			pipe->scale.enable_pxl_ext = 0;
			return -EINVAL;
		}
	}

	return 0;
}

static int __mdss_mdp_overlay_setup_scaling(struct mdss_mdp_pipe *pipe)
{
	u32 src;
	int rc;

	src = DECIMATED_DIMENSION(pipe->src.w, pipe->horz_deci);

	if (pipe->scale.enable_pxl_ext) {
		rc = __mdss_mdp_validate_pxl_extn(pipe);
		return rc;
	}

	memset(&pipe->scale, 0, sizeof(struct mdp_scale_data));
	rc = mdss_mdp_calc_phase_step(src, pipe->dst.w,
			&pipe->scale.phase_step_x[0]);
	if (rc == -EOVERFLOW) {
		/* overflow on horizontal direction is acceptable */
		rc = 0;
	} else if (rc) {
		pr_err("Horizontal scaling calculation failed=%d! %d->%d\n",
				rc, src, pipe->dst.w);
		return rc;
	}

	src = DECIMATED_DIMENSION(pipe->src.h, pipe->vert_deci);
	rc = mdss_mdp_calc_phase_step(src, pipe->dst.h,
			&pipe->scale.phase_step_y[0]);

	if ((rc == -EOVERFLOW) && (pipe->type == MDSS_MDP_PIPE_TYPE_VIG)) {
		/* overflow on Qseed2 scaler is acceptable */
		rc = 0;
	} else if (rc) {
		pr_err("Vertical scaling calculation failed=%d! %d->%d\n",
				rc, src, pipe->dst.h);
		return rc;
	}
	return rc;
}

static inline void __mdss_mdp_overlay_set_chroma_sample(
	struct mdss_mdp_pipe *pipe)
{
	pipe->chroma_sample_v = pipe->chroma_sample_h = 0;

	switch (pipe->src_fmt->chroma_sample) {
	case MDSS_MDP_CHROMA_H1V2:
		pipe->chroma_sample_v = 1;
		break;
	case MDSS_MDP_CHROMA_H2V1:
		pipe->chroma_sample_h = 1;
		break;
	case MDSS_MDP_CHROMA_420:
		pipe->chroma_sample_v = 1;
		pipe->chroma_sample_h = 1;
		break;
	}
	if (pipe->horz_deci)
		pipe->chroma_sample_h = 0;
	if (pipe->vert_deci)
		pipe->chroma_sample_v = 0;
}

static bool __find_pipe_in_list(struct list_head *head,
		int pipe_type, struct mdss_mdp_pipe **out_pipe)
{
	struct mdss_mdp_pipe *pipe;

	list_for_each_entry(pipe, head, list) {
		if (pipe_type == pipe->type) {
			*out_pipe = pipe;
			return true;
		}
	}

	return false;
}

static struct mdss_mdp_pipe *mdss_mdp_overlay_pipe_reuse(
		struct msm_fb_data_type *mfd, int pipe_type)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_pipe *pipe = NULL;

	mutex_lock(&mdp5_data->list_lock);
	if (__find_pipe_in_list(&mdp5_data->pipes_destroy, pipe_type, &pipe) ||
	    __find_pipe_in_list(&mdp5_data->pipes_cleanup, pipe_type, &pipe)) {
		pr_debug("reusing pipe%d\n", pipe->num);
		list_del_init(&pipe->list);
	}
	mutex_unlock(&mdp5_data->list_lock);

	return pipe;
}

int mdss_mdp_overlay_pipe_setup(struct msm_fb_data_type *mfd,
				       struct mdp_overlay *req,
				       struct mdss_mdp_pipe **ppipe)
{
	struct mdss_mdp_format_params *fmt;
	struct mdss_mdp_pipe *pipe;
	struct mdss_mdp_mixer *mixer = NULL;
	u32 pipe_type, mixer_mux, len;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdp_histogram_start_req hist;
	int ret;
	u32 bwc_enabled;
	u32 left_lm_w = left_lm_w_from_mfd(mfd);

	if (mdp5_data->ctl == NULL)
		return -ENODEV;

	if (req->flags & MDP_ROT_90) {
		pr_err("unsupported inline rotation\n");
		return -EOPNOTSUPP;
	}

	if ((req->dst_rect.w > MAX_DST_W) || (req->dst_rect.h > MAX_DST_H)) {
		pr_err("exceeded max mixer supported resolution %dx%d\n",
				req->dst_rect.w, req->dst_rect.h);
		return -EOVERFLOW;
	}

	if (IS_RIGHT_MIXER_OV(req->flags, req->dst_rect.x, left_lm_w))
		mixer_mux = MDSS_MDP_MIXER_MUX_RIGHT;
	else
		mixer_mux = MDSS_MDP_MIXER_MUX_LEFT;

	pr_debug("pipe ctl=%u req id=%x mux=%d\n", mdp5_data->ctl->num, req->id,
			mixer_mux);

	if (req->flags & (MDP_SOURCE_ROTATED_90 | MDP_BWC_EN))
		req->src.format =
			mdss_mdp_get_rotator_dst_format(req->src.format, 1);

	fmt = mdss_mdp_get_format_params(req->src.format);
	if (!fmt) {
		pr_err("invalid pipe format %d\n", req->src.format);
		return -EINVAL;
	}

	ret = mdss_mdp_ov_xres_check(mfd, req);
	if (ret)
		return ret;

	ret = mdss_mdp_overlay_req_check(mfd, req, fmt);
	if (ret)
		return ret;

	mixer = mdss_mdp_mixer_get(mdp5_data->ctl, mixer_mux);
	if (!mixer) {
		pr_err("unable to get mixer\n");
		return -ENODEV;
	}

	if (req->id == MSMFB_NEW_REQUEST) {
		switch (req->pipe_type) {
                case PIPE_TYPE_VIG:
                        pipe_type = MDSS_MDP_PIPE_TYPE_VIG;
                        break;
                case PIPE_TYPE_RGB:
                        pipe_type = MDSS_MDP_PIPE_TYPE_RGB;
                        break;
                case PIPE_TYPE_DMA:
                        pipe_type = MDSS_MDP_PIPE_TYPE_DMA;
                        break;
                case PIPE_TYPE_AUTO:
                default:
                        if (req->flags & MDP_OV_PIPE_FORCE_DMA)
                                pipe_type = MDSS_MDP_PIPE_TYPE_DMA;
                        else if (fmt->is_yuv ||
                                (req->flags & MDP_OV_PIPE_SHARE))
                                pipe_type = MDSS_MDP_PIPE_TYPE_VIG;
                        else
                                pipe_type = MDSS_MDP_PIPE_TYPE_RGB;
                        break;
                }

		pipe = mdss_mdp_pipe_alloc(mixer, pipe_type);

		/* try to reuse any pipe pending cleanup */
		if (!pipe)
			pipe = mdss_mdp_overlay_pipe_reuse(mfd, pipe_type);

		/* RGB pipes can be used instead of DMA */
		if ((req->pipe_type == PIPE_TYPE_AUTO) && !pipe &&
			(pipe_type == MDSS_MDP_PIPE_TYPE_DMA)) {
			pipe_type = MDSS_MDP_PIPE_TYPE_RGB;
			pipe = mdss_mdp_pipe_alloc(mixer, pipe_type);
		}

		/* VIG pipes can also support RGB format */
		if ((req->pipe_type == PIPE_TYPE_AUTO) && !pipe &&
			(pipe_type == MDSS_MDP_PIPE_TYPE_RGB)) {
			pipe_type = MDSS_MDP_PIPE_TYPE_VIG;
			pipe = mdss_mdp_pipe_alloc(mixer, pipe_type);
		}

		if (pipe == NULL) {
			pr_err("error allocating pipe\n");
			return -ENODEV;
		}

		ret = mdss_mdp_pipe_map(pipe);
		if (ret) {
			pr_err("unable to map pipe=%d\n", pipe->num);
			return ret;
		}

		mutex_lock(&mdp5_data->list_lock);
		list_add(&pipe->list, &mdp5_data->pipes_used);
		mutex_unlock(&mdp5_data->list_lock);
		pipe->mixer = mixer;
		pipe->mfd = mfd;
		pipe->pid = current->tgid;
		pipe->play_cnt = 0;
	} else {
		pipe = __overlay_find_pipe(mfd, req->id);
		if (!pipe) {
			pr_err("invalid pipe ndx=%x\n", req->id);
			return -ENODEV;
		}

		ret = mdss_mdp_pipe_map(pipe);
		if (IS_ERR_VALUE(ret)) {
			pr_err("Unable to map used pipe%d ndx=%x\n",
					pipe->num, pipe->ndx);
			return ret;
		}

		if (pipe->mixer != mixer) {
			if (!mixer->ctl || (mixer->ctl->mfd != mfd)) {
				pr_err("Can't switch mixer %d->%d pnum %d!\n",
						pipe->mixer->num, mixer->num,
						pipe->num);
				ret = -EINVAL;
				goto exit_fail;
			}
			pr_debug("switching pipe mixer %d->%d pnum %d\n",
					pipe->mixer->num, mixer->num,
					pipe->num);
			mdss_mdp_mixer_pipe_unstage(pipe);
			pipe->mixer = mixer;
		}
	}

	if (!pipe->dirty && !memcmp(req, &pipe->req_data, sizeof(*req))) {
		pr_debug("skipping pipe_reconfiguration\n");
		goto skip_reconfigure;
	}

	pipe->flags = req->flags;
	bwc_enabled = req->flags & MDP_BWC_EN;
	if (bwc_enabled  &&  !mdp5_data->mdata->has_bwc) {
		pr_err("BWC is not supported in MDP version %x\n",
			mdp5_data->mdata->mdp_rev);
		pipe->bwc_mode = 0;
	} else {
		pipe->bwc_mode = pipe->mixer->rotator_mode ?
			0 : (bwc_enabled ? 1 : 0) ;
	}
	pipe->img_width = req->src.width & 0x3fff;
	pipe->img_height = req->src.height & 0x3fff;
	pipe->src.x = req->src_rect.x;
	pipe->src.y = req->src_rect.y;
	pipe->src.w = req->src_rect.w;
	pipe->src.h = req->src_rect.h;
	pipe->dst.x = req->dst_rect.x;
	pipe->dst.y = req->dst_rect.y;
	pipe->dst.w = req->dst_rect.w;
	pipe->dst.h = req->dst_rect.h;
	pipe->horz_deci = req->horz_deci;
	pipe->vert_deci = req->vert_deci;

	memcpy(&pipe->scale, &req->scale, sizeof(struct mdp_scale_data));
	pipe->src_fmt = fmt;
	__mdss_mdp_overlay_set_chroma_sample(pipe);

	pipe->mixer_stage = req->z_order;
	pipe->is_fg = req->is_fg;
	pipe->alpha = req->alpha;
	pipe->transp = req->transp_mask;
	pipe->blend_op = req->blend_op;
	if (pipe->blend_op == BLEND_OP_NOT_DEFINED)
		pipe->blend_op = fmt->alpha_enable ?
					BLEND_OP_PREMULTIPLIED :
					BLEND_OP_OPAQUE;

	if (!fmt->alpha_enable && (pipe->blend_op != BLEND_OP_OPAQUE))
		pr_debug("Unintended blend_op %d on layer with no alpha plane\n",
			pipe->blend_op);

	if (fmt->is_yuv && !(pipe->flags & MDP_SOURCE_ROTATED_90) &&
			!pipe->scale.enable_pxl_ext) {
		pipe->overfetch_disable = OVERFETCH_DISABLE_BOTTOM;

		if (!(pipe->flags & MDSS_MDP_DUAL_PIPE) ||
				IS_RIGHT_MIXER_OV(req->flags,
					req->dst_rect.x, left_lm_w))
			pipe->overfetch_disable |= OVERFETCH_DISABLE_RIGHT;
		pr_debug("overfetch flags=%x\n", pipe->overfetch_disable);
	} else {
		pipe->overfetch_disable = 0;
	}
	pipe->bg_color = req->bg_color;

	if (pipe->flags & MDP_OVERLAY_PP_CFG_EN) {
		memcpy(&pipe->pp_cfg, &req->overlay_pp_cfg,
					sizeof(struct mdp_overlay_pp_params));
		len = pipe->pp_cfg.igc_cfg.len;
		if ((pipe->pp_cfg.config_ops & MDP_OVERLAY_PP_IGC_CFG) &&
						(len == IGC_LUT_ENTRIES)) {
			ret = copy_from_user(pipe->pp_res.igc_c0_c1,
					pipe->pp_cfg.igc_cfg.c0_c1_data,
					sizeof(uint32_t) * len);
			if (ret) {
				ret = -ENOMEM;
				goto exit_fail;
			}
			ret = copy_from_user(pipe->pp_res.igc_c2,
					pipe->pp_cfg.igc_cfg.c2_data,
					sizeof(uint32_t) * len);
			if (ret) {
				ret = -ENOMEM;
				goto exit_fail;
			}
			pipe->pp_cfg.igc_cfg.c0_c1_data =
							pipe->pp_res.igc_c0_c1;
			pipe->pp_cfg.igc_cfg.c2_data = pipe->pp_res.igc_c2;
		}
		if (pipe->pp_cfg.config_ops & MDP_OVERLAY_PP_HIST_CFG) {
			if (pipe->pp_cfg.hist_cfg.ops & MDP_PP_OPS_ENABLE) {
				hist.block = pipe->pp_cfg.hist_cfg.block;
				hist.frame_cnt =
					pipe->pp_cfg.hist_cfg.frame_cnt;
				hist.bit_mask = pipe->pp_cfg.hist_cfg.bit_mask;
				hist.num_bins = pipe->pp_cfg.hist_cfg.num_bins;
				mdss_mdp_hist_start(&hist);
			} else if (pipe->pp_cfg.hist_cfg.ops &
							MDP_PP_OPS_DISABLE) {
				mdss_mdp_hist_stop(pipe->pp_cfg.hist_cfg.block);
			}
		}
		len = pipe->pp_cfg.hist_lut_cfg.len;
		if ((pipe->pp_cfg.config_ops & MDP_OVERLAY_PP_HIST_LUT_CFG) &&
						(len == ENHIST_LUT_ENTRIES)) {
			ret = copy_from_user(pipe->pp_res.hist_lut,
					pipe->pp_cfg.hist_lut_cfg.data,
					sizeof(uint32_t) * len);
			if (ret) {
				ret = -ENOMEM;
				goto exit_fail;
			}
			pipe->pp_cfg.hist_lut_cfg.data = pipe->pp_res.hist_lut;
		}
	}

	/*
	 * When scaling is enabled src crop and image
	 * width and height is modified by user
	 */
	if ((pipe->flags & MDP_DEINTERLACE) && !pipe->scale.enable_pxl_ext) {
		if (pipe->flags & MDP_SOURCE_ROTATED_90) {
			pipe->src.x = DIV_ROUND_UP(pipe->src.x, 2);
			pipe->src.x &= ~1;
			pipe->src.w /= 2;
			pipe->img_width /= 2;
		} else {
			pipe->src.h /= 2;
			pipe->src.y = DIV_ROUND_UP(pipe->src.y, 2);
			pipe->src.y &= ~1;
		}
	}

	ret = __mdp_pipe_tune_perf(pipe);
	if (ret) {
		pr_debug("unable to satisfy performance. ret=%d\n", ret);
		goto exit_fail;
	}

	ret = __mdss_mdp_overlay_setup_scaling(pipe);
	if (ret)
		goto exit_fail;

	if ((mixer->type == MDSS_MDP_MIXER_TYPE_WRITEBACK) &&
		!mdp5_data->mdata->has_wfd_blk)
		mdss_mdp_smp_release(pipe);

	ret = mdss_mdp_smp_reserve(pipe);
	if (ret) {
		pr_debug("mdss_mdp_smp_reserve failed. ret=%d\n", ret);
		goto exit_fail;
	}

	req->id = pipe->ndx;

	req->vert_deci = pipe->vert_deci;

	pipe->req_data = *req;
	pipe->dirty = false;

	pipe->params_changed++;
skip_reconfigure:
	*ppipe = pipe;

	mdss_mdp_pipe_unmap(pipe);

	return ret;
exit_fail:
	mdss_mdp_pipe_unmap(pipe);

	mutex_lock(&mdp5_data->list_lock);
	if (pipe->play_cnt == 0) {
		pr_debug("failed for pipe %d\n", pipe->num);
		if (!list_empty(&pipe->list))
			list_del_init(&pipe->list);
		__overlay_pipe_cleanup(mfd, pipe);
	}

	/* invalidate any overlays in this framebuffer after failure */
	list_for_each_entry(pipe, &mdp5_data->pipes_used, list) {
		pr_debug("freeing allocations for pipe %d\n", pipe->num);
		mdss_mdp_smp_unreserve(pipe);
		pipe->params_changed = 0;
		pipe->dirty = true;
	}
	mutex_unlock(&mdp5_data->list_lock);
	return ret;
}

static int mdss_mdp_overlay_set(struct msm_fb_data_type *mfd,
				struct mdp_overlay *req)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	int ret;

	ret = mutex_lock_interruptible(&mdp5_data->ov_lock);
	if (ret)
		return ret;

	if (mdss_fb_is_power_off(mfd)) {
		mutex_unlock(&mdp5_data->ov_lock);
		return -EPERM;
	}

	if (req->flags & MDSS_MDP_ROT_ONLY) {
		ret = mdss_mdp_rotator_setup(mfd, req);
	} else if (req->src.format == MDP_RGB_BORDERFILL) {
		req->id = BORDERFILL_NDX;
	} else {
		struct mdss_mdp_pipe *pipe;

		/* userspace zorder start with stage 0 */
		req->z_order += MDSS_MDP_STAGE_0;

		ret = mdss_mdp_overlay_pipe_setup(mfd, req, &pipe);

		req->z_order -= MDSS_MDP_STAGE_0;
	}

	mutex_unlock(&mdp5_data->ov_lock);

	return ret;
}

/*
 * it's caller responsibility to acquire mdp5_data->list_lock while calling
 * this function
 */
static
struct mdss_mdp_data *__mdp_overlay_buf_alloc(struct msm_fb_data_type *mfd,
		struct mdss_mdp_pipe *pipe)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_data *buf;
	int i;

	if (list_empty(&mdp5_data->bufs_pool)) {
		pr_debug("allocating %u bufs for fb%d\n",
					BUF_POOL_SIZE, mfd->index);

		buf = kzalloc(sizeof(*buf) * BUF_POOL_SIZE, GFP_KERNEL);
		if (!buf) {
			pr_err("Unable to allocate buffer pool\n");
			return NULL;
		}

		list_add(&buf->chunk_list, &mdp5_data->bufs_chunks);
		for (i = 0; i < BUF_POOL_SIZE; i++) {
			buf->state = MDP_BUF_STATE_UNUSED;
			list_add(&buf[i].buf_list, &mdp5_data->bufs_pool);
		}
	}

	buf = list_first_entry(&mdp5_data->bufs_pool,
			struct mdss_mdp_data, buf_list);
	BUG_ON(buf->state != MDP_BUF_STATE_UNUSED);
	buf->state = MDP_BUF_STATE_READY;
	buf->last_alloc = local_clock();
	buf->last_pipe = pipe;

	list_move_tail(&buf->buf_list, &mdp5_data->bufs_used);
	list_add_tail(&buf->pipe_list, &pipe->buf_queue);

	pr_debug("buffer alloc: %p\n", buf);

	return buf;
}

struct mdss_mdp_data *mdss_mdp_overlay_buf_alloc(struct msm_fb_data_type *mfd,
		struct mdss_mdp_pipe *pipe)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_data *buf;

	mutex_lock(&mdp5_data->list_lock);
	buf = __mdp_overlay_buf_alloc(mfd, pipe);
	mutex_unlock(&mdp5_data->list_lock);

	return buf;
}

static void mdss_mdp_overlay_buf_deinit(struct msm_fb_data_type *mfd)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_data *buf, *t;

	pr_debug("performing cleanup of buffers pool on fb%d\n", mfd->index);

	BUG_ON(!list_empty(&mdp5_data->bufs_used));

	list_for_each_entry_safe(buf, t, &mdp5_data->bufs_pool, buf_list)
		list_del(&buf->buf_list);

	list_for_each_entry_safe(buf, t, &mdp5_data->bufs_chunks, chunk_list) {
		list_del(&buf->chunk_list);
		kfree(buf);
	}
}

/*
 * it's caller responsibility to acquire mdp5_data->list_lock while calling
 * this function
 */
static void __mdp_overlay_buf_free(struct msm_fb_data_type *mfd,
		struct mdss_mdp_data *buf)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);

	if (!list_empty(&buf->pipe_list))
		list_del_init(&buf->pipe_list);

	mdss_mdp_data_free(buf);

	buf->last_freed = local_clock();
	buf->state = MDP_BUF_STATE_UNUSED;

	pr_debug("buffer freed: %p\n", buf);

	list_move_tail(&buf->buf_list, &mdp5_data->bufs_pool);
}

void mdss_mdp_overlay_buf_free(struct msm_fb_data_type *mfd,
		struct mdss_mdp_data *buf)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);

	mutex_lock(&mdp5_data->list_lock);
	__mdp_overlay_buf_free(mfd, buf);
	mutex_unlock(&mdp5_data->list_lock);
}

static inline void __pipe_buf_mark_cleanup(struct msm_fb_data_type *mfd,
		struct mdss_mdp_data *buf)
{
	/* buffer still in bufs_used, marking it as cleanup will clean it up */
	buf->state = MDP_BUF_STATE_CLEANUP;
	list_del_init(&buf->pipe_list);
}

/**
 * __mdss_mdp_overlay_free_list_purge() - clear free list of buffers
 * @mfd:	Msm frame buffer data structure for the associated fb
 *
 * Frees memory and clears current list of buffers which are pending free
 */
static void __mdss_mdp_overlay_free_list_purge(struct msm_fb_data_type *mfd)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_data *buf, *t;

	pr_debug("purging fb%d free list\n", mfd->index);

	list_for_each_entry_safe(buf, t, &mdp5_data->bufs_freelist, buf_list)
		__mdp_overlay_buf_free(mfd, buf);
}

static void __overlay_pipe_cleanup(struct msm_fb_data_type *mfd,
		struct mdss_mdp_pipe *pipe)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_data *buf, *tmpbuf;

	list_for_each_entry_safe(buf, tmpbuf, &pipe->buf_queue, pipe_list) {
		__pipe_buf_mark_cleanup(mfd, buf);
		list_move(&buf->buf_list, &mdp5_data->bufs_freelist);

		/*
		 * in case of secure UI, the buffer needs to be released as
		 * soon as session is closed.
		 */
		if (pipe->flags & MDP_SECURE_DISPLAY_OVERLAY_SESSION)
			__mdp_overlay_buf_free(mfd, buf);
	}

	mdss_mdp_pipe_destroy(pipe);
}

/**
 * mdss_mdp_overlay_cleanup() - handles cleanup after frame commit
 * @mfd:           Msm frame buffer data structure for the associated fb
 * @destroy_pipes: list of pipes that should be destroyed as part of cleanup
 *
 * Goes through destroy_pipes list and ensures they are ready to be destroyed
 * and cleaned up. Also cleanup of any pipe buffers after flip.
 */
static void mdss_mdp_overlay_cleanup(struct msm_fb_data_type *mfd,
		struct list_head *destroy_pipes)
{
	struct mdss_mdp_pipe *pipe, *tmp;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);
	bool recovery_mode = false;
	struct mdss_mdp_data *buf, *tmpbuf;

	mutex_lock(&mdp5_data->list_lock);
	list_for_each_entry(pipe, destroy_pipes, list) {
		/* make sure pipe fetch has been halted before freeing buffer */
		if (mdss_mdp_pipe_fetch_halt(pipe)) {
			/*
			 * if pipe is not able to halt. Enter recovery mode,
			 * by un-staging any pipes that are attached to mixer
			 * so that any freed pipes that are not able to halt
			 * can be staged in solid fill mode and be reset
			 * with next vsync
			 */
			if (!recovery_mode) {
				recovery_mode = true;
				mdss_mdp_mixer_unstage_all(ctl->mixer_left);
				mdss_mdp_mixer_unstage_all(ctl->mixer_right);
			}
			pipe->params_changed++;
			mdss_mdp_pipe_queue_data(pipe, NULL);
		}
	}

	if (recovery_mode) {
		pr_warn("performing recovery sequence for fb%d\n", mfd->index);
		__overlay_kickoff_requeue(mfd);
	}

	__mdss_mdp_overlay_free_list_purge(mfd);

	list_for_each_entry_safe(buf, tmpbuf, &mdp5_data->bufs_used, buf_list) {
		if (buf->state == MDP_BUF_STATE_CLEANUP)
			list_move(&buf->buf_list, &mdp5_data->bufs_freelist);
	}

	list_for_each_entry_safe(pipe, tmp, destroy_pipes, list) {
		list_del_init(&pipe->list);
		__overlay_pipe_cleanup(mfd, pipe);
	}
	mutex_unlock(&mdp5_data->list_lock);
}

void mdss_mdp_handoff_cleanup_pipes(struct msm_fb_data_type *mfd,
	u32 type)
{
	u32 i, npipes;
	struct mdss_mdp_pipe *pipes;
	struct mdss_mdp_pipe *pipe;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_data_type *mdata = mfd_to_mdata(mfd);

	switch (type) {
	case MDSS_MDP_PIPE_TYPE_VIG:
		pipes = mdata->vig_pipes;
		npipes = mdata->nvig_pipes;
		break;
	case MDSS_MDP_PIPE_TYPE_RGB:
		pipes = mdata->rgb_pipes;
		npipes = mdata->nrgb_pipes;
		break;
	case MDSS_MDP_PIPE_TYPE_DMA:
		pipes = mdata->dma_pipes;
		npipes = mdata->ndma_pipes;
		break;
	default:
		return;
	}

	for (i = 0; i < npipes; i++) {
		pipe = &pipes[i];
		if (pipe->is_handed_off) {
			pr_debug("Unmapping handed off pipe %d\n", pipe->num);
			list_add(&pipe->list, &mdp5_data->pipes_cleanup);
			mdss_mdp_mixer_pipe_unstage(pipe);
			pipe->is_handed_off = false;
		}
	}
}

/**
 * mdss_mdp_overlay_start() - Programs the MDP control data path to hardware
 * @mfd: Msm frame buffer structure associated with fb device.
 *
 * Program the MDP hardware with the control settings for the framebuffer
 * device. In addition to this, this function also handles the transition
 * from the the splash screen to the android boot animation when the
 * continuous splash screen feature is enabled.
 */
int mdss_mdp_overlay_start(struct msm_fb_data_type *mfd)
{
	int rc;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_ctl *ctl = mdp5_data->ctl;

	if (mdss_mdp_ctl_is_power_on(ctl)) {
		if (!mdp5_data->mdata->batfet)
			mdss_mdp_batfet_ctrl(mdp5_data->mdata, true);
		mdss_mdp_release_splash_pipe(mfd);
		return 0;
	} else if (mfd->panel_info->cont_splash_enabled) {
		mutex_lock(&mdp5_data->list_lock);
		rc = list_empty(&mdp5_data->pipes_used);
		mutex_unlock(&mdp5_data->list_lock);
		if (rc) {
			pr_debug("empty kickoff on fb%d during cont splash\n",
					mfd->index);
			return 0;
		}
	}

	pr_debug("starting fb%d overlay\n", mfd->index);

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);

	/*
	 * If idle pc feature is not enabled, then get a reference to the
	 * runtime device which will be released when overlay is turned off
	 */
	if (!mdp5_data->mdata->idle_pc_enabled) {
		rc = pm_runtime_get_sync(&mfd->pdev->dev);
		if (IS_ERR_VALUE(rc)) {
			pr_err("unable to resume with pm_runtime_get_sync rc=%d\n",
				rc);
			goto end;
		}
	}

	/*
	 * We need to do hw init before any hw programming.
	 * Also, hw init involves programming the VBIF registers which
	 * should be done only after attaching IOMMU which in turn would call
	 * in to TZ to restore security configs on the VBIF registers.
	 * This is not needed when continuous splash screen is enabled since
	 * we would have called in to TZ to restore security configs from LK.
	 */
	if (!is_mdss_iommu_attached()) {
		if (!mfd->panel_info->cont_splash_enabled) {
			rc = mdss_iommu_ctrl(1);
			if (IS_ERR_VALUE(rc)) {
				pr_err("iommu attach failed rc=%d\n", rc);
				goto end;
			}
			mdss_hw_init(mdss_res);
			mdss_iommu_ctrl(0);
		}
	}

	/*
	 * Increment the overlay active count prior to calling ctl_start.
	 * This is needed to ensure that if idle power collapse kicks in
	 * right away, it would be handled correctly.
	 */
	atomic_inc(&mdp5_data->mdata->active_intf_cnt);
	rc = mdss_mdp_ctl_start(ctl, false);
	if (rc == 0) {
		mdss_mdp_ctl_notifier_register(mdp5_data->ctl,
				&mfd->mdp_sync_pt_data.notifier);
	} else {
		pr_err("mdp ctl start failed.\n");
		goto ctl_error;
	}

	rc = mdss_mdp_splash_cleanup(mfd, true);
	if (!rc)
		goto end;

ctl_error:
	mdss_mdp_ctl_destroy(ctl);
	atomic_dec(&mdp5_data->mdata->active_intf_cnt);
	mdp5_data->ctl = NULL;
end:
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);
	return rc;
}

static void mdss_mdp_overlay_update_pm(struct mdss_overlay_private *mdp5_data)
{
	ktime_t wakeup_time;

	if (!mdp5_data->cpu_pm_hdl)
		return;

	if (mdss_mdp_display_wakeup_time(mdp5_data->ctl, &wakeup_time))
		return;

	activate_event_timer(mdp5_data->cpu_pm_hdl, wakeup_time);
}

static int __overlay_queue_pipes(struct msm_fb_data_type *mfd)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_pipe *pipe;
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);
	struct mdss_mdp_ctl *tmp;
	int ret = 0;

	/*
	 * Setup pipe in solid fill before unstaging,
	 * to ensure no fetches are happening after dettach or reattach.
	 */
	list_for_each_entry(pipe, &mdp5_data->pipes_cleanup, list) {
		mdss_mdp_pipe_queue_data(pipe, NULL);
		mdss_mdp_mixer_pipe_unstage(pipe);
	}

	list_for_each_entry(pipe, &mdp5_data->pipes_used, list) {
		struct mdss_mdp_data *buf;

		if (pipe->dirty) {
			pr_err("fb%d: pipe %d dirty! skipping configuration\n",
					mfd->index, pipe->num);
			continue;
		}

		/*
		 * When secure display is enabled, if there is a non secure
		 * display pipe, skip that
		 */
		if (mdss_get_sd_client_cnt() &&
			!(pipe->flags & MDP_SECURE_DISPLAY_OVERLAY_SESSION)) {
			pr_warn("Non secure pipe during secure display: %u: %08X, skip\n",
					pipe->num, pipe->flags);
			continue;
		}
		/*
		 * When external is connected and no dedicated wfd is present,
		 * reprogram DMA pipe before kickoff to clear out any previous
		 * block mode configuration.
		 */
		if ((pipe->type == MDSS_MDP_PIPE_TYPE_DMA) &&
		    (ctl->shared_lock && !ctl->mdata->has_wfd_blk)) {
			if (ctl->mdata->mixer_switched) {
				ret = mdss_mdp_overlay_pipe_setup(mfd,
						&pipe->req_data, &pipe);
				pr_debug("reseting DMA pipe for ctl=%d",
					 ctl->num);
			}
			if (ret) {
				pr_err("can't reset DMA pipe ret=%d ctl=%d\n",
					ret, ctl->num);
				return ret;
			}

			tmp = mdss_mdp_ctl_mixer_switch(ctl,
					MDSS_MDP_WB_CTL_TYPE_LINE);
			if (!tmp)
				return -EINVAL;
			pipe->mixer = mdss_mdp_mixer_get(tmp,
					MDSS_MDP_MIXER_MUX_DEFAULT);
		}

		buf = list_first_entry_or_null(&pipe->buf_queue,
				struct mdss_mdp_data, pipe_list);
		if (buf) {
			switch (buf->state) {
			case MDP_BUF_STATE_READY:
				pr_debug("pnum=%d buf=%p first buffer ready\n",
						pipe->num, buf);
				break;
			case MDP_BUF_STATE_ACTIVE:
				if (list_is_last(&buf->pipe_list,
						&pipe->buf_queue)) {
					pr_debug("pnum=%d no buf update\n",
							pipe->num);
				} else {
					struct mdss_mdp_data *tmp = buf;
					/*
					 * buffer flip, new buffer will
					 * replace currently active one,
					 * mark currently active for cleanup
					 */
					buf = list_next_entry(tmp, pipe_list);
					__pipe_buf_mark_cleanup(mfd, tmp);
				}
				break;
			default:
				pr_err("invalid state of buf %p=%d\n",
						buf, buf->state);
				BUG();
				break;
			}
		}

		/* ensure pipes are reconfigured after power off/on */
		if (ctl->play_cnt == 0)
			pipe->params_changed++;

		if (buf && (buf->state == MDP_BUF_STATE_READY)) {
			buf->state = MDP_BUF_STATE_ACTIVE;
			ret = mdss_mdp_data_map(buf);
		} else if (!pipe->params_changed) {
			/* nothing to update so continue with next */
			continue;
		} else if (buf) {
			BUG_ON(buf->state != MDP_BUF_STATE_ACTIVE);
			pr_debug("requeueing active buffer on pnum=%d\n",
					pipe->num);
		} else if ((pipe->flags & MDP_SOLID_FILL) == 0) {
			pr_warn("commit without buffer on pipe %d\n",
				pipe->num);
			ret = -EINVAL;
		}

		/*
		 * if we reach here without errors and buf == NULL
		 * then solid fill will be set
		 */
		if (!IS_ERR_VALUE(ret))
			ret = mdss_mdp_pipe_queue_data(pipe, buf);

		if (IS_ERR_VALUE(ret)) {
			pr_warn("Unable to queue data for pnum=%d\n",
					pipe->num);
			mdss_mdp_mixer_pipe_unstage(pipe);
			pipe->dirty = true;

			if (buf)
				__pipe_buf_mark_cleanup(mfd, buf);
		}
	}

	return 0;
}

static void __overlay_kickoff_requeue(struct msm_fb_data_type *mfd)
{
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);

	mdss_mdp_display_commit(ctl, NULL, NULL);
	mdss_mdp_display_wait4comp(ctl);

	ATRACE_BEGIN("sspp_programming");
	__overlay_queue_pipes(mfd);
	ATRACE_END("sspp_programming");

	mdss_mdp_display_commit(ctl, NULL,  NULL);
	mdss_mdp_display_wait4comp(ctl);
}

static int mdss_mdp_commit_cb(enum mdp_commit_stage_type commit_stage,
	void *data)
{
	int ret = 0;
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)data;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_ctl *ctl;

	switch (commit_stage) {
	case MDP_COMMIT_STAGE_SETUP_DONE:
		ctl = mfd_to_ctl(mfd);
		mdss_mdp_ctl_notify(ctl, MDP_NOTIFY_FRAME_CTX_DONE);
		mdp5_data->kickoff_released = true;
		mutex_unlock(&mdp5_data->ov_lock);
		break;
	case MDP_COMMIT_STAGE_READY_FOR_KICKOFF:
		mutex_lock(&mdp5_data->ov_lock);
		break;
	default:
		pr_err("Invalid commit stage %x", commit_stage);
		break;
	}

	return ret;
}

int mdss_mdp_overlay_kickoff(struct msm_fb_data_type *mfd,
				struct mdp_display_commit *data)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_pipe *pipe, *tmp;
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);
	int ret = 0;
	int sd_in_pipe = 0;
	struct mdss_mdp_commit_cb commit_cb;

	if (!ctl)
		return -ENODEV;

	ATRACE_BEGIN(__func__);
	if (ctl->shared_lock) {
		mdss_mdp_ctl_notify(ctl, MDP_NOTIFY_FRAME_BEGIN);
		mdss_mdp_ctl_notify(ctl, MDP_NOTIFY_FRAME_READY);
		mutex_lock(ctl->shared_lock);
		mutex_lock(ctl->wb_lock);
	}

	mutex_lock(&mdp5_data->ov_lock);
	ctl->bw_pending = 0;
	ret = mdss_mdp_overlay_start(mfd);
	if (ret) {
		pr_err("unable to start overlay %d (%d)\n", mfd->index, ret);
		mutex_unlock(&mdp5_data->ov_lock);
		if (ctl->shared_lock)
			mutex_unlock(ctl->shared_lock);
		return ret;
	}

	ret = mdss_iommu_ctrl(1);
	if (IS_ERR_VALUE(ret)) {
		pr_err("iommu attach failed rc=%d\n", ret);
		mutex_unlock(&mdp5_data->ov_lock);
		if (ctl->shared_lock)
			mutex_unlock(ctl->shared_lock);
		return ret;
	}
	mutex_lock(&mdp5_data->list_lock);

	/*
	 * check if there is a secure display session
	 */
	list_for_each_entry(pipe, &mdp5_data->pipes_used, list) {
		if (pipe->flags & MDP_SECURE_DISPLAY_OVERLAY_SESSION) {
			sd_in_pipe = 1;
			pr_debug("Secure pipe: %u : %08X\n",
					pipe->num, pipe->flags);
		}
	}
	/*
	 * If there is no secure display session and sd_enabled, disable the
	 * secure display session
	 */
	if (!sd_in_pipe && mdp5_data->sd_enabled) {
		/* disable the secure display on last client */
		if (mdss_get_sd_client_cnt() == 1)
			ret = mdss_mdp_secure_display_ctrl(0);
		if (!ret) {
			mdss_update_sd_client(mdp5_data->mdata, false);
			mdp5_data->sd_enabled = 0;
		}
	}

	if (!ctl->shared_lock)
		mdss_mdp_ctl_notify(ctl, MDP_NOTIFY_FRAME_BEGIN);

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);

	__vsync_set_vsync_handler(mfd);

	if (data)
		mdss_mdp_set_roi(ctl, data);

	/*
	 * Setup pipe in solid fill before unstaging,
	 * to ensure no fetches are happening after dettach or reattach.
	 */
	list_for_each_entry_safe(pipe, tmp, &mdp5_data->pipes_cleanup, list) {
		mdss_mdp_pipe_queue_data(pipe, NULL);
		mdss_mdp_mixer_pipe_unstage(pipe);
		list_move(&pipe->list, &mdp5_data->pipes_destroy);
	}

	ATRACE_BEGIN("sspp_programming");
	ret = __overlay_queue_pipes(mfd);
	ATRACE_END("sspp_programming");

	mutex_unlock(&mdp5_data->list_lock);

	mdp5_data->kickoff_released = false;

	if (mfd->panel.type == WRITEBACK_PANEL) {
		ATRACE_BEGIN("wb_kickoff");
		commit_cb.commit_cb_fnc = mdss_mdp_commit_cb;
		commit_cb.data = mfd;
		ret = mdss_mdp_wb_kickoff(mfd, &commit_cb);
		ATRACE_END("wb_kickoff");
	} else {
		ATRACE_BEGIN("display_commit");
		commit_cb.commit_cb_fnc = mdss_mdp_commit_cb;
		commit_cb.data = mfd;
		ret = mdss_mdp_display_commit(mdp5_data->ctl, NULL,
			&commit_cb);
		ATRACE_END("display_commit");
	}

	if (!mdp5_data->kickoff_released)
		mdss_mdp_ctl_notify(ctl, MDP_NOTIFY_FRAME_CTX_DONE);

	if (IS_ERR_VALUE(ret))
		goto commit_fail;

	mutex_unlock(&mdp5_data->ov_lock);
	mdss_mdp_overlay_update_pm(mdp5_data);

	ATRACE_BEGIN("display_wait4comp");
	ret = mdss_mdp_display_wait4comp(mdp5_data->ctl);
	ATRACE_END("display_wait4comp");
	mutex_lock(&mdp5_data->ov_lock);

	if (ret == 0) {
		if (!mdp5_data->sd_enabled && sd_in_pipe) {
			if (!mdss_get_sd_client_cnt())
				ret = mdss_mdp_secure_display_ctrl(1);
			if (!ret) {
				mdp5_data->sd_enabled = 1;
				mdss_update_sd_client(mdp5_data->mdata, true);
			}
		}
	}

	mdss_fb_update_notify_update(mfd);
commit_fail:
	ATRACE_BEGIN("overlay_cleanup");
	mdss_mdp_overlay_cleanup(mfd, &mdp5_data->pipes_destroy);
	ATRACE_END("overlay_cleanup");
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);
	mdss_mdp_ctl_notify(ctl, MDP_NOTIFY_FRAME_FLUSHED);
	if (!mdp5_data->kickoff_released)
		mdss_mdp_ctl_notify(ctl, MDP_NOTIFY_FRAME_CTX_DONE);

	mutex_unlock(&mdp5_data->ov_lock);
	if (ctl->shared_lock) {
		mutex_unlock(ctl->wb_lock);
		mutex_unlock(ctl->shared_lock);
	}
	mdss_iommu_ctrl(0);
	ATRACE_END(__func__);

	return ret;
}

int mdss_mdp_overlay_release(struct msm_fb_data_type *mfd, int ndx)
{
	struct mdss_mdp_pipe *pipe, *tmp;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	u32 unset_ndx = 0;
	int destroy_pipe;

	mutex_lock(&mdp5_data->list_lock);
	list_for_each_entry_safe(pipe, tmp, &mdp5_data->pipes_used, list) {
		if (pipe->ndx & ndx) {
			if (mdss_mdp_pipe_map(pipe)) {
				pr_err("Unable to map used pipe%d ndx=%x\n",
						pipe->num, pipe->ndx);
				continue;
			}

			unset_ndx |= pipe->ndx;

			pipe->pid = 0;
			destroy_pipe = pipe->play_cnt == 0;
			if (!destroy_pipe)
				list_move(&pipe->list,
						&mdp5_data->pipes_cleanup);
			else
				list_del_init(&pipe->list);

			mdss_mdp_pipe_unmap(pipe);
			if (destroy_pipe)
				__overlay_pipe_cleanup(mfd, pipe);

			if (unset_ndx == ndx)
				break;
		}
	}

	mutex_unlock(&mdp5_data->list_lock);

	if (unset_ndx != ndx) {
		pr_warn("Unable to unset pipe(s) ndx=0x%x unset=0x%x\n",
				ndx, unset_ndx);
               return -ENOENT;
	}

	return 0;
}

static int mdss_mdp_overlay_unset(struct msm_fb_data_type *mfd, int ndx)
{
	int ret = 0;
	struct mdss_overlay_private *mdp5_data;

	if (!mfd)
		return -ENODEV;

	mdp5_data = mfd_to_mdp5_data(mfd);

	if (!mdp5_data || !mdp5_data->ctl)
		return -ENODEV;

	ret = mutex_lock_interruptible(&mdp5_data->ov_lock);
	if (ret)
		return ret;

	if (ndx == BORDERFILL_NDX) {
		pr_debug("borderfill disable\n");
		mdp5_data->borderfill_enable = false;
		ret = 0;
		goto done;
	}

	if (mdss_fb_is_power_off(mfd)) {
		ret = -EPERM;
		goto done;
	}

	pr_debug("unset ndx=%x\n", ndx);

	if (ndx & MDSS_MDP_ROT_SESSION_MASK) {
		ret = mdss_mdp_rotator_unset(ndx);
	} else {
		ret = mdss_mdp_overlay_release(mfd, ndx);
	}

done:
	mutex_unlock(&mdp5_data->ov_lock);

	return ret;
}

/**
 * mdss_mdp_overlay_release_all() - release any overlays associated with fb dev
 * @mfd:	Msm frame buffer structure associated with fb device
 * @release_all: ignore pid and release all the pipes
 *
 * Release any resources allocated by calling process, this can be called
 * on fb_release to release any overlays/rotator sessions left open.
 *
 * Return number of resources released
 */
static int __mdss_mdp_overlay_release_all(struct msm_fb_data_type *mfd,
	bool release_all, uint32_t pid)
{
	struct mdss_mdp_pipe *pipe;
	struct mdss_mdp_rotator_session *rot, *tmp;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	u32 unset_ndx = 0;
	int cnt = 0;

	pr_debug("releasing all resources for fb%d pid=%d\n", mfd->index, pid);

	mutex_lock(&mdp5_data->ov_lock);
	mutex_lock(&mdp5_data->list_lock);
	list_for_each_entry(pipe, &mdp5_data->pipes_used, list) {
		if (release_all || (pipe->pid == pid)) {
			unset_ndx |= pipe->ndx;
			cnt++;
		}
	}

	if (!mfd->ref_cnt && !list_empty(&mdp5_data->pipes_cleanup)) {
		pr_debug("fb%d:: free pipes present in cleanup list",
			mfd->index);
		cnt++;
	}

	pr_debug("release_all=%d mfd->ref_cnt=%d unset_ndx=0x%x cnt=%d\n",
		release_all, mfd->ref_cnt, unset_ndx, cnt);

	mutex_unlock(&mdp5_data->list_lock);

	if (unset_ndx) {
		pr_debug("%d pipes need cleanup (%x)\n", cnt, unset_ndx);
		mdss_mdp_overlay_release(mfd, unset_ndx);
	}
	mutex_unlock(&mdp5_data->ov_lock);

	list_for_each_entry_safe(rot, tmp, &mdp5_data->rot_proc_list, list) {
		if (rot->pid == pid) {
			if (!list_empty(&rot->list))
				list_del_init(&rot->list);
			mdss_mdp_rotator_release(rot);
		}
	}

	return cnt;
}

static int mdss_mdp_overlay_play_wait(struct msm_fb_data_type *mfd,
				      struct msmfb_overlay_data *req)
{
	int ret = 0;

	if (!mfd)
		return -ENODEV;

	ret = mfd->mdp.kickoff_fnc(mfd, NULL);
	if (!ret)
		pr_err("error displaying\n");

	return ret;
}

static int mdss_mdp_overlay_queue(struct msm_fb_data_type *mfd,
				  struct msmfb_overlay_data *req)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_pipe *pipe;
	struct mdss_mdp_data *src_data;
	int ret;
	u32 flags;

	pipe = __overlay_find_pipe(mfd, req->id);
	if (!pipe) {
		pr_err("pipe ndx=%x doesn't exist\n", req->id);
		return -ENODEV;
	}

	ret = mdss_mdp_pipe_map(pipe);
	if (IS_ERR_VALUE(ret)) {
		pr_err("Unable to map used pipe%d ndx=%x\n",
				pipe->num, pipe->ndx);
		return ret;
	}

	if (pipe->dirty) {
		pr_warn("dirty pipe, will not queue pipe pnum=%d\n", pipe->num);
		return -ENODEV;
	}

	pr_debug("ov queue pnum=%d\n", pipe->num);

	if (pipe->flags & MDP_SOLID_FILL)
		pr_warn("Unexpected buffer queue to a solid fill pipe\n");

	flags = (pipe->flags & MDP_SECURE_OVERLAY_SESSION);

	mutex_lock(&mdp5_data->list_lock);
	src_data = __mdp_overlay_buf_alloc(mfd, pipe);
	if (!src_data) {
		pr_err("unable to allocate source buffer\n");
		ret = -ENOMEM;
	} else {
		ret = mdss_mdp_data_get(src_data, &req->data,
				1, flags);
		if (IS_ERR_VALUE(ret)) {
			__mdp_overlay_buf_free(mfd, src_data);
			pr_err("src_data pmem error\n");
		}
	}
	mutex_unlock(&mdp5_data->list_lock);

	mdss_mdp_pipe_unmap(pipe);

	return ret;
}

static void mdss_mdp_overlay_force_cleanup(struct msm_fb_data_type *mfd)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_ctl *ctl = mdp5_data->ctl;
	struct mdss_mdp_pipe *pipe, *tmp;
	LIST_HEAD(destroy_pipes);
	int ret;

	pr_debug("forcing cleanup to unset dma pipes on fb%d\n", mfd->index);

	/*
	 * video mode panels require the layer to be unstaged and wait for
	 * vsync to be able to release buffer.
	 */
	if (ctl && ctl->is_video_mode) {
		ret = mdss_mdp_display_commit(ctl, NULL, NULL);
		if (!IS_ERR_VALUE(ret))
			mdss_mdp_display_wait4comp(ctl);
	}

	mutex_lock(&mdp5_data->list_lock);
	list_for_each_entry_safe(pipe, tmp, &mdp5_data->pipes_cleanup, list) {
		list_move(&pipe->list, &destroy_pipes);
	}
	mutex_unlock(&mdp5_data->list_lock);
	mdss_mdp_overlay_cleanup(mfd, &destroy_pipes);
}

static void mdss_mdp_overlay_force_dma_cleanup(struct mdss_data_type *mdata)
{
	struct mdss_mdp_pipe *pipe;
	int i;

	for (i = 0; i < mdata->ndma_pipes; i++) {
		pipe = mdata->dma_pipes + i;

		if (!mdss_mdp_pipe_map(pipe)) {
			struct msm_fb_data_type *mfd = pipe->mfd;
			mdss_mdp_pipe_unmap(pipe);
			if (mfd)
				mdss_mdp_overlay_force_cleanup(mfd);
		}
	}
}

static int mdss_mdp_overlay_play(struct msm_fb_data_type *mfd,
				 struct msmfb_overlay_data *req)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	int ret = 0;

	pr_debug("play req id=%x\n", req->id);

	ret = mutex_lock_interruptible(&mdp5_data->ov_lock);
	if (ret)
		return ret;

	if (mdss_fb_is_power_off(mfd)) {
		ret = -EPERM;
		goto done;
	}

	if (req->id & MDSS_MDP_ROT_SESSION_MASK) {
		mdss_mdp_overlay_force_dma_cleanup(mfd_to_mdata(mfd));

		ret = mdss_mdp_rotator_play(mfd, req);
	} else if (req->id == BORDERFILL_NDX) {
		pr_debug("borderfill enable\n");
		mdp5_data->borderfill_enable = true;
		ret = mdss_mdp_overlay_free_fb_pipe(mfd);
	} else {
		ret = mdss_mdp_overlay_queue(mfd, req);
	}

done:
	mutex_unlock(&mdp5_data->ov_lock);

	return ret;
}

static int mdss_mdp_overlay_free_fb_pipe(struct msm_fb_data_type *mfd)
{
	struct mdss_mdp_pipe *pipe;
	u32 fb_ndx = 0;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);

	pipe = mdss_mdp_mixer_stage_pipe(mdp5_data->ctl,
					MDSS_MDP_MIXER_MUX_LEFT,
					 MDSS_MDP_STAGE_BASE);
	if (pipe)
		fb_ndx |= pipe->ndx;

	pipe = mdss_mdp_mixer_stage_pipe(mdp5_data->ctl,
					MDSS_MDP_MIXER_MUX_RIGHT,
					 MDSS_MDP_STAGE_BASE);
	if (pipe)
		fb_ndx |= pipe->ndx;

	if (fb_ndx) {
		pr_debug("unstaging framebuffer pipes %x\n", fb_ndx);
		mdss_mdp_overlay_release(mfd, fb_ndx);
	}
	return 0;
}

static int mdss_mdp_overlay_get_fb_pipe(struct msm_fb_data_type *mfd,
					struct mdss_mdp_pipe **ppipe,
					int mixer_mux)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_pipe *pipe;
	int ret;

	pipe = mdss_mdp_mixer_stage_pipe(mdp5_data->ctl, mixer_mux,
					 MDSS_MDP_STAGE_BASE);

	if (pipe == NULL) {
		struct mdp_overlay req;
		struct fb_info *fbi = mfd->fbi;
		struct mdss_mdp_mixer *mixer;
		int bpp;

		mixer = mdss_mdp_mixer_get(mdp5_data->ctl,
					MDSS_MDP_MIXER_MUX_LEFT);
		if (!mixer) {
			pr_err("unable to retrieve mixer\n");
			return -ENODEV;
		}

		memset(&req, 0, sizeof(req));

		bpp = fbi->var.bits_per_pixel / 8;
		req.id = MSMFB_NEW_REQUEST;
		req.src.format = mfd->fb_imgType;
		req.src.height = fbi->var.yres;
		req.src.width = fbi->fix.line_length / bpp;
		if (mixer_mux == MDSS_MDP_MIXER_MUX_RIGHT) {
			if (req.src.width <= mixer->width) {
				pr_warn("right fb pipe not needed\n");
				return -EINVAL;
			}
			req.flags = req.flags | MDSS_MDP_RIGHT_MIXER;
			req.src_rect.x = mixer->width;
			req.src_rect.w = fbi->var.xres - mixer->width;
		} else {
			req.src_rect.x = 0;
			req.src_rect.w = MIN(fbi->var.xres,
							mixer->width);
		}

		req.src_rect.y = 0;
		req.src_rect.h = req.src.height;
		req.dst_rect.w = req.src_rect.w;
		req.dst_rect.h = req.src_rect.h;
		req.z_order = MDSS_MDP_STAGE_BASE;

		pr_debug("allocating base pipe mux=%d\n", mixer_mux);

		ret = mdss_mdp_overlay_pipe_setup(mfd, &req, &pipe);
		if (ret)
			return ret;
	}
	pr_debug("ctl=%d pnum=%d\n", mdp5_data->ctl->num, pipe->num);

	*ppipe = pipe;
	return 0;
}

static void mdss_mdp_overlay_pan_display(struct msm_fb_data_type *mfd)
{
	struct mdss_mdp_data *buf_l = NULL, *buf_r = NULL;
	struct mdss_mdp_pipe *pipe;
	struct fb_info *fbi;
	struct mdss_overlay_private *mdp5_data;
	u32 offset;
	int bpp, ret;

	if (!mfd)
		return;

	fbi = mfd->fbi;
	mdp5_data = mfd_to_mdp5_data(mfd);

	if (!mdp5_data || !mdp5_data->ctl)
		return;

	if (!fbi->fix.smem_start || fbi->fix.smem_len == 0 ||
			mdp5_data->borderfill_enable) {
		mfd->mdp.kickoff_fnc(mfd, NULL);
		return;
	}

	if (mutex_lock_interruptible(&mdp5_data->ov_lock))
		return;

	if ((mdss_fb_is_power_off(mfd)) &&
		!((mfd->dcm_state == DCM_ENTER) &&
		(mfd->panel.type == MIPI_CMD_PANEL))) {
		mutex_unlock(&mdp5_data->ov_lock);
		return;
	}

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);


	bpp = fbi->var.bits_per_pixel / 8;
	offset = fbi->var.xoffset * bpp +
		 fbi->var.yoffset * fbi->fix.line_length;

	if (offset > fbi->fix.smem_len) {
		pr_err("invalid fb offset=%u total length=%u\n",
		       offset, fbi->fix.smem_len);
		goto pan_display_error;
	}

	ret = mdss_mdp_overlay_start(mfd);
	if (ret) {
		pr_err("unable to start overlay %d (%d)\n", mfd->index, ret);
		goto pan_display_error;
	}

	ret = mdss_iommu_ctrl(1);
	if (IS_ERR_VALUE(ret)) {
		pr_err("IOMMU attach failed\n");
		goto pan_display_error;
	}

	ret = mdss_mdp_overlay_get_fb_pipe(mfd, &pipe,
					MDSS_MDP_MIXER_MUX_LEFT);
	if (ret) {
		pr_err("unable to allocate base pipe\n");
		goto pan_display_error;
	}

	if (mdss_mdp_pipe_map(pipe)) {
		pr_err("unable to map base pipe\n");
		goto pan_display_error;
	}

	buf_l = mdss_mdp_overlay_buf_alloc(mfd, pipe);
	if (!buf_l) {
		pr_err("unable to allocate memory for fb buffer\n");
		goto pan_display_error;
	}

	if (is_mdss_iommu_attached()) {
		if (!mfd->iova) {
			pr_err("mfd iova is zero\n");
			mdss_mdp_pipe_unmap(pipe);
			goto pan_display_error;
		}
		buf_l->p[0].addr = mfd->iova;
	} else {
		buf_l->p[0].addr = fbi->fix.smem_start;
	}

	buf_l->p[0].addr += offset;
	buf_l->p[0].len = fbi->fix.smem_len - offset;
	buf_l->num_planes = 1;

	mdss_mdp_pipe_unmap(pipe);

	if (fbi->var.xres > MAX_MIXER_WIDTH || mfd->split_display) {
		ret = mdss_mdp_overlay_get_fb_pipe(mfd, &pipe,
					   MDSS_MDP_MIXER_MUX_RIGHT);
		if (ret) {
			pr_err("unable to allocate right base pipe\n");
			goto pan_display_error;
		}
		if (mdss_mdp_pipe_map(pipe)) {
			pr_err("unable to map right base pipe\n");
			goto pan_display_error;
		}

		buf_r = mdss_mdp_overlay_buf_alloc(mfd, pipe);
		if (!buf_r) {
			pr_err("unable to allocate memory for fb buffer\n");
			goto pan_display_error;
		}

		buf_r->p[0] = buf_l->p[0];
		buf_r->num_planes = 1;

		mdss_mdp_pipe_unmap(pipe);
	}
	mutex_unlock(&mdp5_data->ov_lock);

	if ((fbi->var.activate & FB_ACTIVATE_VBL) ||
	    (fbi->var.activate & FB_ACTIVATE_FORCE))
		mfd->mdp.kickoff_fnc(mfd, NULL);

	mdss_iommu_ctrl(0);
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);
	return;

pan_display_error:
	if (buf_l)
		mdss_mdp_overlay_buf_free(mfd, buf_l);
	if (buf_r)
		mdss_mdp_overlay_buf_free(mfd, buf_r);
	mdss_iommu_ctrl(0);
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);
	mutex_unlock(&mdp5_data->ov_lock);
}

static void remove_underrun_vsync_handler(struct work_struct *work)
{
	int rc;
	struct mdss_mdp_ctl *ctl =
		container_of(work, typeof(*ctl), remove_underrun_handler);

	if (!ctl || !ctl->remove_vsync_handler) {
		pr_err("ctl or vsync handler is NULL\n");
		return;
	}

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);
	rc = ctl->remove_vsync_handler(ctl,
			&ctl->recover_underrun_handler);
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);
}

static void mdss_mdp_recover_underrun_handler(struct mdss_mdp_ctl *ctl,
						ktime_t t)
{
	if (!ctl) {
		pr_err("ctl is NULL\n");
		return;
	}

	mdss_mdp_ctl_reset(ctl);
	schedule_work(&ctl->remove_underrun_handler);
}

/* function is called in irq context should have minimum processing */
static void mdss_mdp_overlay_handle_vsync(struct mdss_mdp_ctl *ctl,
						ktime_t t)
{
	struct msm_fb_data_type *mfd = NULL;
	struct mdss_overlay_private *mdp5_data = NULL;

	if (!ctl) {
		pr_err("ctl is NULL\n");
		return;
	}

	mfd = ctl->mfd;
	if (!mfd || !mfd->mdp.private1) {
		pr_warn("Invalid handle for vsync\n");
		return;
	}

	mdp5_data = mfd_to_mdp5_data(mfd);
	if (!mdp5_data) {
		pr_err("mdp5_data is NULL\n");
		return;
	}

	pr_debug("vsync on fb%d play_cnt=%d\n", mfd->index, ctl->play_cnt);

	mdp5_data->vsync_time = t;
	sysfs_notify_dirent(mdp5_data->vsync_event_sd);
}

int mdss_mdp_overlay_vsync_ctrl(struct msm_fb_data_type *mfd, int en)
{
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);
	int rc;

	if (!ctl)
		return -ENODEV;
	if (!ctl->add_vsync_handler || !ctl->remove_vsync_handler)
		return -EOPNOTSUPP;
	if (!ctl->panel_data->panel_info.cont_splash_enabled
			&& !mdss_mdp_ctl_is_power_on(ctl)) {
		pr_debug("fb%d vsync pending first update en=%d\n",
				mfd->index, en);
		return -EPERM;
	}

	pr_debug("fb%d vsync en=%d\n", mfd->index, en);

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);
	if (en)
		rc = ctl->add_vsync_handler(ctl, &ctl->vsync_handler);
	else
		rc = ctl->remove_vsync_handler(ctl, &ctl->vsync_handler);
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);

	return rc;
}

static ssize_t dynamic_fps_sysfs_rda_dfps(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	struct mdss_panel_data *pdata;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);

	if (!mdp5_data->ctl || !mdss_mdp_ctl_is_power_on(mdp5_data->ctl))
		return 0;

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata) {
		pr_err("no panel connected for fb%d\n", mfd->index);
		return -ENODEV;
	}

	mutex_lock(&mdp5_data->dfps_lock);
	ret = snprintf(buf, PAGE_SIZE, "%d\n",
		       pdata->panel_info.mipi.frame_rate);
	pr_debug("%s: '%d'\n", __func__,
		pdata->panel_info.mipi.frame_rate);
	mutex_unlock(&mdp5_data->dfps_lock);

	return ret;
} /* dynamic_fps_sysfs_rda_dfps */

static ssize_t dynamic_fps_sysfs_wta_dfps(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int dfps, rc = 0;
	struct mdss_panel_data *pdata;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);

	rc = kstrtoint(buf, 10, &dfps);
	if (rc) {
		pr_err("%s: kstrtoint failed. rc=%d\n", __func__, rc);
		return rc;
	}

	if (!mdp5_data->ctl || !mdss_mdp_ctl_is_power_on(mdp5_data->ctl))
		return 0;

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata) {
		pr_err("no panel connected for fb%d\n", mfd->index);
		return -ENODEV;
	}

	if (dfps == pdata->panel_info.mipi.frame_rate) {
		pr_debug("%s: FPS is already %d\n",
			__func__, dfps);
		return count;
	}

	mutex_lock(&mdp5_data->dfps_lock);
	if (dfps < pdata->panel_info.min_fps) {
		pr_err("Unsupported FPS. min_fps = %d\n",
				pdata->panel_info.min_fps);
		mutex_unlock(&mdp5_data->dfps_lock);
		return -EINVAL;
	} else if (dfps > pdata->panel_info.max_fps) {
		pr_warn("Unsupported FPS. Configuring to max_fps = %d\n",
				pdata->panel_info.max_fps);
		dfps = pdata->panel_info.max_fps;
		rc = mdss_mdp_ctl_update_fps(mdp5_data->ctl, dfps);
	} else {
		rc = mdss_mdp_ctl_update_fps(mdp5_data->ctl, dfps);
	}
	if (!rc) {
		pr_info("%s: configured to '%d' FPS\n", __func__, dfps);
	} else {
		pr_err("Failed to configure '%d' FPS. rc = %d\n",
							dfps, rc);
		mutex_unlock(&mdp5_data->dfps_lock);
		return rc;
	}
	pdata->panel_info.new_fps = dfps;
	mutex_unlock(&mdp5_data->dfps_lock);
	return count;
} /* dynamic_fps_sysfs_wta_dfps */


static DEVICE_ATTR(dynamic_fps, S_IRUGO | S_IWUSR, dynamic_fps_sysfs_rda_dfps,
	dynamic_fps_sysfs_wta_dfps);

static struct attribute *dynamic_fps_fs_attrs[] = {
	&dev_attr_dynamic_fps.attr,
	NULL,
};
static struct attribute_group dynamic_fps_fs_attrs_group = {
	.attrs = dynamic_fps_fs_attrs,
};

static ssize_t mdss_mdp_vsync_show_event(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	u64 vsync_ticks;
	int ret;

	if (!mdp5_data->ctl ||
		(!mdp5_data->ctl->panel_data->panel_info.cont_splash_enabled
			&& !mdss_mdp_ctl_is_power_on(mdp5_data->ctl)))
		return -EAGAIN;

	vsync_ticks = ktime_to_ns(mdp5_data->vsync_time);

	pr_debug("fb%d vsync=%llu", mfd->index, vsync_ticks);
	ret = scnprintf(buf, PAGE_SIZE, "VSYNC=%llu\n", vsync_ticks);

	return ret;
}

static inline int mdss_mdp_ad_is_supported(struct msm_fb_data_type *mfd)
{
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);
	struct mdss_mdp_mixer *mixer;

	if (!ctl) {
		pr_debug("there is no ctl attached to fb\n");
		return 0;
	}

	mixer = mdss_mdp_mixer_get(ctl, MDSS_MDP_MIXER_MUX_LEFT);
	if (mixer && (mixer->num > ctl->mdata->nad_cfgs)) {
		if (!mixer)
			pr_warn("there is no mixer attached to fb\n");
		else
			pr_debug("mixer attached (%d) doesnt support ad\n",
				 mixer->num);
		return 0;
	}

	mixer = mdss_mdp_mixer_get(ctl, MDSS_MDP_MIXER_MUX_RIGHT);
	if (mixer && (mixer->num > ctl->mdata->nad_cfgs))
		return 0;

	return 1;
}

static ssize_t mdss_mdp_ad_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	int ret, state;

	state = mdss_mdp_ad_is_supported(mfd) ? mdp5_data->ad_state : -1;

	ret = scnprintf(buf, PAGE_SIZE, "%d", state);

	return ret;
}

static ssize_t mdss_mdp_ad_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	int ret, ad;

	ret = kstrtoint(buf, 10, &ad);
	if (ret) {
		pr_err("Invalid input for ad\n");
		return -EINVAL;
	}

	mdp5_data->ad_state = ad;
	sysfs_notify(&dev->kobj, NULL, "ad");

	return count;
}


static DEVICE_ATTR(vsync_event, S_IRUGO, mdss_mdp_vsync_show_event, NULL);
static DEVICE_ATTR(ad, S_IRUGO | S_IWUSR | S_IWGRP, mdss_mdp_ad_show,
	mdss_mdp_ad_store);

static struct attribute *mdp_overlay_sysfs_attrs[] = {
	&dev_attr_vsync_event.attr,
	&dev_attr_ad.attr,
	NULL,
};

static struct attribute_group mdp_overlay_sysfs_group = {
	.attrs = mdp_overlay_sysfs_attrs,
};

static int mdss_mdp_hw_cursor_update(struct msm_fb_data_type *mfd,
				     struct fb_cursor *cursor)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_mixer *mixer;
	struct fb_image *img = &cursor->image;
	u32 blendcfg;
	int off, ret = 0;

	if (!mfd->cursor_buf && (cursor->set & FB_CUR_SETIMAGE)) {
		mfd->cursor_buf = dma_alloc_coherent(NULL, MDSS_MDP_CURSOR_SIZE,
					(dma_addr_t *) &mfd->cursor_buf_phys,
					GFP_KERNEL);
		if (!mfd->cursor_buf) {
			pr_err("can't allocate cursor buffer\n");
			return -ENOMEM;
		}

		ret = msm_iommu_map_contig_buffer(mfd->cursor_buf_phys,
			mdss_get_iommu_domain(MDSS_IOMMU_DOMAIN_UNSECURE),
			0, MDSS_MDP_CURSOR_SIZE, SZ_4K, 0,
			&(mfd->cursor_buf_iova));
		if (IS_ERR_VALUE(ret)) {
			dma_free_coherent(NULL, MDSS_MDP_CURSOR_SIZE,
					  mfd->cursor_buf,
					  (dma_addr_t) mfd->cursor_buf_phys);
			pr_err("unable to map cursor buffer to iommu(%d)\n",
			       ret);
			return -ENOMEM;
		}
	}

	mixer = mdss_mdp_mixer_get(mdp5_data->ctl, MDSS_MDP_MIXER_MUX_DEFAULT);
	off = MDSS_MDP_REG_LM_OFFSET(mixer->num);
	if (!mixer)
		return -ENODEV;

	if ((img->width > MDSS_MDP_CURSOR_WIDTH) ||
		(img->height > MDSS_MDP_CURSOR_HEIGHT) ||
		(img->depth != 32))
		return -EINVAL;

	pr_debug("mixer=%d enable=%x set=%x\n", mixer->num, cursor->enable,
			cursor->set);


	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);
	blendcfg = MDSS_MDP_REG_READ(off + MDSS_MDP_REG_LM_CURSOR_BLEND_CONFIG);

	if (cursor->set & FB_CUR_SETPOS)
		MDSS_MDP_REG_WRITE(off + MDSS_MDP_REG_LM_CURSOR_START_XY,
				   (img->dy << 16) | img->dx);

	if (cursor->set & FB_CUR_SETIMAGE) {
		int calpha_en, transp_en, alpha, size, cursor_addr;
		ret = copy_from_user(mfd->cursor_buf, img->data,
				     img->width * img->height * 4);

		if (ret)
			return ret;

		if (is_mdss_iommu_attached())
			cursor_addr = mfd->cursor_buf_iova;

		else
			cursor_addr = mfd->cursor_buf_phys;

		if (img->bg_color == 0xffffffff)
			transp_en = 0;
		else
			transp_en = 1;

		alpha = (img->fg_color & 0xff000000) >> 24;

		if (alpha)
			calpha_en = 0x0; /* xrgb */
		else
			calpha_en = 0x2; /* argb */

		size = (img->height << 16) | img->width;
		MDSS_MDP_REG_WRITE(off + MDSS_MDP_REG_LM_CURSOR_IMG_SIZE, size);
		MDSS_MDP_REG_WRITE(off + MDSS_MDP_REG_LM_CURSOR_SIZE, size);
		MDSS_MDP_REG_WRITE(off + MDSS_MDP_REG_LM_CURSOR_STRIDE,
				   img->width * 4);
		MDSS_MDP_REG_WRITE(off + MDSS_MDP_REG_LM_CURSOR_BASE_ADDR,
				   cursor_addr);

		wmb();

		blendcfg &= ~0x1;
		blendcfg |= (transp_en << 3) | (calpha_en << 1);
		MDSS_MDP_REG_WRITE(off + MDSS_MDP_REG_LM_CURSOR_BLEND_CONFIG,
				   blendcfg);
		if (calpha_en)
			MDSS_MDP_REG_WRITE(off +
					   MDSS_MDP_REG_LM_CURSOR_BLEND_PARAM,
					   alpha);

		if (transp_en) {
			MDSS_MDP_REG_WRITE(off +
				   MDSS_MDP_REG_LM_CURSOR_BLEND_TRANSP_LOW0,
				   ((img->bg_color & 0xff00) << 8) |
				   (img->bg_color & 0xff));
			MDSS_MDP_REG_WRITE(off +
				   MDSS_MDP_REG_LM_CURSOR_BLEND_TRANSP_LOW1,
				   ((img->bg_color & 0xff0000) >> 16));
			MDSS_MDP_REG_WRITE(off +
				   MDSS_MDP_REG_LM_CURSOR_BLEND_TRANSP_HIGH0,
				   ((img->bg_color & 0xff00) << 8) |
				   (img->bg_color & 0xff));
			MDSS_MDP_REG_WRITE(off +
				   MDSS_MDP_REG_LM_CURSOR_BLEND_TRANSP_HIGH1,
				   ((img->bg_color & 0xff0000) >> 16));
		}
	}

	if (!cursor->enable != !(blendcfg & 0x1)) {
		if (cursor->enable) {
			pr_debug("enable hw cursor on mixer=%d\n", mixer->num);
			blendcfg |= 0x1;
		} else {
			pr_debug("disable hw cursor on mixer=%d\n", mixer->num);
			blendcfg &= ~0x1;
		}

		MDSS_MDP_REG_WRITE(off + MDSS_MDP_REG_LM_CURSOR_BLEND_CONFIG,
				   blendcfg);

		mixer->cursor_enabled = cursor->enable;
		mixer->params_changed++;
	}

	mixer->ctl->flush_bits |= BIT(6) << mixer->num;
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);

	return 0;
}

static int mdss_bl_scale_config(struct msm_fb_data_type *mfd,
						struct mdp_bl_scale_data *data)
{
	int ret = 0;
	int curr_bl;
	mutex_lock(&mfd->bl_lock);
	curr_bl = mfd->bl_level;
	mfd->bl_scale = data->scale;
	mfd->bl_min_lvl = data->min_lvl;
	pr_debug("update scale = %d, min_lvl = %d\n", mfd->bl_scale,
							mfd->bl_min_lvl);

	/* update current backlight to use new scaling*/
	mdss_fb_set_backlight(mfd, curr_bl);
	mutex_unlock(&mfd->bl_lock);
	return ret;
}

static int mdss_mdp_pp_ioctl(struct msm_fb_data_type *mfd,
				void __user *argp)
{
	int ret;
	struct msmfb_mdp_pp mdp_pp;
	u32 copyback = 0;
	u32 copy_from_kernel = 0;

	if (mfd->panel_info->partial_update_enabled) {
		pr_err("Partical update feature is enabled.");
		return -EPERM;
	}

	ret = copy_from_user(&mdp_pp, argp, sizeof(mdp_pp));
	if (ret)
		return ret;

	/* Supprt only MDP register read/write and
	exit_dcm in DCM state*/
	if (mfd->dcm_state == DCM_ENTER &&
			(mdp_pp.op != mdp_op_calib_buffer &&
			mdp_pp.op != mdp_op_calib_dcm_state))
		return -EPERM;

	switch (mdp_pp.op) {
	case mdp_op_pa_cfg:
		ret = mdss_mdp_pa_config(&mdp_pp.data.pa_cfg_data,
					&copyback);
		break;

	case mdp_op_pa_v2_cfg:
		ret = mdss_mdp_pa_v2_config(&mdp_pp.data.pa_v2_cfg_data,
					&copyback);
		break;

	case mdp_op_pcc_cfg:
		ret = mdss_mdp_pcc_config(&mdp_pp.data.pcc_cfg_data,
					&copyback);
		break;

	case mdp_op_lut_cfg:
		switch (mdp_pp.data.lut_cfg_data.lut_type) {
		case mdp_lut_igc:
			ret = mdss_mdp_igc_lut_config(
					(struct mdp_igc_lut_data *)
					&mdp_pp.data.lut_cfg_data.data,
					&copyback, copy_from_kernel);
			break;

		case mdp_lut_pgc:
			ret = mdss_mdp_argc_config(
				&mdp_pp.data.lut_cfg_data.data.pgc_lut_data,
				&copyback);
			break;

		case mdp_lut_hist:
			ret = mdss_mdp_hist_lut_config(
				(struct mdp_hist_lut_data *)
				&mdp_pp.data.lut_cfg_data.data, &copyback);
			break;

		default:
			ret = -ENOTSUPP;
			break;
		}
		break;
	case mdp_op_dither_cfg:
		ret = mdss_mdp_dither_config(
				&mdp_pp.data.dither_cfg_data,
				&copyback);
		break;
	case mdp_op_gamut_cfg:
		ret = mdss_mdp_gamut_config(
				&mdp_pp.data.gamut_cfg_data,
				&copyback);
		break;
	case mdp_bl_scale_cfg:
		ret = mdss_bl_scale_config(mfd, (struct mdp_bl_scale_data *)
						&mdp_pp.data.bl_scale_data);
		break;
	case mdp_op_ad_cfg:
		ret = mdss_mdp_ad_config(mfd, &mdp_pp.data.ad_init_cfg);
		break;
	case mdp_op_ad_input:
		ret = mdss_mdp_ad_input(mfd, &mdp_pp.data.ad_input, 1);
		if (ret > 0) {
			ret = 0;
			copyback = 1;
		}
		break;
	case mdp_op_calib_cfg:
		ret = mdss_mdp_calib_config((struct mdp_calib_config_data *)
					 &mdp_pp.data.calib_cfg, &copyback);
		break;
	case mdp_op_calib_mode:
		ret = mdss_mdp_calib_mode(mfd, &mdp_pp.data.mdss_calib_cfg);
		break;
	case mdp_op_calib_buffer:
		ret = mdss_mdp_calib_config_buffer(
				(struct mdp_calib_config_buffer *)
				 &mdp_pp.data.calib_buffer, &copyback);
		break;
	case mdp_op_calib_dcm_state:
		ret = mdss_fb_dcm(mfd, mdp_pp.data.calib_dcm.dcm_state);
		break;
	default:
		pr_err("Unsupported request to MDP_PP IOCTL. %d = op\n",
								mdp_pp.op);
		ret = -EINVAL;
		break;
	}
	if ((ret == 0) && copyback)
		ret = copy_to_user(argp, &mdp_pp, sizeof(struct msmfb_mdp_pp));
	return ret;
}

static int mdss_mdp_histo_ioctl(struct msm_fb_data_type *mfd, u32 cmd,
				void __user *argp)
{
	int ret = -ENOSYS;
	struct mdp_histogram_data hist;
	struct mdp_histogram_start_req hist_req;
	u32 block;
	u32 pp_bus_handle;
	static int req = -1;

	if (mfd->panel_info->partial_update_enabled) {
		pr_err("Partical update feature is enabled.");
		return -EPERM;
	}

	switch (cmd) {
	case MSMFB_HISTOGRAM_START:
		if (mdss_fb_is_power_off(mfd))
			return -EPERM;

		pp_bus_handle = mdss_mdp_get_mdata()->pp_bus_hdl;
		req = msm_bus_scale_client_update_request(pp_bus_handle,
				PP_CLK_CFG_ON);
		if (req)
			pr_err("Updated pp_bus_scale failed, ret = %d", req);

		ret = copy_from_user(&hist_req, argp, sizeof(hist_req));
		if (ret)
			return ret;

		ret = mdss_mdp_hist_start(&hist_req);
		break;

	case MSMFB_HISTOGRAM_STOP:
		ret = copy_from_user(&block, argp, sizeof(int));
		if (ret)
			return ret;

		ret = mdss_mdp_hist_stop(block);
		if (ret)
			return ret;

		if (!req) {
			pp_bus_handle = mdss_mdp_get_mdata()->pp_bus_hdl;
			req = msm_bus_scale_client_update_request(pp_bus_handle,
				 PP_CLK_CFG_OFF);
			if (req)
				pr_err("Updated pp_bus_scale failed, ret = %d",
					req);
		}
		break;

	case MSMFB_HISTOGRAM:
		if (mdss_fb_is_power_off(mfd))
			return -EPERM;

		ret = copy_from_user(&hist, argp, sizeof(hist));
		if (ret)
			return ret;

		ret = mdss_mdp_hist_collect(&hist);
		if (!ret)
			ret = copy_to_user(argp, &hist, sizeof(hist));
		break;
	default:
		break;
	}
	return ret;
}

static int mdss_fb_set_metadata(struct msm_fb_data_type *mfd,
				struct msmfb_metadata *metadata)
{
	struct mdss_data_type *mdata = mfd_to_mdata(mfd);
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);
	int ret = 0;
	if (!ctl)
		return  -EPERM;
	switch (metadata->op) {
	case metadata_op_vic:
		if (mfd->panel_info)
			mfd->panel_info->vic =
				metadata->data.video_info_code;
		else
			ret = -EINVAL;
		break;
	case metadata_op_crc:
		if (mdss_fb_is_power_off(mfd))
			return -EPERM;
		ret = mdss_misr_set(mdata, &metadata->data.misr_request, ctl);
		break;
	case metadata_op_wb_format:
		ret = mdss_mdp_wb_set_format(mfd,
				metadata->data.mixer_cfg.writeback_format);
		break;
	case metadata_op_wb_secure:
		ret = mdss_mdp_wb_set_secure(mfd, metadata->data.secure_en);
		break;
	default:
		pr_warn("unsupported request to MDP META IOCTL\n");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int mdss_fb_get_hw_caps(struct msm_fb_data_type *mfd,
		struct mdss_hw_caps *caps)
{
	struct mdss_data_type *mdata = mfd_to_mdata(mfd);
	caps->mdp_rev = mdata->mdp_rev;
	caps->vig_pipes = mdata->nvig_pipes;
	caps->rgb_pipes = mdata->nrgb_pipes;
	caps->dma_pipes = mdata->ndma_pipes;
	if (mdata->has_bwc)
		caps->features |= MDP_BWC_EN;
	if (mdata->has_decimation)
		caps->features |= MDP_DECIMATION_EN;

	caps->max_smp_cnt = mdss_res->smp_mb_cnt;
	caps->smp_per_pipe = mdata->smp_mb_per_pipe;

	return 0;
}

static int mdss_fb_get_metadata(struct msm_fb_data_type *mfd,
				struct msmfb_metadata *metadata)
{
	struct mdss_data_type *mdata = mfd_to_mdata(mfd);
	struct mdss_mdp_ctl *ctl = mfd_to_ctl(mfd);
	int ret = 0;
	if (!ctl)
		return -EPERM;
	switch (metadata->op) {
	case metadata_op_frame_rate:
		metadata->data.panel_frame_rate =
			mdss_panel_get_framerate(mfd->panel_info);
		break;
	case metadata_op_get_caps:
		ret = mdss_fb_get_hw_caps(mfd, &metadata->data.caps);
		break;
	case metadata_op_get_ion_fd:
		if (mfd->fb_ion_handle) {
			metadata->data.fbmem_ionfd =
				dma_buf_fd(mfd->fbmem_buf, 0);
			if (metadata->data.fbmem_ionfd < 0)
				pr_err("fd allocation failed. fd = %d\n",
						metadata->data.fbmem_ionfd);
		}
		break;
	case metadata_op_crc:
		if (mdss_fb_is_power_off(mfd))
			return -EPERM;
		ret = mdss_misr_get(mdata, &metadata->data.misr_request, ctl);
		break;
	case metadata_op_wb_format:
		ret = mdss_mdp_wb_get_format(mfd, &metadata->data.mixer_cfg);
		break;
	case metadata_op_wb_secure:
		ret = mdss_mdp_wb_get_secure(mfd, &metadata->data.secure_en);
		break;
	default:
		pr_warn("Unsupported request to MDP META IOCTL.\n");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int __mdss_mdp_clean_dirty_pipes(struct msm_fb_data_type *mfd)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_pipe *pipe;
	int unset_ndx = 0;

	mutex_lock(&mdp5_data->list_lock);
	list_for_each_entry(pipe, &mdp5_data->pipes_used, list) {
		if (pipe->dirty)
			unset_ndx |= pipe->ndx;
	}
	mutex_unlock(&mdp5_data->list_lock);
	if (unset_ndx)
		mdss_mdp_overlay_release(mfd, unset_ndx);

	return unset_ndx;
}

static int mdss_mdp_overlay_precommit(struct msm_fb_data_type *mfd)
{
	struct mdss_overlay_private *mdp5_data;
	int ret;

	if (!mfd)
		return -ENODEV;

	mdp5_data = mfd_to_mdp5_data(mfd);
	if (!mdp5_data)
		return -ENODEV;

	ret = mutex_lock_interruptible(&mdp5_data->ov_lock);
	if (ret)
		return ret;

	/*
	 * we can assume that any pipes that are still dirty at this point are
	 * not properly tracked by user land. This could be for any reason,
	 * mark them for cleanup at this point.
	 */
	ret = __mdss_mdp_clean_dirty_pipes(mfd);
	if (ret) {
		pr_warn("fb%d: dirty pipes remaining %x\n",
				mfd->index, ret);
		ret = -EPIPE;
	}
	mutex_unlock(&mdp5_data->ov_lock);

	return ret;
}

static int __handle_overlay_prepare(struct msm_fb_data_type *mfd,
		struct mdp_overlay_list *ovlist,
		struct mdp_overlay *overlays)
{
	struct mdss_mdp_pipe *right_plist[MDSS_MDP_MAX_STAGE] = { 0 };
	struct mdss_mdp_pipe *left_plist[MDSS_MDP_MAX_STAGE] = { 0 };
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_pipe *pipe;
	struct mdp_overlay *req;
	int ret = 0, left_cnt = 0, right_cnt = 0;
	int i;
	u32 new_reqs = 0;
	u32 left_lm_w = left_lm_w_from_mfd(mfd);

	ret = mutex_lock_interruptible(&mdp5_data->ov_lock);
	if (ret)
		return ret;

	if (mdss_fb_is_power_off(mfd)) {
		mutex_unlock(&mdp5_data->ov_lock);
		return -EPERM;
	}

	pr_debug("prepare fb%d num_overlays=%d\n", mfd->index,
			ovlist->num_overlays);

	for (i = 0; i < ovlist->num_overlays; i++) {
		req = overlays + i;

		req->z_order += MDSS_MDP_STAGE_0;
		ret = mdss_mdp_overlay_pipe_setup(mfd, req, &pipe);
		req->z_order -= MDSS_MDP_STAGE_0;

		if (IS_ERR_VALUE(ret))
			goto validate_exit;

		/* keep track of the new overlays to unset in case of errors */
		if (pipe->play_cnt == 0)
			new_reqs |= pipe->ndx;

		if (IS_RIGHT_MIXER_OV(req->flags, req->dst_rect.x, left_lm_w)) {
			if (right_cnt >= MDSS_MDP_MAX_STAGE) {
				pr_err("too many pipes on right mixer\n");
				ret = -EINVAL;
				goto validate_exit;
			}
			right_plist[right_cnt] = pipe;
			right_cnt++;
		} else {
			if (left_cnt >= MDSS_MDP_MAX_STAGE) {
				pr_err("too many pipes on left mixer\n");
				ret = -EINVAL;
				goto validate_exit;
			}
			left_plist[left_cnt] = pipe;
			left_cnt++;
		}
	}

	ret = mdss_mdp_perf_bw_check(mdp5_data->ctl, left_plist, left_cnt,
			right_plist, right_cnt);

validate_exit:
	if (IS_ERR_VALUE(ret))
		mdss_mdp_overlay_release(mfd, new_reqs);
	mutex_unlock(&mdp5_data->ov_lock);

	ovlist->processed_overlays = i;

	return ret;
}

static int __handle_ioctl_overlay_prepare(struct msm_fb_data_type *mfd,
		void __user *argp)
{
	struct mdp_overlay_list ovlist;
	struct mdp_overlay *req_list[OVERLAY_MAX];
	struct mdp_overlay *overlays;
	int i, ret;

	if (!mfd_to_ctl(mfd))
		return -ENODEV;

	if (copy_from_user(&ovlist, argp, sizeof(ovlist)))
		return -EFAULT;

	if (ovlist.num_overlays >= OVERLAY_MAX) {
		pr_err("Number of overlays exceeds max\n");
		return -EINVAL;
	}

	overlays = kmalloc(ovlist.num_overlays * sizeof(*overlays), GFP_KERNEL);
	if (!overlays) {
		pr_err("Unable to allocate memory for overlays\n");
		return -ENOMEM;
	}

	if (copy_from_user(req_list, ovlist.overlay_list,
				sizeof(struct mdp_overlay*) * ovlist.num_overlays)) {
		ret = -EFAULT;
		goto validate_exit;
	}

	for (i = 0; i < ovlist.num_overlays; i++) {
		if (copy_from_user(overlays + i, req_list[i],
				sizeof(struct mdp_overlay))) {
			ret = -EFAULT;
			goto validate_exit;
		}
	}

	ret = __handle_overlay_prepare(mfd, &ovlist, overlays);
	if (!IS_ERR_VALUE(ret)) {
		for (i = 0; i < ovlist.num_overlays; i++) {
			if (copy_to_user(req_list[i], overlays + i,
					sizeof(struct mdp_overlay))) {
				ret = -EFAULT;
				goto validate_exit;
			}
		}
	}

	if (copy_to_user(argp, &ovlist, sizeof(ovlist)))
		ret = -EFAULT;

validate_exit:
	kfree(overlays);

	return ret;
}

static int mdss_mdp_overlay_ioctl_handler(struct msm_fb_data_type *mfd,
					  u32 cmd, void __user *argp)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdp_overlay *req = NULL;
	int val, ret = -ENOSYS;
	struct msmfb_metadata metadata;

	switch (cmd) {
	case MSMFB_MDP_PP:
		ret = mdss_mdp_pp_ioctl(mfd, argp);
		break;

	case MSMFB_HISTOGRAM_START:
	case MSMFB_HISTOGRAM_STOP:
	case MSMFB_HISTOGRAM:
		ret = mdss_mdp_histo_ioctl(mfd, cmd, argp);
		break;

	case MSMFB_OVERLAY_GET:
		req = kmalloc(sizeof(struct mdp_overlay), GFP_KERNEL);
		if (!req)
			return -ENOMEM;
		ret = copy_from_user(req, argp, sizeof(*req));
		if (!ret) {
			ret = mdss_mdp_overlay_get(mfd, req);

			if (!IS_ERR_VALUE(ret))
				ret = copy_to_user(argp, req, sizeof(*req));
		}

		if (ret)
			pr_debug("OVERLAY_GET failed (%d)\n", ret);
		break;

	case MSMFB_OVERLAY_SET:
		req = kmalloc(sizeof(struct mdp_overlay), GFP_KERNEL);
		if (!req)
			return -ENOMEM;
		ret = copy_from_user(req, argp, sizeof(*req));
		if (!ret) {
			ret = mdss_mdp_overlay_set(mfd, req);

			if (!IS_ERR_VALUE(ret))
				ret = copy_to_user(argp, req, sizeof(*req));
		}
		if (ret)
			pr_debug("OVERLAY_SET failed (%d)\n", ret);
		break;


	case MSMFB_OVERLAY_UNSET:
		if (!IS_ERR_VALUE(copy_from_user(&val, argp, sizeof(val))))
			ret = mdss_mdp_overlay_unset(mfd, val);
		break;

	case MSMFB_OVERLAY_PLAY_ENABLE:
		if (!copy_from_user(&val, argp, sizeof(val))) {
			mdp5_data->overlay_play_enable = val;
			ret = 0;
		} else {
			pr_err("OVERLAY_PLAY_ENABLE failed (%d)\n", ret);
			ret = -EFAULT;
		}
		break;

	case MSMFB_OVERLAY_PLAY:
		if (mdp5_data->overlay_play_enable) {
			struct msmfb_overlay_data data;

			ret = copy_from_user(&data, argp, sizeof(data));
			if (!ret)
				ret = mdss_mdp_overlay_play(mfd, &data);

			if (ret)
				pr_debug("OVERLAY_PLAY failed (%d)\n", ret);
		} else {
			ret = 0;
		}
		break;

	case MSMFB_OVERLAY_PLAY_WAIT:
		if (mdp5_data->overlay_play_enable) {
			struct msmfb_overlay_data data;

			ret = copy_from_user(&data, argp, sizeof(data));
			if (!ret)
				ret = mdss_mdp_overlay_play_wait(mfd, &data);

			if (ret)
				pr_err("OVERLAY_PLAY_WAIT failed (%d)\n", ret);
		} else {
			ret = 0;
		}
		break;

	case MSMFB_VSYNC_CTRL:
	case MSMFB_OVERLAY_VSYNC_CTRL:
		if (!copy_from_user(&val, argp, sizeof(val))) {
			ret = mdss_mdp_overlay_vsync_ctrl(mfd, val);
		} else {
			pr_err("MSMFB_OVERLAY_VSYNC_CTRL failed (%d)\n", ret);
			ret = -EFAULT;
		}
		break;
	case MSMFB_OVERLAY_COMMIT:
		mdss_fb_wait_for_fence(&(mfd->mdp_sync_pt_data));
		ret = mfd->mdp.kickoff_fnc(mfd, NULL);
		break;
	case MSMFB_METADATA_SET:
		ret = copy_from_user(&metadata, argp, sizeof(metadata));
		if (ret)
			return ret;
		ret = mdss_fb_set_metadata(mfd, &metadata);
		break;
	case MSMFB_METADATA_GET:
		ret = copy_from_user(&metadata, argp, sizeof(metadata));
		if (ret)
			return ret;
		ret = mdss_fb_get_metadata(mfd, &metadata);
		if (!ret)
			ret = copy_to_user(argp, &metadata, sizeof(metadata));
		break;
	case MSMFB_OVERLAY_PREPARE:
		ret = __handle_ioctl_overlay_prepare(mfd, argp);
		break;
	default:
		if (mfd->panel.type == WRITEBACK_PANEL)
			ret = mdss_mdp_wb_ioctl_handler(mfd, cmd, argp);
		break;
	}

	kfree(req);
	return ret;
}

/**
 * __mdss_mdp_overlay_ctl_init - Helper function to intialize control structure
 * @mfd: msm frame buffer data structure associated with the fb device.
 *
 * Helper function that allocates and initializes the mdp control structure
 * for a frame buffer device. Whenver applicable, this function will also setup
 * the control for the split display path as well.
 *
 * Return: pointer to the newly allocated control structure.
 */
static struct mdss_mdp_ctl *__mdss_mdp_overlay_ctl_init(
	struct msm_fb_data_type *mfd)
{
	int rc = 0;
	struct mdss_mdp_ctl *ctl;
	struct mdss_panel_data *pdata;

	if (!mfd)
		return ERR_PTR(-EINVAL);

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata) {
		pr_err("no panel connected for fb%d\n", mfd->index);
		rc = -ENODEV;
		goto error;
	}

	ctl = mdss_mdp_ctl_init(pdata, mfd);
	if (IS_ERR_OR_NULL(ctl)) {
		pr_err("Unable to initialize ctl for fb%d\n",
			mfd->index);
		rc = PTR_ERR(ctl);
		goto error;
	}
	ctl->vsync_handler.vsync_handler =
					mdss_mdp_overlay_handle_vsync;
	ctl->vsync_handler.cmd_post_flush = false;

	ctl->recover_underrun_handler.vsync_handler =
			mdss_mdp_recover_underrun_handler;
	ctl->recover_underrun_handler.cmd_post_flush = false;

	INIT_WORK(&ctl->remove_underrun_handler,
				remove_underrun_vsync_handler);

	if (mfd->split_display && pdata->next) {
		/* enable split display */
		rc = mdss_mdp_ctl_split_display_setup(ctl, pdata->next);
		if (rc) {
			mdss_mdp_ctl_destroy(ctl);
			goto error;
		}
	}

error:
	if (rc)
		return ERR_PTR(rc);
	else
		return ctl;
}

static int mdss_mdp_overlay_on(struct msm_fb_data_type *mfd)
{
	int rc;
	struct mdss_overlay_private *mdp5_data;
	struct mdss_mdp_ctl *ctl = NULL;

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	mdp5_data = mfd_to_mdp5_data(mfd);
	if (!mdp5_data)
		return -EINVAL;

	if (!mdp5_data->ctl) {
		ctl = __mdss_mdp_overlay_ctl_init(mfd);
		if (IS_ERR_OR_NULL(ctl))
			return PTR_ERR(ctl);
		mdp5_data->ctl = ctl;
	}

	if (mdss_fb_is_power_on(mfd)) {
		pr_debug("panel was never turned off\n");
		rc = mdss_mdp_ctl_start(mdp5_data->ctl, false);
		goto panel_on;
	}

	rc = mdss_mdp_ctl_intf_event(mdp5_data->ctl,
		MDSS_EVENT_RESET, NULL);
	if (rc)
		goto panel_on;

	if (!mfd->panel_info->cont_splash_enabled &&
		(mfd->panel_info->type != DTV_PANEL)) {
		rc = mdss_mdp_overlay_start(mfd);
		if (rc)
			goto end;
		if (mfd->panel_info->type != WRITEBACK_PANEL) {
			atomic_inc(&mfd->mdp_sync_pt_data.commit_cnt);
			rc = mdss_mdp_overlay_kickoff(mfd, NULL);
		}
	} else {
		rc = mdss_mdp_ctl_setup(mdp5_data->ctl);
		if (rc)
			goto end;
	}

panel_on:
	if (IS_ERR_VALUE(rc)) {
		pr_err("Failed to turn on fb%d\n", mfd->index);
		mdss_mdp_overlay_off(mfd);
		goto end;
	}

end:
	return rc;
}

static int mdss_mdp_overlay_off(struct msm_fb_data_type *mfd)
{
	int rc;
	struct mdss_overlay_private *mdp5_data;
	struct mdss_mdp_mixer *mixer;
	int need_cleanup;

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	mdp5_data = mfd_to_mdp5_data(mfd);

	if (!mdp5_data || !mdp5_data->ctl) {
		pr_err("ctl not initialized\n");
		return -ENODEV;
	}

	if (!mdss_mdp_ctl_is_power_on(mdp5_data->ctl))
		return 0;

	/*
	 * Keep a reference to the runtime pm until the overlay is turned
	 * off, and then release this last reference at the end. This will
	 * help in distinguishing between idle power collapse versus suspend
	 * power collapse
	 */
	pm_runtime_get_sync(&mfd->pdev->dev);

	if (mdss_fb_is_power_on_lp(mfd)) {
		pr_debug("panel not turned off. keeping overlay on\n");
		goto ctl_stop;
	}

	mutex_lock(&mdp5_data->ov_lock);

	mdss_mdp_overlay_free_fb_pipe(mfd);

	mixer = mdss_mdp_mixer_get(mdp5_data->ctl, MDSS_MDP_MIXER_MUX_LEFT);
	if (mixer)
		mixer->cursor_enabled = 0;

	mixer = mdss_mdp_mixer_get(mdp5_data->ctl, MDSS_MDP_MIXER_MUX_RIGHT);
	if (mixer)
		mixer->cursor_enabled = 0;

	mutex_lock(&mdp5_data->list_lock);
	need_cleanup = !list_empty(&mdp5_data->pipes_cleanup);
	mutex_unlock(&mdp5_data->list_lock);
	mutex_unlock(&mdp5_data->ov_lock);

	if (need_cleanup) {
		pr_debug("cleaning up pipes on fb%d\n", mfd->index);
		mdss_mdp_overlay_kickoff(mfd, NULL);
	}

	/*
	 * If retire fences are still active wait for a vsync time
	 * for retire fence to be updated.
	 * As a last resort signal the timeline if vsync doesn't arrive.
	 */
	if (mdp5_data->retire_cnt) {
		u32 fps = mdss_panel_get_framerate(mfd->panel_info);
		u32 vsync_time = 1000 / (fps ? : DEFAULT_FRAME_RATE);

		msleep(vsync_time);

		__vsync_retire_signal(mfd, mdp5_data->retire_cnt);
	}

ctl_stop:
	rc = mdss_mdp_ctl_stop(mdp5_data->ctl, mfd->panel_power_state);
	if (rc == 0) {
            if (mdss_fb_is_power_off(mfd)) {
				mutex_lock(&mdp5_data->list_lock); 
                __mdss_mdp_overlay_free_list_purge(mfd);
				if (!mfd->ref_cnt)
					mdss_mdp_overlay_buf_deinit(mfd);
				mutex_unlock(&mdp5_data->list_lock);
                mdss_mdp_ctl_notifier_unregister(mdp5_data->ctl,
                                &mfd->mdp_sync_pt_data.notifier);

		    if (!mfd->ref_cnt) {
		    	    mdp5_data->borderfill_enable = false;
			    mdss_mdp_ctl_destroy(mdp5_data->ctl);
			    mdp5_data->ctl = NULL;
		    }       

                    if (atomic_dec_return(
			&mdp5_data->mdata->active_intf_cnt) == 0)
			mdss_mdp_rotator_release_all();

		    if (!mdp5_data->mdata->idle_pc_enabled) {
                        rc = pm_runtime_put(&mfd->pdev->dev);
			if (rc)
			    pr_err("unable to suspend w/pm_runtime_put (%d)\n",
					rc);
		    }
	    }
	}

	mutex_unlock(&mdp5_data->ov_lock);

	/* Release the last reference to the runtime device */
	rc = pm_runtime_put(&mfd->pdev->dev);
	if (rc)
		pr_err("unable to suspend w/pm_runtime_put (%d)\n", rc);

	return rc;
}

int mdss_panel_register_done(struct mdss_panel_data *pdata)
{
	if (pdata->panel_info.cont_splash_enabled)
		mdss_mdp_footswitch_ctrl_splash(1);

	return 0;
}

static int __mdss_mdp_ctl_handoff(struct mdss_mdp_ctl *ctl,
	struct mdss_data_type *mdata)
{
	int rc = 0;
	int i, j;
	u32 mixercfg;
	struct mdss_mdp_pipe *pipe = NULL;

	if (!ctl || !mdata)
		return -EINVAL;

	for (i = 0; i < mdata->nmixers_intf; i++) {
		mixercfg = mdss_mdp_ctl_read(ctl, MDSS_MDP_REG_CTL_LAYER(i));
		pr_debug("for lm%d mixercfg = 0x%09x\n", i, mixercfg);

		j = MDSS_MDP_SSPP_VIG0;
		for (; j < MDSS_MDP_MAX_SSPP && mixercfg; j++) {
			u32 cfg = j * 3;
			if ((j == MDSS_MDP_SSPP_VIG3) ||
			    (j == MDSS_MDP_SSPP_RGB3)) {
				/* Add 2 to account for Cursor & Border bits */
				cfg += 2;
			}
			if (mixercfg & (0x7 << cfg)) {
				pr_debug("Pipe %d staged\n", j);
				pipe = mdss_mdp_pipe_search(mdata, BIT(j));
				if (!pipe) {
					pr_warn("Invalid pipe %d staged\n", j);
					continue;
				}

				rc = mdss_mdp_pipe_handoff(pipe);
				if (rc) {
					pr_err("Failed to handoff pipe%d\n",
						pipe->num);
					goto exit;
				}

				rc = mdss_mdp_mixer_handoff(ctl, i, pipe);
				if (rc) {
					pr_err("failed to handoff mix%d\n", i);
					goto exit;
				}
			}
		}
	}
exit:
	return rc;
}

/**
 * mdss_mdp_overlay_handoff() - Read MDP registers to handoff an active ctl path
 * @mfd: Msm frame buffer structure associated with the fb device.
 *
 * This function populates the MDP software structures with the current state of
 * the MDP hardware to handoff any active control path for the framebuffer
 * device. This is needed to identify any ctl, mixers and pipes being set up by
 * the bootloader to display the splash screen when the continuous splash screen
 * feature is enabled in kernel.
 */
static int mdss_mdp_overlay_handoff(struct msm_fb_data_type *mfd)
{
	int rc = 0;
	struct mdss_data_type *mdata = mfd_to_mdata(mfd);
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_ctl *ctl = NULL;
	struct mdss_mdp_ctl *sctl = NULL;

	if (!mdp5_data->ctl) {
		ctl = __mdss_mdp_overlay_ctl_init(mfd);
		if (IS_ERR_OR_NULL(ctl)) {
			rc = PTR_ERR(ctl);
			goto error;
		}
		mdp5_data->ctl = ctl;
	}

	/*
	 * vsync interrupt needs on during continuous splash, this is
	 * to initialize necessary ctl members here.
	 */
	rc = mdss_mdp_ctl_start(ctl, true);
	if (rc) {
		pr_err("Failed to initialize ctl\n");
		goto error;
	}

	ctl->clk_rate = mdss_mdp_get_clk_rate(MDSS_CLK_MDP_SRC);
	pr_debug("Set the ctl clock rate to %d Hz\n", ctl->clk_rate);

	rc = __mdss_mdp_ctl_handoff(ctl, mdata);
	if (rc) {
		pr_err("primary ctl handoff failed. rc=%d\n", rc);
		goto error;
	}

	if (mfd->split_display) {
		sctl = mdss_mdp_get_split_ctl(ctl);
		if (!sctl) {
			pr_err("cannot get secondary ctl. fail the handoff\n");
			rc = -EPERM;
			goto error;
		}
		rc = __mdss_mdp_ctl_handoff(sctl, mdata);
		if (rc) {
			pr_err("secondary ctl handoff failed. rc=%d\n", rc);
			goto error;
		}
	}

	rc = mdss_mdp_smp_handoff(mdata);
	if (rc)
		pr_err("Failed to handoff smps\n");

	mdp5_data->handoff = true;

error:
	if (rc && ctl) {
		mdss_mdp_handoff_cleanup_pipes(mfd, MDSS_MDP_PIPE_TYPE_RGB);
		mdss_mdp_handoff_cleanup_pipes(mfd, MDSS_MDP_PIPE_TYPE_VIG);
		mdss_mdp_handoff_cleanup_pipes(mfd, MDSS_MDP_PIPE_TYPE_DMA);
		mdss_mdp_ctl_destroy(ctl);
		mdp5_data->ctl = NULL;
		mdp5_data->handoff = false;
	}

	return rc;
}

static void __vsync_retire_handle_vsync(struct mdss_mdp_ctl *ctl, ktime_t t)
{
	struct msm_fb_data_type *mfd = ctl->mfd;
	struct mdss_overlay_private *mdp5_data;

	if (!mfd || !mfd->mdp.private1) {
		pr_warn("Invalid handle for vsync\n");
		return;
	}

	mdp5_data = mfd_to_mdp5_data(mfd);
	schedule_work(&mdp5_data->retire_work);
}

static void __vsync_retire_work_handler(struct work_struct *work)
{
	struct mdss_overlay_private *mdp5_data =
		container_of(work, typeof(*mdp5_data), retire_work);

	if (!mdp5_data->ctl || !mdp5_data->ctl->mfd)
		return;

	if (!mdp5_data->ctl->remove_vsync_handler)
		return;

	__vsync_retire_signal(mdp5_data->ctl->mfd, 1);
}

static void __vsync_retire_signal(struct msm_fb_data_type *mfd, int val)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);

	mutex_lock(&mfd->mdp_sync_pt_data.sync_mutex);
	if (mdp5_data->retire_cnt > 0) {
		sw_sync_timeline_inc(mdp5_data->vsync_timeline, val);

		mdp5_data->retire_cnt -= min(val, mdp5_data->retire_cnt);
		if (mdp5_data->retire_cnt == 0) {
			mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON);
			mdp5_data->ctl->remove_vsync_handler(mdp5_data->ctl,
					&mdp5_data->vsync_retire_handler);
			mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);
		}
	}
	mutex_unlock(&mfd->mdp_sync_pt_data.sync_mutex);
}

static struct sync_fence *
__vsync_retire_get_fence(struct msm_sync_pt_data *sync_pt_data)
{
	struct msm_fb_data_type *mfd;
	struct mdss_overlay_private *mdp5_data;
	struct mdss_mdp_ctl *ctl;
	int value;

	mfd = container_of(sync_pt_data, typeof(*mfd), mdp_sync_pt_data);
	mdp5_data = mfd_to_mdp5_data(mfd);

	if (!mdp5_data || !mdp5_data->ctl)
		return ERR_PTR(-ENODEV);

	ctl = mdp5_data->ctl;
	if (!ctl->add_vsync_handler)
		return ERR_PTR(-EOPNOTSUPP);

	if (!mdss_mdp_ctl_is_power_on(ctl)) {
		pr_debug("fb%d vsync pending first update\n", mfd->index);
		return ERR_PTR(-EPERM);
	}

	value = mdp5_data->vsync_timeline->value + 1 + mdp5_data->retire_cnt;
	mdp5_data->retire_cnt++;

	return mdss_fb_sync_get_fence(mdp5_data->vsync_timeline,
			"mdp-retire", value);
}

static int __vsync_set_vsync_handler(struct msm_fb_data_type *mfd)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_ctl *ctl;
	int rc;

	ctl = mdp5_data->ctl;
	if (!mdp5_data->retire_cnt ||
		mdp5_data->vsync_retire_handler.enabled)
		return 0;

	if (!ctl->add_vsync_handler)
		return -EOPNOTSUPP;

	if (!mdss_mdp_ctl_is_power_on(ctl)) {
		pr_debug("fb%d vsync pending first update\n", mfd->index);
		return -EPERM;
	}

	rc = ctl->add_vsync_handler(ctl,
			&mdp5_data->vsync_retire_handler);
	return rc;
}

static int __vsync_retire_setup(struct msm_fb_data_type *mfd)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	char name[24];

	snprintf(name, sizeof(name), "mdss_fb%d_retire", mfd->index);
	mdp5_data->vsync_timeline = sw_sync_timeline_create(name);
	if (mdp5_data->vsync_timeline == NULL) {
		pr_err("cannot vsync create time line");
		return -ENOMEM;
	}
	mfd->mdp_sync_pt_data.get_retire_fence = __vsync_retire_get_fence;

	mdp5_data->vsync_retire_handler.vsync_handler =
		__vsync_retire_handle_vsync;
	mdp5_data->vsync_retire_handler.cmd_post_flush = false;
	INIT_WORK(&mdp5_data->retire_work, __vsync_retire_work_handler);

	return 0;
}

static int mdss_mdp_update_panel_info(struct msm_fb_data_type *mfd, int mode)
{
	int ret = 0;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_mdp_ctl *ctl = mdp5_data->ctl;

	ret = mdss_mdp_ctl_intf_event(ctl, MDSS_EVENT_DSI_DYNAMIC_SWITCH,
						(void *)(unsigned long)mode);
	if (ret)
		pr_err("Dynamic switch to %s mode failed!\n",
					mode ? "command" : "video");
	/*
	 * Destroy current ctrl sturcture as this is
	 * going to be re-initialized with the requested mode.
	 */
	mdss_mdp_ctl_destroy(mdp5_data->ctl);
	mdp5_data->ctl = NULL;

	return 0;
}

int mdss_mdp_overlay_init(struct msm_fb_data_type *mfd)
{
	struct device *dev = mfd->fbi->dev;
	struct msm_mdp_interface *mdp5_interface = &mfd->mdp;
	struct mdss_overlay_private *mdp5_data = NULL;
	int rc;

	mdp5_interface->on_fnc = mdss_mdp_overlay_on;
	mdp5_interface->off_fnc = mdss_mdp_overlay_off;
	mdp5_interface->release_fnc = __mdss_mdp_overlay_release_all;
	mdp5_interface->do_histogram = NULL;
	mdp5_interface->cursor_update = mdss_mdp_hw_cursor_update;
	mdp5_interface->dma_fnc = mdss_mdp_overlay_pan_display;
	mdp5_interface->ioctl_handler = mdss_mdp_overlay_ioctl_handler;
	mdp5_interface->panel_register_done = mdss_panel_register_done;
	mdp5_interface->kickoff_fnc = mdss_mdp_overlay_kickoff;
	mdp5_interface->pre_commit_fnc = mdss_mdp_overlay_precommit;
	mdp5_interface->get_sync_fnc = mdss_mdp_rotator_sync_pt_get;
	mdp5_interface->splash_init_fnc = mdss_mdp_splash_init;
	mdp5_interface->configure_panel = mdss_mdp_update_panel_info;

	mdp5_data = kmalloc(sizeof(struct mdss_overlay_private), GFP_KERNEL);
	if (!mdp5_data) {
		pr_err("fail to allocate mdp5 private data structure");
		return -ENOMEM;
	}
	memset(mdp5_data, 0, sizeof(struct mdss_overlay_private));

	INIT_LIST_HEAD(&mdp5_data->pipes_used);
	INIT_LIST_HEAD(&mdp5_data->pipes_cleanup);
	INIT_LIST_HEAD(&mdp5_data->pipes_destroy);
	INIT_LIST_HEAD(&mdp5_data->bufs_pool);
	INIT_LIST_HEAD(&mdp5_data->bufs_chunks);
	INIT_LIST_HEAD(&mdp5_data->bufs_used);
	INIT_LIST_HEAD(&mdp5_data->bufs_freelist);
	INIT_LIST_HEAD(&mdp5_data->rot_proc_list);
	mutex_init(&mdp5_data->list_lock);
	mutex_init(&mdp5_data->ov_lock);
	mutex_init(&mdp5_data->dfps_lock);
	mdp5_data->hw_refresh = true;
	mdp5_data->overlay_play_enable = true;

	mdp5_data->mdata = dev_get_drvdata(mfd->pdev->dev.parent);
	if (!mdp5_data->mdata) {
		pr_err("unable to initialize overlay for fb%d\n", mfd->index);
		rc = -ENODEV;
		goto init_fail;
	}
	mfd->mdp.private1 = mdp5_data;
	mfd->wait_for_kickoff = true;

	rc = mdss_mdp_overlay_fb_parse_dt(mfd);
	if (rc)
		return rc;

	rc = sysfs_create_group(&dev->kobj, &mdp_overlay_sysfs_group);
	if (rc) {
		pr_err("vsync sysfs group creation failed, ret=%d\n", rc);
		goto init_fail;
	}

	mdp5_data->vsync_event_sd = sysfs_get_dirent(dev->kobj.sd, NULL,
						     "vsync_event");
	if (!mdp5_data->vsync_event_sd) {
		pr_err("vsync_event sysfs lookup failed\n");
		rc = -ENODEV;
		goto init_fail;
	}

	if (mfd->panel_info->type == MIPI_VIDEO_PANEL) {
		rc = sysfs_create_group(&dev->kobj,
			&dynamic_fps_fs_attrs_group);
		if (rc) {
			pr_err("Error dfps sysfs creation ret=%d\n", rc);
			goto init_fail;
		}
	}

	if (mfd->panel_info->mipi.dynamic_switch_enabled ||
			mfd->panel_info->type == MIPI_CMD_PANEL) {
		rc = __vsync_retire_setup(mfd);
		if (IS_ERR_VALUE(rc)) {
			pr_err("unable to create vsync timeline\n");
			goto init_fail;
		}
	}
	mfd->mdp_sync_pt_data.async_wait_fences = true;
	rc = sysfs_create_link_nowarn(&dev->kobj,
			&mdp5_data->mdata->pdev->dev.kobj, "mdp");
	if (rc)
		pr_warn("problem creating link to mdp sysfs\n");

	pm_runtime_set_suspended(&mfd->pdev->dev);
	pm_runtime_enable(&mfd->pdev->dev);

	kobject_uevent(&dev->kobj, KOBJ_ADD);
	pr_debug("vsync kobject_uevent(KOBJ_ADD)\n");

	mdp5_data->cpu_pm_hdl = add_event_timer(NULL, (void *)mdp5_data);
	if (!mdp5_data->cpu_pm_hdl)
		pr_warn("%s: unable to add event timer\n", __func__);

	if (mfd->panel_info->cont_splash_enabled) {
		rc = mdss_mdp_overlay_handoff(mfd);
		if (rc) {
			/*
			 * Even though handoff failed, it is not fatal.
			 * MDP can continue, just that we would have a longer
			 * delay in transitioning from splash screen to boot
			 * animation
			 */
			pr_warn("Overlay handoff failed for fb%d. rc=%d\n",
				mfd->index, rc);
			rc = 0;
		}
	}

	if (mdss_mdp_pp_overlay_init(mfd))
		pr_warn("Failed to initialize pp overlay data.\n");
	return rc;
init_fail:
	kfree(mdp5_data);
	return rc;
}

static int mdss_mdp_overlay_fb_parse_dt(struct msm_fb_data_type *mfd)
{
	int rc = 0;
	struct platform_device *pdev = mfd->pdev;
	struct mdss_overlay_private *mdp5_mdata = mfd_to_mdp5_data(mfd);

	mdp5_mdata->mixer_swap = of_property_read_bool(pdev->dev.of_node,
					   "qcom,mdss-mixer-swap");
	if (mdp5_mdata->mixer_swap) {
		pr_info("mixer swap is enabled for fb device=%s\n",
			pdev->name);
	}

	return rc;
}
