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

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/lcd_notify.h>

#include "mdss.h"
#include "mdss_panel.h"
#include "mdss_dsi.h"
#include "mdss_debug.h"
#include "mdss_livedisplay.h"

struct device_node *of_get_child_by_name(const struct device_node *node,
				const char *name)
{
	struct device_node *child;

	for_each_child_of_node(node, child)
		if (child->name && (of_node_cmp(child->name, name) == 0))
			break;
	return child;
}
EXPORT_SYMBOL(of_get_child_by_name);

static int mdss_dsi_pinctrl_set_state(struct mdss_dsi_ctrl_pdata *ctrl_pdata,
					bool active);

static int mdss_dsi_regulator_init(struct platform_device *pdev)
{
	int rc = 0;

	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int i = 0;

	if (!pdev) {
		pr_err("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = platform_get_drvdata(pdev);
	if (!ctrl_pdata) {
		pr_err("%s: invalid driver data\n", __func__);
		return -EINVAL;
	}

	for (i = 0; !rc && (i < DSI_MAX_PM); i++) {
		rc = msm_dss_config_vreg(&pdev->dev,
			ctrl_pdata->power_data[i].vreg_config,
			ctrl_pdata->power_data[i].num_vreg, 1);
		if (rc)
			pr_err("%s: failed to init vregs for %s\n",
				__func__, __mdss_dsi_pm_name(i));
	}

	return rc;
}

static int mdss_dsi_panel_power_off(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int i = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		ret = -EINVAL;
		goto end;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	ret = mdss_dsi_panel_reset(pdata, 0);
	if (ret) {
		pr_warn("%s: Panel reset failed. rc=%d\n", __func__, ret);
		ret = 0;
	}

	if (mdss_dsi_pinctrl_set_state(ctrl_pdata, false))
		pr_debug("reset disable: pinctrl not enabled\n");

	for (i = DSI_MAX_PM - 1; i >= 0; i--) {
		/*
		 * Core power module will be disabled when the
		 * clocks are disabled
		 */
		if (DSI_CORE_PM == i)
			continue;
		ret = msm_dss_enable_vreg(
			ctrl_pdata->power_data[i].vreg_config,
			ctrl_pdata->power_data[i].num_vreg, 0);
		if (ret)
			pr_err("%s: failed to disable vregs for %s\n",
				__func__, __mdss_dsi_pm_name(i));
	}

end:
	return ret;
}

static int mdss_dsi_panel_power_on(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int i = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	for (i = 0; i < DSI_MAX_PM; i++) {
		/*
		 * Core power module will be enabled when the
		 * clocks are enabled
		 */
		if (DSI_CORE_PM == i)
			continue;
		ret = msm_dss_enable_vreg(
			ctrl_pdata->power_data[i].vreg_config,
			ctrl_pdata->power_data[i].num_vreg, 1);
		if (ret) {
			pr_err("%s: failed to enable vregs for %s\n",
				__func__, __mdss_dsi_pm_name(i));
			goto error;
		}
	}

	i--;

	/*
	 * If continuous splash screen feature is enabled, then we need to
	 * request all the GPIOs that have already been configured in the
	 * bootloader. This needs to be done irresepective of whether
	 * the lp11_init flag is set or not.
	 */
	if (pdata->panel_info.cont_splash_enabled ||
		!pdata->panel_info.mipi.lp11_init) {
		if (mdss_dsi_pinctrl_set_state(ctrl_pdata, true))
			pr_debug("reset enable: pinctrl not enabled\n");

		ret = mdss_dsi_panel_reset(pdata, 1);
		if (ret)
			pr_err("%s: Panel reset failed. rc=%d\n",
					__func__, ret);
	}

error:
	if (ret) {
		for (; i >= 0; i--)
			msm_dss_enable_vreg(
				ctrl_pdata->power_data[i].vreg_config,
				ctrl_pdata->power_data[i].num_vreg, 0);
	}
	return ret;
}

static int mdss_dsi_panel_power_lp(struct mdss_panel_data *pdata, int enable)
{
	/* Panel power control when entering/exiting lp mode */
	return 0;
}

static int mdss_dsi_panel_power_ulp(struct mdss_panel_data *pdata,
	int enable)
{
	int ret = 0, i;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	u32 mode = enable ? DSS_REG_MODE_ULP : DSS_REG_MODE_ENABLE;

	pr_debug("%s: +\n", __func__);
	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	for (i = 0; i < DSI_MAX_PM; i++) {
		/*
		 * Core power module need to be controlled along with
		 * DSI core clocks.
		 */
		if (DSI_CORE_PM == i)
			continue;
		ret = msm_dss_config_vreg_opt_mode(
			ctrl_pdata->power_data[i].vreg_config,
			ctrl_pdata->power_data[i].num_vreg, mode);
		if (ret) {
			pr_err("%s: failed to config lp opt mode for %s.rc=%d\n",
				__func__, __mdss_dsi_pm_name(i), ret);
			goto error;
		}
	}

error:
	if (ret) {
		mode = enable ? DSS_REG_MODE_ENABLE : DSS_REG_MODE_ULP;
		for (; i >= 0; i--)
			msm_dss_config_vreg_opt_mode(
				ctrl_pdata->power_data[i].vreg_config,
				ctrl_pdata->power_data[i].num_vreg, mode);
	}
	pr_debug("%s: -\n", __func__);
	return ret;
}

int mdss_dsi_panel_power_ctrl(struct mdss_panel_data *pdata, int power_state)
{
	int ret = 0;
	struct mdss_panel_info *pinfo;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	pinfo = &pdata->panel_info;
	pr_debug("%pS-->%s: cur_power_state=%d req_power_state=%d\n",
		__builtin_return_address(0), __func__,
		pinfo->panel_power_state, power_state);

	if (pinfo->panel_power_state == power_state) {
		pr_debug("%s: no change needed\n", __func__);
		return 0;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	/*
	 * If a dynamic mode switch is pending, the regulators should not
	 * be turned off or on.
	 */
	if (pdata->panel_info.dynamic_switch_pending)
		return 0;

	switch (power_state) {
	case MDSS_PANEL_POWER_OFF:
		ret = mdss_dsi_panel_power_off(pdata);
		break;
	case MDSS_PANEL_POWER_ON:
		if (mdss_dsi_is_panel_on_ulp(pdata))
			ret = mdss_dsi_panel_power_ulp(pdata, false);
		else if (mdss_dsi_is_panel_on_lp(pdata))
			ret = mdss_dsi_panel_power_lp(pdata, false);
		else
			ret = mdss_dsi_panel_power_on(pdata);
		break;
	case MDSS_PANEL_POWER_LP1:
		if (mdss_dsi_is_panel_on_ulp(pdata))
			ret = mdss_dsi_panel_power_ulp(pdata, false);
		else
			ret = mdss_dsi_panel_power_lp(pdata, true);
		/*
		 * temp workaround until framework issues pertaining to LP2
		 * power state transitions are fixed. For now, we internally
		 * transition to LP2 state whenever core power is turned off
		 * in LP1 state
		 */
	case MDSS_PANEL_POWER_LP2:
		if (!ctrl_pdata->core_power) {
			power_state = MDSS_PANEL_POWER_LP2;
			ret = mdss_dsi_panel_power_ulp(pdata, true);
		}
		break;
	default:
		pr_err("%s: unknown panel power state requested (%d)\n",
			__func__, power_state);
		ret = -EINVAL;
	}

	if (!ret)
		pinfo->panel_power_state = power_state;

	return ret;
}

static void mdss_dsi_put_dt_vreg_data(struct device *dev,
	struct dss_module_power *module_power)
{
	if (!module_power) {
		pr_err("%s: invalid input\n", __func__);
		return;
	}

	if (module_power->vreg_config) {
		devm_kfree(dev, module_power->vreg_config);
		module_power->vreg_config = NULL;
	}
	module_power->num_vreg = 0;
}

static int mdss_dsi_get_dt_vreg_data(struct device *dev,
	struct dss_module_power *mp, enum dsi_pm_type module)
{
	int i = 0, rc = 0;
	u32 tmp = 0;
	struct device_node *of_node = NULL, *supply_node = NULL;
	const char *pm_supply_name = NULL;
	struct device_node *supply_root_node = NULL;

	if (!dev || !mp) {
		pr_err("%s: invalid input\n", __func__);
		rc = -EINVAL;
		return rc;
	}

	of_node = dev->of_node;

	mp->num_vreg = 0;
	pm_supply_name = __mdss_dsi_pm_supply_node_name(module);
	supply_root_node = of_get_child_by_name(of_node, pm_supply_name);
	if (!supply_root_node) {
		pr_err("no supply entry present\n");
		goto novreg;
	}

	for_each_child_of_node(supply_root_node, supply_node) {
		mp->num_vreg++;
	}

	if (mp->num_vreg == 0) {
		pr_debug("%s: no vreg\n", __func__);
		goto novreg;
	} else {
		pr_debug("%s: vreg found. count=%d\n", __func__, mp->num_vreg);
	}

	mp->vreg_config = devm_kzalloc(dev, sizeof(struct dss_vreg) *
		mp->num_vreg, GFP_KERNEL);
	if (!mp->vreg_config) {
		pr_err("%s: can't alloc vreg mem\n", __func__);
		rc = -ENOMEM;
		goto error;
	}

	for_each_child_of_node(supply_root_node, supply_node) {
		const char *st = NULL;
		/* vreg-name */
		rc = of_property_read_string(supply_node,
			"qcom,supply-name", &st);
		if (rc) {
			pr_err("%s: error reading name. rc=%d\n",
				__func__, rc);
			goto error;
		}
		snprintf(mp->vreg_config[i].vreg_name,
			ARRAY_SIZE((mp->vreg_config[i].vreg_name)), "%s", st);
		/* vreg-min-voltage */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-min-voltage", &tmp);
		if (rc) {
			pr_err("%s: error reading min volt. rc=%d\n",
				__func__, rc);
			goto error;
		}
		mp->vreg_config[i].min_voltage = tmp;

		/* vreg-max-voltage */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-max-voltage", &tmp);
		if (rc) {
			pr_err("%s: error reading max volt. rc=%d\n",
				__func__, rc);
			goto error;
		}
		mp->vreg_config[i].max_voltage = tmp;

		/* enable-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-enable-load", &tmp);
		if (rc) {
			pr_err("%s: error reading enable load. rc=%d\n",
				__func__, rc);
			goto error;
		}
		mp->vreg_config[i].load[DSS_REG_MODE_ENABLE] = tmp;

		/* disable-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-disable-load", &tmp);
		if (rc) {
			pr_err("%s: error reading disable load. rc=%d\n",
				__func__, rc);
			goto error;
		}
		mp->vreg_config[i].load[DSS_REG_MODE_DISABLE] = tmp;

		/* ulp-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-ulp-load", &tmp);
		if (rc) {
			pr_debug("%s: error reading disable load. rc=%d\n",
				__func__, rc);
			rc = 0;
		} else {
			pr_debug("%s: ulp_load for %s=%d\n", __func__,
				mp->vreg_config[i].vreg_name, tmp);
			mp->vreg_config[i].load[DSS_REG_MODE_ULP] = tmp;
		}

		/* pre-sleep */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-pre-on-sleep", &tmp);
		if (rc) {
			pr_debug("%s: error reading supply pre sleep value. rc=%d\n",
				__func__, rc);
			rc = 0;
		} else {
			mp->vreg_config[i].pre_on_sleep = tmp;
		}

		rc = of_property_read_u32(supply_node,
			"qcom,supply-pre-off-sleep", &tmp);
		if (rc) {
			pr_debug("%s: error reading supply pre sleep value. rc=%d\n",
				__func__, rc);
			rc = 0;
		} else {
			mp->vreg_config[i].pre_off_sleep = tmp;
		}

		/* post-sleep */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-post-on-sleep", &tmp);
		if (rc) {
			pr_debug("%s: error reading supply post sleep value. rc=%d\n",
				__func__, rc);
			rc = 0;
		} else {
			mp->vreg_config[i].post_on_sleep = tmp;
		}

		rc = of_property_read_u32(supply_node,
			"qcom,supply-post-off-sleep", &tmp);
		if (rc) {
			pr_debug("%s: error reading supply post sleep value. rc=%d\n",
				__func__, rc);
			rc = 0;
		} else {
			mp->vreg_config[i].post_off_sleep = tmp;
		}

		pr_debug("%s: %s min=%d, max=%d, enable=%d, disable=%d, preonsleep=%d, postonsleep=%d, preoffsleep=%d, postoffsleep=%d\n",
			__func__,
			mp->vreg_config[i].vreg_name,
			mp->vreg_config[i].min_voltage,
			mp->vreg_config[i].max_voltage,
			mp->vreg_config[i].load[DSS_REG_MODE_ENABLE],
			mp->vreg_config[i].load[DSS_REG_MODE_DISABLE],
			mp->vreg_config[i].pre_on_sleep,
			mp->vreg_config[i].post_on_sleep,
			mp->vreg_config[i].pre_off_sleep,
			mp->vreg_config[i].post_off_sleep
			);
		++i;
	}

	return rc;

error:
	if (mp->vreg_config) {
		devm_kfree(dev, mp->vreg_config);
		mp->vreg_config = NULL;
	}
novreg:
	mp->num_vreg = 0;

	return rc;
}

static int mdss_dsi_get_panel_cfg(char *panel_cfg)
{
	int rc;
	struct mdss_panel_cfg *pan_cfg = NULL;

	if (!panel_cfg)
		return MDSS_PANEL_INTF_INVALID;

	pan_cfg = mdss_panel_intf_type(MDSS_PANEL_INTF_DSI);
	if (IS_ERR(pan_cfg)) {
		return PTR_ERR(pan_cfg);
	} else if (!pan_cfg) {
		panel_cfg[0] = 0;
		return 0;
	}

	pr_debug("%s:%d: cfg:[%s]\n", __func__, __LINE__,
		 pan_cfg->arg_cfg);
	rc = strlcpy(panel_cfg, pan_cfg->arg_cfg,
		     sizeof(pan_cfg->arg_cfg));
	return rc;
}

static int mdss_dsi_off(struct mdss_panel_data *pdata, int power_state)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *panel_info = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	panel_info = &ctrl_pdata->panel_data.panel_info;

	pr_debug("%s+: ctrl=%p ndx=%d power_state=%d\n",
		__func__, ctrl_pdata, ctrl_pdata->ndx, power_state);

	if (power_state == panel_info->panel_power_state) {
		pr_debug("%s: No change in power state %d -> %d\n", __func__,
			panel_info->panel_power_state, power_state);
		goto end;
	}

	if (mdss_panel_is_power_on(power_state)) {
		pr_debug("%s: dsi_off with panel always on\n", __func__);
		goto panel_power_ctrl;
	}

	if (pdata->panel_info.type == MIPI_CMD_PANEL)
		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);

	/* disable DSI controller */
	mdss_dsi_controller_cfg(0, pdata);

	/* disable DSI phy */
	mdss_dsi_phy_disable(ctrl_pdata);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);

panel_power_ctrl:
	ret = mdss_dsi_panel_power_ctrl(pdata, power_state);
	if (ret) {
		pr_err("%s: Panel power off failed\n", __func__);
		goto end;
	}

	if (panel_info->dynamic_fps
	    && (panel_info->dfps_update == DFPS_SUSPEND_RESUME_MODE)
	    && (panel_info->new_fps != panel_info->mipi.frame_rate))
		panel_info->mipi.frame_rate = panel_info->new_fps;

end:
	pr_debug("%s-:\n", __func__);

	return ret;
}

static int mdss_dsi_update_panel_config(struct mdss_dsi_ctrl_pdata *ctrl_pdata,
				int mode)
{
	int ret = 0;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);

	if (mode == DSI_CMD_MODE) {
		pinfo->mipi.mode = DSI_CMD_MODE;
		pinfo->type = MIPI_CMD_PANEL;
		pinfo->mipi.vsync_enable = 1;
		pinfo->mipi.hw_vsync_mode = 1;
	} else {	/*video mode*/
		pinfo->mipi.mode = DSI_VIDEO_MODE;
		pinfo->type = MIPI_VIDEO_PANEL;
		pinfo->mipi.vsync_enable = 0;
		pinfo->mipi.hw_vsync_mode = 0;
	}

	ctrl_pdata->panel_mode = pinfo->mipi.mode;
	mdss_panel_get_dst_fmt(pinfo->bpp, pinfo->mipi.mode,
			pinfo->mipi.pixel_packing, &(pinfo->mipi.dst_format));
	pinfo->cont_splash_enabled = 0;

	return ret;
}

int mdss_dsi_on(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_panel_info *pinfo;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int cur_power_state;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	cur_power_state = pdata->panel_info.panel_power_state;
	pr_debug("%s+: ctrl=%p ndx=%d cur_power_state=%d\n", __func__,
		ctrl_pdata, ctrl_pdata->ndx, cur_power_state);

	pinfo = &pdata->panel_info;
	mipi = &pdata->panel_info.mipi;

	if (mdss_dsi_is_panel_on_interactive(pdata)) {
		pr_debug("%s: panel already on\n", __func__);
		goto end;
	}

	ret = mdss_dsi_panel_power_ctrl(pdata, MDSS_PANEL_POWER_ON);
	if (ret) {
		pr_err("%s:Panel power on failed. rc=%d\n", __func__, ret);
		return ret;
	}

	if (cur_power_state != MDSS_PANEL_POWER_OFF) {
		pr_debug("%s: dsi_on from panel low power state\n", __func__);
		goto end;
	}

	/*
	 * Enable DSI clocks.
	 * This is also enable the DSI core power block and reset/setup
	 * DSI phy
	 */
	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);
	mdss_dsi_sw_reset(ctrl_pdata, true);

	/*
	 * Issue hardware reset line after enabling the DSI clocks and data
	 * data lanes for LP11 init
	 */
	if (mipi->lp11_init) {
		if (mdss_dsi_pinctrl_set_state(ctrl_pdata, true))
			pr_debug("reset enable: pinctrl not enabled\n");
		mdss_dsi_panel_reset(pdata, 1);
	}

	if (mipi->init_delay)
		usleep(mipi->init_delay);

	if (mipi->force_clk_lane_hs) {
		u32 tmp;

		tmp = MIPI_INP((ctrl_pdata->ctrl_base) + 0xac);
		tmp |= (1<<28);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0xac, tmp);
		wmb();
	}

	if (pdata->panel_info.type == MIPI_CMD_PANEL)
		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);

end:
	pr_debug("%s-:\n", __func__);
	return 0;
}

static int mdss_dsi_pinctrl_set_state(
	struct mdss_dsi_ctrl_pdata *ctrl_pdata,	
		bool active)
{
	struct pinctrl_state *pin_state;
	int rc = -EFAULT;

	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.pinctrl))
		return PTR_ERR(ctrl_pdata->pin_res.pinctrl);

	pin_state = active ? ctrl_pdata->pin_res.gpio_state_active
			: ctrl_pdata->pin_res.gpio_state_suspend;
	if (!IS_ERR_OR_NULL(pin_state)) {
		rc = pinctrl_select_state(ctrl_pdata->pin_res.pinctrl,
				pin_state);
		if (rc)
			pr_err("%s: can not set %s pins\n", __func__,
			 	active ? MDSS_PINCTRL_STATE_DEFAULT
			 	: MDSS_PINCTRL_STATE_SLEEP);
	} else {
	pr_err("%s: invalid '%s' pinstate\n", __func__,
		active ? MDSS_PINCTRL_STATE_DEFAULT
		: MDSS_PINCTRL_STATE_SLEEP);
}
return rc;
}

static int mdss_dsi_pinctrl_init(struct platform_device *pdev)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata;	

	ctrl_pdata = platform_get_drvdata(pdev);
	ctrl_pdata->pin_res.pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.pinctrl)) {
		pr_err("%s: failed to get pinctrl\n", __func__);
		return PTR_ERR(ctrl_pdata->pin_res.pinctrl);
	}

	ctrl_pdata->pin_res.gpio_state_active
		= pinctrl_lookup_state(ctrl_pdata->pin_res.pinctrl,
				MDSS_PINCTRL_STATE_DEFAULT);
	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.gpio_state_active))
		pr_warn("%s: can not get default pinstate\n", __func__);

	ctrl_pdata->pin_res.gpio_state_suspend
		= pinctrl_lookup_state(ctrl_pdata->pin_res.pinctrl,
			MDSS_PINCTRL_STATE_SLEEP);
	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.gpio_state_suspend))
		pr_warn("%s: can not get sleep pinstate\n", __func__);

	return 0;
}


static int mdss_dsi_unblank(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	mipi  = &pdata->panel_info.mipi;

	pr_debug("%s+: ctrl=%p ndx=%d cur_blank_state=%d\n", __func__,
		ctrl_pdata, ctrl_pdata->ndx, pdata->panel_info.blank_state);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);

	if (pdata->panel_info.blank_state == MDSS_PANEL_BLANK_LOW_POWER) {
		pr_debug("%s: dsi_unblank with panel always on\n", __func__);
		if (ctrl_pdata->low_power_config)
			ret = ctrl_pdata->low_power_config(pdata, false);
		goto error;
	}

	if (!(ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT)) {
		if (!pdata->panel_info.dynamic_switch_pending) {
			ret = ctrl_pdata->on(pdata);
			if (ret) {
				pr_err("%s: unable to initialize the panel\n",
							__func__);
				goto error;
			}
		}
		ctrl_pdata->ctrl_state |= CTRL_STATE_PANEL_INIT;
	}

	if ((pdata->panel_info.type == MIPI_CMD_PANEL) &&
		mipi->vsync_enable && mipi->hw_vsync_mode)
		mdss_dsi_set_tear_on(ctrl_pdata);

	mdss_livedisplay_update(pdata->panel_info.livedisplay,
			MODE_UPDATE_ALL);

error:
	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);
	pr_debug("%s-:\n", __func__);

	return ret;
}

static int mdss_dsi_blank(struct mdss_panel_data *pdata, int power_state)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	mipi = &pdata->panel_info.mipi;

	pr_debug("%s+: ctrl=%p ndx=%d power_state=%d\n",
		__func__, ctrl_pdata, ctrl_pdata->ndx, power_state);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);

	if (mdss_panel_is_power_on_lp(power_state)) {
		pr_debug("%s: low power state requested\n", __func__);
		if (ctrl_pdata->low_power_config)
			ret = ctrl_pdata->low_power_config(pdata, true);
		goto error;
	}

	if (pdata->panel_info.type == MIPI_VIDEO_PANEL &&
			ctrl_pdata->off_cmds.link_state == DSI_LP_MODE) {
		mdss_dsi_sw_reset(ctrl_pdata, false);
		mdss_dsi_host_init(pdata);
	}

	mdss_dsi_op_mode_config(DSI_CMD_MODE, pdata);

	if (pdata->panel_info.dynamic_switch_pending) {
		pr_info("%s: switching to %s mode\n", __func__,
			(pdata->panel_info.mipi.mode ? "video" : "command"));
		if (pdata->panel_info.type == MIPI_CMD_PANEL) {
			ctrl_pdata->switch_mode(pdata, DSI_VIDEO_MODE);
		} else if (pdata->panel_info.type == MIPI_VIDEO_PANEL) {
			ctrl_pdata->switch_mode(pdata, DSI_CMD_MODE);
			mdss_dsi_set_tear_off(ctrl_pdata);
		}
	}

	if ((pdata->panel_info.type == MIPI_CMD_PANEL) &&
		mipi->vsync_enable && mipi->hw_vsync_mode)
		mdss_dsi_set_tear_off(ctrl_pdata);

	if (ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT) {
		if (!pdata->panel_info.dynamic_switch_pending) {
			ret = ctrl_pdata->off(pdata);
			if (ret) {
				pr_err("%s: Panel OFF failed\n", __func__);
				goto error;
			}
		}
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_PANEL_INIT;
	}
error:
	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);

	pr_debug("%s-:End\n", __func__);
	return ret;
}

int mdss_dsi_cont_splash_on(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	pr_info("%s:%d DSI on for continuous splash.\n", __func__, __LINE__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	mipi = &pdata->panel_info.mipi;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s+: ctrl=%p ndx=%d\n", __func__,
				ctrl_pdata, ctrl_pdata->ndx);

	WARN((ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT),
		"Incorrect Ctrl state=0x%x\n", ctrl_pdata->ctrl_state);

	mdss_dsi_ctrl_setup(ctrl_pdata);
	mdss_dsi_sw_reset(ctrl_pdata, true);
	pr_debug("%s-:End\n", __func__);
	return ret;
}

static int mdss_dsi_dfps_config(struct mdss_panel_data *pdata, int new_fps)
{
	int rc = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	u32 dsi_ctrl;

	pr_debug("%s+:\n", __func__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (!ctrl_pdata->panel_data.panel_info.dynamic_fps) {
		pr_err("%s: Dynamic fps not enabled for this panel\n",
					__func__);
		return -EINVAL;
	}

	if (new_fps !=
		ctrl_pdata->panel_data.panel_info.mipi.frame_rate) {
		if (pdata->panel_info.dfps_update
			== DFPS_IMMEDIATE_PORCH_UPDATE_MODE) {
			u32 hsync_period, vsync_period;
			u32 new_dsi_v_total, current_dsi_v_total;
			vsync_period =
				mdss_panel_get_vtotal(&pdata->panel_info);
			hsync_period =
				mdss_panel_get_htotal(&pdata->panel_info, true);
			current_dsi_v_total =
				MIPI_INP((ctrl_pdata->ctrl_base) + 0x2C);
			new_dsi_v_total =
				((vsync_period - 1) << 16) | (hsync_period - 1);
			MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x2C,
				(current_dsi_v_total | 0x8000000));
			if (new_dsi_v_total & 0x8000000) {
				MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x2C,
					new_dsi_v_total);
			} else {
				MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x2C,
					(new_dsi_v_total | 0x8000000));
				MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x2C,
					(new_dsi_v_total & 0x7ffffff));
			}
			pdata->panel_info.mipi.frame_rate = new_fps;
		} else {
			rc = mdss_dsi_clk_div_config
				(&ctrl_pdata->panel_data.panel_info, new_fps);
			if (rc) {
				pr_err("%s: unable to initialize the clk dividers\n",
								__func__);
				return rc;
			}
			ctrl_pdata->pclk_rate =
				pdata->panel_info.mipi.dsi_pclk_rate;
			ctrl_pdata->byte_clk_rate =
				pdata->panel_info.clk_rate / 8;

			if (pdata->panel_info.dfps_update
					== DFPS_IMMEDIATE_CLK_UPDATE_MODE) {
				dsi_ctrl = MIPI_INP((ctrl_pdata->ctrl_base) +
						    0x0004);
				pdata->panel_info.mipi.frame_rate = new_fps;
				dsi_ctrl &= ~0x2;
				MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x0004,
								dsi_ctrl);
				mdss_dsi_controller_cfg(true, pdata);
				mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);
				mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);
				dsi_ctrl |= 0x2;
				MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x0004,
								dsi_ctrl);
			}
		}
	} else {
		pr_debug("%s: Panel is already at this FPS\n", __func__);
	}

	return rc;
}

static int mdss_dsi_ctl_partial_update(struct mdss_panel_data *pdata)
{
	int rc = -EINVAL;
	u32 data;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	/* DSI_COMMAND_MODE_MDP_STREAM_CTRL */
	data = (((pdata->panel_info.roi_w * 3) + 1) << 16) |
			(pdata->panel_info.mipi.vc << 8) | DTYPE_DCS_LWRITE;
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x60, data);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x58, data);

	/* DSI_COMMAND_MODE_MDP_STREAM_TOTAL */
	data = pdata->panel_info.roi_h << 16 | pdata->panel_info.roi_w;
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x64, data);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x5C, data);

	if (ctrl_pdata->partial_update_fnc)
		rc = ctrl_pdata->partial_update_fnc(pdata);

	if (rc) {
		pr_err("%s: unable to initialize the panel\n",
				__func__);
		return rc;
	}

	return rc;
}

int mdss_dsi_register_recovery_handler(struct mdss_dsi_ctrl_pdata *ctrl,
	struct mdss_panel_recovery *recovery)
{
	mutex_lock(&ctrl->mutex);
	ctrl->recovery = recovery;
	mutex_unlock(&ctrl->mutex);
	return 0;
}

static int mdss_dsi_event_handler(struct mdss_panel_data *pdata,
				  int event, void *arg)
{
	int rc = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int power_state;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}
	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	pr_debug("%s+:event=%d\n", __func__, event);

	MDSS_XLOG(event, arg, ctrl_pdata->ndx, 0x3333);

	switch (event) {
	case MDSS_EVENT_UNBLANK:
		lcd_notifier_call_chain(LCD_EVENT_ON_START, NULL);
		rc = mdss_dsi_on(pdata);
		mdss_dsi_op_mode_config(pdata->panel_info.mipi.mode,
							pdata);
		if (ctrl_pdata->on_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_unblank(pdata);
		break;
	case MDSS_EVENT_PANEL_ON:
		ctrl_pdata->ctrl_state |= CTRL_STATE_MDP_ACTIVE;
		if (ctrl_pdata->on_cmds.link_state == DSI_HS_MODE)
			rc = mdss_dsi_unblank(pdata);
		lcd_notifier_call_chain(LCD_EVENT_ON_END, NULL);
		break;
	case MDSS_EVENT_BLANK:
		power_state = (int) (unsigned long) arg;
		lcd_notifier_call_chain(LCD_EVENT_OFF_START, NULL);
		if (ctrl_pdata->off_cmds.link_state == DSI_HS_MODE)
			rc = mdss_dsi_blank(pdata, power_state);
		break;
	case MDSS_EVENT_PANEL_OFF:
		power_state = (int) (unsigned long) arg;
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_MDP_ACTIVE;
		if (ctrl_pdata->off_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_blank(pdata, power_state);
		rc = mdss_dsi_off(pdata, power_state);
		lcd_notifier_call_chain(LCD_EVENT_OFF_END, NULL);
		break;
	case MDSS_EVENT_CONT_SPLASH_FINISH:
		if (ctrl_pdata->off_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_blank(pdata, MDSS_PANEL_POWER_OFF);
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_MDP_ACTIVE;
		rc = mdss_dsi_cont_splash_on(pdata);
		break;
	case MDSS_EVENT_PANEL_CLK_CTRL:
		mdss_dsi_clk_req(ctrl_pdata, (int)arg);
		break;
	case MDSS_EVENT_DSI_CMDLIST_KOFF:
		mdss_dsi_cmdlist_commit(ctrl_pdata, 1);
		break;
	case MDSS_EVENT_PANEL_UPDATE_FPS:
		if (arg != NULL) {
			rc = mdss_dsi_dfps_config(pdata, (int)arg);
			pr_debug("%s:update fps to = %d\n",
				__func__, (int)arg);
		}
		break;
	case MDSS_EVENT_CONT_SPLASH_BEGIN:
		if (ctrl_pdata->off_cmds.link_state == DSI_HS_MODE) {
			/* Panel is Enabled in Bootloader */
			rc = mdss_dsi_blank(pdata, MDSS_PANEL_POWER_OFF);
		}
		break;
	case MDSS_EVENT_ENABLE_PARTIAL_UPDATE:
		rc = mdss_dsi_ctl_partial_update(pdata);
		break;
	case MDSS_EVENT_REGISTER_RECOVERY_HANDLER:
		rc = mdss_dsi_register_recovery_handler(ctrl_pdata,
			(struct mdss_panel_recovery *)arg);
		break;
	case MDSS_EVENT_DSI_DYNAMIC_SWITCH:
		rc = mdss_dsi_update_panel_config(ctrl_pdata,
					(int)(unsigned long) arg);
		break;
	default:
		pr_debug("%s: unhandled event=%d\n", __func__, event);
		break;
	}
	pr_debug("%s-:event=%d, rc=%d\n", __func__, event, rc);
	return rc;
}

static struct device_node *mdss_dsi_pref_prim_panel(
		struct platform_device *pdev)
{
	struct device_node *dsi_pan_node = NULL;

	pr_debug("%s:%d: Select primary panel from dt\n",
					__func__, __LINE__);
	dsi_pan_node = of_parse_phandle(pdev->dev.of_node,
					"qcom,dsi-pref-prim-pan", 0);
	if (!dsi_pan_node)
		pr_err("%s:can't find panel phandle\n", __func__);

	return dsi_pan_node;
}

/**
 * mdss_dsi_find_panel_of_node(): find device node of dsi panel
 * @pdev: platform_device of the dsi ctrl node
 * @panel_cfg: string containing intf specific config data
 *
 * Function finds the panel device node using the interface
 * specific configuration data. This configuration data is
 * could be derived from the result of bootloader's GCDB
 * panel detection mechanism. If such config data doesn't
 * exist then this panel returns the default panel configured
 * in the device tree.
 *
 * returns pointer to panel node on success, NULL on error.
 */
static struct device_node *mdss_dsi_find_panel_of_node(
		struct platform_device *pdev, char *panel_cfg)
{
	int len, i;
	int ctrl_id = pdev->id - 1;
	char panel_name[MDSS_MAX_PANEL_LEN];
	char ctrl_id_stream[3] =  "0:";
	char *stream = NULL, *pan = NULL;
	struct device_node *dsi_pan_node = NULL, *mdss_node = NULL;

	len = strlen(panel_cfg);
	if (!len) {
		/* no panel cfg chg, parse dt */
		pr_debug("%s:%d: no cmd line cfg present\n",
			 __func__, __LINE__);
		goto end;
	} else {
		if (ctrl_id == 1)
			strlcpy(ctrl_id_stream, "1:", 3);

		stream = strnstr(panel_cfg, ctrl_id_stream, len);
		if (!stream) {
			pr_err("controller config is not present\n");
			goto end;
		}
		stream += 2;

		pan = strnchr(stream, strlen(stream), ':');
		if (!pan) {
			strlcpy(panel_name, stream, MDSS_MAX_PANEL_LEN);
		} else {
			for (i = 0; (stream + i) < pan; i++)
				panel_name[i] = *(stream + i);
			panel_name[i] = 0;
		}

		pr_debug("%s:%d:%s:%s\n", __func__, __LINE__,
			 panel_cfg, panel_name);

		mdss_node = of_parse_phandle(pdev->dev.of_node,
					     "qcom,mdss-mdp", 0);

		if (!mdss_node) {
			pr_err("%s: %d: mdss_node null\n",
			       __func__, __LINE__);
			return NULL;
		}
		dsi_pan_node = of_find_node_by_name(mdss_node,
						    panel_name);
		if (!dsi_pan_node) {
			pr_err("%s: invalid pan node, selecting prim panel\n",
			       __func__);
			goto end;
		}
		return dsi_pan_node;
	}
end:
	dsi_pan_node = mdss_dsi_pref_prim_panel(pdev);

	return dsi_pan_node;
}

static int __devinit mdss_dsi_ctrl_probe(struct platform_device *pdev)
{
	int rc = 0, i = 0;
	u32 index;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct device_node *dsi_pan_node = NULL;
	char panel_cfg[MDSS_MAX_PANEL_LEN];
	const char *ctrl_name;
	bool cmd_cfg_cont_splash = true;

	if (!mdss_is_ready()) {
		pr_err("%s: MDP not probed yet!\n", __func__);
		return -EPROBE_DEFER;
	}

	if (!pdev->dev.of_node) {
		pr_err("DSI driver only supports device tree probe\n");
		return -ENOTSUPP;
	}

	ctrl_pdata = platform_get_drvdata(pdev);
	if (!ctrl_pdata) {
		ctrl_pdata = devm_kzalloc(&pdev->dev,
					  sizeof(struct mdss_dsi_ctrl_pdata),
					  GFP_KERNEL);
		if (!ctrl_pdata) {
			pr_err("%s: FAILED: cannot alloc dsi ctrl\n",
			       __func__);
			rc = -ENOMEM;
			goto error_no_mem;
		}
		platform_set_drvdata(pdev, ctrl_pdata);
	}

	ctrl_name = of_get_property(pdev->dev.of_node, "label", NULL);
	if (!ctrl_name)
		pr_info("%s:%d, DSI Ctrl name not specified\n",
			__func__, __LINE__);
	else
		pr_info("%s: DSI Ctrl name = %s\n",
			__func__, ctrl_name);

	rc = of_property_read_u32(pdev->dev.of_node,
				  "cell-index", &index);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: Cell-index not specified, rc=%d\n",
			__func__, rc);
		goto error_no_mem;
	}

	if (index == 0)
		pdev->id = 1;
	else
		pdev->id = 2;

	rc = of_platform_populate(pdev->dev.of_node,
				  NULL, NULL, &pdev->dev);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: failed to add child nodes, rc=%d\n",
			__func__, rc);
		goto error_no_mem;
	}

	rc = mdss_dsi_pinctrl_init(pdev);
	if (rc)
		pr_warn("%s: failed to get pin resources\n", __func__); 

	/* Parse the regulator information */
	for (i = 0; i < DSI_MAX_PM; i++) {
		rc = mdss_dsi_get_dt_vreg_data(&pdev->dev,
			&ctrl_pdata->power_data[i], i);
		if (rc) {
			DEV_ERR("%s: '%s' get_dt_vreg_data failed.rc=%d\n",
				__func__, __mdss_dsi_pm_name(i), rc);
			goto error_vreg;
		}
	}

	/* DSI panels can be different between controllers */
	rc = mdss_dsi_get_panel_cfg(panel_cfg);
	if (!rc)
		/* dsi panel cfg not present */
		pr_warn("%s:%d:dsi specific cfg not present\n",
			__func__, __LINE__);

	/* find panel device node */
	dsi_pan_node = mdss_dsi_find_panel_of_node(pdev, panel_cfg);
	if (!dsi_pan_node) {
		pr_err("%s: can't find panel node %s\n", __func__, panel_cfg);
		goto error_pan_node;
	}

	cmd_cfg_cont_splash = mdss_panel_get_boot_cfg() ? true : false;

	rc = mdss_dsi_panel_init(dsi_pan_node, ctrl_pdata, cmd_cfg_cont_splash);
	if (rc) {
		pr_err("%s: dsi panel init failed\n", __func__);
		goto error_pan_node;
	}

	rc = dsi_panel_device_register(dsi_pan_node, ctrl_pdata);
	if (rc) {
		pr_err("%s: dsi panel dev reg failed\n", __func__);
		goto error_pan_node;
	}

	pr_debug("%s: Dsi Ctrl->%d initialized\n", __func__, index);
	return 0;

error_pan_node:
	of_node_put(dsi_pan_node);
error_vreg:
	for (; i >= 0; i--)
		mdss_dsi_put_dt_vreg_data(&pdev->dev,
			&ctrl_pdata->power_data[i]);
error_no_mem:
	devm_kfree(&pdev->dev, ctrl_pdata);

	return rc;
}

static int __devexit mdss_dsi_ctrl_remove(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = platform_get_drvdata(pdev);
	int i = 0;

	if (!ctrl_pdata) {
		pr_err("%s: no driver data\n", __func__);
		return -ENODEV;
	}

	for (i = DSI_MAX_PM - 1; i >= 0; i--) {
		if (msm_dss_config_vreg(&pdev->dev,
				ctrl_pdata->power_data[i].vreg_config,
				ctrl_pdata->power_data[i].num_vreg, 1) < 0)
			pr_err("%s: failed to de-init vregs for %s\n",
				__func__, __mdss_dsi_pm_name(i));
		mdss_dsi_put_dt_vreg_data(&pdev->dev,
			&ctrl_pdata->power_data[i]);
	}

	mfd = platform_get_drvdata(pdev);
	msm_dss_iounmap(&ctrl_pdata->mmss_misc_io);
	msm_dss_iounmap(&ctrl_pdata->phy_io);
	msm_dss_iounmap(&ctrl_pdata->ctrl_io);
	return 0;
}

struct device dsi_dev;

int mdss_dsi_retrieve_ctrl_resources(struct platform_device *pdev, int mode,
			struct mdss_dsi_ctrl_pdata *ctrl)
{
	int rc = 0;
	u32 index;

	rc = of_property_read_u32(pdev->dev.of_node, "cell-index", &index);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: Cell-index not specified, rc=%d\n",
						__func__, rc);
		return rc;
	}

	if (index == 0) {
		if (mode != DISPLAY_1) {
			pr_err("%s:%d Panel->Ctrl mapping is wrong",
				       __func__, __LINE__);
			return -EPERM;
		}
	} else if (index == 1) {
		if (mode != DISPLAY_2) {
			pr_err("%s:%d Panel->Ctrl mapping is wrong",
				       __func__, __LINE__);
			return -EPERM;
		}
	} else {
		pr_err("%s:%d Unknown Ctrl mapped to panel",
			       __func__, __LINE__);
		return -EPERM;
	}

	rc = msm_dss_ioremap_byname(pdev, &ctrl->ctrl_io, "dsi_ctrl");
	if (rc) {
		pr_err("%s:%d unable to remap dsi ctrl resources",
			       __func__, __LINE__);
		return rc;
	}

	ctrl->ctrl_base = ctrl->ctrl_io.base;
	ctrl->reg_size = ctrl->ctrl_io.len;

	rc = msm_dss_ioremap_byname(pdev, &ctrl->phy_io, "dsi_phy");
	if (rc) {
		pr_err("%s:%d unable to remap dsi phy resources",
			       __func__, __LINE__);
		return rc;
	}

	pr_info("%s: ctrl_base=%p ctrl_size=%x phy_base=%p phy_size=%x\n",
		__func__, ctrl->ctrl_base, ctrl->reg_size, ctrl->phy_io.base,
		ctrl->phy_io.len);

	rc = msm_dss_ioremap_byname(pdev, &ctrl->mmss_misc_io,
		"mmss_misc_phys");
	if (rc) {
		pr_debug("%s:%d mmss_misc IO remap failed\n",
			__func__, __LINE__);
	}

	return 0;
}

int dsi_panel_device_register(struct device_node *pan_node,
				struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mipi_panel_info *mipi;
	int rc, i, len;
	struct device_node *dsi_ctrl_np = NULL;
	struct platform_device *ctrl_pdev = NULL;
	const char *data;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);

	mipi  = &(pinfo->mipi);

	pinfo->type =
		((mipi->mode == DSI_VIDEO_MODE)
			? MIPI_VIDEO_PANEL : MIPI_CMD_PANEL);

	rc = mdss_dsi_clk_div_config(pinfo, mipi->frame_rate);
	if (rc) {
		pr_err("%s: unable to initialize the clk dividers\n", __func__);
		return rc;
	}

	dsi_ctrl_np = of_parse_phandle(pan_node,
				"qcom,mdss-dsi-panel-controller", 0);
	if (!dsi_ctrl_np) {
		pr_err("%s: Dsi controller node not initialized\n", __func__);
		return -EPROBE_DEFER;
	}

	ctrl_pdev = of_find_device_by_node(dsi_ctrl_np);

	rc = mdss_dsi_regulator_init(ctrl_pdev);
	if (rc) {
		pr_err("%s: failed to init regulator, rc=%d\n",
						__func__, rc);
		return rc;
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-strength-ctrl", &len);
	if ((!data) || (len != 2)) {
		pr_err("%s:%d, Unable to read Phy Strength ctrl settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	pinfo->mipi.dsi_phy_db.strength[0] = data[0];
	pinfo->mipi.dsi_phy_db.strength[1] = data[1];

	pinfo->mipi.dsi_phy_db.reg_ldo_mode = of_property_read_bool(
		ctrl_pdev->dev.of_node, "qcom,regulator-ldo-mode");

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-regulator-settings", &len);
	if ((!data) || (len != 7)) {
		pr_err("%s:%d, Unable to read Phy regulator settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		pinfo->mipi.dsi_phy_db.regulator[i]
			= data[i];
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-bist-ctrl", &len);
	if ((!data) || (len != 6)) {
		pr_err("%s:%d, Unable to read Phy Bist Ctrl settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		pinfo->mipi.dsi_phy_db.bistctrl[i]
			= data[i];
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-lane-config", &len);
	if ((!data) || (len != 45)) {
		pr_err("%s:%d, Unable to read Phy lane configure settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		pinfo->mipi.dsi_phy_db.lanecfg[i] =
			data[i];
	}

	rc = of_property_read_u32(ctrl_pdev->dev.of_node,
			"qcom,mmss-ulp-clamp-ctrl-offset",
			&ctrl_pdata->ulps_clamp_ctrl_off);
	if (!rc) {
		rc = of_property_read_u32(ctrl_pdev->dev.of_node,
				"qcom,mmss-phyreset-ctrl-offset",
				&ctrl_pdata->ulps_phyrst_ctrl_off);
	}
	if (rc && pinfo->ulps_feature_enabled) {
		pr_err("%s: dsi ulps clamp register settings missing\n",
				__func__);
		return -EINVAL;
	}

	ctrl_pdata->shared_pdata.broadcast_enable = of_property_read_bool(
		pan_node, "qcom,mdss-dsi-panel-broadcast-mode");

	pinfo->panel_max_fps = mdss_panel_get_framerate(pinfo);
	pinfo->panel_max_vtotal = mdss_panel_get_vtotal(pinfo);
	ctrl_pdata->disp_en_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
		"qcom,platform-enable-gpio", 0);

	if (!gpio_is_valid(ctrl_pdata->disp_en_gpio))
		pr_err("%s:%d, Disp_en gpio not specified\n",
						__func__, __LINE__);

	ctrl_pdata->rst_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
			 "qcom,platform-reset-gpio", 0);
	if (!gpio_is_valid(ctrl_pdata->rst_gpio))
		pr_err("%s:%d, reset gpio not specified\n",
						__func__, __LINE__);

	if (pinfo->mode_gpio_state != MODE_GPIO_NOT_VALID) {

		ctrl_pdata->mode_gpio = of_get_named_gpio(
					ctrl_pdev->dev.of_node,
					"qcom,platform-mode-gpio", 0);
		if (!gpio_is_valid(ctrl_pdata->mode_gpio))
			pr_info("%s:%d, mode gpio not specified\n",
							__func__, __LINE__);
	} else {
		ctrl_pdata->mode_gpio = -EINVAL;
	}

#ifdef CONFIG_MACH_OPPO
	ctrl_pdata->lcd_5v_en_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
			  "qcom,platform-lcd-5v-en-gpio", 0);
	if (gpio_is_valid(ctrl_pdata->lcd_5v_en_gpio)) {
		rc = gpio_request(ctrl_pdata->lcd_5v_en_gpio, "lcd_5v_en");
		if (rc) {
			pr_err("request LCD 5V EN gpio failed, rc=%d\n", rc);
			return -ENODEV;
		}
		rc = gpio_tlmm_config(GPIO_CFG(
					ctrl_pdata->lcd_5v_en_gpio, 0,
					GPIO_CFG_OUTPUT,
					GPIO_CFG_NO_PULL,
					GPIO_CFG_8MA),
				GPIO_CFG_DISABLE);
		if (rc) {
			pr_err("%s: unable to config tlmm = %d\n",
					__func__, ctrl_pdata->lcd_5v_en_gpio);
			gpio_free(ctrl_pdata->lcd_5v_en_gpio);
			return -ENODEV;
		}
	} else {
		pr_err("%s:%d, lcd 5v en gpio not specified\n",
						__func__, __LINE__);
	}
#endif

	if (mdss_dsi_clk_init(ctrl_pdev, ctrl_pdata)) {
		pr_err("%s: unable to initialize Dsi ctrl clks\n", __func__);
		return -EPERM;
	}

	if (mdss_dsi_retrieve_ctrl_resources(ctrl_pdev,
					     pinfo->pdest,
					     ctrl_pdata)) {
		pr_err("%s: unable to get Dsi controller res\n", __func__);
		return -EPERM;
	}

	ctrl_pdata->panel_data.event_handler = mdss_dsi_event_handler;

	if (ctrl_pdata->status_mode == ESD_REG)
		ctrl_pdata->check_status = mdss_dsi_reg_status_check;
	else if (ctrl_pdata->status_mode == ESD_BTA)
		ctrl_pdata->check_status = mdss_dsi_bta_status_check;

	if (ctrl_pdata->status_mode == ESD_MAX) {
		pr_err("%s: Using default BTA for ESD check\n", __func__);
		ctrl_pdata->check_status = mdss_dsi_bta_status_check;
	}
	if (ctrl_pdata->bklt_ctrl == BL_PWM)
		mdss_dsi_panel_pwm_cfg(ctrl_pdata);

	mdss_dsi_ctrl_init(&ctrl_pdev->dev, ctrl_pdata);
	/*
	 * register in mdp driver
	 */

	ctrl_pdata->pclk_rate = mipi->dsi_pclk_rate;
	ctrl_pdata->byte_clk_rate = pinfo->clk_rate / 8;
	pr_debug("%s: pclk=%d, bclk=%d\n", __func__,
			ctrl_pdata->pclk_rate, ctrl_pdata->byte_clk_rate);

	ctrl_pdata->ctrl_state = CTRL_STATE_UNKNOWN;

	if (pinfo->cont_splash_enabled) {
		rc = mdss_dsi_panel_power_ctrl(&(ctrl_pdata->panel_data),
			MDSS_PANEL_POWER_ON);
		if (rc) {
			pr_err("%s: Panel power on failed\n", __func__);
			return rc;
		}

		pinfo->blank_state = MDSS_PANEL_BLANK_UNBLANK;
		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);
		ctrl_pdata->ctrl_state |=
			(CTRL_STATE_PANEL_INIT | CTRL_STATE_MDP_ACTIVE);
	} else {
		pinfo->panel_power_state = MDSS_PANEL_POWER_OFF;
	}

	rc = mdss_register_panel(ctrl_pdev, &(ctrl_pdata->panel_data));
	if (rc) {
		pr_err("%s: unable to register MIPI DSI panel\n", __func__);
		return rc;
	}

	if (pinfo->pdest == DISPLAY_1) {
		mdss_debug_register_base("dsi0",
			ctrl_pdata->ctrl_base, ctrl_pdata->reg_size);
		ctrl_pdata->ndx = 0;
	} else {
		mdss_debug_register_base("dsi1",
			ctrl_pdata->ctrl_base, ctrl_pdata->reg_size);
		ctrl_pdata->ndx = 1;
	}

	pr_debug("%s: Panel data initialized\n", __func__);
	return 0;
}

static const struct of_device_id mdss_dsi_ctrl_dt_match[] = {
	{.compatible = "qcom,mdss-dsi-ctrl"},
	{}
};
MODULE_DEVICE_TABLE(of, mdss_dsi_ctrl_dt_match);

static struct platform_driver mdss_dsi_ctrl_driver = {
	.probe = mdss_dsi_ctrl_probe,
	.remove = __devexit_p(mdss_dsi_ctrl_remove),
	.shutdown = NULL,
	.driver = {
		.name = "mdss_dsi_ctrl",
		.of_match_table = mdss_dsi_ctrl_dt_match,
	},
};

static int mdss_dsi_register_driver(void)
{
	return platform_driver_register(&mdss_dsi_ctrl_driver);
}

static int __init mdss_dsi_driver_init(void)
{
	int ret;

	ret = mdss_dsi_register_driver();
	if (ret) {
		pr_err("mdss_dsi_register_driver() failed!\n");
		return ret;
	}

	return ret;
}
module_init(mdss_dsi_driver_init);

static void __exit mdss_dsi_driver_cleanup(void)
{
	platform_driver_unregister(&mdss_dsi_ctrl_driver);
}
module_exit(mdss_dsi_driver_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DSI controller driver");
MODULE_AUTHOR("Chandan Uddaraju <chandanu@codeaurora.org>");
