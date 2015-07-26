/* Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
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
#ifndef __QPNP_CHARGER_H__
#define __QPNP_CHARGER_H__

#include <linux/power_supply.h>

typedef enum {
	/*! Battery is cold               */
	CV_BATTERY_TEMP_REGION__COLD,
	/*! Battery is little cold        */
	CV_BATTERY_TEMP_REGION__LITTLE_COLD,
	/*! Battery is cool               */
	CV_BATTERY_TEMP_REGION__COOL,
	/*! Battery is little cool        */
	CV_BATTERY_TEMP_REGION__LITTLE_COOL,
	/*! Battery is normal             */
	CV_BATTERY_TEMP_REGION__NORMAL,
	/*! Battery is warm               */
	CV_BATTERY_TEMP_REGION__WARM,
	/*! Battery is hot                */
	CV_BATTERY_TEMP_REGION__HOT,
	/*! Invalid battery temp region   */
	CV_BATTERY_TEMP_REGION__INVALID,
} chg_cv_battery_temp_region_type;

struct qpnp_battery_gauge {
	int (*get_battery_mvolts) (void);
	int (*get_battery_temperature) (void);
	int (*is_battery_present) (void);
	int (*is_battery_temp_within_range) (void);
	int (*is_battery_id_valid) (void);
	int (*get_battery_status)(void);
	int (*get_batt_remaining_capacity) (void);
	int (*monitor_for_recharging) (void);
	int (*get_battery_soc) (void);
	int (*get_average_current) (void);
	int (*is_battery_authenticated) (void);
	int (*fast_chg_started) (void);
	int (*fast_switch_to_normal) (void);
	int (*set_switch_to_noraml_false) (void);
	int (*set_fast_chg_allow) (int enable);
	int (*get_fast_chg_allow) (void);
	int (*fast_normal_to_warm) (void);
	int (*set_normal_to_warm_false) (void);
	int (*get_fast_chg_ing) (void);
	int (*get_fast_low_temp_full) (void);
	int (*set_low_temp_full_false) (void);
};

struct qpnp_external_charger {
	int (*chg_vddmax_set) (int mV);
	int (*chg_vbatdet_set) (int mV);
	int (*chg_iusbmax_set) (int mA);
	int (*chg_ibatmax_set) (int mA);
	int (*chg_ibatterm_set) (int mA);
	int (*chg_vinmin_set)(int mV);
	int (*chg_charge_en) (int enable);
	int (*check_charge_timeout) (int hours);
	int (*chg_get_system_status) (void);
	int (*chg_get_charge_en) (void);
	int (*chg_usb_suspend_enable) (int enable);
	int (*chg_otg_current_set) (int mA);
	int (*chg_wdt_set) (int seconds);
	int (*chg_regs_reset) (int reset);
};

void qpnp_battery_gauge_register(struct qpnp_battery_gauge *batt_gauge);
void qpnp_battery_gauge_unregister(struct qpnp_battery_gauge *batt_gauge);

void qpnp_external_charger_register(struct qpnp_external_charger *external_charger);
void qpnp_external_charger_unregister(struct qpnp_external_charger *external_charger);


#endif /* __QPNP_CHARGER_H__ */
