/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/spmi.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/radix-tree.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/power_supply.h>
#include <linux/bitops.h>
#include <linux/ratelimit.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/of_batterydata.h>
#include <linux/qpnp-revid.h>
#include <linux/android_alarm.h>
#include <linux/spinlock.h>

#ifdef CONFIG_MACH_OPPO
#include <linux/boot_mode.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/notifier.h>
#include <linux/pcb_version.h>
#include <linux/qpnp-charger.h>
#endif

/* Interrupt offsets */
#define INT_RT_STS(base)			(base + 0x10)
#define INT_SET_TYPE(base)			(base + 0x11)
#define INT_POLARITY_HIGH(base)			(base + 0x12)
#define INT_POLARITY_LOW(base)			(base + 0x13)
#define INT_LATCHED_CLR(base)			(base + 0x14)
#define INT_EN_SET(base)			(base + 0x15)
#define INT_EN_CLR(base)			(base + 0x16)
#define INT_LATCHED_STS(base)			(base + 0x18)
#define INT_PENDING_STS(base)			(base + 0x19)
#define INT_MID_SEL(base)			(base + 0x1A)
#define INT_PRIORITY(base)			(base + 0x1B)

/* Peripheral register offsets */
#define CHGR_CHG_OPTION				0x08
#define CHGR_ATC_STATUS				0x0A
#define CHGR_VBAT_STATUS			0x0B
#define CHGR_IBAT_BMS				0x0C
#define CHGR_IBAT_STS				0x0D
#define CHGR_VDD_MAX				0x40
#define CHGR_VDD_SAFE				0x41
#define CHGR_VDD_MAX_STEP			0x42
#define CHGR_IBAT_MAX				0x44
#define CHGR_IBAT_SAFE				0x45
#define CHGR_VIN_MIN				0x47
#define CHGR_VIN_MIN_STEP			0x48
#define CHGR_CHG_CTRL				0x49
#define CHGR_CHG_FAILED				0x4A
#define CHGR_ATC_CTRL				0x4B
#define CHGR_ATC_FAILED				0x4C
#define CHGR_VBAT_TRKL				0x50
#define CHGR_VBAT_WEAK				0x52
#define CHGR_IBAT_ATC_A				0x54
#define CHGR_IBAT_ATC_B				0x55
#define CHGR_IBAT_TERM_CHGR			0x5B
#define CHGR_IBAT_TERM_BMS			0x5C
#define CHGR_VBAT_DET				0x5D
#define CHGR_TTRKL_MAX_EN			0x5E
#define CHGR_TTRKL_MAX				0x5F
#define CHGR_TCHG_MAX_EN			0x60
#define CHGR_TCHG_MAX				0x61
#define CHGR_CHG_WDOG_TIME			0x62
#define CHGR_CHG_WDOG_DLY			0x63
#define CHGR_CHG_WDOG_PET			0x64
#define CHGR_CHG_WDOG_EN			0x65
#define CHGR_IR_DROP_COMPEN			0x67
#define CHGR_I_MAX_REG			0x44
#define CHGR_USB_USB_SUSP			0x47
#define CHGR_USB_USB_OTG_CTL			0x48
#define CHGR_USB_ENUM_T_STOP			0x4E
#define CHGR_USB_TRIM				0xF1
#define CHGR_CHG_TEMP_THRESH			0x66
#define CHGR_BAT_IF_PRES_STATUS			0x08
#define CHGR_STATUS				0x09
#define CHGR_BAT_IF_VCP				0x42
#define CHGR_BAT_IF_BATFET_CTRL1		0x90
#define CHGR_BAT_IF_BATFET_CTRL4		0x93
#define CHGR_BAT_IF_SPARE			0xDF
#define CHGR_MISC_BOOT_DONE			0x42
#define CHGR_BUCK_PSTG_CTRL			0x73
#define CHGR_BUCK_COMPARATOR_OVRIDE_1		0xEB
#define CHGR_BUCK_COMPARATOR_OVRIDE_3		0xED
#define CHGR_BUCK_BCK_VBAT_REG_MODE		0x74
#define MISC_REVISION2				0x01
#define USB_OVP_CTL				0x42
#define USB_CHG_GONE_REV_BST			0xED
#define BUCK_VCHG_OV				0x77
#define BUCK_TEST_SMBC_MODES			0xE6
#define BUCK_CTRL_TRIM1				0xF1
#define BUCK_CTRL_TRIM3				0xF3
#define SEC_ACCESS				0xD0
#define BAT_IF_VREF_BAT_THM_CTRL		0x4A
#define BAT_IF_BPD_CTRL				0x48
#define BOOST_VSET				0x41
#define BOOST_ENABLE_CONTROL			0x46
#define COMP_OVR1				0xEA
#define BAT_IF_BTC_CTRL				0x49
#define USB_OCP_THR				0x52
#define USB_OCP_CLR				0x53
#define BAT_IF_TEMP_STATUS			0x09
#define BOOST_ILIM				0x78

#define REG_OFFSET_PERP_SUBTYPE			0x05

/* SMBB peripheral subtype values */
#define SMBB_CHGR_SUBTYPE			0x01
#define SMBB_BUCK_SUBTYPE			0x02
#define SMBB_BAT_IF_SUBTYPE			0x03
#define SMBB_USB_CHGPTH_SUBTYPE			0x04
#define SMBB_DC_CHGPTH_SUBTYPE			0x05
#define SMBB_BOOST_SUBTYPE			0x06
#define SMBB_MISC_SUBTYPE			0x07

/* SMBB peripheral subtype values */
#define SMBBP_CHGR_SUBTYPE			0x31
#define SMBBP_BUCK_SUBTYPE			0x32
#define SMBBP_BAT_IF_SUBTYPE			0x33
#define SMBBP_USB_CHGPTH_SUBTYPE		0x34
#define SMBBP_BOOST_SUBTYPE			0x36
#define SMBBP_MISC_SUBTYPE			0x37

/* SMBCL peripheral subtype values */
#define SMBCL_CHGR_SUBTYPE			0x41
#define SMBCL_BUCK_SUBTYPE			0x42
#define SMBCL_BAT_IF_SUBTYPE			0x43
#define SMBCL_USB_CHGPTH_SUBTYPE		0x44
#define SMBCL_MISC_SUBTYPE			0x47

#define QPNP_CHARGER_DEV_NAME	"qcom,qpnp-charger"

/* Status bits and masks */
#define CHGR_BOOT_DONE			BIT(7)
#define CHGR_CHG_EN			BIT(7)
#define CHGR_ON_BAT_FORCE_BIT		BIT(0)
#define USB_VALID_DEB_20MS		0x03
#define BUCK_VBAT_REG_NODE_SEL_BIT	BIT(0)
#define VREF_BATT_THERM_FORCE_ON	0xC0
#define BAT_IF_BPD_CTRL_SEL		0x03
#define VREF_BAT_THM_ENABLED_FSM	0x80
#define REV_BST_DETECTED		BIT(0)
#define BAT_THM_EN			BIT(1)
#define BAT_ID_EN			BIT(0)
#define BOOST_PWR_EN			BIT(7)
#define OCP_CLR_BIT			BIT(7)
#define OCP_THR_MASK			0x03
#define OCP_THR_900_MA			0x02
#define OCP_THR_500_MA			0x01
#define OCP_THR_200_MA			0x00

/* Interrupt definitions */
/* smbb_chg_interrupts */
#define CHG_DONE_IRQ			BIT(7)
#define CHG_FAILED_IRQ			BIT(6)
#define FAST_CHG_ON_IRQ			BIT(5)
#define TRKL_CHG_ON_IRQ			BIT(4)
#define STATE_CHANGE_ON_IR		BIT(3)
#define CHGWDDOG_IRQ			BIT(2)
#define VBAT_DET_HI_IRQ			BIT(1)
#define VBAT_DET_LOW_IRQ		BIT(0)

/* smbb_buck_interrupts */
#define VDD_LOOP_IRQ			BIT(6)
#define IBAT_LOOP_IRQ			BIT(5)
#define ICHG_LOOP_IRQ			BIT(4)
#define VCHG_LOOP_IRQ			BIT(3)
#define OVERTEMP_IRQ			BIT(2)
#define VREF_OV_IRQ			BIT(1)
#define VBAT_OV_IRQ			BIT(0)

/* smbb_bat_if_interrupts */
#define PSI_IRQ				BIT(4)
#define VCP_ON_IRQ			BIT(3)
#define BAT_FET_ON_IRQ			BIT(2)
#define BAT_TEMP_OK_IRQ			BIT(1)
#define BATT_PRES_IRQ			BIT(0)

/* smbb_usb_interrupts */
#define CHG_GONE_IRQ			BIT(2)
#define USBIN_VALID_IRQ			BIT(1)
#define COARSE_DET_USB_IRQ		BIT(0)

/* smbb_dc_interrupts */
#define DCIN_VALID_IRQ			BIT(1)
#define COARSE_DET_DC_IRQ		BIT(0)

/* smbb_boost_interrupts */
#define LIMIT_ERROR_IRQ			BIT(1)
#define BOOST_PWR_OK_IRQ		BIT(0)

/* smbb_misc_interrupts */
#define TFTWDOG_IRQ			BIT(0)

/* SMBB types */
#define SMBB				BIT(1)
#define SMBBP				BIT(2)
#define SMBCL				BIT(3)

/* Workaround flags */
#define CHG_FLAGS_VCP_WA		BIT(0)
#define BOOST_FLASH_WA			BIT(1)
#define POWER_STAGE_WA			BIT(2)

#ifdef CONFIG_MACH_OPPO
/* Oppo flags */
#define BATT_HEARTBEAT_INTERVAL		6000
#define BATT_CHG_TIMEOUT_COUNT_DCP	12 * 10 * 60
#define BATT_CHG_TIMEOUT_COUNT_USB_PRO	16 * 10 * 60
#define BATT_CHG_DONE_CHECK_COUNT	10
#define CHARGER_SOFT_OVP_VOLTAGE	5800
#define CHARGER_SOFT_UVP_VOLTAGE	4300
#define CHARGER_VOLTAGE_NORMAL		5000
#define BATTERY_SOFT_OVP_VOLTAGE	4500 * 1000
#define MSM_CHARGER_GAUGE_MISSING_VOLTS 3500 * 1000

#define AUTO_CHARGING_BATT_TEMP_T0				-100
#define AUTO_CHARGING_BATT_TEMP_T1				0
#define AUTO_CHARGING_BATT_TEMP_T2				50
#define AUTO_CHARGING_BATT_TEMP_T3				450
#define AUTO_CHARGING_BATT_TEMP_T4				550
#define AUTO_CHARGING_BATT_REMOVE_TEMP				-400
#define AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_HOT_TO_WARM	30
#define AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_WARM_TO_NORMAL	10
#define AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_COOL_TO_NORMAL	10
#define AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_COLD_TO_COOL	30

enum chg_charger_status_type {
	/* The charger is good      */
	CHARGER_STATUS_GOOD,
	/* The charger is bad       */
	CHARGER_STATUS_BAD,
	/* The charger is weak      */
	CHARGER_STATUS_WEAK,
	CHARGER_STATUS_OVER,
	/* Invalid charger status.  */
	CHARGER_STATUS_INVALID
};

enum chg_battery_status_type {
	/* The battery is good        */
	BATTERY_STATUS_GOOD,
	/* The battery is cold/hot    */
	BATTERY_STATUS_BAD_TEMP,
	/* The battery is bad         */
	BATTERY_STATUS_BAD,
	/* The battery is removed     */
	BATTERY_STATUS_REMOVED,
	/* on v2.2 only */
	BATTERY_STATUS_INVALID_v1 = BATTERY_STATUS_REMOVED,
	/* Invalid battery status.    */
	BATTERY_STATUS_INVALID
};

typedef enum {
	/*! Battery is cold           */
	CV_BATTERY_TEMP_REGION__COLD,
	/*! Battery is little cold    */
	CV_BATTERY_TEMP_REGION_LITTLE__COLD,
	/*! Battery is cool           */
	CV_BATTERY_TEMP_REGION__COOL,
	/*! Battery is normal         */
	CV_BATTERY_TEMP_REGION__NORMAL,
	/*! Battery is warm           */
	CV_BATTERY_TEMP_REGION__WARM,
	/*! Battery is hot            */
	CV_BATTERY_TEMP_REGION__HOT,
	/*! Invalid battery temp region */
	CV_BATTERY_TEMP_REGION__INVALID,
} chg_cv_battery_temp_region_type;

static bool use_fake_temp = false;
static int fake_temp = 300;
static bool use_fake_chgvol = false;
static int fake_chgvol = 0;

#ifdef CONFIG_BATTERY_BQ27541
static struct qpnp_battery_gauge *qpnp_batt_gauge = NULL;
#endif

#ifdef CONFIG_BQ24196_CHARGER
static struct qpnp_external_charger *qpnp_ext_charger = NULL;
#endif

#endif /* CONFIG_MACH_OPPO */

struct qpnp_chg_irq {
	int		irq;
	unsigned long		disabled;
};

struct qpnp_chg_regulator {
	struct regulator_desc			rdesc;
	struct regulator_dev			*rdev;
};

/**
 * struct qpnp_chg_chip - device information
 * @dev:			device pointer to access the parent
 * @spmi:			spmi pointer to access spmi information
 * @chgr_base:			charger peripheral base address
 * @buck_base:			buck  peripheral base address
 * @bat_if_base:		battery interface  peripheral base address
 * @usb_chgpth_base:		USB charge path peripheral base address
 * @dc_chgpth_base:		DC charge path peripheral base address
 * @boost_base:			boost peripheral base address
 * @misc_base:			misc peripheral base address
 * @freq_base:			freq peripheral base address
 * @bat_is_cool:		indicates that battery is cool
 * @bat_is_warm:		indicates that battery is warm
 * @chg_done:			indicates that charging is completed
 * @usb_present:		present status of usb
 * @dc_present:			present status of dc
 * @batt_present:		present status of battery
 * @use_default_batt_values:	flag to report default battery properties
 * @btc_disabled		Flag to disable btc (disables hot and cold irqs)
 * @max_voltage_mv:		the max volts the batt should be charged up to
 * @min_voltage_mv:		min battery voltage before turning the FET on
 * @batt_weak_voltage_mv:	Weak battery voltage threshold
 * @vbatdet_max_err_mv		resume voltage hysterisis
 * @max_bat_chg_current:	maximum battery charge current in mA
 * @warm_bat_chg_ma:	warm battery maximum charge current in mA
 * @cool_bat_chg_ma:	cool battery maximum charge current in mA
 * @warm_bat_mv:		warm temperature battery target voltage
 * @cool_bat_mv:		cool temperature battery target voltage
 * @resume_delta_mv:		voltage delta at which battery resumes charging
 * @term_current:		the charging based term current
 * @safe_current:		battery safety current setting
 * @maxinput_usb_ma:		Maximum Input current USB
 * @maxinput_dc_ma:		Maximum Input current DC
 * @hot_batt_p			Hot battery threshold setting
 * @cold_batt_p			Cold battery threshold setting
 * @warm_bat_decidegc		Warm battery temperature in degree Celsius
 * @cool_bat_decidegc		Cool battery temperature in degree Celsius
 * @revision:			PMIC revision
 * @type:			SMBB type
 * @tchg_mins			maximum allowed software initiated charge time
 * @thermal_levels		amount of thermal mitigation levels
 * @thermal_mitigation		thermal mitigation level values
 * @therm_lvl_sel		thermal mitigation level selection
 * @dc_psy			power supply to export information to userspace
 * @usb_psy			power supply to export information to userspace
 * @bms_psy			power supply to export information to userspace
 * @batt_psy:			power supply to export information to userspace
 * @flags:			flags to activate specific workarounds
 *				throughout the driver
 *
 */
struct qpnp_chg_chip {
	struct device			*dev;
	struct spmi_device		*spmi;
	u16				chgr_base;
	u16				buck_base;
	u16				bat_if_base;
	u16				usb_chgpth_base;
	u16				dc_chgpth_base;
	u16				boost_base;
	u16				misc_base;
	u16				freq_base;
	struct qpnp_chg_irq		usbin_valid;
	struct qpnp_chg_irq		usb_ocp;
	struct qpnp_chg_irq		dcin_valid;
	struct qpnp_chg_irq		chg_gone;
	struct qpnp_chg_irq		chg_fastchg;
	struct qpnp_chg_irq		chg_trklchg;
	struct qpnp_chg_irq		chg_failed;
	struct qpnp_chg_irq		chg_vbatdet_lo;
	struct qpnp_chg_irq		batt_pres;
	struct qpnp_chg_irq		batt_temp_ok;
	struct qpnp_chg_irq		coarse_det_usb;
	bool				bat_is_cool;
	bool				bat_is_warm;
	bool				chg_done;
	bool				charger_monitor_checked;
	bool				usb_present;
	u8				usbin_health;
	bool				usb_coarse_det;
	bool				dc_present;
	bool				batt_present;
	bool				charging_disabled;
	bool				ovp_monitor_enable;
	bool				usb_valid_check_ovp;
	bool				btc_disabled;
	bool				use_default_batt_values;
	bool				duty_cycle_100p;
	bool				ibat_calibration_enabled;
	bool				aicl_settled;
	bool				use_external_rsense;
	unsigned int			bpd_detection;
	unsigned int			max_bat_chg_current;
	unsigned int			warm_bat_chg_ma;
	unsigned int			cool_bat_chg_ma;
	unsigned int			safe_voltage_mv;
	unsigned int			max_voltage_mv;
	unsigned int			min_voltage_mv;
	unsigned int			batt_weak_voltage_mv;
	unsigned int			vbatdet_max_err_mv;
	int				prev_usb_max_ma;
	int				set_vddmax_mv;
	int				delta_vddmax_mv;
	u8				trim_center;
	unsigned int			warm_bat_mv;
	unsigned int			cool_bat_mv;
	unsigned int			resume_delta_mv;
	int				insertion_ocv_uv;
	int				term_current;
	int				soc_resume_limit;
	bool				resuming_charging;
	unsigned int			maxinput_usb_ma;
	unsigned int			maxinput_dc_ma;
	unsigned int			hot_batt_p;
	unsigned int			cold_batt_p;
	int				warm_bat_decidegc;
	int				cool_bat_decidegc;
	int				fake_battery_soc;
	unsigned int			safe_current;
	unsigned int			revision;
	unsigned int			type;
	unsigned int			tchg_mins;
	unsigned int			thermal_levels;
	unsigned int			therm_lvl_sel;
	unsigned int			*thermal_mitigation;
	struct power_supply		dc_psy;
	struct power_supply		*usb_psy;
	struct power_supply		*bms_psy;
	struct power_supply		batt_psy;
	uint32_t			flags;
	struct qpnp_adc_tm_btm_param	adc_param;
	struct work_struct		adc_measure_work;
	struct work_struct		adc_disable_work;
	struct delayed_work		arb_stop_work;
	struct delayed_work		eoc_work;
	struct delayed_work		usbin_health_check;
	struct work_struct		soc_check_work;
	struct delayed_work		aicl_check_work;
	struct work_struct		insertion_ocv_work;
	struct work_struct		ocp_clear_work;
	struct qpnp_chg_regulator	otg_vreg;
	struct qpnp_chg_regulator	boost_vreg;
	struct qpnp_chg_regulator	batfet_vreg;
	bool				batfet_ext_en;
	struct work_struct		batfet_lcl_work;
	struct qpnp_vadc_chip		*vadc_dev;
	struct qpnp_iadc_chip		*iadc_dev;
	struct qpnp_adc_tm_chip		*adc_tm_dev;
	struct mutex			jeita_configure_lock;
	spinlock_t			usbin_health_monitor_lock;
	struct mutex			batfet_vreg_lock;
	struct alarm			reduce_power_stage_alarm;
	struct work_struct		reduce_power_stage_work;
	bool				power_stage_workaround_running;
	bool				power_stage_workaround_enable;
#ifdef CONFIG_MACH_OPPO
	bool				chg_display_full;//wangjc add for charge full
	struct delayed_work		update_heartbeat_work;
	enum chg_charger_status_type	charger_status;
	chg_cv_battery_temp_region_type	mBatteryTempRegion;
	short				mBatteryTempBoundT0;
	short				mBatteryTempBoundT1;
	short				mBatteryTempBoundT2;
	short				mBatteryTempBoundT3;
	short				mBatteryTempBoundT4;
	enum chg_battery_status_type	battery_status;
	int				batt_health;
	bool				time_out;
	unsigned int			aicl_current;
	struct notifier_block		fb_notif;
	atomic_t			suspended;
	unsigned int			usbin_counts;
#ifdef CONFIG_BQ24196_CHARGER
	struct work_struct		start_charge_work;
	struct work_struct		stop_charge_work;
	struct work_struct		ext_charger_hwinit_work;
#endif
#endif
};


static struct of_device_id qpnp_charger_match_table[] = {
	{ .compatible = QPNP_CHARGER_DEV_NAME, },
	{}
};

enum bpd_type {
	BPD_TYPE_BAT_ID,
	BPD_TYPE_BAT_THM,
	BPD_TYPE_BAT_THM_BAT_ID,
};

static const char * const bpd_label[] = {
	[BPD_TYPE_BAT_ID] = "bpd_id",
	[BPD_TYPE_BAT_THM] = "bpd_thm",
	[BPD_TYPE_BAT_THM_BAT_ID] = "bpd_thm_id",
};

enum btc_type {
	HOT_THD_25_PCT = 25,
	HOT_THD_35_PCT = 35,
	COLD_THD_70_PCT = 70,
	COLD_THD_80_PCT = 80,
};

static u8 btc_value[] = {
	[HOT_THD_25_PCT] = 0x0,
	[HOT_THD_35_PCT] = BIT(0),
	[COLD_THD_70_PCT] = 0x0,
	[COLD_THD_80_PCT] = BIT(1),
};

enum usbin_health {
	USBIN_UNKNOW,
	USBIN_OK,
	USBIN_OVP,
};

static inline int
get_bpd(const char *name)
{
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(bpd_label); i++) {
		if (strcmp(bpd_label[i], name) == 0)
			return i;
	}
	return -EINVAL;
}

static bool
is_within_range(int value, int left, int right)
{
	if (left >= right && left >= value && value >= right)
		return 1;
	if (left <= right && left <= value && value <= right)
		return 1;
	return 0;
}

static int
qpnp_chg_read(struct qpnp_chg_chip *chip, u8 *val,
			u16 base, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return -EINVAL;
	}

	rc = spmi_ext_register_readl(spmi->ctrl, spmi->sid, base, val, count);
	if (rc) {
		pr_err("SPMI read failed base=0x%02x sid=0x%02x rc=%d\n", base,
				spmi->sid, rc);
		return rc;
	}
	return 0;
}

static int
qpnp_chg_write(struct qpnp_chg_chip *chip, u8 *val,
			u16 base, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return -EINVAL;
	}

	rc = spmi_ext_register_writel(spmi->ctrl, spmi->sid, base, val, count);
	if (rc) {
		pr_err("write failed base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return rc;
	}

	return 0;
}

static int
qpnp_chg_masked_write(struct qpnp_chg_chip *chip, u16 base,
						u8 mask, u8 val, int count)
{
	int rc;
	u8 reg;

	rc = qpnp_chg_read(chip, &reg, base, count);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n", base, rc);
		return rc;
	}
	pr_debug("addr = 0x%x read 0x%x\n", base, reg);

	reg &= ~mask;
	reg |= val & mask;

	pr_debug("Writing 0x%x\n", reg);

	rc = qpnp_chg_write(chip, &reg, base, count);
	if (rc) {
		pr_err("spmi write failed: addr=%03X, rc=%d\n", base, rc);
		return rc;
	}

	return 0;
}

static void
qpnp_chg_enable_irq(struct qpnp_chg_irq *irq)
{
	if (__test_and_clear_bit(0, &irq->disabled)) {
		pr_debug("number = %d\n", irq->irq);
		enable_irq(irq->irq);
	}
}

static void
qpnp_chg_disable_irq(struct qpnp_chg_irq *irq)
{
	if (!__test_and_set_bit(0, &irq->disabled)) {
		pr_debug("number = %d\n", irq->irq);
		disable_irq_nosync(irq->irq);
	}
}

#define USB_OTG_EN_BIT	BIT(0)

#ifdef CONFIG_BQ24196_CHARGER
static int is_otg_en_set = false;
static int
qpnp_chg_is_otg_en_set(struct qpnp_chg_chip *chip)
{
	int status;
	
	if (qpnp_ext_charger && qpnp_ext_charger->chg_get_system_status)
		status = qpnp_ext_charger->chg_get_system_status();
	else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}

	if ((status & 0xc0) == 0xc0) {
		return 1;
	} else {
		return 0;
	}
}
#else
static int
qpnp_chg_is_otg_en_set(struct qpnp_chg_chip *chip)
{
	u8 usb_otg_en;
	int rc;

	rc = qpnp_chg_read(chip, &usb_otg_en,
				 chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
				 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}
	pr_debug("usb otg en 0x%x\n", usb_otg_en);

	return (usb_otg_en & USB_OTG_EN_BIT) ? 1 : 0;
}
#endif

static int
qpnp_chg_is_boost_en_set(struct qpnp_chg_chip *chip)
{
	u8 boost_en_ctl;
	int rc;

	rc = qpnp_chg_read(chip, &boost_en_ctl,
		chip->boost_base + BOOST_ENABLE_CONTROL, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->boost_base + BOOST_ENABLE_CONTROL, rc);
		return rc;
	}

	pr_debug("boost en 0x%x\n", boost_en_ctl);

	return (boost_en_ctl & BOOST_PWR_EN) ? 1 : 0;
}

static int
qpnp_chg_is_batt_temp_ok(struct qpnp_chg_chip *chip)
{
	u8 batt_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &batt_rt_sts,
				 INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->bat_if_base), rc);
		return rc;
	}

	return (batt_rt_sts & BAT_TEMP_OK_IRQ) ? 1 : 0;
}

static int
qpnp_chg_is_batt_present(struct qpnp_chg_chip *chip)
{
	u8 batt_pres_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &batt_pres_rt_sts,
				 INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->bat_if_base), rc);
		return rc;
	}

	return (batt_pres_rt_sts & BATT_PRES_IRQ) ? 1 : 0;
}

static int
qpnp_chg_is_batfet_closed(struct qpnp_chg_chip *chip)
{
	u8 batfet_closed_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &batfet_closed_rt_sts,
				 INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->bat_if_base), rc);
		return rc;
	}

	return (batfet_closed_rt_sts & BAT_FET_ON_IRQ) ? 1 : 0;
}

#define USB_VALID_BIT	BIT(7)
static int
qpnp_chg_is_usb_chg_plugged_in(struct qpnp_chg_chip *chip)
{
	u8 usbin_valid_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &usbin_valid_rt_sts,
				 chip->usb_chgpth_base + CHGR_STATUS , 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}
	pr_debug("chgr usb sts 0x%x\n", usbin_valid_rt_sts);

#ifdef CONFIG_BQ24196_CHARGER
	if (is_otg_en_set == true)
		return 0;
#endif

	return (usbin_valid_rt_sts & USB_VALID_BIT) ? 1 : 0;
}

static bool
qpnp_chg_is_ibat_loop_active(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 buck_sts;

	rc = qpnp_chg_read(chip, &buck_sts,
			INT_RT_STS(chip->buck_base), 1);
	if (rc) {
		pr_err("failed to read buck RT status rc=%d\n", rc);
		return 0;
	}

	return !!(buck_sts & IBAT_LOOP_IRQ);
}

#define USB_VALID_MASK 0xC0
#define USB_COARSE_DET 0x10
#define USB_VALID_UVP_VALUE    0x00
#define USB_VALID_OVP_VALUE    0x40
static int
qpnp_chg_check_usb_coarse_det(struct qpnp_chg_chip *chip)
{
	u8 usbin_chg_rt_sts;
	int rc;
	rc = qpnp_chg_read(chip, &usbin_chg_rt_sts,
		chip->usb_chgpth_base + CHGR_STATUS , 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
			chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}
	return (usbin_chg_rt_sts & USB_COARSE_DET) ? 1 : 0;
}

static int
qpnp_chg_check_usbin_health(struct qpnp_chg_chip *chip)
{
	u8 usbin_chg_rt_sts, usbin_health = 0;
	int rc;

	rc = qpnp_chg_read(chip, &usbin_chg_rt_sts,
		chip->usb_chgpth_base + CHGR_STATUS , 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
		chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}

	pr_debug("chgr usb sts 0x%x\n", usbin_chg_rt_sts);
	if ((usbin_chg_rt_sts & USB_COARSE_DET) == USB_COARSE_DET) {
		if ((usbin_chg_rt_sts & USB_VALID_MASK)
			 == USB_VALID_OVP_VALUE) {
			usbin_health = USBIN_OVP;
			pr_err("Over voltage charger inserted\n");
		} else if ((usbin_chg_rt_sts & USB_VALID_BIT) != 0) {
			usbin_health = USBIN_OK;
			pr_debug("Valid charger inserted\n");
		}
	} else {
		usbin_health = USBIN_UNKNOW;
		pr_debug("Charger plug out\n");
	}

	return usbin_health;
}

static int
qpnp_chg_is_dc_chg_plugged_in(struct qpnp_chg_chip *chip)
{
	u8 dcin_valid_rt_sts;
	int rc;

	if (!chip->dc_chgpth_base)
		return 0;

	rc = qpnp_chg_read(chip, &dcin_valid_rt_sts,
				 INT_RT_STS(chip->dc_chgpth_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->dc_chgpth_base), rc);
		return rc;
	}

	return (dcin_valid_rt_sts & DCIN_VALID_IRQ) ? 1 : 0;
}

static int
qpnp_chg_is_ichg_loop_active(struct qpnp_chg_chip *chip)
{
	u8 buck_sts;
	int rc;

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->buck_base), rc);
		return rc;
	}
	pr_debug("buck usb sts 0x%x\n", buck_sts);

	return (buck_sts & ICHG_LOOP_IRQ) ? 1 : 0;
}

#define QPNP_CHG_I_MAX_MIN_100		100
#define QPNP_CHG_I_MAX_MIN_150		150
#define QPNP_CHG_I_MAX_MIN_MA		200
#define QPNP_CHG_I_MAX_MAX_MA		2500
#define QPNP_CHG_I_MAXSTEP_MA		100
static int
qpnp_chg_idcmax_set(struct qpnp_chg_chip *chip, int mA)
{
	int rc = 0;
	u8 dc = 0;

	if (mA < QPNP_CHG_I_MAX_MIN_100
			|| mA > QPNP_CHG_I_MAX_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", mA);
		return -EINVAL;
	}

	if (mA == QPNP_CHG_I_MAX_MIN_100) {
		dc = 0x00;
		pr_debug("current=%d setting %02x\n", mA, dc);
		return qpnp_chg_write(chip, &dc,
			chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);
	} else if (mA == QPNP_CHG_I_MAX_MIN_150) {
		dc = 0x01;
		pr_debug("current=%d setting %02x\n", mA, dc);
		return qpnp_chg_write(chip, &dc,
			chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);
	}

	dc = mA / QPNP_CHG_I_MAXSTEP_MA;

	pr_debug("current=%d setting 0x%x\n", mA, dc);
	rc = qpnp_chg_write(chip, &dc,
		chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);

	return rc;
}

static int
qpnp_chg_iusb_trim_get(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	u8 trim_reg;

	rc = qpnp_chg_read(chip, &trim_reg,
			chip->usb_chgpth_base + CHGR_USB_TRIM, 1);
	if (rc) {
		pr_err("failed to read USB_TRIM rc=%d\n", rc);
		return 0;
	}

	return trim_reg;
}

static int
qpnp_chg_iusb_trim_set(struct qpnp_chg_chip *chip, int trim)
{
	int rc = 0;

	rc = qpnp_chg_masked_write(chip,
		chip->usb_chgpth_base + SEC_ACCESS,
		0xFF,
		0xA5, 1);
	if (rc) {
		pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->usb_chgpth_base + CHGR_USB_TRIM,
		0xFF,
		trim, 1);
	if (rc) {
		pr_err("failed to write USB TRIM rc=%d\n", rc);
		return rc;
	}

	return rc;
}

#ifdef CONFIG_BQ24196_CHARGER
static int
qpnp_chg_iusbmax_set(struct qpnp_chg_chip *chip, int mA)
{
	if (get_boot_mode() != MSM_BOOT_MODE__NORMAL)
		return -EINVAL;

	if (mA < QPNP_CHG_I_MAX_MIN_100
			|| mA > QPNP_CHG_I_MAX_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", mA);
		return -EINVAL;
	}

	if (qpnp_ext_charger && qpnp_ext_charger->chg_iusbmax_set)
		return qpnp_ext_charger->chg_iusbmax_set(mA);
	else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}
#else
static int
qpnp_chg_iusbmax_set(struct qpnp_chg_chip *chip, int mA)
{
	int rc = 0;
	u8 usb_reg = 0, temp = 8;

	if (mA < 0 || mA > QPNP_CHG_I_MAX_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", mA);
		return -EINVAL;
	}

	if (mA <= QPNP_CHG_I_MAX_MIN_100) {
		usb_reg = 0x00;
		pr_debug("current=%d setting %02x\n", mA, usb_reg);
		return qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	} else if (mA == QPNP_CHG_I_MAX_MIN_150) {
		usb_reg = 0x01;
		pr_debug("current=%d setting %02x\n", mA, usb_reg);
		return qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	}

	/* Impose input current limit */
	if (chip->maxinput_usb_ma)
		mA = (chip->maxinput_usb_ma) <= mA ? chip->maxinput_usb_ma : mA;

	usb_reg = mA / QPNP_CHG_I_MAXSTEP_MA;

	if (chip->flags & CHG_FLAGS_VCP_WA) {
		temp = 0xA5;
		rc =  qpnp_chg_write(chip, &temp,
			chip->buck_base + SEC_ACCESS, 1);
		rc =  qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_3,
			0x0C, 0x0C, 1);
	}

	pr_debug("current=%d setting 0x%x\n", mA, usb_reg);
	rc = qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);

	if (chip->flags & CHG_FLAGS_VCP_WA) {
		temp = 0xA5;
		udelay(200);
		rc =  qpnp_chg_write(chip, &temp,
			chip->buck_base + SEC_ACCESS, 1);
		rc =  qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_3,
			0x0C, 0x00, 1);
	}

	return rc;
}
#endif /* CONFIG_BQ24196_CHARGER */

#define QPNP_CHG_VINMIN_MIN_MV		4200
#define QPNP_CHG_VINMIN_HIGH_MIN_MV	5600
#define QPNP_CHG_VINMIN_HIGH_MIN_VAL	0x2B
#define QPNP_CHG_VINMIN_MAX_MV		9600
#define QPNP_CHG_VINMIN_STEP_MV		50
#define QPNP_CHG_VINMIN_STEP_HIGH_MV	200
#define QPNP_CHG_VINMIN_MASK		0x3F
#define QPNP_CHG_VINMIN_MIN_VAL	0x10
#ifdef CONFIG_BQ24196_CHARGER
static int
qpnp_chg_vinmin_set(struct qpnp_chg_chip *chip, int voltage)
{
	if (qpnp_ext_charger && qpnp_ext_charger->chg_vinmin_set)
		return qpnp_ext_charger->chg_vinmin_set(voltage);
	else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}
#else
static int
qpnp_chg_vinmin_set(struct qpnp_chg_chip *chip, int voltage)
{
	u8 temp;

	if (voltage < QPNP_CHG_VINMIN_MIN_MV
			|| voltage > QPNP_CHG_VINMIN_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	if (voltage >= QPNP_CHG_VINMIN_HIGH_MIN_MV) {
		temp = QPNP_CHG_VINMIN_HIGH_MIN_VAL;
		temp += (voltage - QPNP_CHG_VINMIN_HIGH_MIN_MV)
			/ QPNP_CHG_VINMIN_STEP_HIGH_MV;
	} else {
		temp = QPNP_CHG_VINMIN_MIN_VAL;
		temp += (voltage - QPNP_CHG_VINMIN_MIN_MV)
			/ QPNP_CHG_VINMIN_STEP_MV;
	}

	pr_debug("voltage=%d setting %02x\n", voltage, temp);
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_VIN_MIN,
			QPNP_CHG_VINMIN_MASK, temp, 1);
}
#endif

static int
qpnp_chg_vinmin_get(struct qpnp_chg_chip *chip)
{
	int rc, vin_min_mv;
	u8 vin_min;

	rc = qpnp_chg_read(chip, &vin_min, chip->chgr_base + CHGR_VIN_MIN, 1);
	if (rc) {
		pr_err("failed to read VIN_MIN rc=%d\n", rc);
		return 0;
	}

	if (vin_min == 0)
		vin_min_mv = QPNP_CHG_I_MAX_MIN_100;
	else if (vin_min >= QPNP_CHG_VINMIN_HIGH_MIN_VAL)
		vin_min_mv = QPNP_CHG_VINMIN_HIGH_MIN_MV +
			(vin_min - QPNP_CHG_VINMIN_HIGH_MIN_VAL)
				* QPNP_CHG_VINMIN_STEP_HIGH_MV;
	else
		vin_min_mv = QPNP_CHG_VINMIN_MIN_MV +
			(vin_min - QPNP_CHG_VINMIN_MIN_VAL)
				* QPNP_CHG_VINMIN_STEP_MV;
	pr_debug("vin_min= 0x%02x, ma = %d\n", vin_min, vin_min_mv);

	return vin_min_mv;
}

#define QPNP_CHG_VBATWEAK_MIN_MV	2100
#define QPNP_CHG_VBATWEAK_MAX_MV	3600
#define QPNP_CHG_VBATWEAK_STEP_MV	100
static int
qpnp_chg_vbatweak_set(struct qpnp_chg_chip *chip, int vbatweak_mv)
{
	u8 temp;

	if (vbatweak_mv < QPNP_CHG_VBATWEAK_MIN_MV
			|| vbatweak_mv > QPNP_CHG_VBATWEAK_MAX_MV)
		return -EINVAL;

	temp = (vbatweak_mv - QPNP_CHG_VBATWEAK_MIN_MV)
			/ QPNP_CHG_VBATWEAK_STEP_MV;

	pr_debug("voltage=%d setting %02x\n", vbatweak_mv, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VBAT_WEAK, 1);
}

static int
qpnp_chg_usb_iusbmax_get(struct qpnp_chg_chip *chip)
{
	int rc, iusbmax_ma;
	u8 iusbmax;

	rc = qpnp_chg_read(chip, &iusbmax,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	if (rc) {
		pr_err("failed to read IUSB_MAX rc=%d\n", rc);
		return 0;
	}

	if (iusbmax == 0)
		iusbmax_ma = QPNP_CHG_I_MAX_MIN_100;
	else if (iusbmax == 0x01)
		iusbmax_ma = QPNP_CHG_I_MAX_MIN_150;
	else
		iusbmax_ma = iusbmax * QPNP_CHG_I_MAXSTEP_MA;

	pr_debug("iusbmax = 0x%02x, ma = %d\n", iusbmax, iusbmax_ma);

	return iusbmax_ma;
}

#define USB_SUSPEND_BIT	BIT(0)

#ifdef CONFIG_BQ24196_CHARGER
static int
qpnp_chg_usb_suspend_enable(struct qpnp_chg_chip *chip, int enable)
{
	if (get_boot_mode() != MSM_BOOT_MODE__NORMAL && !enable)
		return -EINVAL;

	if (qpnp_ext_charger && qpnp_ext_charger->chg_usb_suspend_enable) {
		return qpnp_ext_charger->chg_usb_suspend_enable(enable);
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}
#else
static int
qpnp_chg_usb_suspend_enable(struct qpnp_chg_chip *chip, int enable)
{
	return qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_SUSP,
			USB_SUSPEND_BIT,
			enable ? USB_SUSPEND_BIT : 0, 1);
}
#endif

#ifdef CONFIG_BQ24196_CHARGER
static int
qpnp_chg_charge_en(struct qpnp_chg_chip *chip, int enable)
{
	if (get_boot_mode() != MSM_BOOT_MODE__NORMAL)
		return -EINVAL;

	if (qpnp_ext_charger && qpnp_ext_charger->chg_charge_en) {
		return qpnp_ext_charger->chg_charge_en(enable);
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}

#ifdef CONFIG_PIC1503_FASTCG
static int qpnp_chg_get_charge_en(void)
{
	if (qpnp_ext_charger && qpnp_ext_charger->chg_get_charge_en) {
		return qpnp_ext_charger->chg_get_charge_en();
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}
#endif
#else
static int
qpnp_chg_charge_en(struct qpnp_chg_chip *chip, int enable)
{
	if (chip->insertion_ocv_uv == 0 && enable) {
		pr_debug("Battery not present, skipping\n");
		return 0;
	}
	pr_debug("charging %s\n", enable ? "enabled" : "disabled");
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_CHG_CTRL,
			CHGR_CHG_EN,
			enable ? CHGR_CHG_EN : 0, 1);
}
#endif

#ifndef CONFIG_BQ24196_CHARGER
static int
qpnp_chg_force_run_on_batt(struct qpnp_chg_chip *chip, int disable)
{
	/* Don't run on battery for batteryless hardware */
	if (chip->use_default_batt_values)
		return 0;
	/* Don't force on battery if battery is not present */
	if (!qpnp_chg_is_batt_present(chip))
		return 0;

	/* This bit forces the charger to run off of the battery rather
	 * than a connected charger */
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_CHG_CTRL,
			CHGR_ON_BAT_FORCE_BIT,
			disable ? CHGR_ON_BAT_FORCE_BIT : 0, 1);
}
#endif

#define BUCK_DUTY_MASK_100P	0x30
static int
qpnp_buck_set_100_duty_cycle_enable(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	pr_debug("enable: %d\n", enable);

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS, 0xA5, 0xA5, 1);
	if (rc) {
		pr_debug("failed to write sec access rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + BUCK_TEST_SMBC_MODES,
			BUCK_DUTY_MASK_100P, enable ? 0x00 : 0x10, 1);
	if (rc) {
		pr_debug("failed enable 100p duty cycle rc=%d\n", rc);
		return rc;
	}

	return rc;
}

#define COMPATATOR_OVERRIDE_0	0x80
static int
qpnp_chg_toggle_chg_done_logic(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	pr_debug("toggle: %d\n", enable);

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS, 0xA5, 0xA5, 1);
	if (rc) {
		pr_debug("failed to write sec access rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_1,
			0xC0, enable ? 0x00 : COMPATATOR_OVERRIDE_0, 1);
	if (rc) {
		pr_debug("failed to toggle chg done override rc=%d\n", rc);
		return rc;
	}

	return rc;
}

#define QPNP_CHG_VBATDET_MIN_MV	3240
#define QPNP_CHG_VBATDET_MAX_MV	5780
#define QPNP_CHG_VBATDET_STEP_MV	20
#ifdef CONFIG_BQ24196_CHARGER
static int
qpnp_chg_vbatdet_set(struct qpnp_chg_chip *chip, int vbatdet_mv)
{
	if (qpnp_ext_charger && qpnp_ext_charger->chg_vbatdet_set) {
		return qpnp_ext_charger->chg_vbatdet_set(vbatdet_mv);
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}
#else
static int
qpnp_chg_vbatdet_set(struct qpnp_chg_chip *chip, int vbatdet_mv)
{
	u8 temp;

	if (vbatdet_mv < QPNP_CHG_VBATDET_MIN_MV
			|| vbatdet_mv > QPNP_CHG_VBATDET_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", vbatdet_mv);
		return -EINVAL;
	}
	temp = (vbatdet_mv - QPNP_CHG_VBATDET_MIN_MV)
			/ QPNP_CHG_VBATDET_STEP_MV;

	pr_debug("voltage=%d setting %02x\n", vbatdet_mv, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VBAT_DET, 1);
}
#endif

static void
qpnp_chg_set_appropriate_vbatdet(struct qpnp_chg_chip *chip)
{
	if (chip->bat_is_cool)
		qpnp_chg_vbatdet_set(chip, chip->cool_bat_mv
			- chip->resume_delta_mv);
	else if (chip->bat_is_warm)
		qpnp_chg_vbatdet_set(chip, chip->warm_bat_mv
			- chip->resume_delta_mv);
	else if (chip->resuming_charging)
		qpnp_chg_vbatdet_set(chip, chip->max_voltage_mv
			+ chip->resume_delta_mv);
	else
		qpnp_chg_vbatdet_set(chip, chip->max_voltage_mv
			- chip->resume_delta_mv);
}

#ifndef CONFIG_BQ24196_CHARGER
static void
qpnp_arb_stop_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, arb_stop_work);

	if (!chip->chg_done)
		qpnp_chg_charge_en(chip, !chip->charging_disabled);
	qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
}
#endif

static void
qpnp_bat_if_adc_measure_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, adc_measure_work);

	if (qpnp_adc_tm_channel_measure(chip->adc_tm_dev, &chip->adc_param))
		pr_err("request ADC error\n");
}

static void
qpnp_bat_if_adc_disable_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, adc_disable_work);

	qpnp_adc_tm_disable_chan_meas(chip->adc_tm_dev, &chip->adc_param);
}

#define EOC_CHECK_PERIOD_MS	10000
static irqreturn_t
qpnp_chg_vbatdet_lo_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	u8 chg_sts = 0;
	int rc;

	pr_debug("vbatdet-lo triggered\n");

	rc = qpnp_chg_read(chip, &chg_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc)
		pr_err("failed to read chg_sts rc=%d\n", rc);

	pr_debug("chg_done chg_sts: 0x%x triggered\n", chg_sts);
	if (!chip->charging_disabled && (chg_sts & FAST_CHG_ON_IRQ)) {
		schedule_delayed_work(&chip->eoc_work,
			msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		pm_stay_awake(chip->dev);
	}
	qpnp_chg_disable_irq(&chip->chg_vbatdet_lo);

#ifndef CONFIG_MACH_OPPO
	power_supply_changed(chip->usb_psy);
	pr_debug("psy changed usb_psy\n");
	if (chip->dc_chgpth_base) {
		pr_debug("psy changed dc_psy\n");
		power_supply_changed(&chip->dc_psy);
	}
	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}
#endif
	return IRQ_HANDLED;
}

#define ARB_STOP_WORK_MS	1000
static irqreturn_t
qpnp_chg_usb_chg_gone_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	u8 usb_sts;
	int rc;

	rc = qpnp_chg_read(chip, &usb_sts,
			INT_RT_STS(chip->usb_chgpth_base), 1);
	if (rc)
		pr_err("failed to read usb_chgpth_sts rc=%d\n", rc);

	pr_debug("chg_gone triggered\n");
	if ((qpnp_chg_is_usb_chg_plugged_in(chip)
			|| qpnp_chg_is_dc_chg_plugged_in(chip))
			&& (usb_sts & CHG_GONE_IRQ)) {
#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_charge_en(chip, 0);
		qpnp_chg_force_run_on_batt(chip, 1);
		schedule_delayed_work(&chip->arb_stop_work,
			msecs_to_jiffies(ARB_STOP_WORK_MS));
#endif
	}

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_usb_usb_ocp_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	pr_debug("usb-ocp triggered\n");

	schedule_work(&chip->ocp_clear_work);

	return IRQ_HANDLED;
}

#define BOOST_ILIMIT_MIN	0x07
#define BOOST_ILIMIT_DEF	0x02
#define BOOST_ILIMT_MASK	0xFF
static void
qpnp_chg_ocp_clear_work(struct work_struct *work)
{
	int rc;
	u8 usb_sts;
	struct qpnp_chg_chip *chip = container_of(work,
		struct qpnp_chg_chip, ocp_clear_work);

	if (chip->type == SMBBP) {
		rc = qpnp_chg_masked_write(chip,
				chip->boost_base + BOOST_ILIM,
				BOOST_ILIMT_MASK,
				BOOST_ILIMIT_MIN, 1);
		if (rc) {
			pr_err("Failed to turn configure ilim rc = %d\n", rc);
			return;
		}
	}

	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_OCP_CLR,
			OCP_CLR_BIT,
			OCP_CLR_BIT, 1);
	if (rc)
		pr_err("Failed to clear OCP bit rc = %d\n", rc);

	/* force usb ovp fet off */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			USB_OTG_EN_BIT, 1);
	if (rc)
		pr_err("Failed to turn off usb ovp rc = %d\n", rc);

	if (chip->type == SMBBP) {
		/* Wait for OCP circuitry to be powered up */
		msleep(100);
		rc = qpnp_chg_read(chip, &usb_sts,
				INT_RT_STS(chip->usb_chgpth_base), 1);
		if (rc) {
			pr_err("failed to read interrupt sts %d\n", rc);
			return;
		}

		if (usb_sts & COARSE_DET_USB_IRQ) {
			rc = qpnp_chg_masked_write(chip,
				chip->boost_base + BOOST_ILIM,
				BOOST_ILIMT_MASK,
				BOOST_ILIMIT_DEF, 1);
			if (rc) {
				pr_err("Failed to set ilim rc = %d\n", rc);
				return;
			}
		} else {
			pr_warn_ratelimited("USB short to GND detected!\n");
		}
	}
}

#define QPNP_CHG_VDDMAX_MIN		3400
#define QPNP_CHG_V_MIN_MV		3240
#define QPNP_CHG_V_MAX_MV		4500
#define QPNP_CHG_V_STEP_MV		10
#define QPNP_CHG_BUCK_TRIM1_STEP	10
#define QPNP_CHG_BUCK_VDD_TRIM_MASK	0xF0
#ifndef CONFIG_MACH_OPPO
static int
qpnp_chg_vddmax_and_trim_set(struct qpnp_chg_chip *chip,
		int voltage, int trim_mv)
{
	int rc, trim_set;
	u8 vddmax = 0, trim = 0;

	if (voltage < QPNP_CHG_VDDMAX_MIN
			|| voltage > QPNP_CHG_V_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}

	vddmax = (voltage - QPNP_CHG_V_MIN_MV) / QPNP_CHG_V_STEP_MV;
	rc = qpnp_chg_write(chip, &vddmax, chip->chgr_base + CHGR_VDD_MAX, 1);
	if (rc) {
		pr_err("Failed to write vddmax: %d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS,
		0xFF,
		0xA5, 1);
	if (rc) {
		pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
		return rc;
	}
	trim_set = clamp((int)chip->trim_center
			+ (trim_mv / QPNP_CHG_BUCK_TRIM1_STEP),
			0, 0xF);
	trim = (u8)trim_set << 4;
	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + BUCK_CTRL_TRIM1,
		QPNP_CHG_BUCK_VDD_TRIM_MASK,
		trim, 1);
	if (rc) {
		pr_err("Failed to write buck trim1: %d\n", rc);
		return rc;
	}
	pr_debug("voltage=%d+%d setting vddmax: %02x, trim: %02x\n",
			voltage, trim_mv, vddmax, trim);
	return 0;
}
#endif /* CONFIG_MACH_OPPO */

#ifndef CONFIG_BQ24196_CHARGER
static int
qpnp_chg_vddmax_get(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 vddmax = 0;

	rc = qpnp_chg_read(chip, &vddmax, chip->chgr_base + CHGR_VDD_MAX, 1);
	if (rc) {
		pr_err("Failed to write vddmax: %d\n", rc);
		return rc;
	}

	return QPNP_CHG_V_MIN_MV + (int)vddmax * QPNP_CHG_V_STEP_MV;
}
#endif

#ifdef CONFIG_MACH_OPPO
static int get_prop_authenticate(struct qpnp_chg_chip *);
static int
qpnp_chg_vddmax_set(struct qpnp_chg_chip *chip, int voltage)
{
	if (voltage < QPNP_CHG_VDDMAX_MIN
			|| voltage > QPNP_CHG_V_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}

	//wangjc add for authentication
#ifdef CONFIG_BATTERY_BQ27541
	if (!get_prop_authenticate(chip)) {
		if (voltage > 4200) {
			voltage = 4200;
		}
	}
#endif

	if (qpnp_ext_charger && qpnp_ext_charger->chg_vddmax_set)
		return qpnp_ext_charger->chg_vddmax_set(voltage);
	else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}

static void
qpnp_chg_set_appropriate_vddmax(struct qpnp_chg_chip *chip)
{
	if (chip->mBatteryTempRegion == CV_BATTERY_TEMP_REGION_LITTLE__COLD)
		qpnp_chg_vddmax_set(chip,4000);
	else if (chip->mBatteryTempRegion == CV_BATTERY_TEMP_REGION__COOL)
		qpnp_chg_vddmax_set(chip,chip->cool_bat_mv);
	else if (chip->mBatteryTempRegion == CV_BATTERY_TEMP_REGION__WARM)
		qpnp_chg_vddmax_set(chip,chip->warm_bat_mv);
	else
		qpnp_chg_vddmax_set(chip, chip->max_voltage_mv);
}
#else
/* JEITA compliance logic */
static void
qpnp_chg_set_appropriate_vddmax(struct qpnp_chg_chip *chip)
{
	if (chip->bat_is_cool)
		qpnp_chg_vddmax_and_trim_set(chip, chip->cool_bat_mv,
				chip->delta_vddmax_mv);
	else if (chip->bat_is_warm)
		qpnp_chg_vddmax_and_trim_set(chip, chip->warm_bat_mv,
				chip->delta_vddmax_mv);
	else
		qpnp_chg_vddmax_and_trim_set(chip, chip->max_voltage_mv,
				chip->delta_vddmax_mv);
}
#endif /* CONFIG_MACH_OPPO */

#ifdef CONFIG_BATTERY_BQ27541
static int
get_prop_authenticate(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->is_battery_authenticated)
		return qpnp_batt_gauge->is_battery_authenticated();
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}
#endif

#ifdef CONFIG_PIC1503_FASTCG
static int
get_prop_fast_chg_started(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->fast_chg_started)
		return qpnp_batt_gauge->fast_chg_started();
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}

static int
get_prop_fast_switch_to_normal(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->fast_switch_to_normal)
		return qpnp_batt_gauge->fast_switch_to_normal();
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}

static int
set_fast_switch_to_normal_false(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->set_switch_to_noraml_false)
		return qpnp_batt_gauge->set_switch_to_noraml_false();
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}

static int
qpnp_get_fast_low_temp_full(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->get_fast_low_temp_full)
		return qpnp_batt_gauge->get_fast_low_temp_full();
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}

static int
qpnp_set_fast_low_temp_full_false(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->set_low_temp_full_false)
		return qpnp_batt_gauge->set_low_temp_full_false();
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}

static int
get_prop_fast_normal_to_warm(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->fast_normal_to_warm)
		return qpnp_batt_gauge->fast_normal_to_warm();
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}

static int
set_fast_normal_to_warm_false(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->set_normal_to_warm_false)
		return qpnp_batt_gauge->set_normal_to_warm_false();
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}

static int
qpnp_set_fast_chg_allow(struct qpnp_chg_chip *chip,int enable)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->set_fast_chg_allow)
		return qpnp_batt_gauge->set_fast_chg_allow(enable);
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}

static int
qpnp_get_fast_chg_allow(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->get_fast_chg_allow)
		return qpnp_batt_gauge->get_fast_chg_allow();
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}

static int
qpnp_get_fast_chg_ing(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->get_fast_chg_ing)
		return qpnp_batt_gauge->get_fast_chg_ing();
	else {
		pr_err("qpnp-charger no batt gauge assuming false\n");
		return false;
	}
}
#else
static int
get_prop_fast_chg_started(struct qpnp_chg_chip *chip)
{
	return false;
}

static int
get_prop_fast_switch_to_normal(struct qpnp_chg_chip *chip)
{
	return false;
}

static int
get_prop_fast_normal_to_warm(struct qpnp_chg_chip *chip)
{
	return false;
}

static int
qpnp_get_fast_chg_ing(struct qpnp_chg_chip *chip)
{
	return false;
}
#endif /* CONFIG_PIC1503_FASTCG */

static void
qpnp_usbin_health_check_work(struct work_struct *work)
{
	int usbin_health = 0;
	u8 psy_health_sts = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, usbin_health_check);

	usbin_health = qpnp_chg_check_usbin_health(chip);
	spin_lock(&chip->usbin_health_monitor_lock);
	if (chip->usbin_health != usbin_health) {
		pr_debug("health_check_work: pr_usbin_health = %d, usbin_health = %d",
			chip->usbin_health, usbin_health);
		chip->usbin_health = usbin_health;
		if (usbin_health == USBIN_OVP)
			psy_health_sts = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else if (usbin_health == USBIN_OK)
			psy_health_sts = POWER_SUPPLY_HEALTH_GOOD;
		power_supply_set_health_state(chip->usb_psy, psy_health_sts);
#ifndef CONFIG_MACH_OPPO
		power_supply_changed(chip->usb_psy);
#endif
	}
	/* enable OVP monitor in usb valid after coarse-det complete */
	chip->usb_valid_check_ovp = true;
	spin_unlock(&chip->usbin_health_monitor_lock);
	return;
}

#define USB_VALID_DEBOUNCE_TIME_MASK		0x3
#define USB_DEB_BYPASS		0x0
#define USB_DEB_5MS			0x1
#define USB_DEB_10MS		0x2
#define USB_DEB_20MS		0x3
static irqreturn_t
qpnp_chg_coarse_det_usb_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int host_mode, rc = 0;
	int debounce[] = {
		[USB_DEB_BYPASS] = 0,
		[USB_DEB_5MS] = 5,
		[USB_DEB_10MS] = 10,
		[USB_DEB_20MS] = 20 };
	u8 ovp_ctl;
	bool usb_coarse_det;

	host_mode = qpnp_chg_is_otg_en_set(chip);
	usb_coarse_det = qpnp_chg_check_usb_coarse_det(chip);
	pr_debug("usb coarse-det triggered: %d host_mode: %d\n",
			usb_coarse_det, host_mode);

	if (host_mode)
		return IRQ_HANDLED;
	/* ignore to monitor OVP in usbin valid irq handler
	 if the coarse-det fired first, do the OVP state monitor
	 in the usbin_health_check work, and after the work,
	 enable monitor OVP in usbin valid irq handler */
	chip->usb_valid_check_ovp = false;
	if (chip->usb_coarse_det ^ usb_coarse_det) {
		chip->usb_coarse_det = usb_coarse_det;
		if (usb_coarse_det) {
			/* usb coarse-det rising edge, check the usbin_valid
			debounce time setting, and start a delay work to
			check the OVP status*/
			rc = qpnp_chg_read(chip, &ovp_ctl,
					chip->usb_chgpth_base + USB_OVP_CTL, 1);

			if (rc) {
				pr_err("spmi read failed: addr=%03X, rc=%d\n",
					chip->usb_chgpth_base + USB_OVP_CTL,
					rc);
				return rc;
			}
			ovp_ctl = ovp_ctl & USB_VALID_DEBOUNCE_TIME_MASK;
			schedule_delayed_work(&chip->usbin_health_check,
					msecs_to_jiffies(debounce[ovp_ctl]));
		} else {
			/* usb coarse-det rising edge, set the usb psy health
			status to unknown */
			pr_debug("usb coarse det clear, set usb health to unknown\n");
			chip->usbin_health = USBIN_UNKNOW;
			power_supply_set_health_state(chip->usb_psy,
				POWER_SUPPLY_HEALTH_UNKNOWN);
#ifndef CONFIG_MACH_OPPO
			power_supply_changed(chip->usb_psy);
#endif
		}

	}
	return IRQ_HANDLED;
}

#define BATFET_LPM_MASK		0xC0
#define BATFET_LPM		0x40
#define BATFET_NO_LPM		0x00
static int
qpnp_chg_regulator_batfet_set(struct qpnp_chg_chip *chip, bool enable)
{
	int rc = 0;

	if (chip->charging_disabled || !chip->bat_if_base)
		return rc;

	if (chip->type == SMBB)
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + CHGR_BAT_IF_SPARE,
			BATFET_LPM_MASK,
			enable ? BATFET_NO_LPM : BATFET_LPM, 1);
	else
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + CHGR_BAT_IF_BATFET_CTRL4,
			BATFET_LPM_MASK,
			enable ? BATFET_NO_LPM : BATFET_LPM, 1);

	return rc;
}

#define ENUM_T_STOP_BIT		BIT(0)
static irqreturn_t
qpnp_chg_usb_usbin_valid_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
#ifdef CONFIG_BQ24196_CHARGER
	int usb_present, host_mode;
	static bool is_work_struct_init = 0;
#else
	int usb_present, host_mode, usbin_health;
	u8 psy_health_sts;
#endif

	usb_present = qpnp_chg_is_usb_chg_plugged_in(chip);
#ifdef CONFIG_BQ24196_CHARGER
	host_mode = is_otg_en_set;
#else
	host_mode = qpnp_chg_is_otg_en_set(chip);
#endif
	pr_debug("usbin-valid triggered: %d host_mode: %d\n",
		usb_present, host_mode);

	/* In host mode notifications cmoe from USB supply */
	if (host_mode)
		return IRQ_HANDLED;

	if (chip->usb_present ^ usb_present) {
		chip->usb_present = usb_present;
		if (!usb_present) {
			/* when a valid charger inserted, and increase the
			 *  charger voltage to OVP threshold, then
			 *  usb_in_valid falling edge interrupt triggers.
			 *  So we handle the OVP monitor here, and ignore
			 *  other health state changes */
#ifndef CONFIG_BQ24196_CHARGER
			if (chip->ovp_monitor_enable &&
				       (chip->usb_valid_check_ovp)) {
				usbin_health =
					qpnp_chg_check_usbin_health(chip);
				if ((chip->usbin_health != usbin_health)
					&& (usbin_health == USBIN_OVP)) {
					chip->usbin_health = usbin_health;
					psy_health_sts =
					POWER_SUPPLY_HEALTH_OVERVOLTAGE;
					power_supply_set_health_state(
						chip->usb_psy,
						psy_health_sts);
					power_supply_changed(chip->usb_psy);
				}
			}
			if (!qpnp_chg_is_dc_chg_plugged_in(chip)) {
				chip->delta_vddmax_mv = 0;
				qpnp_chg_set_appropriate_vddmax(chip);
				chip->chg_done = false;
			}
#endif
#ifdef CONFIG_BQ24196_CHARGER
			schedule_work(&chip->stop_charge_work);
#else
			qpnp_chg_usb_suspend_enable(chip, 0);
			qpnp_chg_iusbmax_set(chip, QPNP_CHG_I_MAX_MIN_100);
#endif
			chip->prev_usb_max_ma = -EINVAL;
			chip->aicl_settled = false;
#ifdef CONFIG_MACH_OPPO
			chip->usbin_counts = 0;//sjc0522 for Find7s temp rising problem
#endif
		} else {
			/* when OVP clamped usbin, and then decrease
			 * the charger voltage to lower than the OVP
			 * threshold, a usbin_valid rising edge
			 * interrupt triggered. So we change the usb
			 * psy health state back to good */
#ifndef CONFIG_BQ24196_CHARGER
			if (chip->ovp_monitor_enable &&
				       (chip->usb_valid_check_ovp)) {
				usbin_health =
					qpnp_chg_check_usbin_health(chip);
				if ((chip->usbin_health != usbin_health)
					&& (usbin_health == USBIN_OK)) {
					chip->usbin_health = usbin_health;
					psy_health_sts =
						POWER_SUPPLY_HEALTH_GOOD;
					power_supply_set_health_state(
						chip->usb_psy,
						psy_health_sts);
					power_supply_changed(chip->usb_psy);
				}
			}

			if (!qpnp_chg_is_dc_chg_plugged_in(chip)) {
				chip->delta_vddmax_mv = 0;
				qpnp_chg_set_appropriate_vddmax(chip);
			}
#endif
			schedule_delayed_work(&chip->eoc_work,
				msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
			schedule_work(&chip->soc_check_work);
#ifdef CONFIG_MACH_OPPO
			pm_stay_awake(chip->dev);
			power_supply_changed(&chip->dc_psy);
#endif
		}

		power_supply_set_present(chip->usb_psy, chip->usb_present);
		schedule_work(&chip->batfet_lcl_work);
#ifdef CONFIG_MACH_OPPO
		if (!usb_present) {
			power_supply_set_online(chip->usb_psy, 0);
			power_supply_set_current_limit(chip->usb_psy, 0);
			power_supply_set_online(&chip->dc_psy, 0);
			power_supply_set_current_limit(&chip->dc_psy, 0);
		}
#endif
#ifdef CONFIG_BQ24196_CHARGER
		if (is_work_struct_init)
			schedule_work(&chip->ext_charger_hwinit_work);
#endif
	}

#ifdef CONFIG_BQ24196_CHARGER
	is_work_struct_init = 1;
#endif

	return IRQ_HANDLED;
}

#define TEST_EN_SMBC_LOOP		0xE5
#define IBAT_REGULATION_DISABLE		BIT(2)
static irqreturn_t
qpnp_chg_bat_if_batt_temp_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int batt_temp_good, rc;

	batt_temp_good = qpnp_chg_is_batt_temp_ok(chip);
	pr_debug("batt-temp triggered: %d\n", batt_temp_good);

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS,
		0xFF,
		0xA5, 1);
	if (rc) {
		pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + TEST_EN_SMBC_LOOP,
		IBAT_REGULATION_DISABLE,
		batt_temp_good ? 0 : IBAT_REGULATION_DISABLE, 1);
	if (rc) {
		pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
		return rc;
	}

#ifndef CONFIG_MACH_OPPO
	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
#endif
	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_bat_if_batt_pres_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int batt_present;

	batt_present = qpnp_chg_is_batt_present(chip);
	pr_debug("batt-pres triggered: %d\n", batt_present);

	if (chip->batt_present ^ batt_present) {
		if (batt_present) {
			schedule_work(&chip->insertion_ocv_work);
		} else {
			chip->insertion_ocv_uv = 0;
			qpnp_chg_charge_en(chip, 0);
		}
		chip->batt_present = batt_present;
#ifndef CONFIG_MACH_OPPO
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
		pr_debug("psy changed usb_psy\n");
		power_supply_changed(chip->usb_psy);
#endif

		if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
						&& batt_present) {
			pr_debug("enabling vadc notifications\n");
			schedule_work(&chip->adc_measure_work);
		} else if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
				&& !batt_present) {
			schedule_work(&chip->adc_disable_work);
			pr_debug("disabling vadc notifications\n");
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_dc_dcin_valid_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int dc_present;

	dc_present = qpnp_chg_is_dc_chg_plugged_in(chip);
	pr_debug("dcin-valid triggered: %d\n", dc_present);

	if (chip->dc_present ^ dc_present) {
		chip->dc_present = dc_present;
#ifndef CONFIG_BQ24196_CHARGER
		if (qpnp_chg_is_otg_en_set(chip))
			qpnp_chg_force_run_on_batt(chip, !dc_present ? 1 : 0);
#endif
		if (!dc_present && !qpnp_chg_is_usb_chg_plugged_in(chip)) {
			chip->delta_vddmax_mv = 0;
			qpnp_chg_set_appropriate_vddmax(chip);
			chip->chg_done = false;
		} else {
			if (!qpnp_chg_is_usb_chg_plugged_in(chip)) {
				chip->delta_vddmax_mv = 0;
				qpnp_chg_set_appropriate_vddmax(chip);
			}
			schedule_delayed_work(&chip->eoc_work,
				msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
			schedule_work(&chip->soc_check_work);
		}
#ifndef CONFIG_MACH_OPPO
		pr_debug("psy changed dc_psy\n");
		power_supply_changed(&chip->dc_psy);
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
#endif
		schedule_work(&chip->batfet_lcl_work);
	}

	return IRQ_HANDLED;
}

#define CHGR_CHG_FAILED_BIT	BIT(7)
static irqreturn_t
qpnp_chg_chgr_chg_failed_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int rc;

	pr_debug("chg_failed triggered\n");

	rc = qpnp_chg_masked_write(chip,
		chip->chgr_base + CHGR_CHG_FAILED,
		CHGR_CHG_FAILED_BIT,
		CHGR_CHG_FAILED_BIT, 1);
	if (rc)
		pr_err("Failed to write chg_fail clear bit!\n");

#ifndef CONFIG_MACH_OPPO
	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}
	pr_debug("psy changed usb_psy\n");
	power_supply_changed(chip->usb_psy);
	if (chip->dc_chgpth_base) {
		pr_debug("psy changed dc_psy\n");
		power_supply_changed(&chip->dc_psy);
	}
#endif
	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_chgr_chg_trklchg_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	pr_debug("TRKL IRQ triggered\n");

	chip->chg_done = false;
#ifndef CONFIG_MACH_OPPO
	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}
#endif

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_chgr_chg_fastchg_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	u8 chgr_sts;
	int rc;

	rc = qpnp_chg_read(chip, &chgr_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc)
		pr_err("failed to read interrupt sts %d\n", rc);

	pr_debug("FAST_CHG IRQ triggered\n");
	chip->chg_done = false;
#ifndef CONFIG_MACH_OPPO
	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}

	pr_debug("psy changed usb_psy\n");
	power_supply_changed(chip->usb_psy);

	if (chip->dc_chgpth_base) {
		pr_debug("psy changed dc_psy\n");
		power_supply_changed(&chip->dc_psy);
	}
#endif

	if (chip->resuming_charging) {
		chip->resuming_charging = false;
		qpnp_chg_set_appropriate_vbatdet(chip);
	}

	if (!chip->charging_disabled) {
		schedule_delayed_work(&chip->eoc_work,
			msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		pm_stay_awake(chip->dev);
	}

	qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);

	return IRQ_HANDLED;
}

static int
qpnp_dc_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}

static int
qpnp_batt_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_COOL_TEMP:
	case POWER_SUPPLY_PROP_WARM_TEMP:
	case POWER_SUPPLY_PROP_CAPACITY:
		return 1;
	default:
		break;
	}

	return 0;
}

static int
qpnp_chg_buck_control(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	if (chip->charging_disabled && enable) {
		pr_debug("Charging disabled\n");
		return 0;
	}

	rc = qpnp_chg_charge_en(chip, enable);
	if (rc) {
		pr_err("Failed to control charging %d\n", rc);
		return rc;
	}

#ifndef CONFIG_BQ24196_CHARGER
	rc = qpnp_chg_force_run_on_batt(chip, !enable);
	if (rc)
		pr_err("Failed to control charging %d\n", rc);
#endif

	return rc;
}

#ifdef CONFIG_BQ24196_CHARGER
static int
switch_usb_to_charge_mode(struct qpnp_chg_chip *chip)
{
	is_otg_en_set = false;
	if (qpnp_ext_charger && qpnp_ext_charger->chg_charge_en) {
		return qpnp_ext_charger->chg_charge_en(1);
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}
#else
static int
switch_usb_to_charge_mode(struct qpnp_chg_chip *chip)
{
	int rc;

	pr_debug("switch to charge mode\n");
	if (!qpnp_chg_is_otg_en_set(chip))
		return 0;

	if (chip->type == SMBBP) {
		rc = qpnp_chg_masked_write(chip,
			chip->boost_base + BOOST_ILIM,
			BOOST_ILIMT_MASK,
			BOOST_ILIMIT_DEF, 1);
		if (rc) {
			pr_err("Failed to set ilim rc = %d\n", rc);
			return rc;
		}
	}

	/* enable usb ovp fet */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			0, 1);
	if (rc) {
		pr_err("Failed to turn on usb ovp rc = %d\n", rc);
		return rc;
	}

	rc = qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
	if (rc) {
		pr_err("Failed re-enable charging rc = %d\n", rc);
		return rc;
	}

	return 0;
}
#endif

#ifdef CONFIG_BQ24196_CHARGER
static void qpnp_chg_ext_charger_hwinit(struct qpnp_chg_chip *chip);

static int
switch_usb_to_host_mode(struct qpnp_chg_chip *chip)
{
	is_otg_en_set = true;
	if (qpnp_ext_charger && qpnp_ext_charger->chg_charge_en) {
		qpnp_chg_ext_charger_hwinit(chip);
		return qpnp_ext_charger->chg_charge_en(2);
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
	
}
#else
static int
switch_usb_to_host_mode(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 usb_sts;

	pr_debug("switch to host mode\n");
	if (qpnp_chg_is_otg_en_set(chip))
		return 0;

	if (chip->type == SMBBP) {
		rc = qpnp_chg_masked_write(chip,
				chip->boost_base + BOOST_ILIM,
				BOOST_ILIMT_MASK,
				BOOST_ILIMIT_MIN, 1);
		if (rc) {
			pr_err("Failed to turn configure ilim rc = %d\n", rc);
			return rc;
		}
	}

	if (!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		rc = qpnp_chg_force_run_on_batt(chip, 1);
		if (rc) {
			pr_err("Failed to disable charging rc = %d\n", rc);
			return rc;
		}
	}

	/* force usb ovp fet off */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			USB_OTG_EN_BIT, 1);
	if (rc) {
		pr_err("Failed to turn off usb ovp rc = %d\n", rc);
		return rc;
	}

	if (chip->type == SMBBP) {
		/* Wait for OCP circuitry to be powered up */
		msleep(100);
		rc = qpnp_chg_read(chip, &usb_sts,
				INT_RT_STS(chip->usb_chgpth_base), 1);
		if (rc) {
			pr_err("failed to read interrupt sts %d\n", rc);
			return rc;
		}

		if (usb_sts & COARSE_DET_USB_IRQ) {
			rc = qpnp_chg_masked_write(chip,
				chip->boost_base + BOOST_ILIM,
				BOOST_ILIMT_MASK,
				BOOST_ILIMIT_DEF, 1);
			if (rc) {
				pr_err("Failed to set ilim rc = %d\n", rc);
				return rc;
			}
		} else {
			pr_warn_ratelimited("USB short to GND detected!\n");
		}
	}

	return 0;
}
#endif

static enum power_supply_property pm_power_props_mains[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static enum power_supply_property msm_batt_power_props[] = {
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_COOL_TEMP,
	POWER_SUPPLY_PROP_WARM_TEMP,
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
#ifdef CONFIG_MACH_OPPO
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_TIMEOUT,
#else
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
#endif
#ifdef CONFIG_BATTERY_BQ27541
	POWER_SUPPLY_PROP_AUTHENTICATE,
#endif
};

static char *pm_power_supplied_to[] = {
	"battery",
};

static char *pm_batt_supplied_to[] = {
	"bms",
};

static int charger_monitor;
module_param(charger_monitor, int, 0644);

static int ext_ovp_present;
module_param(ext_ovp_present, int, 0444);

#ifdef CONFIG_MACH_OPPO
static int qpnp_charger_type_get(struct qpnp_chg_chip *chip);
#define USB_WALL_THRESHOLD_MA	2000
#else
#define USB_WALL_THRESHOLD_MA	500
#endif
#define OVP_USB_WALL_THRESHOLD_MA	200
static int
qpnp_power_get_property_mains(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								dc_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		if (chip->charging_disabled)
			return 0;

#ifdef CONFIG_MACH_OPPO
		if (!qpnp_chg_is_usb_chg_plugged_in(chip))
			/* Return offline if USB is not present */
			return 0;

#ifndef CONFIG_BATTERY_BQ27541
		if (qpnp_charger_type_get(chip) == POWER_SUPPLY_TYPE_USB_DCP)
			val->intval = 1;
		else
			val->intval = 0;
#else
		if ((get_prop_fast_chg_started(chip) == true)
			|| (get_prop_fast_switch_to_normal(chip) == true)
			|| (get_prop_fast_normal_to_warm(chip) == true)) {
			val->intval = 1;
		} else {
			if (qpnp_charger_type_get(chip) == POWER_SUPPLY_TYPE_USB_DCP)
				val->intval = 1;
			else
				val->intval = 0;
		}
#endif /* CONFIG_BATTERY_BQ27541 */
#else
		val->intval = qpnp_chg_is_dc_chg_plugged_in(chip);
#endif
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = chip->maxinput_dc_ma * 1000;
		break;
#ifdef CONFIG_PIC1503_FASTCG
	case POWER_SUPPLY_PROP_FASTCHARGER:
		val->intval = get_prop_fast_chg_started(chip);
		break;
#endif
	default:
		return -EINVAL;
	}
	return 0;
}

static void
qpnp_aicl_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, aicl_check_work);
	union power_supply_propval ret = {0,};

	if (!charger_monitor && qpnp_chg_is_usb_chg_plugged_in(chip)) {
		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		if ((ret.intval / 1000) > USB_WALL_THRESHOLD_MA) {
			pr_debug("no charger_monitor present set iusbmax %d\n",
					ret.intval / 1000);
			qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
		}
	} else {
		pr_debug("charger_monitor is present\n");
	}
	chip->charger_monitor_checked = true;
}

#ifdef CONFIG_BATTERY_BQ27541
static int
get_prop_battery_voltage_now(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->get_battery_mvolts) {
		return qpnp_batt_gauge->get_battery_mvolts();
	} else {
		pr_err("qpnp-charger no batt gauge assuming 3.5V\n");
		return MSM_CHARGER_GAUGE_MISSING_VOLTS;
	}
}
#else
static int
get_prop_battery_voltage_now(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	if (chip->revision == 0 && chip->type == SMBB) {
		pr_err("vbat reading not supported for 1.0 rc=%d\n", rc);
		return 0;
	} else {
		rc = qpnp_vadc_read(chip->vadc_dev, VBAT_SNS, &results);
		if (rc) {
			pr_err("Unable to read vbat rc=%d\n", rc);
			return 0;
		}
		return results.physical;
	}
}
#endif

#ifdef CONFIG_MACH_OPPO
static int
get_prop_charger_voltage_now(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	if (use_fake_chgvol)
		return fake_chgvol;

	if (chip->revision == 0 && chip->type == SMBB) {
		pr_err("vchg reading not supported for 1.0 rc=%d\n", rc);
		return 0;
	} else {
		rc = qpnp_vadc_read(chip->vadc_dev, USBIN, &results);
		if (rc) {
			pr_err("Unable to read vchg rc=%d\n", rc);
			return 0;
		}
		return (int)results.physical/1000;
	}
}
#endif

#define BATT_PRES_BIT BIT(7)
static int
get_prop_batt_present(struct qpnp_chg_chip *chip)
{
	u8 batt_present;
	int rc;

	rc = qpnp_chg_read(chip, &batt_present,
				chip->bat_if_base + CHGR_BAT_IF_PRES_STATUS, 1);
	if (rc) {
		pr_err("Couldn't read battery status read failed rc=%d\n", rc);
		return 0;
	};
	return (batt_present & BATT_PRES_BIT) ? 1 : 0;
}

#ifdef CONFIG_MACH_OPPO
static chg_cv_battery_temp_region_type qpnp_battery_temp_region_get(struct qpnp_chg_chip *chip)
{
	return chip->mBatteryTempRegion;
}

static void qpnp_battery_temp_region_set(struct qpnp_chg_chip *chip,
		chg_cv_battery_temp_region_type batt_temp_region)
{
	chip->mBatteryTempRegion = batt_temp_region;
}

static enum chg_battery_status_type qpnp_battery_status_get(struct qpnp_chg_chip *chip);
static void qpnp_check_charger_uovp(struct qpnp_chg_chip *chip);
static int qpnp_chg_ibatmax_set(struct qpnp_chg_chip *chip, int chg_current);
static int get_prop_batt_temp(struct qpnp_chg_chip *chip);
#endif

#define BATT_TEMP_HOT	BIT(6)
#define BATT_TEMP_OK	BIT(7)
#ifdef CONFIG_MACH_OPPO
static int
get_prop_batt_health(struct qpnp_chg_chip *chip)
{
	int temp;

	temp = get_prop_batt_temp(chip);
	if (temp == AUTO_CHARGING_BATT_REMOVE_TEMP) {
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	} else if (qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__HOT) {
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	} else if (qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__COLD) {
		return POWER_SUPPLY_HEALTH_COLD;
	} else if (qpnp_battery_status_get(chip) == BATTERY_STATUS_BAD) {
		return POWER_SUPPLY_HEALTH_DEAD;
	} else if (chip->charger_status == CHARGER_STATUS_OVER) {
		if (get_prop_fast_chg_started(chip) == true) {
			return POWER_SUPPLY_HEALTH_GOOD;
		}
		return POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else {
		return POWER_SUPPLY_HEALTH_GOOD;
	}
}
#else
static int
get_prop_batt_health(struct qpnp_chg_chip *chip)
{
	u8 batt_health;
	int rc;

	rc = qpnp_chg_read(chip, &batt_health,
				chip->bat_if_base + CHGR_STATUS, 1);
	if (rc) {
		pr_err("Couldn't read battery health read failed rc=%d\n", rc);
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	};

	if (BATT_TEMP_OK & batt_health)
		return POWER_SUPPLY_HEALTH_GOOD;
	if (BATT_TEMP_HOT & batt_health)
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	else
		return POWER_SUPPLY_HEALTH_COLD;
}
#endif

#ifdef CONFIG_BQ24196_CHARGER
static int
get_prop_charge_type(struct qpnp_chg_chip *chip)
{
	int status;
	
	if (!get_prop_batt_present(chip))
		return POWER_SUPPLY_CHARGE_TYPE_NONE;

	if (qpnp_ext_charger && qpnp_ext_charger->chg_get_system_status) {
		status = qpnp_ext_charger->chg_get_system_status();
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}

	if ((status & 0x30) == 0x10) {
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	} else if((status & 0x30) == 0x20) {
		return POWER_SUPPLY_CHARGE_TYPE_FAST;
	} else if((status & 0x30) == 0x30) {
		return POWER_SUPPLY_CHARGE_TYPE_TERMINATE;
	} else {
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}
}
#else
static int
get_prop_charge_type(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 chgr_sts;

	if (!get_prop_batt_present(chip))
		return POWER_SUPPLY_CHARGE_TYPE_NONE;

	rc = qpnp_chg_read(chip, &chgr_sts,
				INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	if (chgr_sts & TRKL_CHG_ON_IRQ)
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	if (chgr_sts & FAST_CHG_ON_IRQ)
		return POWER_SUPPLY_CHARGE_TYPE_FAST;

	return POWER_SUPPLY_CHARGE_TYPE_NONE;
}
#endif

#define DEFAULT_CAPACITY	50
#ifndef CONFIG_BQ24196_CHARGER
static int
get_batt_capacity(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->fake_battery_soc >= 0)
		return chip->fake_battery_soc;
	if (chip->use_default_batt_values || !get_prop_batt_present(chip))
		return DEFAULT_CAPACITY;
	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CAPACITY, &ret);
		return ret.intval;
	}
	return DEFAULT_CAPACITY;
}
#endif

#ifdef CONFIG_BQ24196_CHARGER
static int
get_prop_batt_status(struct qpnp_chg_chip *chip)
{
	int status;

#ifdef CONFIG_PIC1503_FASTCG
	if (get_prop_fast_chg_started(chip) == true)
		return POWER_SUPPLY_STATUS_CHARGING;
#endif

	if (qpnp_chg_is_usb_chg_plugged_in(chip) &&
			chip->chg_display_full) {
		return POWER_SUPPLY_STATUS_FULL;
	}

	if (get_prop_batt_temp(chip) <= AUTO_CHARGING_BATT_REMOVE_TEMP) {
		return POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (qpnp_ext_charger && qpnp_ext_charger->chg_get_system_status) {
		status = qpnp_ext_charger->chg_get_system_status();
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}

	if ((status & 0x30) == 0x10) {
		return POWER_SUPPLY_STATUS_CHARGING;
	} else if ((status & 0x30) == 0x20) {
		return POWER_SUPPLY_STATUS_CHARGING;
	} else if ((status & 0x30) == 0x30) {
		return POWER_SUPPLY_STATUS_CHARGING;
	} else {
		return POWER_SUPPLY_STATUS_DISCHARGING;
	}
}
#else
static int
get_prop_batt_status(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 chgr_sts, bat_if_sts;

	if ((qpnp_chg_is_usb_chg_plugged_in(chip) ||
		qpnp_chg_is_dc_chg_plugged_in(chip)) && chip->chg_done) {
		return POWER_SUPPLY_STATUS_FULL;
	}

	rc = qpnp_chg_read(chip, &chgr_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	rc = qpnp_chg_read(chip, &bat_if_sts, INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("failed to read bat_if sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	if ((chgr_sts & TRKL_CHG_ON_IRQ) && !(bat_if_sts & BAT_FET_ON_IRQ))
		return POWER_SUPPLY_STATUS_CHARGING;
	if (chgr_sts & FAST_CHG_ON_IRQ && bat_if_sts & BAT_FET_ON_IRQ)
		return POWER_SUPPLY_STATUS_CHARGING;

	/* report full if state of charge is 100 and a charger is connected */
	if ((qpnp_chg_is_usb_chg_plugged_in(chip) ||
		qpnp_chg_is_dc_chg_plugged_in(chip))
			&& get_batt_capacity(chip) == 100) {
		return POWER_SUPPLY_STATUS_FULL;
	}

	return POWER_SUPPLY_STATUS_DISCHARGING;
}
#endif

#ifdef CONFIG_BATTERY_BQ27541
static int
get_prop_current_now(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->get_average_current) {
		return qpnp_batt_gauge->get_average_current();
	} else {
		pr_err("qpnp-charger no batt gauge assuming 0mA\n");
		return 0;
	}
}
#else
static int
get_prop_current_now(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CURRENT_NOW, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}
#endif

static int
get_prop_full_design(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

static int
get_prop_charge_full(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CHARGE_FULL, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

#ifdef CONFIG_BATTERY_BQ27541
static int
get_prop_capacity(struct qpnp_chg_chip *chip)
{
	if (qpnp_batt_gauge && qpnp_batt_gauge->get_battery_soc) {
		return qpnp_batt_gauge->get_battery_soc();
	} else {
		pr_err("qpnp-charger no batt gauge assuming 50percent\n");
		return DEFAULT_CAPACITY;
	}
}
#else
static int
get_prop_capacity(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};
	int battery_status, bms_status, soc, charger_in;

	if (chip->fake_battery_soc >= 0)
		return chip->fake_battery_soc;

	if (chip->use_default_batt_values || !get_prop_batt_present(chip))
		return DEFAULT_CAPACITY;

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CAPACITY, &ret);
		soc = ret.intval;
		battery_status = get_prop_batt_status(chip);
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_STATUS, &ret);
		bms_status = ret.intval;
		charger_in = qpnp_chg_is_usb_chg_plugged_in(chip) ||
			qpnp_chg_is_dc_chg_plugged_in(chip);

		if (battery_status != POWER_SUPPLY_STATUS_CHARGING
				&& bms_status != POWER_SUPPLY_STATUS_CHARGING
				&& charger_in
				&& !chip->bat_is_cool
				&& !chip->bat_is_warm
				&& !chip->resuming_charging
				&& !chip->charging_disabled
				&& chip->soc_resume_limit
				&& soc <= chip->soc_resume_limit) {
			pr_debug("resuming charging at %d%% soc\n", soc);
			chip->resuming_charging = true;
			qpnp_chg_set_appropriate_vbatdet(chip);
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
		}
		if (soc == 0) {
			if (!qpnp_chg_is_usb_chg_plugged_in(chip)
				&& !qpnp_chg_is_usb_chg_plugged_in(chip))
				pr_warn_ratelimited("Battery 0, CHG absent\n");
		}
		return soc;
	} else {
		pr_debug("No BMS supply registered return 50\n");
	}

	/* return default capacity to avoid userspace
	 * from shutting down unecessarily */
	return DEFAULT_CAPACITY;
}
#endif

#define DEFAULT_TEMP		250
#define MAX_TOLERABLE_BATT_TEMP_DDC	680
#ifdef CONFIG_BATTERY_BQ27541
static int
get_prop_batt_temp(struct qpnp_chg_chip *chip)
{
	if (use_fake_temp)
		return fake_temp;
		
	if (qpnp_batt_gauge && qpnp_batt_gauge->get_battery_temperature) {
		return qpnp_batt_gauge->get_battery_temperature();
	} else {
		pr_err("qpnp-charger no batt gauge assuming 35 deg G\n");
		return AUTO_CHARGING_BATT_REMOVE_TEMP;
	}
}
#else
static int
get_prop_batt_temp(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	if (chip->use_default_batt_values || !get_prop_batt_present(chip))
		return DEFAULT_TEMP;

	rc = qpnp_vadc_read(chip->vadc_dev, LR_MUX1_BATT_THERM, &results);
	if (rc) {
		pr_debug("Unable to read batt temperature rc=%d\n", rc);
		return 0;
	}
	pr_debug("get_bat_temp %d %lld\n",
		results.adc_code, results.physical);

	return (int)results.physical;
}
#endif

static int get_prop_cycle_count(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy)
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CYCLE_COUNT, &ret);
	return ret.intval;
}

static int get_prop_vchg_loop(struct qpnp_chg_chip *chip)
{
	u8 buck_sts;
	int rc;

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->buck_base), rc);
		return rc;
	}
	pr_debug("buck usb sts 0x%x\n", buck_sts);

	return (buck_sts & VCHG_LOOP_IRQ) ? 1 : 0;
}

static int get_prop_online(struct qpnp_chg_chip *chip)
{
	return qpnp_chg_is_batfet_closed(chip);
}

#ifdef CONFIG_MACH_OPPO
static int qpnp_start_charging(struct qpnp_chg_chip *chip);
#endif
#ifdef CONFIG_PIC1503_FASTCG
static void switch_fast_chg(struct qpnp_chg_chip *chip);
#endif

static void
qpnp_batt_external_power_changed(struct power_supply *psy)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);
	union power_supply_propval ret = {0,};

#ifdef CONFIG_BQ24196_CHARGER
	if (chip->chg_done){
		pr_info("%s chg done\n",__func__);
		return ;
	}
#endif

	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("bms");

	chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_ONLINE, &ret);

	/* Only honour requests while USB is present */
	if (qpnp_chg_is_usb_chg_plugged_in(chip)) {
		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);

		if (chip->prev_usb_max_ma == ret.intval)
			goto skip_set_iusb_max;

		chip->prev_usb_max_ma = ret.intval;

		if (ret.intval <= 2 && !chip->use_default_batt_values &&
						get_prop_batt_present(chip)) {
#ifdef CONFIG_BQ24196_CHARGER
			qpnp_chg_charge_en(chip, 0);
#else
			if (ret.intval ==  2)
				qpnp_chg_usb_suspend_enable(chip, 1);
#endif
#ifndef CONFIG_MACH_OPPO
			qpnp_chg_iusbmax_set(chip, QPNP_CHG_I_MAX_MIN_100);
#endif
#ifdef CONFIG_MACH_OPPO
			qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__INVALID);
			chip->charger_status = CHARGER_STATUS_GOOD;
			chip->aicl_current = 0;
#endif
		} else {
#ifdef CONFIG_MACH_OPPO
			qpnp_check_charger_uovp(chip);
			if (qpnp_battery_temp_region_get(chip) != CV_BATTERY_TEMP_REGION__HOT &&
				qpnp_battery_temp_region_get(chip) != CV_BATTERY_TEMP_REGION__COLD &&
				chip->charger_status != CHARGER_STATUS_OVER &&
				qpnp_battery_status_get(chip) != BATTERY_STATUS_BAD) {
				qpnp_chg_usb_suspend_enable(chip, 0);
				qpnp_chg_charge_en(chip, !chip->charging_disabled);
				power_supply_changed(&chip->batt_psy);
				if (((ret.intval / 1000) > USB_WALL_THRESHOLD_MA)
						&& (charger_monitor ||
						!chip->charger_monitor_checked)) {
					if (!ext_ovp_present) {
						qpnp_chg_iusbmax_set(chip,
							USB_WALL_THRESHOLD_MA);
#ifdef CONFIG_BQ24196_CHARGER
						qpnp_chg_ibatmax_set(chip, USB_WALL_THRESHOLD_MA);
#endif
					} else {
						qpnp_chg_iusbmax_set(chip,
							OVP_USB_WALL_THRESHOLD_MA);
#ifdef CONFIG_BQ24196_CHARGER
						qpnp_chg_ibatmax_set(chip, OVP_USB_WALL_THRESHOLD_MA);
#endif
					}
				} else {
					if (get_prop_fast_chg_started(chip) == false) {
						qpnp_start_charging(chip);
					}
				}
#ifndef CONFIG_BQ24196_CHARGER
				if ((chip->flags & POWER_STAGE_WA)
						&& ((ret.intval / 1000) > USB_WALL_THRESHOLD_MA)
						&& !chip->power_stage_workaround_running
						&& chip->power_stage_workaround_enable) {
					chip->power_stage_workaround_running = true;
					pr_debug("usb wall chg inserted starting power stage workaround charger_monitor = %d\n",
							charger_monitor);
					schedule_work(&chip->reduce_power_stage_work);
				}
#endif
				power_supply_changed(&chip->batt_psy);
			}
#else
			qpnp_chg_usb_suspend_enable(chip, 0);
			if (((ret.intval / 1000) > USB_WALL_THRESHOLD_MA)
					&& (charger_monitor ||
					!chip->charger_monitor_checked)) {
				if (!ext_ovp_present)
					qpnp_chg_iusbmax_set(chip,
						USB_WALL_THRESHOLD_MA);
				else
					qpnp_chg_iusbmax_set(chip,
						OVP_USB_WALL_THRESHOLD_MA);
			} else {
				qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
			}

#ifndef CONFIG_BQ24196_CHARGER
			if ((chip->flags & POWER_STAGE_WA)
			&& ((ret.intval / 1000) > USB_WALL_THRESHOLD_MA)
			&& !chip->power_stage_workaround_running
			&& chip->power_stage_workaround_enable) {
				chip->power_stage_workaround_running = true;
				pr_debug("usb wall chg inserted starting power stage workaround charger_monitor = %d\n",
						charger_monitor);
				schedule_work(&chip->reduce_power_stage_work);
			}
#endif
#endif /* CONFIG_MACH_OPPO */
		}
	}

skip_set_iusb_max:
	pr_debug("end of power supply changed\n");
	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
}

static int
qpnp_batt_power_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = get_prop_batt_status(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = get_prop_charge_type(chip);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = get_prop_batt_health(chip);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = get_prop_batt_present(chip);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = chip->max_voltage_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = chip->min_voltage_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = get_prop_battery_voltage_now(chip);
		break;
#ifndef CONFIG_MACH_OPPO
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		val->intval = chip->insertion_ocv_uv;
		break;
#endif
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = get_prop_batt_temp(chip);
		break;
	case POWER_SUPPLY_PROP_COOL_TEMP:
		val->intval = chip->cool_bat_decidegc;
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		val->intval = chip->warm_bat_decidegc;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_prop_capacity(chip);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = get_prop_current_now(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = get_prop_full_design(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = get_prop_charge_full(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = !(chip->charging_disabled);
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		val->intval = chip->therm_lvl_sel;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = get_prop_cycle_count(chip);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		val->intval = get_prop_vchg_loop(chip);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		val->intval = qpnp_chg_usb_iusbmax_get(chip) * 1000;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM:
		val->intval = qpnp_chg_iusb_trim_get(chip);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		val->intval = chip->aicl_settled;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = qpnp_chg_vinmin_get(chip) * 1000;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = get_prop_online(chip);
		break;
#ifdef CONFIG_MACH_OPPO
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = get_prop_charger_voltage_now(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TIMEOUT:
		val->intval = (int)chip->time_out;
		break;
#endif
#ifdef CONFIG_BATTERY_BQ27541
	case POWER_SUPPLY_PROP_AUTHENTICATE:
		val->intval = get_prop_authenticate(chip);
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

#define BTC_CONFIG_ENABLED	BIT(7)
#define BTC_COLD		BIT(1)
#define BTC_HOT			BIT(0)
static int
qpnp_chg_bat_if_configure_btc(struct qpnp_chg_chip *chip)
{
	u8 btc_cfg = 0, mask = 0;

	/* Do nothing if battery peripheral not present */
	if (!chip->bat_if_base)
		return 0;

	if ((chip->hot_batt_p == HOT_THD_25_PCT)
			|| (chip->hot_batt_p == HOT_THD_35_PCT)) {
		btc_cfg |= btc_value[chip->hot_batt_p];
		mask |= BTC_HOT;
	}

	if ((chip->cold_batt_p == COLD_THD_70_PCT) ||
			(chip->cold_batt_p == COLD_THD_80_PCT)) {
		btc_cfg |= btc_value[chip->cold_batt_p];
		mask |= BTC_COLD;
	}

	if (chip->btc_disabled)
		mask |= BTC_CONFIG_ENABLED;

	return qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_BTC_CTRL,
			mask, btc_cfg, 1);
}

#define QPNP_CHG_IBATSAFE_MIN_MA		100
#define QPNP_CHG_IBATSAFE_MAX_MA		3250
#define QPNP_CHG_I_STEP_MA		50
#define QPNP_CHG_I_MIN_MA		100
#define QPNP_CHG_I_MASK			0x3F
static int
qpnp_chg_ibatsafe_set(struct qpnp_chg_chip *chip, int safe_current)
{
	u8 temp;

	if (safe_current < QPNP_CHG_IBATSAFE_MIN_MA
			|| safe_current > QPNP_CHG_IBATSAFE_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", safe_current);
		return -EINVAL;
	}

	temp = safe_current / QPNP_CHG_I_STEP_MA;
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_IBAT_SAFE,
			QPNP_CHG_I_MASK, temp, 1);
}

#define QPNP_CHG_ITERM_MIN_MA		100
#define QPNP_CHG_ITERM_MAX_MA		250
#define QPNP_CHG_ITERM_STEP_MA		50
#define QPNP_CHG_ITERM_MASK			0x03
#ifdef CONFIG_BQ24196_CHARGER
static int
qpnp_chg_ibatterm_set(struct qpnp_chg_chip *chip, int term_current)
{
	if (qpnp_ext_charger && qpnp_ext_charger->chg_ibatterm_set) {
		return qpnp_ext_charger->chg_ibatterm_set(term_current);
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}
#else
static int
qpnp_chg_ibatterm_set(struct qpnp_chg_chip *chip, int term_current)
{
	u8 temp;

	if (term_current < QPNP_CHG_ITERM_MIN_MA
			|| term_current > QPNP_CHG_ITERM_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", term_current);
		return -EINVAL;
	}

	temp = (term_current - QPNP_CHG_ITERM_MIN_MA)
				/ QPNP_CHG_ITERM_STEP_MA;
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_IBAT_TERM_CHGR,
			QPNP_CHG_ITERM_MASK, temp, 1);
}
#endif

#define QPNP_CHG_IBATMAX_MIN	50
#define QPNP_CHG_IBATMAX_MAX	3250
#ifdef CONFIG_BQ24196_CHARGER
static int
qpnp_chg_ibatmax_set(struct qpnp_chg_chip *chip, int chg_current)
{
#ifdef CONFIG_BATTERY_BQ27541
	if (!get_prop_authenticate(chip)) {
		if (chg_current > 500) {
			chg_current = 500;
		}
	}
#endif

	if (qpnp_ext_charger && qpnp_ext_charger->chg_ibatmax_set) {
		return qpnp_ext_charger->chg_ibatmax_set(chg_current);
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}
#else
static int
qpnp_chg_ibatmax_set(struct qpnp_chg_chip *chip, int chg_current)
{
	u8 temp;

	if (chg_current < QPNP_CHG_IBATMAX_MIN
			|| chg_current > QPNP_CHG_IBATMAX_MAX) {
		pr_err("bad mA=%d asked to set\n", chg_current);
		return -EINVAL;
	}
	temp = chg_current / QPNP_CHG_I_STEP_MA;
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_IBAT_MAX,
			QPNP_CHG_I_MASK, temp, 1);
}
#endif

static int
qpnp_chg_ibatmax_get(struct qpnp_chg_chip *chip, int *chg_current)
{
	int rc;
	u8 temp;

	*chg_current = 0;
	rc = qpnp_chg_read(chip, &temp, chip->chgr_base + CHGR_IBAT_MAX, 1);
	if (rc) {
		pr_err("failed read ibat_max rc=%d\n", rc);
		return rc;
	}

	*chg_current = ((temp & QPNP_CHG_I_MASK) * QPNP_CHG_I_STEP_MA);

	return 0;
}

#define QPNP_CHG_TCHG_MASK	0x7F
#define QPNP_CHG_TCHG_EN_MASK	0x80
#define QPNP_CHG_TCHG_MIN	4
#define QPNP_CHG_TCHG_MAX	512
#define QPNP_CHG_TCHG_STEP	4
#ifdef CONFIG_BQ24196_CHARGER
static int qpnp_chg_tchg_max_set(struct qpnp_chg_chip *chip, int minutes)
{
	if (qpnp_ext_charger && qpnp_ext_charger->check_charge_timeout) {
		return qpnp_ext_charger->check_charge_timeout(minutes);
	} else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}
#else
static int qpnp_chg_tchg_max_set(struct qpnp_chg_chip *chip, int minutes)
{
	u8 temp;
	int rc;

	if (minutes < QPNP_CHG_TCHG_MIN || minutes > QPNP_CHG_TCHG_MAX) {
		pr_err("bad max minutes =%d asked to set\n", minutes);
		return -EINVAL;
	}

	rc = qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_TCHG_MAX_EN,
			QPNP_CHG_TCHG_EN_MASK, 0, 1);
	if (rc) {
		pr_err("failed write tchg_max_en rc=%d\n", rc);
		return rc;
	}

	temp = minutes / QPNP_CHG_TCHG_STEP - 1;

	rc = qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_TCHG_MAX,
			QPNP_CHG_TCHG_MASK, temp, 1);
	if (rc) {
		pr_err("failed write tchg_max_en rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_TCHG_MAX_EN,
			QPNP_CHG_TCHG_EN_MASK, QPNP_CHG_TCHG_EN_MASK, 1);
	if (rc) {
		pr_err("failed write tchg_max_en rc=%d\n", rc);
		return rc;
	}

	return 0;
}
#endif

static int
qpnp_chg_vddsafe_set(struct qpnp_chg_chip *chip, int voltage)
{
	u8 temp;

	if (voltage < QPNP_CHG_V_MIN_MV
			|| voltage > QPNP_CHG_V_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	temp = (voltage - QPNP_CHG_V_MIN_MV) / QPNP_CHG_V_STEP_MV;
	pr_debug("voltage=%d setting %02x\n", voltage, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VDD_SAFE, 1);
}

#define IBAT_TRIM_TGT_MA		500
#define IBAT_TRIM_OFFSET_MASK		0x7F
#define IBAT_TRIM_GOOD_BIT		BIT(7)
#define IBAT_TRIM_LOW_LIM		20
#define IBAT_TRIM_HIGH_LIM		114
#define IBAT_TRIM_MEAN			64

static void
qpnp_chg_trim_ibat(struct qpnp_chg_chip *chip, u8 ibat_trim)
{
	int ibat_now_ma, ibat_diff_ma, rc;
	struct qpnp_iadc_result i_result;
	enum qpnp_iadc_channels iadc_channel;

	iadc_channel = chip->use_external_rsense ?
				EXTERNAL_RSENSE : INTERNAL_RSENSE;
	rc = qpnp_iadc_read(chip->iadc_dev, iadc_channel, &i_result);
	if (rc) {
		pr_err("Unable to read bat rc=%d\n", rc);
		return;
	}

	ibat_now_ma = i_result.result_ua / 1000;

	if (qpnp_chg_is_ibat_loop_active(chip)) {
		ibat_diff_ma = ibat_now_ma - IBAT_TRIM_TGT_MA;

		if (abs(ibat_diff_ma) > 50) {
			ibat_trim += (ibat_diff_ma / 20);
			ibat_trim &= IBAT_TRIM_OFFSET_MASK;
			/* reject new ibat_trim if it is outside limits */
			if (!is_within_range(ibat_trim, IBAT_TRIM_LOW_LIM,
						IBAT_TRIM_HIGH_LIM))
				return;
		}
		ibat_trim |= IBAT_TRIM_GOOD_BIT;
		rc = qpnp_chg_write(chip, &ibat_trim,
				chip->buck_base + BUCK_CTRL_TRIM3, 1);
		if (rc)
			pr_err("failed to set IBAT_TRIM rc=%d\n", rc);

		pr_debug("ibat_now=%dmA, itgt=%dmA, ibat_diff=%dmA, ibat_trim=%x\n",
					ibat_now_ma, IBAT_TRIM_TGT_MA,
					ibat_diff_ma, ibat_trim);
	} else {
		pr_debug("ibat loop not active - cannot calibrate ibat\n");
	}
}

static int
qpnp_chg_input_current_settled(struct qpnp_chg_chip *chip)
{
	int rc, ibat_max_ma;
	u8 reg, chgr_sts, ibat_trim, i;

	chip->aicl_settled = true;

	/*
	 * Perform the ibat calibration.
	 * This is for devices which have a IBAT_TRIM error
	 * which can show IBAT_MAX out of spec.
	 */
	if (!chip->ibat_calibration_enabled)
		return 0;

	if (chip->type != SMBB)
		return 0;

	rc = qpnp_chg_read(chip, &reg,
			chip->buck_base + BUCK_CTRL_TRIM3, 1);
	if (rc) {
		pr_err("failed to read BUCK_CTRL_TRIM3 rc=%d\n", rc);
		return rc;
	}
	if (reg & IBAT_TRIM_GOOD_BIT) {
		pr_debug("IBAT_TRIM_GOOD bit already set. Quitting!\n");
		return 0;
	}
	ibat_trim = reg & IBAT_TRIM_OFFSET_MASK;

	if (!is_within_range(ibat_trim, IBAT_TRIM_LOW_LIM,
					IBAT_TRIM_HIGH_LIM)) {
		pr_debug("Improper ibat_trim value=%x setting to value=%x\n",
						ibat_trim, IBAT_TRIM_MEAN);
		ibat_trim = IBAT_TRIM_MEAN;
		rc = qpnp_chg_masked_write(chip,
				chip->buck_base + BUCK_CTRL_TRIM3,
				IBAT_TRIM_OFFSET_MASK, ibat_trim, 1);
		if (rc) {
			pr_err("failed to set ibat_trim to %x rc=%d\n",
						IBAT_TRIM_MEAN, rc);
			return rc;
		}
	}

	rc = qpnp_chg_read(chip, &chgr_sts,
				INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts rc=%d\n", rc);
		return rc;
	}
	if (!(chgr_sts & FAST_CHG_ON_IRQ)) {
		pr_debug("Not in fastchg\n");
		return rc;
	}

	/* save the ibat_max to restore it later */
	rc = qpnp_chg_ibatmax_get(chip, &ibat_max_ma);
	if (rc) {
		pr_debug("failed to save ibatmax rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_ibatmax_set(chip, IBAT_TRIM_TGT_MA);
	if (rc) {
		pr_err("failed to set ibatmax rc=%d\n", rc);
		return rc;
	}

	for (i = 0; i < 3; i++) {
		/*
		 * ibat settling delay - to make sure the BMS controller
		 * has sufficient time to sample ibat for the configured
		 * ibat_max
		 */
		msleep(20);
		if (qpnp_chg_is_ibat_loop_active(chip))
			qpnp_chg_trim_ibat(chip, ibat_trim);
		else
			pr_debug("ibat loop not active\n");

		/* read the adjusted ibat_trim for further adjustments */
		rc = qpnp_chg_read(chip, &ibat_trim,
			chip->buck_base + BUCK_CTRL_TRIM3, 1);
		if (rc) {
			pr_err("failed to read BUCK_CTRL_TRIM3 rc=%d\n", rc);
			break;
		}
	}

	/* restore IBATMAX */
	rc = qpnp_chg_ibatmax_set(chip, ibat_max_ma);
	if (rc)
		pr_err("failed to restore ibatmax rc=%d\n", rc);

	return rc;
}

#ifdef CONFIG_BQ24196_CHARGER
static int
qpnp_chg_ext_charger_reset(struct qpnp_chg_chip *chip,int reset)
{
	pr_info("%s reset:%d\n",__func__,reset);
	if (qpnp_ext_charger && qpnp_ext_charger->chg_regs_reset)
		return qpnp_ext_charger->chg_regs_reset(reset);
	else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}

static int
qpnp_chg_ext_charger_wdt_set(struct qpnp_chg_chip *chip,int seconds)
{
	pr_info("%s seconds:%d\n",__func__,seconds);
	if (qpnp_ext_charger && qpnp_ext_charger->chg_wdt_set)
		return qpnp_ext_charger->chg_wdt_set(seconds);
	else {
		pr_err("qpnp-charger no externel charger\n");
		return -ENODEV;
	}
}

static void qpnp_chg_ext_charger_hwinit(struct qpnp_chg_chip *chip)
{
#ifdef CONFIG_PIC1503_FASTCG
	if (get_prop_fast_chg_started(chip)) {
		pr_info("%s fast chg started,don't init bq24196\n", __func__);
		return;
	}
#endif

	qpnp_chg_ext_charger_reset(chip, 1);
	qpnp_chg_ext_charger_reset(chip, 0); //reset bq24196 regs to default
	qpnp_chg_ext_charger_wdt_set(chip, 0); //disable wdt

#ifdef CONFIG_MACH_OPPO
	qpnp_chg_charge_en(chip, 0);
	qpnp_chg_vddmax_set(chip, chip->max_voltage_mv);
#endif
	qpnp_chg_vinmin_set(chip, chip->min_voltage_mv);
	qpnp_chg_ibatterm_set(chip, chip->term_current);
	qpnp_chg_tchg_max_set(chip, chip->tchg_mins);
}

static void
qpnp_chg_ext_charger_hwinit_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip,ext_charger_hwinit_work);

	qpnp_chg_ext_charger_hwinit(chip);
}
#endif /* CONFIG_BQ24196_CHARGER */

#define BOOST_MIN_UV	4200000
#define BOOST_MAX_UV	5500000
#define BOOST_STEP_UV	50000
#define BOOST_MIN	16
#define N_BOOST_V	((BOOST_MAX_UV - BOOST_MIN_UV) / BOOST_STEP_UV + 1)
static int
qpnp_boost_vset(struct qpnp_chg_chip *chip, int voltage)
{
	u8 reg = 0;

	if (voltage < BOOST_MIN_UV || voltage > BOOST_MAX_UV) {
		pr_err("invalid voltage requested %d uV\n", voltage);
		return -EINVAL;
	}

	reg = DIV_ROUND_UP(voltage - BOOST_MIN_UV, BOOST_STEP_UV) + BOOST_MIN;

	pr_debug("voltage=%d setting %02x\n", voltage, reg);
	return qpnp_chg_write(chip, &reg, chip->boost_base + BOOST_VSET, 1);
}

static int
qpnp_boost_vget_uv(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 boost_reg;

	rc = qpnp_chg_read(chip, &boost_reg,
		 chip->boost_base + BOOST_VSET, 1);
	if (rc) {
		pr_err("failed to read BOOST_VSET rc=%d\n", rc);
		return rc;
	}

	if (boost_reg < BOOST_MIN) {
		pr_err("Invalid reading from 0x%x\n", boost_reg);
		return -EINVAL;
	}

	return BOOST_MIN_UV + ((boost_reg - BOOST_MIN) * BOOST_STEP_UV);
}

static void
qpnp_chg_set_appropriate_battery_current(struct qpnp_chg_chip *chip)
{
	unsigned int chg_current = chip->max_bat_chg_current;

	if (chip->bat_is_cool)
		chg_current = min(chg_current, chip->cool_bat_chg_ma);

	if (chip->bat_is_warm)
		chg_current = min(chg_current, chip->warm_bat_chg_ma);

	if (chip->therm_lvl_sel != 0 && chip->thermal_mitigation)
		chg_current = min(chg_current,
			chip->thermal_mitigation[chip->therm_lvl_sel]);

	pr_debug("setting %d mA\n", chg_current);
	qpnp_chg_ibatmax_set(chip, chg_current);
}

static void
qpnp_batt_system_temp_level_set(struct qpnp_chg_chip *chip, int lvl_sel)
{
	if (lvl_sel >= 0 && lvl_sel < chip->thermal_levels) {
		chip->therm_lvl_sel = lvl_sel;
		if (lvl_sel == (chip->thermal_levels - 1)) {
			/* disable charging if highest value selected */
			qpnp_chg_buck_control(chip, 0);
		} else {
			qpnp_chg_buck_control(chip, 1);
			qpnp_chg_set_appropriate_battery_current(chip);
		}
	} else {
		pr_err("Unsupported level selected %d\n", lvl_sel);
	}
}

/* OTG regulator operations */
static int
qpnp_chg_regulator_otg_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return switch_usb_to_host_mode(chip);
}

static int
qpnp_chg_regulator_otg_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return switch_usb_to_charge_mode(chip);
}

static int
qpnp_chg_regulator_otg_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_chg_is_otg_en_set(chip);
}

static int
qpnp_chg_regulator_boost_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;

	if (qpnp_chg_is_usb_chg_plugged_in(chip) &&
			(chip->flags & BOOST_FLASH_WA)) {
#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_usb_suspend_enable(chip, 1);
#endif

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + COMP_OVR1,
			0xFF,
			0x2F, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}
	}

	return qpnp_chg_masked_write(chip,
		chip->boost_base + BOOST_ENABLE_CONTROL,
		BOOST_PWR_EN,
		BOOST_PWR_EN, 1);
}

/* Boost regulator operations */
#define ABOVE_VBAT_WEAK		BIT(1)
static int
qpnp_chg_regulator_boost_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;
	u8 vbat_sts;

	rc = qpnp_chg_masked_write(chip,
		chip->boost_base + BOOST_ENABLE_CONTROL,
		BOOST_PWR_EN,
		0, 1);
	if (rc) {
		pr_err("failed to disable boost rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_read(chip, &vbat_sts,
			chip->chgr_base + CHGR_VBAT_STATUS, 1);
	if (rc) {
		pr_err("failed to read bat sts rc=%d\n", rc);
		return rc;
	}

	if (!(vbat_sts & ABOVE_VBAT_WEAK) && (chip->flags & BOOST_FLASH_WA)) {
		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + COMP_OVR1,
			0xFF,
			0x20, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}

		usleep(2000);

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + COMP_OVR1,
			0xFF,
			0x00, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}
	}

	if (qpnp_chg_is_usb_chg_plugged_in(chip)
			&& (chip->flags & BOOST_FLASH_WA)) {
		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + COMP_OVR1,
			0xFF,
			0x00, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}

		usleep(1000);

#ifdef CONFIG_BQ24196_CHARGER
		qpnp_chg_charge_en(chip, 1);
#else
		qpnp_chg_usb_suspend_enable(chip, 0);
#endif
	}

	return rc;
}

static int
qpnp_chg_regulator_boost_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_chg_is_boost_en_set(chip);
}

static int
qpnp_chg_regulator_boost_set_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	int uV = min_uV;
	int rc;
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	if (uV < BOOST_MIN_UV && max_uV >= BOOST_MIN_UV)
		uV = BOOST_MIN_UV;


	if (uV < BOOST_MIN_UV || uV > BOOST_MAX_UV) {
		pr_err("request %d uV is out of bounds\n", uV);
		return -EINVAL;
	}

	*selector = DIV_ROUND_UP(uV - BOOST_MIN_UV, BOOST_STEP_UV);
	if ((*selector * BOOST_STEP_UV + BOOST_MIN_UV) > max_uV) {
		pr_err("no available setpoint [%d, %d] uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	rc = qpnp_boost_vset(chip, uV);

	return rc;
}

static int
qpnp_chg_regulator_boost_get_voltage(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_boost_vget_uv(chip);
}

static int
qpnp_chg_regulator_boost_list_voltage(struct regulator_dev *rdev,
			unsigned selector)
{
	if (selector >= N_BOOST_V)
		return 0;

	return BOOST_MIN_UV + (selector * BOOST_STEP_UV);
}

static struct regulator_ops qpnp_chg_otg_reg_ops = {
	.enable			= qpnp_chg_regulator_otg_enable,
	.disable		= qpnp_chg_regulator_otg_disable,
	.is_enabled		= qpnp_chg_regulator_otg_is_enabled,
};

static struct regulator_ops qpnp_chg_boost_reg_ops = {
	.enable			= qpnp_chg_regulator_boost_enable,
	.disable		= qpnp_chg_regulator_boost_disable,
	.is_enabled		= qpnp_chg_regulator_boost_is_enabled,
	.set_voltage		= qpnp_chg_regulator_boost_set_voltage,
	.get_voltage		= qpnp_chg_regulator_boost_get_voltage,
	.list_voltage		= qpnp_chg_regulator_boost_list_voltage,
};

static int
qpnp_chg_bat_if_batfet_reg_enabled(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	u8 reg = 0;

	if (!chip->bat_if_base)
		return rc;

	if (chip->type == SMBB)
		rc = qpnp_chg_read(chip, &reg,
				chip->bat_if_base + CHGR_BAT_IF_SPARE, 1);
	else
		rc = qpnp_chg_read(chip, &reg,
			chip->bat_if_base + CHGR_BAT_IF_BATFET_CTRL4, 1);

	if (rc) {
		pr_err("failed to read batt_if rc=%d\n", rc);
		return rc;
	}

	if ((reg & BATFET_LPM_MASK) == BATFET_NO_LPM)
		return 1;

	return 0;
}

static int
qpnp_chg_regulator_batfet_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc = 0;

	mutex_lock(&chip->batfet_vreg_lock);
	/* Only enable if not already enabled */
	if (!qpnp_chg_bat_if_batfet_reg_enabled(chip)) {
		rc = qpnp_chg_regulator_batfet_set(chip, 1);
		if (rc)
			pr_err("failed to write to batt_if rc=%d\n", rc);
	}

	chip->batfet_ext_en = true;
	mutex_unlock(&chip->batfet_vreg_lock);

	return rc;
}

static int
qpnp_chg_regulator_batfet_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc = 0;

	mutex_lock(&chip->batfet_vreg_lock);
	/* Don't allow disable if charger connected */
	if (!qpnp_chg_is_usb_chg_plugged_in(chip) &&
			!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		rc = qpnp_chg_regulator_batfet_set(chip, 0);
		if (rc)
			pr_err("failed to write to batt_if rc=%d\n", rc);
	}

	chip->batfet_ext_en = false;
	mutex_unlock(&chip->batfet_vreg_lock);

	return rc;
}

static int
qpnp_chg_regulator_batfet_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return chip->batfet_ext_en;
}

static struct regulator_ops qpnp_chg_batfet_vreg_ops = {
	.enable			= qpnp_chg_regulator_batfet_enable,
	.disable		= qpnp_chg_regulator_batfet_disable,
	.is_enabled		= qpnp_chg_regulator_batfet_is_enabled,
};

#define MIN_DELTA_MV_TO_INCREASE_VDD_MAX	8
#define MAX_DELTA_VDD_MAX_MV			80
#define VDD_MAX_CENTER_OFFSET			4
#ifndef CONFIG_BQ24196_CHARGER
static void
qpnp_chg_adjust_vddmax(struct qpnp_chg_chip *chip, int vbat_mv)
{
	int delta_mv, closest_delta_mv, sign;

	delta_mv = chip->max_voltage_mv - VDD_MAX_CENTER_OFFSET - vbat_mv;
	if (delta_mv > 0 && delta_mv < MIN_DELTA_MV_TO_INCREASE_VDD_MAX) {
		pr_debug("vbat is not low enough to increase vdd\n");
		return;
	}

	sign = delta_mv > 0 ? 1 : -1;
	closest_delta_mv = ((delta_mv + sign * QPNP_CHG_BUCK_TRIM1_STEP / 2)
			/ QPNP_CHG_BUCK_TRIM1_STEP) * QPNP_CHG_BUCK_TRIM1_STEP;
	pr_debug("max_voltage = %d, vbat_mv = %d, delta_mv = %d, closest = %d\n",
			chip->max_voltage_mv, vbat_mv,
			delta_mv, closest_delta_mv);
	chip->delta_vddmax_mv = clamp(chip->delta_vddmax_mv + closest_delta_mv,
			-MAX_DELTA_VDD_MAX_MV, MAX_DELTA_VDD_MAX_MV);
	pr_debug("using delta_vddmax_mv = %d\n", chip->delta_vddmax_mv);
	qpnp_chg_set_appropriate_vddmax(chip);
}
#endif

#define CONSECUTIVE_COUNT	3
#define CONSECUTIVE_COUNT_POSITIVE	6
#define VBATDET_MAX_ERR_MV	50
#ifdef CONFIG_BQ24196_CHARGER
static void
qpnp_eoc_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, eoc_work);
	static int count;
	int ibat_ma, vbat_mv;
	u8 chg_sts = 0;
	int bat_status = 0;
	unsigned int max_comp_volt = 0;

	pm_stay_awake(chip->dev);

	chg_sts = get_prop_charge_type(chip);
	if (!chg_sts) {
		pr_err("failed to get charge type\n");
	}

	if (!qpnp_chg_is_usb_chg_plugged_in(chip)) {
		pr_debug("no chg connected, stopping\n");
		count = 0;
		if (chip->usb_present) {
			chip->usb_present = false;
			chip->usbin_counts = 0;//sjc0522 for Find7s temp rising problem
			schedule_work(&chip->stop_charge_work);
			chip->prev_usb_max_ma = -EINVAL;
			power_supply_set_present(chip->usb_psy, chip->usb_present);
			if (!chip->usb_present) {
				power_supply_set_online(chip->usb_psy, 0);
				power_supply_set_current_limit(chip->usb_psy, 0);
				power_supply_set_online(&chip->dc_psy, 0);
				power_supply_set_current_limit(&chip->dc_psy, 0);
			}
		}
		pm_relax(chip->dev);
		return;
	}

	if ((chg_sts == POWER_SUPPLY_CHARGE_TYPE_TRICKLE)
					|| (chg_sts == POWER_SUPPLY_CHARGE_TYPE_FAST)
					|| (chg_sts == POWER_SUPPLY_CHARGE_TYPE_TERMINATE)){
		ibat_ma = get_prop_current_now(chip);
		vbat_mv = get_prop_battery_voltage_now(chip) / 1000;

		pr_debug("ibat_ma = %d vbat_mv = %d term_current_ma = %d\n",
				ibat_ma, vbat_mv, chip->term_current);

		if (qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION_LITTLE__COLD) {
			max_comp_volt = 4000 - 50;
		} else if (qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__COOL) {
			max_comp_volt = chip->cool_bat_mv - 50;
		} else if (qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__NORMAL) {
			max_comp_volt = chip->max_voltage_mv - 50;
		} else if (qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__WARM) {
			max_comp_volt = chip->warm_bat_mv - 50;
		}

		if (vbat_mv < max_comp_volt) {
			count = 0;
			goto stop_eoc;
		}

		if ((ibat_ma * -1) > chip->term_current) {
			pr_debug("Not at EOC, battery current too high\n");
			count = 0;
		} else if (ibat_ma >= 0) {
			count = 0;
			if (count == CONSECUTIVE_COUNT_POSITIVE) {
				if (qpnp_ext_charger && qpnp_ext_charger->chg_get_system_status)
					bat_status = qpnp_ext_charger->chg_get_system_status();
				if ((bat_status & 0x30) == 0x30) {
					pr_info("End of Charging when ibat>=0\n");
					chip->delta_vddmax_mv = 0;
					qpnp_chg_set_appropriate_vddmax(chip);
					chip->chg_done = true;
					chip->chg_display_full = true;//wangjc add for charge full
					qpnp_chg_charge_en(chip, 0);
					power_supply_changed(&chip->batt_psy);
					qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);
					count = 0;
					goto stop_eoc;
				} else {
					count = 0;
				}
			} else {
				count += 1;
				pr_debug("EOC count = %d\n", count);
			}
		} else {
			if (count == CONSECUTIVE_COUNT) {
				pr_info("End of Charging\n");
				chip->delta_vddmax_mv = 0;
				qpnp_chg_set_appropriate_vddmax(chip);
				chip->chg_done = true;
				chip->chg_display_full = true;//wangjc add for charge full
				qpnp_chg_charge_en(chip, 0);
				/* sleep for a second before enabling */
				msleep(2000);

				power_supply_changed(&chip->batt_psy);
				qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);
				count = 0;
				goto stop_eoc;
			} else {
				count += 1;
				pr_debug("EOC count = %d\n", count);
			}
		}
	} else {
			pr_debug("not charging\n");
			count = 0;
			goto stop_eoc;
	}

stop_eoc:
	schedule_delayed_work(&chip->eoc_work,
		msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
}
#else
static void
qpnp_eoc_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, eoc_work);
	static int count;
	static int vbat_low_count;
	int ibat_ma, vbat_mv, rc = 0;
	u8 batt_sts = 0, buck_sts = 0, chg_sts = 0;
	bool vbat_lower_than_vbatdet;

	pm_stay_awake(chip->dev);
	qpnp_chg_charge_en(chip, !chip->charging_disabled);

	rc = qpnp_chg_read(chip, &batt_sts, INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("failed to read batt_if rc=%d\n", rc);
		return;
	}

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);
	if (rc) {
		pr_err("failed to read buck rc=%d\n", rc);
		return;
	}

	rc = qpnp_chg_read(chip, &chg_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read chg_sts rc=%d\n", rc);
		return;
	}

	pr_debug("chgr: 0x%x, bat_if: 0x%x, buck: 0x%x\n",
		chg_sts, batt_sts, buck_sts);

	if (!qpnp_chg_is_usb_chg_plugged_in(chip) &&
			!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		pr_debug("no chg connected, stopping\n");
		goto stop_eoc;
	}

	if ((batt_sts & BAT_FET_ON_IRQ) && (chg_sts & FAST_CHG_ON_IRQ
					|| chg_sts & TRKL_CHG_ON_IRQ)) {
		ibat_ma = get_prop_current_now(chip) / 1000;
		vbat_mv = get_prop_battery_voltage_now(chip) / 1000;

		pr_debug("ibat_ma = %d vbat_mv = %d term_current_ma = %d\n",
				ibat_ma, vbat_mv, chip->term_current);

		vbat_lower_than_vbatdet = !(chg_sts & VBAT_DET_LOW_IRQ);
		if (vbat_lower_than_vbatdet && vbat_mv <
				(chip->max_voltage_mv - chip->resume_delta_mv
				 - chip->vbatdet_max_err_mv)) {
			vbat_low_count++;
			pr_debug("woke up too early vbat_mv = %d, max_mv = %d, resume_mv = %d tolerance_mv = %d low_count = %d\n",
					vbat_mv, chip->max_voltage_mv,
					chip->resume_delta_mv,
					chip->vbatdet_max_err_mv,
					vbat_low_count);
			if (vbat_low_count >= CONSECUTIVE_COUNT) {
				pr_debug("woke up too early stopping\n");
				qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);
				goto stop_eoc;
			} else {
				goto check_again_later;
			}
		} else {
			vbat_low_count = 0;
		}

		if (buck_sts & VDD_LOOP_IRQ)
			qpnp_chg_adjust_vddmax(chip, vbat_mv);

		if (!(buck_sts & VDD_LOOP_IRQ)) {
			pr_debug("Not in CV\n");
			count = 0;
		} else if ((ibat_ma * -1) > chip->term_current) {
			pr_debug("Not at EOC, battery current too high\n");
			count = 0;
		} else if (ibat_ma > 0) {
			pr_debug("Charging but system demand increased\n");
			count = 0;
		} else {
			if (count == CONSECUTIVE_COUNT) {
				if (!chip->bat_is_cool && !chip->bat_is_warm) {
					pr_info("End of Charging\n");
					chip->chg_done = true;
				} else {
					pr_info("stop charging: battery is %s, vddmax = %d reached\n",
						chip->bat_is_cool
							? "cool" : "warm",
						qpnp_chg_vddmax_get(chip));
				}
				chip->delta_vddmax_mv = 0;
				qpnp_chg_set_appropriate_vddmax(chip);
				qpnp_chg_charge_en(chip, 0);
				/* sleep for a second before enabling */
				msleep(2000);
				qpnp_chg_charge_en(chip,
						!chip->charging_disabled);
				pr_debug("psy changed batt_psy\n");
				power_supply_changed(&chip->batt_psy);
				qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);
				goto stop_eoc;
			} else {
				count += 1;
				pr_debug("EOC count = %d\n", count);
			}
		}
	} else {
		pr_debug("not charging\n");
		goto stop_eoc;
	}

check_again_later:
	schedule_delayed_work(&chip->eoc_work,
		msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
	return;

stop_eoc:
	vbat_low_count = 0;
	count = 0;
	pm_relax(chip->dev);
}
#endif /* CONFIG_BQ24196_CHARGER */

static void
qpnp_chg_insertion_ocv_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, insertion_ocv_work);
	u8 bat_if_sts = 0, charge_en = 0;
	int rc;

	chip->insertion_ocv_uv = get_prop_battery_voltage_now(chip);

	rc = qpnp_chg_read(chip, &bat_if_sts, INT_RT_STS(chip->bat_if_base), 1);
	if (rc)
		pr_err("failed to read bat_if sts %d\n", rc);

	rc = qpnp_chg_read(chip, &charge_en,
			chip->chgr_base + CHGR_CHG_CTRL, 1);
	if (rc)
		pr_err("failed to read bat_if sts %d\n", rc);

	pr_debug("batfet sts = %02x, charge_en = %02x ocv = %d\n",
			bat_if_sts, charge_en, chip->insertion_ocv_uv);
	qpnp_chg_charge_en(chip, !chip->charging_disabled);
#ifndef CONFIG_MACH_OPPO
	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
#endif
}

static void
qpnp_chg_soc_check_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, soc_check_work);

	get_prop_capacity(chip);
}

#define HYSTERISIS_DECIDEGC 20
static void
qpnp_chg_adc_notification(enum qpnp_tm_state state, void *ctx)
{
	struct qpnp_chg_chip *chip = ctx;
	bool bat_warm = 0, bat_cool = 0;
	int temp;

	if (state >= ADC_TM_STATE_NUM) {
		pr_err("invalid notification %d\n", state);
		return;
	}

	temp = get_prop_batt_temp(chip);

	pr_debug("temp = %d state = %s\n", temp,
			state == ADC_TM_WARM_STATE ? "warm" : "cool");

	if (state == ADC_TM_WARM_STATE) {
		if (temp > chip->warm_bat_decidegc) {
			/* Normal to warm */
			bat_warm = true;
			bat_cool = false;
			chip->adc_param.low_temp =
				chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
				ADC_TM_COOL_THR_ENABLE;
		} else if (temp >
				chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC){
			/* Cool to normal */
			bat_warm = false;
			bat_cool = false;

			chip->adc_param.low_temp = chip->cool_bat_decidegc;
			chip->adc_param.high_temp = chip->warm_bat_decidegc;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		}
	} else {
		if (temp < chip->cool_bat_decidegc) {
			/* Normal to cool */
			bat_warm = false;
			bat_cool = true;
			chip->adc_param.high_temp =
				chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
				ADC_TM_WARM_THR_ENABLE;
		} else if (temp <
				chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC){
			/* Warm to normal */
			bat_warm = false;
			bat_cool = false;

			chip->adc_param.low_temp = chip->cool_bat_decidegc;
			chip->adc_param.high_temp = chip->warm_bat_decidegc;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		}
	}

	if (chip->bat_is_cool ^ bat_cool || chip->bat_is_warm ^ bat_warm) {
		chip->bat_is_cool = bat_cool;
		chip->bat_is_warm = bat_warm;

		if (bat_cool || bat_warm)
			chip->resuming_charging = false;

		/**
		 * set appropriate voltages and currents.
		 *
		 * Note that when the battery is hot or cold, the charger
		 * driver will not resume with SoC. Only vbatdet is used to
		 * determine resume of charging.
		 */
		qpnp_chg_set_appropriate_vddmax(chip);
		qpnp_chg_set_appropriate_battery_current(chip);
		qpnp_chg_set_appropriate_vbatdet(chip);
	}

	pr_debug("warm %d, cool %d, low = %d deciDegC, high = %d deciDegC\n",
			chip->bat_is_warm, chip->bat_is_cool,
			chip->adc_param.low_temp, chip->adc_param.high_temp);

	if (qpnp_adc_tm_channel_measure(chip->adc_tm_dev, &chip->adc_param))
		pr_err("request ADC error\n");
}

#define MIN_COOL_TEMP	-300
#define MAX_WARM_TEMP	1000

static int
qpnp_chg_configure_jeita(struct qpnp_chg_chip *chip,
		enum power_supply_property psp, int temp_degc)
{
	int rc = 0;

	if ((temp_degc < MIN_COOL_TEMP) || (temp_degc > MAX_WARM_TEMP)) {
		pr_err("Bad temperature request %d\n", temp_degc);
		return -EINVAL;
	}

	mutex_lock(&chip->jeita_configure_lock);
	switch (psp) {
	case POWER_SUPPLY_PROP_COOL_TEMP:
		if (temp_degc >=
			(chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC)) {
			pr_err("Can't set cool %d higher than warm %d - hysterisis %d\n",
					temp_degc, chip->warm_bat_decidegc,
					HYSTERISIS_DECIDEGC);
			rc = -EINVAL;
			goto mutex_unlock;
		}
		if (chip->bat_is_cool)
			chip->adc_param.high_temp =
				temp_degc + HYSTERISIS_DECIDEGC;
		else if (!chip->bat_is_warm)
			chip->adc_param.low_temp = temp_degc;

		chip->cool_bat_decidegc = temp_degc;
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		if (temp_degc <=
			(chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC)) {
			pr_err("Can't set warm %d higher than cool %d + hysterisis %d\n",
					temp_degc, chip->warm_bat_decidegc,
					HYSTERISIS_DECIDEGC);
			rc = -EINVAL;
			goto mutex_unlock;
		}
		if (chip->bat_is_warm)
			chip->adc_param.low_temp =
				temp_degc - HYSTERISIS_DECIDEGC;
		else if (!chip->bat_is_cool)
			chip->adc_param.high_temp = temp_degc;

		chip->warm_bat_decidegc = temp_degc;
		break;
	default:
		rc = -EINVAL;
		goto mutex_unlock;
	}

	schedule_work(&chip->adc_measure_work);

mutex_unlock:
	mutex_unlock(&chip->jeita_configure_lock);
	return rc;
}

#define POWER_STAGE_REDUCE_CHECK_PERIOD_SECONDS		20
#define POWER_STAGE_REDUCE_MAX_VBAT_UV			3900000
#define POWER_STAGE_REDUCE_MIN_VCHG_UV			4800000
#define POWER_STAGE_SEL_MASK				0x0F
#define POWER_STAGE_REDUCED				0x01
#define POWER_STAGE_DEFAULT				0x0F
static bool
qpnp_chg_is_power_stage_reduced(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 reg;

	rc = qpnp_chg_read(chip, &reg,
				 chip->buck_base + CHGR_BUCK_PSTG_CTRL,
				 1);
	if (rc) {
		pr_err("Error %d reading power stage register\n", rc);
		return false;
	}

	if ((reg & POWER_STAGE_SEL_MASK) == POWER_STAGE_DEFAULT)
		return false;

	return true;
}

static int
qpnp_chg_power_stage_set(struct qpnp_chg_chip *chip, bool reduce)
{
	int rc;
	u8 reg = 0xA5;

	rc = qpnp_chg_write(chip, &reg,
				 chip->buck_base + SEC_ACCESS,
				 1);
	if (rc) {
		pr_err("Error %d writing 0xA5 to buck's 0x%x reg\n",
				rc, SEC_ACCESS);
		return rc;
	}

	reg = POWER_STAGE_DEFAULT;
	if (reduce)
		reg = POWER_STAGE_REDUCED;
	rc = qpnp_chg_write(chip, &reg,
				 chip->buck_base + CHGR_BUCK_PSTG_CTRL,
				 1);

	if (rc)
		pr_err("Error %d writing 0x%x power stage register\n", rc, reg);
	return rc;
}

static int
qpnp_chg_get_vusbin_uv(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	rc = qpnp_vadc_read(chip->vadc_dev, USBIN, &results);
	if (rc) {
		pr_err("Unable to read vbat rc=%d\n", rc);
		return 0;
	}
	return results.physical;
}

static
int get_vusb_averaged(struct qpnp_chg_chip *chip, int sample_count)
{
	int vusb_uv = 0;
	int i;

	/* avoid  overflows */
	if (sample_count > 256)
		sample_count = 256;

	for (i = 0; i < sample_count; i++)
		vusb_uv += qpnp_chg_get_vusbin_uv(chip);

	vusb_uv = vusb_uv / sample_count;
	return vusb_uv;
}

static
int get_vbat_averaged(struct qpnp_chg_chip *chip, int sample_count)
{
	int vbat_uv = 0;
	int i;

	/* avoid  overflows */
	if (sample_count > 256)
		sample_count = 256;

	for (i = 0; i < sample_count; i++)
		vbat_uv += get_prop_battery_voltage_now(chip);

	vbat_uv = vbat_uv / sample_count;
	return vbat_uv;
}

static void
qpnp_chg_reduce_power_stage(struct qpnp_chg_chip *chip)
{
	struct timespec ts;
	bool power_stage_reduced_in_hw = qpnp_chg_is_power_stage_reduced(chip);
	bool reduce_power_stage = false;
	int vbat_uv = get_vbat_averaged(chip, 16);
	int vusb_uv = get_vusb_averaged(chip, 16);
	bool fast_chg =
		(get_prop_charge_type(chip) == POWER_SUPPLY_CHARGE_TYPE_FAST);
	static int count_restore_power_stage;
	static int count_reduce_power_stage;
	bool vchg_loop = get_prop_vchg_loop(chip);
	bool ichg_loop = qpnp_chg_is_ichg_loop_active(chip);
	bool usb_present = qpnp_chg_is_usb_chg_plugged_in(chip);
	bool usb_ma_above_wall =
		(qpnp_chg_usb_iusbmax_get(chip) > USB_WALL_THRESHOLD_MA);

	if (fast_chg
		&& usb_present
		&& usb_ma_above_wall
		&& vbat_uv < POWER_STAGE_REDUCE_MAX_VBAT_UV
		&& vusb_uv > POWER_STAGE_REDUCE_MIN_VCHG_UV)
		reduce_power_stage = true;

	if ((usb_present && usb_ma_above_wall)
		&& (vchg_loop || ichg_loop))
		reduce_power_stage = true;

	if (power_stage_reduced_in_hw && !reduce_power_stage) {
		count_restore_power_stage++;
		count_reduce_power_stage = 0;
	} else if (!power_stage_reduced_in_hw && reduce_power_stage) {
		count_reduce_power_stage++;
		count_restore_power_stage = 0;
	} else if (power_stage_reduced_in_hw == reduce_power_stage) {
		count_restore_power_stage = 0;
		count_reduce_power_stage = 0;
	}

	pr_debug("power_stage_hw = %d reduce_power_stage = %d usb_present = %d usb_ma_above_wall = %d vbat_uv(16) = %d vusb_uv(16) = %d fast_chg = %d , ichg = %d, vchg = %d, restore,reduce = %d, %d\n",
			power_stage_reduced_in_hw, reduce_power_stage,
			usb_present, usb_ma_above_wall,
			vbat_uv, vusb_uv, fast_chg,
			ichg_loop, vchg_loop,
			count_restore_power_stage, count_reduce_power_stage);

	if (!power_stage_reduced_in_hw && reduce_power_stage) {
		if (count_reduce_power_stage >= 2) {
			qpnp_chg_power_stage_set(chip, true);
			power_stage_reduced_in_hw = true;
		}
	}

	if (power_stage_reduced_in_hw && !reduce_power_stage) {
		if (count_restore_power_stage >= 6
				|| (!usb_present || !usb_ma_above_wall)) {
			qpnp_chg_power_stage_set(chip, false);
			power_stage_reduced_in_hw = false;
		}
	}

	if (usb_present && usb_ma_above_wall) {
		getnstimeofday(&ts);
		ts.tv_sec += POWER_STAGE_REDUCE_CHECK_PERIOD_SECONDS;
		alarm_start_range(&chip->reduce_power_stage_alarm,
					timespec_to_ktime(ts),
					timespec_to_ktime(ts));
	} else {
		pr_debug("stopping power stage workaround\n");
		chip->power_stage_workaround_running = false;
	}
}

static void
qpnp_chg_batfet_lcl_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, batfet_lcl_work);

	mutex_lock(&chip->batfet_vreg_lock);
	if (qpnp_chg_is_usb_chg_plugged_in(chip) ||
			qpnp_chg_is_dc_chg_plugged_in(chip)) {
		qpnp_chg_regulator_batfet_set(chip, 1);
		pr_debug("disabled ULPM\n");
	} else if (!chip->batfet_ext_en && !qpnp_chg_is_usb_chg_plugged_in(chip)
			&& !qpnp_chg_is_dc_chg_plugged_in(chip)) {
		qpnp_chg_regulator_batfet_set(chip, 0);
		pr_debug("enabled ULPM\n");
	}
	mutex_unlock(&chip->batfet_vreg_lock);
}

static void
qpnp_chg_reduce_power_stage_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, reduce_power_stage_work);

	qpnp_chg_reduce_power_stage(chip);
}

static void
qpnp_chg_reduce_power_stage_callback(struct alarm *alarm)
{
	struct qpnp_chg_chip *chip = container_of(alarm, struct qpnp_chg_chip,
						reduce_power_stage_alarm);

	schedule_work(&chip->reduce_power_stage_work);
}

static int
qpnp_dc_power_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								dc_psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (!val->intval)
			break;

		rc = qpnp_chg_idcmax_set(chip, val->intval / 1000);
		if (rc) {
			pr_err("Error setting idcmax property %d\n", rc);
			return rc;
		}
		chip->maxinput_dc_ma = (val->intval / 1000);

		break;
	default:
		return -EINVAL;
	}

	pr_debug("psy changed dc_psy\n");
	power_supply_changed(&chip->dc_psy);
	return rc;
}

static int
qpnp_batt_power_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_COOL_TEMP:
		rc = qpnp_chg_configure_jeita(chip, psp, val->intval);
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		rc = qpnp_chg_configure_jeita(chip, psp, val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		chip->fake_battery_soc = val->intval;
		power_supply_changed(&chip->batt_psy);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		chip->charging_disabled = !(val->intval);
		if (chip->charging_disabled) {
			/* disable charging */
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
#ifndef CONFIG_BQ24196_CHARGER
			qpnp_chg_force_run_on_batt(chip,
						chip->charging_disabled);
#endif
		} else {
			/* enable charging */
#ifndef CONFIG_BQ24196_CHARGER
			qpnp_chg_force_run_on_batt(chip,
					chip->charging_disabled);
#endif
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
		}
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		qpnp_batt_system_temp_level_set(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		if (qpnp_chg_is_usb_chg_plugged_in(chip))
			qpnp_chg_iusbmax_set(chip, val->intval / 1000);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM:
		qpnp_chg_iusb_trim_set(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		qpnp_chg_input_current_settled(chip);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		qpnp_chg_vinmin_set(chip, val->intval / 1000);
		break;
	default:
		return -EINVAL;
	}

#ifndef CONFIG_MACH_OPPO
	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
#endif
	return rc;
}

static int
qpnp_chg_setup_flags(struct qpnp_chg_chip *chip)
{
	if (chip->revision > 0 && chip->type == SMBB)
		chip->flags |= CHG_FLAGS_VCP_WA;
	if (chip->type == SMBB)
		chip->flags |= BOOST_FLASH_WA;
	if (chip->type == SMBBP) {
		struct device_node *revid_dev_node;
		struct pmic_revid_data *revid_data;

		chip->flags |=  BOOST_FLASH_WA;

		revid_dev_node = of_parse_phandle(chip->spmi->dev.of_node,
						"qcom,pmic-revid", 0);
		if (!revid_dev_node) {
			pr_err("Missing qcom,pmic-revid property\n");
			return -EINVAL;
		}
		revid_data = get_revid_data(revid_dev_node);
		if (IS_ERR(revid_data)) {
			pr_err("Couldnt get revid data rc = %ld\n",
						PTR_ERR(revid_data));
			return PTR_ERR(revid_data);
		}

		if (revid_data->rev4 < PM8226_V2P1_REV4
			|| ((revid_data->rev4 == PM8226_V2P1_REV4)
				&& (revid_data->rev3 <= PM8226_V2P1_REV3))) {
			chip->flags |= POWER_STAGE_WA;
		}
	}
	return 0;
}

static int
qpnp_chg_request_irqs(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct resource *resource;
	struct spmi_resource *spmi_resource;
	u8 subtype;
	struct spmi_device *spmi = chip->spmi;

	spmi_for_each_container_dev(spmi_resource, chip->spmi) {
		if (!spmi_resource) {
				pr_err("qpnp_chg: spmi resource absent\n");
			return rc;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			return rc;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			return rc;
		}

		switch (subtype) {
		case SMBB_CHGR_SUBTYPE:
		case SMBBP_CHGR_SUBTYPE:
		case SMBCL_CHGR_SUBTYPE:
			chip->chg_fastchg.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "fast-chg-on");
			if (chip->chg_fastchg.irq < 0) {
				pr_err("Unable to get fast-chg-on irq\n");
				return rc;
			}

			chip->chg_trklchg.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "trkl-chg-on");
			if (chip->chg_trklchg.irq < 0) {
				pr_err("Unable to get trkl-chg-on irq\n");
				return rc;
			}

			chip->chg_failed.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "chg-failed");
			if (chip->chg_failed.irq < 0) {
				pr_err("Unable to get chg_failed irq\n");
				return rc;
			}

			chip->chg_vbatdet_lo.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "vbat-det-lo");
			if (chip->chg_vbatdet_lo.irq < 0) {
				pr_err("Unable to get fast-chg-on irq\n");
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_failed.irq,
				qpnp_chg_chgr_chg_failed_irq_handler,
				IRQF_TRIGGER_RISING, "chg-failed", chip);
			if (rc < 0) {
				pr_err("Can't request %d chg-failed: %d\n",
						chip->chg_failed.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_fastchg.irq,
					qpnp_chg_chgr_chg_fastchg_irq_handler,
					IRQF_TRIGGER_RISING,
					"fast-chg-on", chip);
			if (rc < 0) {
				pr_err("Can't request %d fast-chg-on: %d\n",
						chip->chg_fastchg.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_trklchg.irq,
				qpnp_chg_chgr_chg_trklchg_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"trkl-chg-on", chip);
			if (rc < 0) {
				pr_err("Can't request %d trkl-chg-on: %d\n",
						chip->chg_trklchg.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev,
				chip->chg_vbatdet_lo.irq,
				qpnp_chg_vbatdet_lo_irq_handler,
				IRQF_TRIGGER_RISING,
				"vbat-det-lo", chip);
			if (rc < 0) {
				pr_err("Can't request %d vbat-det-lo: %d\n",
						chip->chg_vbatdet_lo.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->chg_trklchg.irq);
			enable_irq_wake(chip->chg_failed.irq);
			qpnp_chg_disable_irq(&chip->chg_vbatdet_lo);
			enable_irq_wake(chip->chg_vbatdet_lo.irq);

			break;
		case SMBB_BAT_IF_SUBTYPE:
		case SMBBP_BAT_IF_SUBTYPE:
		case SMBCL_BAT_IF_SUBTYPE:
			chip->batt_pres.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "batt-pres");
			if (chip->batt_pres.irq < 0) {
				pr_err("Unable to get batt-pres irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->batt_pres.irq,
				qpnp_chg_bat_if_batt_pres_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
				| IRQF_SHARED | IRQF_ONESHOT,
				"batt-pres", chip);
			if (rc < 0) {
				pr_err("Can't request %d batt-pres irq: %d\n",
						chip->batt_pres.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->batt_pres.irq);

			chip->batt_temp_ok.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "bat-temp-ok");
			if (chip->batt_temp_ok.irq < 0) {
				pr_err("Unable to get bat-temp-ok irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->batt_temp_ok.irq,
				qpnp_chg_bat_if_batt_temp_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"bat-temp-ok", chip);
			if (rc < 0) {
				pr_err("Can't request %d bat-temp-ok irq: %d\n",
						chip->batt_temp_ok.irq, rc);
				return rc;
			}
			qpnp_chg_bat_if_batt_temp_irq_handler(0, chip);

			enable_irq_wake(chip->batt_temp_ok.irq);

			break;
		case SMBB_BUCK_SUBTYPE:
		case SMBBP_BUCK_SUBTYPE:
		case SMBCL_BUCK_SUBTYPE:
			break;

		case SMBB_USB_CHGPTH_SUBTYPE:
		case SMBBP_USB_CHGPTH_SUBTYPE:
		case SMBCL_USB_CHGPTH_SUBTYPE:
			if (chip->ovp_monitor_enable) {
				chip->coarse_det_usb.irq =
					spmi_get_irq_byname(spmi,
					spmi_resource, "coarse-det-usb");
				if (chip->coarse_det_usb.irq < 0) {
					pr_err("Can't get coarse-det irq\n");
					return rc;
				}
				rc = devm_request_irq(chip->dev,
					chip->coarse_det_usb.irq,
					qpnp_chg_coarse_det_usb_irq_handler,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING,
					"coarse-det-usb", chip);
				if (rc < 0) {
					pr_err("Can't req %d coarse-det: %d\n",
						chip->coarse_det_usb.irq, rc);
					return rc;
				}
			}

			chip->usbin_valid.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "usbin-valid");
			if (chip->usbin_valid.irq < 0) {
				pr_err("Unable to get usbin irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->usbin_valid.irq,
				qpnp_chg_usb_usbin_valid_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
					"usbin-valid", chip);
			if (rc < 0) {
				pr_err("Can't request %d usbin-valid: %d\n",
						chip->usbin_valid.irq, rc);
				return rc;
			}

			chip->chg_gone.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "chg-gone");
			if (chip->chg_gone.irq < 0) {
				pr_err("Unable to get chg-gone irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->chg_gone.irq,
				qpnp_chg_usb_chg_gone_irq_handler,
				IRQF_TRIGGER_RISING,
					"chg-gone", chip);
			if (rc < 0) {
				pr_err("Can't request %d chg-gone: %d\n",
						chip->chg_gone.irq, rc);
				return rc;
			}

			if ((subtype == SMBBP_USB_CHGPTH_SUBTYPE) ||
				(subtype == SMBCL_USB_CHGPTH_SUBTYPE)) {
				chip->usb_ocp.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "usb-ocp");
				if (chip->usb_ocp.irq < 0) {
					pr_err("Unable to get usbin irq\n");
					return rc;
				}
				rc = devm_request_irq(chip->dev,
					chip->usb_ocp.irq,
					qpnp_chg_usb_usb_ocp_irq_handler,
					IRQF_TRIGGER_RISING, "usb-ocp", chip);
				if (rc < 0) {
					pr_err("Can't request %d usb-ocp: %d\n",
							chip->usb_ocp.irq, rc);
					return rc;
				}

				enable_irq_wake(chip->usb_ocp.irq);
			}

			enable_irq_wake(chip->usbin_valid.irq);
			enable_irq_wake(chip->chg_gone.irq);
			break;
		case SMBB_DC_CHGPTH_SUBTYPE:
			chip->dcin_valid.irq = spmi_get_irq_byname(spmi,
					spmi_resource, "dcin-valid");
			if (chip->dcin_valid.irq < 0) {
				pr_err("Unable to get dcin irq\n");
				return -rc;
			}
			rc = devm_request_irq(chip->dev, chip->dcin_valid.irq,
				qpnp_chg_dc_dcin_valid_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"dcin-valid", chip);
			if (rc < 0) {
				pr_err("Can't request %d dcin-valid: %d\n",
						chip->dcin_valid.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->dcin_valid.irq);
			break;
		}
	}

	return rc;
}

static int
qpnp_chg_load_battery_data(struct qpnp_chg_chip *chip)
{
	struct bms_battery_data batt_data;
	struct device_node *node;
	struct qpnp_vadc_result result;
	int rc;

	node = of_find_node_by_name(chip->spmi->dev.of_node,
			"qcom,battery-data");
	if (node) {
		memset(&batt_data, 0, sizeof(struct bms_battery_data));
		rc = qpnp_vadc_read(chip->vadc_dev, LR_MUX2_BAT_ID, &result);
		if (rc) {
			pr_err("error reading batt id channel = %d, rc = %d\n",
						LR_MUX2_BAT_ID, rc);
			return rc;
		}

		batt_data.max_voltage_uv = -1;
		batt_data.iterm_ua = -1;
		rc = of_batterydata_read_data(node,
				&batt_data, result.physical);
		if (rc) {
			pr_err("failed to read battery data: %d\n", rc);
			return rc;
		}

		if (batt_data.max_voltage_uv >= 0) {
			chip->max_voltage_mv = batt_data.max_voltage_uv / 1000;
			chip->safe_voltage_mv = chip->max_voltage_mv
				+ MAX_DELTA_VDD_MAX_MV;
		}
		if (batt_data.iterm_ua >= 0)
			chip->term_current = batt_data.iterm_ua / 1000;
	}

	return 0;
}

#define WDOG_EN_BIT	BIT(7)
static int
qpnp_chg_hwinit(struct qpnp_chg_chip *chip, u8 subtype,
				struct spmi_resource *spmi_resource)
{
	int rc = 0;
	u8 reg = 0;
	struct regulator_init_data *init_data;
	struct regulator_desc *rdesc;

	switch (subtype) {
	case SMBB_CHGR_SUBTYPE:
	case SMBBP_CHGR_SUBTYPE:
	case SMBCL_CHGR_SUBTYPE:
		qpnp_chg_vbatweak_set(chip, chip->batt_weak_voltage_mv);

		rc = qpnp_chg_vinmin_set(chip, chip->min_voltage_mv);
		if (rc) {
			pr_debug("failed setting  min_voltage rc=%d\n", rc);
			return rc;
		}
#ifdef CONFIG_BQ24196_CHARGER
		rc = qpnp_chg_vddmax_set(chip, chip->max_voltage_mv);
		if (rc) {
			pr_debug("failed setting max_voltage rc=%d\n", rc);
			return rc;
		}
#endif
		rc = qpnp_chg_vddsafe_set(chip, chip->safe_voltage_mv);
		if (rc) {
			pr_debug("failed setting safe_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_vbatdet_set(chip,
				chip->max_voltage_mv - chip->resume_delta_mv);
		if (rc) {
			pr_debug("failed setting resume_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_ibatmax_set(chip, chip->max_bat_chg_current);
		if (rc) {
			pr_debug("failed setting ibatmax rc=%d\n", rc);
			return rc;
		}
		if (chip->term_current) {
			rc = qpnp_chg_ibatterm_set(chip, chip->term_current);
			if (rc) {
				pr_debug("failed setting ibatterm rc=%d\n", rc);
				return rc;
			}
		}
		rc = qpnp_chg_ibatsafe_set(chip, chip->safe_current);
		if (rc) {
			pr_debug("failed setting ibat_Safe rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_tchg_max_set(chip, chip->tchg_mins);
		if (rc) {
			pr_debug("failed setting tchg_mins rc=%d\n", rc);
			return rc;
		}

		/* HACK: Disable wdog */
		rc = qpnp_chg_masked_write(chip, chip->chgr_base + 0x62,
			0xFF, 0xA0, 1);

		/* HACK: use analog EOC */
		rc = qpnp_chg_masked_write(chip, chip->chgr_base +
			CHGR_IBAT_TERM_CHGR,
			0xFF, 0x08, 1);

		break;
	case SMBB_BUCK_SUBTYPE:
	case SMBBP_BUCK_SUBTYPE:
	case SMBCL_BUCK_SUBTYPE:
		rc = qpnp_chg_toggle_chg_done_logic(chip, 0);
		if (rc)
			return rc;

		rc = qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_BCK_VBAT_REG_MODE,
			BUCK_VBAT_REG_NODE_SEL_BIT,
			BUCK_VBAT_REG_NODE_SEL_BIT, 1);
		if (rc) {
			pr_debug("failed to enable IR drop comp rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_read(chip, &chip->trim_center,
				chip->buck_base + BUCK_CTRL_TRIM1, 1);
		if (rc) {
			pr_debug("failed to read trim center rc=%d\n", rc);
			return rc;
		}
		chip->trim_center >>= 4;
		pr_debug("trim center = %02x\n", chip->trim_center);
		break;
	case SMBB_BAT_IF_SUBTYPE:
	case SMBBP_BAT_IF_SUBTYPE:
	case SMBCL_BAT_IF_SUBTYPE:
		/* Select battery presence detection */
		switch (chip->bpd_detection) {
		case BPD_TYPE_BAT_THM:
			reg = BAT_THM_EN;
			break;
		case BPD_TYPE_BAT_ID:
			reg = BAT_ID_EN;
			break;
		case BPD_TYPE_BAT_THM_BAT_ID:
			reg = BAT_THM_EN | BAT_ID_EN;
			break;
		default:
			reg = BAT_THM_EN;
			break;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_BPD_CTRL,
			BAT_IF_BPD_CTRL_SEL,
			reg, 1);
		if (rc) {
			pr_debug("failed to chose BPD rc=%d\n", rc);
			return rc;
		}
		/* Force on VREF_BAT_THM */
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BATT_THERM_FORCE_ON, 1);
		if (rc) {
			pr_debug("failed to force on VREF_BAT_THM rc=%d\n", rc);
			return rc;
		}

		init_data = of_get_regulator_init_data(chip->dev,
					       spmi_resource->of_node);

		if (init_data->constraints.name) {
			rdesc			= &(chip->batfet_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_batfet_vreg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS;

			chip->batfet_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->batfet_vreg.rdev)) {
				rc = PTR_ERR(chip->batfet_vreg.rdev);
				chip->batfet_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("batfet reg failed, rc=%d\n",
							rc);
				return rc;
			}
		}
		break;
	case SMBB_USB_CHGPTH_SUBTYPE:
	case SMBBP_USB_CHGPTH_SUBTYPE:
	case SMBCL_USB_CHGPTH_SUBTYPE:
		if (qpnp_chg_is_usb_chg_plugged_in(chip)) {
			rc = qpnp_chg_masked_write(chip,
				chip->usb_chgpth_base + CHGR_USB_ENUM_T_STOP,
				ENUM_T_STOP_BIT,
				ENUM_T_STOP_BIT, 1);
			if (rc) {
				pr_err("failed to write enum stop rc=%d\n", rc);
				return -ENXIO;
			}
		}

		init_data = of_get_regulator_init_data(chip->dev,
						       spmi_resource->of_node);
		if (!init_data) {
			pr_err("unable to allocate memory\n");
			return -ENOMEM;
		}

		if (init_data->constraints.name) {
			if (of_get_property(chip->dev->of_node,
						"otg-parent-supply", NULL))
				init_data->supply_regulator = "otg-parent";

			rdesc			= &(chip->otg_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_otg_reg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS;

			chip->otg_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->otg_vreg.rdev)) {
				rc = PTR_ERR(chip->otg_vreg.rdev);
				chip->otg_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("OTG reg failed, rc=%d\n", rc);
				return rc;
			}
		}

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_OVP_CTL,
			USB_VALID_DEB_20MS,
			USB_VALID_DEB_20MS, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_ENUM_T_STOP,
			ENUM_T_STOP_BIT,
			ENUM_T_STOP_BIT, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_CHG_GONE_REV_BST,
			0xFF,
			0x80, 1);

		if ((subtype == SMBBP_USB_CHGPTH_SUBTYPE) ||
			(subtype == SMBCL_USB_CHGPTH_SUBTYPE)) {
			rc = qpnp_chg_masked_write(chip,
				chip->usb_chgpth_base + USB_OCP_THR,
				OCP_THR_MASK,
				OCP_THR_900_MA, 1);
			if (rc)
				pr_err("Failed to configure OCP rc = %d\n", rc);
		}

		break;
	case SMBB_DC_CHGPTH_SUBTYPE:
		break;
	case SMBB_BOOST_SUBTYPE:
	case SMBBP_BOOST_SUBTYPE:
		init_data = of_get_regulator_init_data(chip->dev,
					       spmi_resource->of_node);
		if (!init_data) {
			pr_err("unable to allocate memory\n");
			return -ENOMEM;
		}

		if (init_data->constraints.name) {
			if (of_get_property(chip->dev->of_node,
						"boost-parent-supply", NULL))
				init_data->supply_regulator = "boost-parent";

			rdesc			= &(chip->boost_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_boost_reg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS
					| REGULATOR_CHANGE_VOLTAGE;

			chip->boost_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->boost_vreg.rdev)) {
				rc = PTR_ERR(chip->boost_vreg.rdev);
				chip->boost_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("boost reg failed, rc=%d\n", rc);
				return rc;
			}
		}
		break;
	case SMBB_MISC_SUBTYPE:
	case SMBBP_MISC_SUBTYPE:
	case SMBCL_MISC_SUBTYPE:
		if (subtype == SMBB_MISC_SUBTYPE)
			chip->type = SMBB;
		else if (subtype == SMBBP_MISC_SUBTYPE)
			chip->type = SMBBP;
		else if (subtype == SMBCL_MISC_SUBTYPE)
			chip->type = SMBCL;

		pr_debug("Setting BOOT_DONE\n");
		rc = qpnp_chg_masked_write(chip,
			chip->misc_base + CHGR_MISC_BOOT_DONE,
			CHGR_BOOT_DONE, CHGR_BOOT_DONE, 1);
		rc = qpnp_chg_read(chip, &reg,
				 chip->misc_base + MISC_REVISION2, 1);
		if (rc) {
			pr_err("failed to read revision register rc=%d\n", rc);
			return rc;
		}

		chip->revision = reg;
		break;
	default:
		pr_err("Invalid peripheral subtype\n");
	}
	return rc;
}

#define OF_PROP_READ(chip, prop, qpnp_dt_property, retval, optional)	\
do {									\
	if (retval)							\
		break;							\
									\
	retval = of_property_read_u32(chip->spmi->dev.of_node,		\
					"qcom," qpnp_dt_property,	\
					&chip->prop);			\
									\
	if (retval && optional)						\
		retval = 0;						\
	else if (retval)						\
		pr_err("Error reading " #qpnp_dt_property		\
				" property rc = %d\n", rc);		\
} while (0)

static int
qpnp_charger_read_dt_props(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	const char *bpd;

	OF_PROP_READ(chip, max_voltage_mv, "vddmax-mv", rc, 0);
	OF_PROP_READ(chip, min_voltage_mv, "vinmin-mv", rc, 0);
	OF_PROP_READ(chip, safe_voltage_mv, "vddsafe-mv", rc, 0);
	OF_PROP_READ(chip, resume_delta_mv, "vbatdet-delta-mv", rc, 0);
	OF_PROP_READ(chip, safe_current, "ibatsafe-ma", rc, 0);
	OF_PROP_READ(chip, max_bat_chg_current, "ibatmax-ma", rc, 0);
	if (rc)
		pr_err("failed to read required dt parameters %d\n", rc);

	OF_PROP_READ(chip, term_current, "ibatterm-ma", rc, 1);
	OF_PROP_READ(chip, maxinput_dc_ma, "maxinput-dc-ma", rc, 1);
	OF_PROP_READ(chip, maxinput_usb_ma, "maxinput-usb-ma", rc, 1);
	OF_PROP_READ(chip, warm_bat_decidegc, "warm-bat-decidegc", rc, 1);
	OF_PROP_READ(chip, cool_bat_decidegc, "cool-bat-decidegc", rc, 1);
	OF_PROP_READ(chip, tchg_mins, "tchg-mins", rc, 1);
	OF_PROP_READ(chip, hot_batt_p, "batt-hot-percentage", rc, 1);
	OF_PROP_READ(chip, cold_batt_p, "batt-cold-percentage", rc, 1);
	OF_PROP_READ(chip, soc_resume_limit, "resume-soc", rc, 1);
	OF_PROP_READ(chip, batt_weak_voltage_mv, "vbatweak-mv", rc, 1);
	OF_PROP_READ(chip, vbatdet_max_err_mv, "vbatdet-maxerr-mv", rc, 1);

#ifdef CONFIG_MACH_OPPO
	OF_PROP_READ(chip, warm_bat_mv, "warm-bat-mv", rc, 1);
	OF_PROP_READ(chip, cool_bat_mv, "cool-bat-mv", rc, 1);
#endif

	if (rc)
		return rc;

	rc = of_property_read_string(chip->spmi->dev.of_node,
		"qcom,bpd-detection", &bpd);
	if (rc) {
		/* Select BAT_THM as default BPD scheme */
		chip->bpd_detection = BPD_TYPE_BAT_THM;
		rc = 0;
	} else {
		chip->bpd_detection = get_bpd(bpd);
		if (chip->bpd_detection < 0) {
			pr_err("failed to determine bpd schema %d\n", rc);
			return rc;
		}
	}

	if (!chip->vbatdet_max_err_mv)
		chip->vbatdet_max_err_mv = VBATDET_MAX_ERR_MV;

	/* Look up JEITA compliance parameters if cool and warm temp provided */
	if (chip->cool_bat_decidegc || chip->warm_bat_decidegc) {
		chip->adc_tm_dev = qpnp_get_adc_tm(chip->dev, "chg");
		if (IS_ERR(chip->adc_tm_dev)) {
			rc = PTR_ERR(chip->adc_tm_dev);
			if (rc != -EPROBE_DEFER)
				pr_err("adc-tm not ready, defer probe\n");
			return rc;
		}

		OF_PROP_READ(chip, warm_bat_chg_ma, "ibatmax-warm-ma", rc, 1);
		OF_PROP_READ(chip, cool_bat_chg_ma, "ibatmax-cool-ma", rc, 1);
#ifndef CONFIG_MACH_OPPO
		OF_PROP_READ(chip, warm_bat_mv, "warm-bat-mv", rc, 1);
		OF_PROP_READ(chip, cool_bat_mv, "cool-bat-mv", rc, 1);
#endif
		if (rc)
			return rc;
	}

	/* Get the use-external-rsense property */
	chip->use_external_rsense = of_property_read_bool(
			chip->spmi->dev.of_node,
			"qcom,use-external-rsense");

	/* Get the btc-disabled property */
	chip->btc_disabled = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,btc-disabled");

	ext_ovp_present = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,ext-ovp-present");

	/* Get the charging-disabled property */
	chip->charging_disabled = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,charging-disabled");

	chip->ovp_monitor_enable = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,ovp-monitor-en");

	/* Get the duty-cycle-100p property */
	chip->duty_cycle_100p = of_property_read_bool(
					chip->spmi->dev.of_node,
					"qcom,duty-cycle-100p");

	/* Get the fake-batt-values property */
	chip->use_default_batt_values =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,use-default-batt-values");

	/* Disable charging when faking battery values */
	if (chip->use_default_batt_values)
		chip->charging_disabled = true;

	chip->power_stage_workaround_enable =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,power-stage-reduced");

	chip->ibat_calibration_enabled =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,ibat-calibration-enabled");

	of_get_property(chip->spmi->dev.of_node, "qcom,thermal-mitigation",
		&(chip->thermal_levels));

	if (chip->thermal_levels > sizeof(int)) {
		chip->thermal_mitigation = devm_kzalloc(chip->dev,
			chip->thermal_levels,
			GFP_KERNEL);

		if (chip->thermal_mitigation == NULL) {
			pr_err("thermal mitigation kzalloc() failed.\n");
			return -ENOMEM;
		}

		chip->thermal_levels /= sizeof(int);
		rc = of_property_read_u32_array(chip->spmi->dev.of_node,
				"qcom,thermal-mitigation",
				chip->thermal_mitigation, chip->thermal_levels);
		if (rc) {
			pr_err("qcom,thermal-mitigation missing in dt\n");
			return rc;
		}
	}

	return rc;
}

#ifdef CONFIG_MACH_OPPO
static enum chg_battery_status_type qpnp_battery_status_get(struct qpnp_chg_chip *chip)
{
	return chip->battery_status;
}
static void qpnp_battery_status_set(struct qpnp_chg_chip *chip,
											  enum chg_battery_status_type battery_status)
{
	chip->battery_status = battery_status;
}

static int qpnp_charger_type_get(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};
	
#ifndef CONFIG_MACH_OPPO
/* jingchun.wang@Onlinerd.Driver, 2013/12/30  Modify for avoid race condition */
	chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_TYPE, &ret);
#else /*CONFIG_MACH_OPPO*/
	chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_POWER_NOW, &ret);
#endif /*CONFIG_MACH_OPPO*/
	
	return ret.intval;
}

static int set_prop_batt_health(struct qpnp_chg_chip *chip, int batt_health)
{
	chip->batt_health = batt_health;
	return 0;
}

#define MAX_COUNT	50
#ifdef CONFIG_MACH_OPPO
/* jingchun.wang@Onlinerd.Driver, 2014/01/02  Add for set soft aicl voltage to 4.4v */
#define SOFT_AICL_VOL	4500
#endif /*CONFIG_MACH_OPPO*/
/* jingchun.wang@Onlinerd.Driver, 2013/12/27  Add for auto adapt current by software. */
static int soft_aicl(struct qpnp_chg_chip *chip)
{
	int i, chg_vol;

	qpnp_chg_iusbmax_set(chip, 150);
	qpnp_chg_ibatmax_set(chip, chip->max_bat_chg_current);
	qpnp_chg_charge_en(chip, 1);
	for(i = 0; i < MAX_COUNT; i++) {
		chg_vol = get_prop_charger_voltage_now(chip);
		if(chg_vol < SOFT_AICL_VOL) {
			chip->aicl_current = 100;
			qpnp_chg_iusbmax_set(chip, 100);
			return 0;
		}
	}

	qpnp_chg_iusbmax_set(chip, 500);
	for(i = 0; i < MAX_COUNT; i++) {
		chg_vol = get_prop_charger_voltage_now(chip);
		if(chg_vol < SOFT_AICL_VOL) {
			qpnp_chg_iusbmax_set(chip, 150);
			chip->aicl_current = 150;
			return 0;
		}
	}

	qpnp_chg_iusbmax_set(chip, 900);
	for(i = 0; i < MAX_COUNT; i++) {
		chg_vol = get_prop_charger_voltage_now(chip);
		if(chg_vol < SOFT_AICL_VOL) {
			qpnp_chg_iusbmax_set(chip, 500);
			chip->aicl_current = 500;
			return 0;
		}
	}

	qpnp_chg_iusbmax_set(chip, 1500);
	for(i = 0; i < MAX_COUNT; i++) {
		chg_vol = get_prop_charger_voltage_now(chip);
		if(chg_vol < SOFT_AICL_VOL) {
			qpnp_chg_iusbmax_set(chip, 900);
			chip->aicl_current = 900;
			qpnp_chg_vinmin_set(chip, chip->min_voltage_mv + 280);///4.68V sjc0401 add for improving current noise (bq24196 hardware bug)
			return 0;
		}
	}

	qpnp_chg_iusbmax_set(chip, 2000);
	for(i = 0; i < MAX_COUNT; i++) {
		chg_vol = get_prop_charger_voltage_now(chip);
		if(chg_vol < SOFT_AICL_VOL) {
#ifdef CONFIG_MACH_FIND7OP
			qpnp_chg_iusbmax_set(chip, 1200);
#else
			qpnp_chg_iusbmax_set(chip, 1500);
#endif
			chip->aicl_current = 1500;
			qpnp_chg_vinmin_set(chip, chip->min_voltage_mv + 280);///4.68V sjc0401 add for improving current noise (bq24196 hardware bug)
			return 0;
		}
	}
#ifdef CONFIG_MACH_FIND7OP
	qpnp_chg_iusbmax_set(chip, 1200);
#else
	qpnp_chg_iusbmax_set(chip, 1500);
#endif
	chip->aicl_current = 2000;
	return 0;
}

static int qpnp_start_charging(struct qpnp_chg_chip *chip)
{
	int rc = -1;
	unsigned int chg_current = chip->max_bat_chg_current;
	union power_supply_propval ret = {0,};
	int batt_temp = get_prop_batt_temp(chip);
	
	pr_err("%s:starting to enable charging\n", __func__);
	if (!qpnp_chg_is_usb_chg_plugged_in(chip)){
		pr_err("%s:charger maybe removed \n", __func__);
		return rc;
	}
	if (batt_temp <= chip->mBatteryTempBoundT0){   // -10
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__COLD);
#ifdef CONFIG_BQ24196_CHARGER
		qpnp_chg_charge_en(chip, 0);
#endif
		set_prop_batt_health(chip, POWER_SUPPLY_HEALTH_COLD);
		return rc;
	}else if (batt_temp <= chip->mBatteryTempBoundT1){ // -10 ~ 0
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION_LITTLE__COLD);

		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);

		qpnp_chg_iusbmax_set(chip, ret.intval / 1000);

		
		qpnp_chg_vddmax_set(chip, 4000);

		qpnp_chg_ibatmax_set(chip, 200);

		qpnp_chg_vbatdet_set(chip, 4000
				- chip->resume_delta_mv);
	}else if (batt_temp <= chip->mBatteryTempBoundT2){ // 0 ~ 10
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__COOL);

		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
		
		qpnp_chg_vddmax_set(chip, chip->cool_bat_mv);
		if(qpnp_charger_type_get(chip) == POWER_SUPPLY_TYPE_USB_DCP){
			if(ret.intval / 1000 == 500)
				chg_current = 1024;
			else
				chg_current = 1024;
		}
		else{
			chg_current = 500;
		}
		qpnp_chg_ibatmax_set(chip, chg_current);
		qpnp_chg_vbatdet_set(chip, chip->cool_bat_mv
				- chip->resume_delta_mv);
	}else if (batt_temp <= chip->mBatteryTempBoundT3){ // 10 ~ 45
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__NORMAL);

		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		if(ret.intval / 1000 == 500) {
			qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
		} else {
		/* jingchun.wang@Onlinerd.Driver, 2013/12/14  Add for reset current. */
		/* jingchun.wang@Onlinerd.Driver, 2013/12/27  Add for auto adapt current by software. */
			if(chip->aicl_current == 0) {
				soft_aicl(chip);
			} else {
				if (chip->aicl_current == 1500) {
#ifdef CONFIG_MACH_FIND7OP
					qpnp_chg_iusbmax_set(chip, 1200);
#else
					qpnp_chg_iusbmax_set(chip, 1500);
#endif
				} else {
					qpnp_chg_iusbmax_set(chip, chip->aicl_current);
				}
			}
				
		}
		
		qpnp_chg_vddmax_set(chip, chip->max_voltage_mv);
		if(qpnp_charger_type_get(chip) == POWER_SUPPLY_TYPE_USB_DCP){
			if(ret.intval / 1000 == 500)
				qpnp_chg_ibatmax_set(chip, 500);
			else
				qpnp_chg_ibatmax_set(chip, chip->max_bat_chg_current);
		}else {
			qpnp_chg_ibatmax_set(chip, 500);
		}
		
		qpnp_chg_vbatdet_set(chip,
				chip->max_voltage_mv - chip->resume_delta_mv);
	}else if (batt_temp <= chip->mBatteryTempBoundT4){  // 45 ~ 55
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__WARM);

		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
		
		qpnp_chg_vddmax_set(chip, chip->warm_bat_mv);
		if(qpnp_charger_type_get(chip) == POWER_SUPPLY_TYPE_USB_DCP){
			if(ret.intval / 1000 == 500)
				chg_current = 1024;
			else
				chg_current = 1024;
		}
		else {
			chg_current = 500;
		}
		qpnp_chg_ibatmax_set(chip, chg_current);
		qpnp_chg_vbatdet_set(chip, chip->warm_bat_mv
				- chip->resume_delta_mv);
	}else{
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__HOT);
#ifdef CONFIG_BQ24196_CHARGER
		qpnp_chg_charge_en(chip, 0);
#endif
		set_prop_batt_health(chip, POWER_SUPPLY_HEALTH_OVERHEAT);
		return rc;
	}
	/*OPPO 2013-10-24 liaofuchun add begin for bq24196 charger*/
	#ifndef CONFIG_BQ24196_CHARGER
	rc = qpnp_chg_usb_suspend_enable(chip, 0);
	#else
	rc = qpnp_chg_charge_en(chip, 1);
	chip->chg_done = false;
	/* jingchun.wang@Onlinerd.Driver, 2013/12/16  Add for charge timeout */
	chip->time_out = false;
	#endif
	/*OPPO 2013-10-24 liaofuchun add end*/
	if (rc){
		pr_err("%s:starting charging failed\n", __func__);
	}
	
	return rc;
}

static int qpnp_handle_battery_uovp(struct qpnp_chg_chip *chip)
{
	pr_debug("%s\n", __func__);

	qpnp_chg_usb_suspend_enable(chip, 1);

	set_prop_batt_health(chip, POWER_SUPPLY_HEALTH_OVERVOLTAGE);
	//qpnp_chg_iusbmax_set(chip, QPNP_CHG_I_MAX_MIN_100);
/* OPPO 2013-11-05 wangjc Add begin for use bq charger */
#ifdef CONFIG_BQ24196_CHARGER
		qpnp_chg_charge_en(chip, 0);
#endif
/* OPPO 2013-11-05 wangjc Add end */
	
	return 0;
}

static int qpnp_handle_battery_restore_from_uovp(struct qpnp_chg_chip *chip)
{
	pr_debug("%s\n", __func__);

	/*restore charging form battery ovp*/
	qpnp_chg_usb_suspend_enable(chip, 0);
	qpnp_start_charging(chip);
	set_prop_batt_health(chip, POWER_SUPPLY_HEALTH_GOOD);
	
	return 0;
}

/*Tbatt <-10C*/
static int handle_batt_temp_cold(struct qpnp_chg_chip *chip)
{
	if (qpnp_battery_temp_region_get(chip) != CV_BATTERY_TEMP_REGION__COLD)
	{
		pr_err("%s\n", __func__);
		/*OPPO 2013-10-31 liaofuchun delete for bq charger*/
		#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_usb_suspend_enable(chip, 1);
		#endif
		/*OPPO 2013-10-31 liaofuchun delete end*/
/* OPPO 2013-11-05 wangjc Delete begin for use bq charger */
#if 0
		qpnp_chg_iusbmax_set(chip, QPNP_CHG_I_MAX_MIN_100);
#endif
/* OPPO 2013-11-05 wangjc Delete end */
/* OPPO 2013-11-05 wangjc Add begin for use bq charger */
#ifdef CONFIG_BQ24196_CHARGER
		qpnp_chg_charge_en(chip, 0);
#endif
/* OPPO 2013-11-05 wangjc Add end */

		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__COLD);
		
		/* Update the temperature boundaries */
		chip->mBatteryTempBoundT0 = AUTO_CHARGING_BATT_TEMP_T0 + AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_COLD_TO_COOL;
		chip->mBatteryTempBoundT1 = AUTO_CHARGING_BATT_TEMP_T1 + AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_COLD_TO_COOL;
		chip->mBatteryTempBoundT2 = AUTO_CHARGING_BATT_TEMP_T2 + AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_COOL_TO_NORMAL;
		chip->mBatteryTempBoundT3 = AUTO_CHARGING_BATT_TEMP_T3;
		chip->mBatteryTempBoundT4 = AUTO_CHARGING_BATT_TEMP_T4;

		set_prop_batt_health(chip, POWER_SUPPLY_HEALTH_COLD);
	}
	return 0;
}

/* -10 C <=Tbatt <= 0C*/
static int handle_batt_temp_little_cold(struct qpnp_chg_chip *chip)
{
	//unsigned int chg_current = chip->max_bat_chg_current;
	union power_supply_propval ret = {0,};

	if(chip->charger_status == CHARGER_STATUS_OVER)
		return 0;
	
	if (qpnp_battery_temp_region_get(chip) != CV_BATTERY_TEMP_REGION_LITTLE__COLD)
	{
		pr_info("%s\n", __func__);

		if(qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__HOT || 
			qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__COLD)
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
		/*OPPO 2013-10-31 liaofuchun delete for bq charger*/
		#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_usb_suspend_enable(chip, 0);
		#endif
		/*OPPO 2013-10-31 liaofuchun delete end*/
		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);

		qpnp_chg_iusbmax_set(chip, ret.intval / 1000);

		
		
/* OPPO 2013-10-17 wangjc Delete begin for use bq charger */
#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_force_run_on_batt(chip, 0);
#endif
/* OPPO 2013-10-17 wangjc Delete end */
		qpnp_chg_vddmax_set(chip, 4000);

		qpnp_chg_ibatmax_set(chip, 200);

		qpnp_chg_vbatdet_set(chip, 4000
				- chip->resume_delta_mv);
		
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION_LITTLE__COLD);
		
		/* Update the temperature boundaries */
		chip->mBatteryTempBoundT0 = AUTO_CHARGING_BATT_TEMP_T0;
		chip->mBatteryTempBoundT1 = AUTO_CHARGING_BATT_TEMP_T1 + AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_COOL_TO_NORMAL;
		chip->mBatteryTempBoundT2 = AUTO_CHARGING_BATT_TEMP_T2 + AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_COOL_TO_NORMAL;
		chip->mBatteryTempBoundT3 = AUTO_CHARGING_BATT_TEMP_T3;
		chip->mBatteryTempBoundT4 = AUTO_CHARGING_BATT_TEMP_T4;


		set_prop_batt_health(chip, POWER_SUPPLY_HEALTH_GOOD);

	}
	return 0;
}
 
/* 0 C <Tbatt <= 10C*/
static int handle_batt_temp_cool(struct qpnp_chg_chip *chip)
{
	unsigned int chg_current = chip->max_bat_chg_current;
	union power_supply_propval ret = {0,};

	if(chip->charger_status == CHARGER_STATUS_OVER)
		return 0;
	
	if (qpnp_battery_temp_region_get(chip) != CV_BATTERY_TEMP_REGION__COOL)
	{
       	pr_err("%s\n", __func__);

		if(qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__HOT || 
			qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__COLD)
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
		/*OPPO 2013-10-31 liaofuchun delete for bq charger*/
		#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_usb_suspend_enable(chip, 0);
		#endif
		/*OPPO 2013-10-31 liaofuchun delete end*/
		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
		
/* OPPO 2013-10-17 wangjc Delete begin for use bq charger */
#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_force_run_on_batt(chip, 0);
#endif
/* OPPO 2013-10-17 wangjc Delete end */
		qpnp_chg_vddmax_set(chip, chip->cool_bat_mv);
		if(qpnp_charger_type_get(chip) == POWER_SUPPLY_TYPE_USB_DCP) {
			if(ret.intval / 1000 == 500)
				chg_current = 500;
			else
				chg_current = 900;
		}
		else {
			chg_current = 500;
		}
		qpnp_chg_ibatmax_set(chip, chg_current);
		qpnp_chg_vbatdet_set(chip, chip->cool_bat_mv
				- chip->resume_delta_mv);

		/* Update battery temp region */
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__COOL);

		/* Update the temperature boundaries */
		chip->mBatteryTempBoundT0 = AUTO_CHARGING_BATT_TEMP_T0;
		chip->mBatteryTempBoundT1 = AUTO_CHARGING_BATT_TEMP_T1;
		chip->mBatteryTempBoundT2 = AUTO_CHARGING_BATT_TEMP_T2 + AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_COOL_TO_NORMAL;
		chip->mBatteryTempBoundT3 = AUTO_CHARGING_BATT_TEMP_T3;
		chip->mBatteryTempBoundT4 = AUTO_CHARGING_BATT_TEMP_T4;

		set_prop_batt_health(chip, POWER_SUPPLY_HEALTH_GOOD);

	}
	return 0;
}
 
/* 10 C <Tbatt <45C*/
static int handle_batt_temp_normal(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if(chip->charger_status == CHARGER_STATUS_OVER)
		return 0;
	
	if (qpnp_battery_temp_region_get(chip) != CV_BATTERY_TEMP_REGION__NORMAL)
	{
		pr_info("%s\n", __func__);

		if(qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__HOT || 
			qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__COLD)
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
		/*OPPO 2013-10-31 liaofuchun delete for bq charger*/
		#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_usb_suspend_enable(chip, 0);
		#endif
		/*OPPO 2013-10-31 liaofuchun delete end*/
		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		if(ret.intval / 1000 == 500) {
			qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
		} else {
			/* jingchun.wang@Onlinerd.Driver, 2013/12/27  Add for auto adapt current by software. */
			if(qpnp_charger_type_get(chip) == POWER_SUPPLY_TYPE_USB_DCP) {
				if(chip->aicl_current == 0) {
					soft_aicl(chip);
				} else {
					if(chip->aicl_current == 1500) {
#ifdef CONFIG_MACH_FIND7OP
						qpnp_chg_iusbmax_set(chip, 1200);
#else
						qpnp_chg_iusbmax_set(chip, 1500);
#endif
					} else {
						qpnp_chg_iusbmax_set(chip, chip->aicl_current);
					}
				}
			}
		}
		
		if(qpnp_charger_type_get(chip) == POWER_SUPPLY_TYPE_USB_DCP) {
			if(ret.intval / 1000 == 500)
				qpnp_chg_ibatmax_set(chip, 500);
			else
				qpnp_chg_ibatmax_set(chip, chip->max_bat_chg_current);
		}
		else {
			qpnp_chg_ibatmax_set(chip, 500);
		}
/* OPPO 2013-10-17 wangjc Delete begin for use bq charger */
#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_force_run_on_batt(chip, 0);
#endif
/* OPPO 2013-10-17 wangjc Delete end */
		qpnp_chg_vddmax_set(chip, chip->max_voltage_mv);
		
		qpnp_chg_vbatdet_set(chip,
				chip->max_voltage_mv - chip->resume_delta_mv);

		/* Update battery temp region */
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__NORMAL);

		/* Update the temperature boundaries */
		chip->mBatteryTempBoundT0 = AUTO_CHARGING_BATT_TEMP_T0;
		chip->mBatteryTempBoundT1 = AUTO_CHARGING_BATT_TEMP_T1;
		chip->mBatteryTempBoundT2 = AUTO_CHARGING_BATT_TEMP_T2;
		chip->mBatteryTempBoundT3 = AUTO_CHARGING_BATT_TEMP_T3;
		chip->mBatteryTempBoundT4 = AUTO_CHARGING_BATT_TEMP_T4;

		set_prop_batt_health(chip, POWER_SUPPLY_HEALTH_GOOD);
	}
	return 0;
}
 
/* 45C <=Tbatt <=55C*/
static int handle_batt_temp_warm(struct qpnp_chg_chip *chip)
{
	unsigned int chg_current = chip->max_bat_chg_current;
	union power_supply_propval ret = {0,};

	if(chip->charger_status == CHARGER_STATUS_OVER)
		return 0;
	
	if(qpnp_battery_temp_region_get(chip) != CV_BATTERY_TEMP_REGION__WARM)
	{
	    
		pr_info("%s\n", __func__);
		if(qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__HOT || 
			qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__COLD)
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
		/*OPPO 2013-10-31 liaofuchun delete for bq charger*/
		#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_usb_suspend_enable(chip, 0);
		#endif
		/*OPPO 2013-10-31 liaofuchun delete end*/
		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
		
/* OPPO 2013-10-17 wangjc Delete begin for use bq charger */
#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_force_run_on_batt(chip, 0);
#endif
/* OPPO 2013-10-17 wangjc Delete end */
		qpnp_chg_vddmax_set(chip, chip->warm_bat_mv);
		if(qpnp_charger_type_get(chip) == POWER_SUPPLY_TYPE_USB_DCP) {
			if(ret.intval / 1000 == 500)
				chg_current = 500;
			else
				chg_current = 900;
		}
		else {
			chg_current = 500;
		}
		qpnp_chg_ibatmax_set(chip, chg_current);
		qpnp_chg_vbatdet_set(chip, chip->warm_bat_mv
				- chip->resume_delta_mv);

		/* Update battery temp region */
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__WARM);

		/* Update the temperature boundaries */
		chip->mBatteryTempBoundT0 = AUTO_CHARGING_BATT_TEMP_T0;
		chip->mBatteryTempBoundT1 = AUTO_CHARGING_BATT_TEMP_T1;
		chip->mBatteryTempBoundT2 = AUTO_CHARGING_BATT_TEMP_T2;
		chip->mBatteryTempBoundT3 = AUTO_CHARGING_BATT_TEMP_T3 - AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_WARM_TO_NORMAL;
		chip->mBatteryTempBoundT4 = AUTO_CHARGING_BATT_TEMP_T4;

		set_prop_batt_health(chip, POWER_SUPPLY_HEALTH_GOOD);
	}
	return 0;	
}
 
/* 55C <Tbatt*/
static int handle_batt_temp_hot(struct qpnp_chg_chip *chip)
{
	if(qpnp_battery_temp_region_get(chip) != CV_BATTERY_TEMP_REGION__HOT)
	{
	
		pr_info("%s\n", __func__);
		/*OPPO 2013-10-31 liaofuchun delete for bq charger*/
		#ifndef CONFIG_BQ24196_CHARGER
		qpnp_chg_usb_suspend_enable(chip, 1);
		#endif
		/*OPPO 2013-10-31 liaofuchun delete end*/
/* OPPO 2013-11-05 wangjc Delete begin for use bq charger */
#if 0
		qpnp_chg_iusbmax_set(chip, QPNP_CHG_I_MAX_MIN_100);
#endif
/* OPPO 2013-11-05 wangjc Delete end */
/* OPPO 2013-11-05 wangjc Add begin for use bq charger */
#ifdef CONFIG_BQ24196_CHARGER
		qpnp_chg_charge_en(chip, 0);
#endif
/* OPPO 2013-11-05 wangjc Add end */

		/* Update battery temp region */
		qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__HOT);

		/* Update the temperature boundaries */
		chip->mBatteryTempBoundT0 = AUTO_CHARGING_BATT_TEMP_T0;
		chip->mBatteryTempBoundT1 = AUTO_CHARGING_BATT_TEMP_T1;
		chip->mBatteryTempBoundT2 = AUTO_CHARGING_BATT_TEMP_T2;
		chip->mBatteryTempBoundT3 = AUTO_CHARGING_BATT_TEMP_T3 - AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_WARM_TO_NORMAL;
		chip->mBatteryTempBoundT4 = AUTO_CHARGING_BATT_TEMP_T4 - AUTO_CHARGING_BATTERY_TEMP_HYST_FROM_HOT_TO_WARM;

		set_prop_batt_health(chip, POWER_SUPPLY_HEALTH_OVERHEAT);
	}
	return 0;
}

static void qpnp_check_charge_timeout(struct qpnp_chg_chip *chip)
{
	static int count = 0;
	int rc = -1;
	union power_supply_propval ret = {0,};
	
	if (chip->chg_done)
		return;

	if(qpnp_chg_is_usb_chg_plugged_in(chip))
		count++;
	else
		count = 0;

	/* jingchun.wang@Onlinerd.Driver, 2013/12/16  Add for charge timeout */
	chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);

	if(((ret.intval / 1000 != 500) && count > BATT_CHG_TIMEOUT_COUNT_DCP)
		||((ret.intval / 1000 == 500) && count > BATT_CHG_TIMEOUT_COUNT_USB_PRO)){
		pr_err("%s:chg timeout stop chaging\n", __func__);
/* OPPO 2013-11-01 wangjc Modify begin for use bq charger */
#ifndef CONFIG_BQ24196_CHARGER
		rc = qpnp_chg_usb_suspend_enable(chip, 1);
#else
		rc = qpnp_chg_charge_en(chip, 0);
#endif
/* OPPO 2013-11-01 wangjc Modify end */
		if (!rc)
			count= 0;
		/* jingchun.wang@Onlinerd.Driver, 2013/12/16  Add for charge timeout */
		chip->time_out = true;
	}
}

static void qpnp_check_charger_uovp(struct qpnp_chg_chip *chip)
{
	int vchg_mv = CHARGER_VOLTAGE_NORMAL;
	
	if (!qpnp_chg_is_usb_chg_plugged_in(chip)) {
#ifdef CONFIG_MACH_OPPO
/* jingchun.wang@Onlinerd.Driver, 2013/12/29  Add for solve missing remove event */
		if(chip->usb_present) {
			chip->usb_present = false;
			chip->usbin_counts = 0;//sjc0522 for Find7s temp rising problem
			schedule_work(&chip->stop_charge_work);
			chip->prev_usb_max_ma = -EINVAL;
			power_supply_set_present(chip->usb_psy, chip->usb_present);
			/* jingchun.wang@Onlinerd.Driver, 2014/01/13  Add for if usb alread send disconnect ed event, it may miss usb plug out event */
			if(!chip->usb_present) {
				power_supply_set_online(chip->usb_psy, 0);
				power_supply_set_current_limit(chip->usb_psy, 0);
				power_supply_set_online(&chip->dc_psy, 0);
				power_supply_set_current_limit(&chip->dc_psy, 0);
			}
		}
#endif /*CONFIG_MACH_OPPO*/
		return;
	}

	vchg_mv = get_prop_charger_voltage_now(chip);

	pr_debug("%s %d %d\n", __func__, vchg_mv, chip->charger_status);

	if(chip->charger_status == CHARGER_STATUS_GOOD) {
		if(vchg_mv > CHARGER_SOFT_OVP_VOLTAGE || 
			vchg_mv <= CHARGER_SOFT_UVP_VOLTAGE) {
			pr_info("charger over voltage\n");

			qpnp_chg_usb_suspend_enable(chip, 1);

			qpnp_chg_iusbmax_set(chip, QPNP_CHG_I_MAX_MIN_100);
/* OPPO 2013-11-05 wangjc Add begin for use bq charger */
#ifdef CONFIG_BQ24196_CHARGER
			qpnp_chg_charge_en(chip, 0);
#endif
/* OPPO 2013-11-05 wangjc Add end */
			chip->charger_status = CHARGER_STATUS_OVER;
		}
	}else if(chip->charger_status == CHARGER_STATUS_OVER){
		if(vchg_mv < (CHARGER_SOFT_OVP_VOLTAGE - 100) && 
		     vchg_mv > (CHARGER_SOFT_UVP_VOLTAGE + 100)) {
			qpnp_chg_usb_suspend_enable(chip, 0);
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
			qpnp_start_charging(chip);
			chip->charger_status = CHARGER_STATUS_GOOD;
		}
	}
	return ;
}

static void qpnp_check_battery_uovp(struct qpnp_chg_chip *chip)
{
	int battery_voltage=0;
	enum chg_battery_status_type battery_status_pre;
	
	if (!qpnp_chg_is_usb_chg_plugged_in(chip))
		return;
	battery_status_pre = qpnp_battery_status_get(chip);	

	battery_voltage = get_prop_battery_voltage_now(chip);
	pr_debug("%s bat vol:%d\n", __func__, battery_voltage);
	if(battery_voltage > BATTERY_SOFT_OVP_VOLTAGE) {
		if (battery_status_pre == BATTERY_STATUS_GOOD) {
			qpnp_battery_status_set(chip, BATTERY_STATUS_BAD);
			qpnp_handle_battery_uovp(chip);
		}
	}
	else {
		if (battery_status_pre == BATTERY_STATUS_BAD) {
			qpnp_battery_status_set(chip, BATTERY_STATUS_GOOD);
			qpnp_handle_battery_restore_from_uovp(chip);
		}
	}

	return;
}


static int qpnp_check_battery_temp(struct qpnp_chg_chip *chip)
{
	int rc = -1;
	int temperature = 0;

	if (!qpnp_chg_is_usb_chg_plugged_in(chip))
		return rc;
	temperature = get_prop_batt_temp(chip);
	pr_debug("%s temp:%d\n", __func__, temperature);
	
	if(temperature < chip->mBatteryTempBoundT0) /* battery is cold */
	{
	        rc = handle_batt_temp_cold(chip);
	}
		else if( (temperature >=  chip->mBatteryTempBoundT0) && 
	         (temperature <= chip->mBatteryTempBoundT1) ) /* battery is more cool */
	{
	        rc = handle_batt_temp_little_cold(chip);
	}
	else if( (temperature >=  chip->mBatteryTempBoundT1) && 
	         (temperature <= chip->mBatteryTempBoundT2) ) /* battery is cool */
	{
	        rc = handle_batt_temp_cool(chip);
	}
	else if( (temperature > chip->mBatteryTempBoundT2) && 
	         (temperature < chip->mBatteryTempBoundT3) ) /* battery is normal */
	{
	        rc = handle_batt_temp_normal(chip);
	}
	else if( (temperature >= chip->mBatteryTempBoundT3) && 
	         (temperature <=  chip->mBatteryTempBoundT4) ) /* battery is warm */
	{
	        rc = handle_batt_temp_warm(chip);
	}
	else if(temperature > chip->mBatteryTempBoundT4)/* battery is hot */
	{
	        rc = handle_batt_temp_hot(chip);
	}
		
	return rc;
}

#define BATT_RECHARGING_VOLTAGE__LITTLE_COLD 	3800 * 1000
#define BATT_RECHARGING_VOLTAGE__COOL 			4100 * 1000
#define BATT_RECHARGING_VOLTAGE__NORMAL 		4200 * 1000
#define BATT_RECHARGING_VOLTAGE__WARM  		4000 * 1000
#define BATT_RECHARGING_CHECK_COUNT				50

static void qpnp_check_recharging(struct qpnp_chg_chip *chip)
{
	chg_cv_battery_temp_region_type batt_temp_region;
	int batt_volt;
	int compare_volt;
	static int count = 0;
	
	if (chip->charging_disabled)
		return;
/* jingchun.wang@Onlinerd.Driver, 2013/12/27  Add for battery display full wrong. */
	if (!qpnp_chg_is_usb_chg_plugged_in(chip)) {
		chip->chg_done = false;
		chip->chg_display_full = false;
		return;
	}
	if (!chip->chg_done)
		return;

	batt_temp_region = qpnp_battery_temp_region_get(chip);
	batt_volt = get_prop_battery_voltage_now(chip);
	switch (batt_temp_region){
		case CV_BATTERY_TEMP_REGION__COLD:
			return;
		case CV_BATTERY_TEMP_REGION_LITTLE__COLD:
			compare_volt = BATT_RECHARGING_VOLTAGE__LITTLE_COLD;
			break;
		case CV_BATTERY_TEMP_REGION__COOL:
			compare_volt = BATT_RECHARGING_VOLTAGE__COOL;
			break;
		case CV_BATTERY_TEMP_REGION__NORMAL:
			//wangjc add for authentication
#ifndef CONFIG_BATTERY_BQ27541
			compare_volt = BATT_RECHARGING_VOLTAGE__NORMAL;
#else /*CONFIG_BATTERY_BQ27541*/
			if(!get_prop_authenticate(chip)) {
				compare_volt = BATT_RECHARGING_VOLTAGE__COOL;
			} else {
				compare_volt = BATT_RECHARGING_VOLTAGE__NORMAL;
			}
#endif /*CONFIG_BATTERY_BQ27541*/
			
			break;
		case CV_BATTERY_TEMP_REGION__WARM:
			compare_volt = BATT_RECHARGING_VOLTAGE__WARM;
			break;
		case CV_BATTERY_TEMP_REGION__HOT:
			return;
		default:
			return;
	}

	if (batt_volt <  compare_volt){
		count++;
		pr_err("%s:count=%d compare_volt:%d\n", __func__, count, compare_volt);
	}else
		count= 0;
	if (count > BATT_RECHARGING_CHECK_COUNT){
		qpnp_start_charging(chip);
	}
}

#ifdef CONFIG_MACH_OPPO
/* OPPO 2014-05-22 sjc Add for Find7s temp rising problem */
#define USBIN_COUNT_FULL		10
#define USBIN_COUNT_FLAG		(USBIN_COUNT_FULL + 1)
static void qpnp_check_chg_current(struct qpnp_chg_chip *chip)
{
	if (get_pcb_version() < HW_VERSION__20
			|| qpnp_charger_type_get(chip) != POWER_SUPPLY_TYPE_USB_DCP
			|| qpnp_get_fast_chg_ing(chip))
		return;

	if (qpnp_battery_temp_region_get(chip) != CV_BATTERY_TEMP_REGION__NORMAL
			|| chip->charger_status != CHARGER_STATUS_GOOD)
		return;
		
	if (chip->usbin_counts < USBIN_COUNT_FULL)
		chip->usbin_counts++;
		
	if (chip->usbin_counts == USBIN_COUNT_FULL) {//60s
		if (chip->aicl_current > 900 && !atomic_read(&chip->suspended)) {
			qpnp_chg_iusbmax_set(chip, 900);
			chip->usbin_counts = USBIN_COUNT_FLAG;
			printk(KERN_ERR "%s: iusbmax set 900.\n", __func__);
		}
	}
}
#endif

/* OPPO 2013-12-22 liaofuchun add for fastchg */
#ifdef CONFIG_PIC1503_FASTCG
bool is_alow_fast_chg(struct qpnp_chg_chip *chip)
{
	bool auth = false;
	int temp = 0;
	int cap = 0;
	int chg_type = 0;
	bool low_temp_full = 0;

	auth = get_prop_authenticate(chip);
	temp = get_prop_batt_temp(chip);
	cap = get_prop_capacity(chip);
	chg_type = qpnp_charger_type_get(chip);
	low_temp_full = qpnp_get_fast_low_temp_full(chip);

	pr_err("%s auth:%d,temp:%d,cap:%d,chg_type:%d,low_temp_full:%d\n",__func__,auth,temp,cap,chg_type,low_temp_full);
	if(auth == false)
		return false;
	if(chg_type != POWER_SUPPLY_TYPE_USB_DCP)
		return false;
#ifndef CONFIG_MACH_FIND7OP
	if(temp < 105)
		return false;
	if((temp < 155) && (low_temp_full == 1)){
		return false;
	}
#else
	if(temp < 205)
		return false;
#endif
	if(temp > 420)
		return false;
	if(cap < 1)
		return false;
	if(cap > 96)
		return false;
	if(get_prop_fast_switch_to_normal(chip) == true){
		pr_err("%s fast_switch_to_noraml is true\n",__func__);
		return false;
	}
	return true;
}

#define AP_SWITCH_USB	GPIO_CFG(96, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA)
#define AP_SWITCH_FAST	GPIO_CFG(96, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA)

static void switch_fast_chg(struct qpnp_chg_chip *chip)
{

	int ret = 0;
	//pr_info("%s alow_fast_chg:%d\n",__func__,qpnp_get_fast_chg_allow(chip));

	if(gpio_get_value(96))
		return;

	if (!qpnp_chg_is_usb_chg_plugged_in(chip))
		return;

	if(qpnp_get_fast_chg_allow(chip) == false){
		if(is_alow_fast_chg(chip) == true) {
			//swtich on fast chg
			gpio_set_value(96, 1);
			ret = gpio_tlmm_config(AP_SWITCH_FAST, GPIO_CFG_ENABLE);
			if (ret) {
				pr_info("%s switch fast error %d\n", __func__, ret);
			}
			pr_info("%s switch on fastchg,GPIO96:%d\n", __func__,gpio_get_value(96));
			qpnp_set_fast_chg_allow(chip,true);
		}
	}
	pr_info("%s end,allow_fast_chg:%d\n",__func__,qpnp_get_fast_chg_allow(chip));
}
#endif
/*OPPO 2013-12-22 liaofuchun add end */

static void update_heartbeat(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, update_heartbeat_work);
	
/* OPPO 2013-12-22 liaofuchun add for fastchg */
#ifdef CONFIG_PIC1503_FASTCG	
	int charge_type = qpnp_charger_type_get(chip);

	if(get_prop_fast_chg_started(chip) == true) {
		switch_fast_chg(chip);
		pr_info("%s fast chg started,GPIO96:%d\n", __func__,gpio_get_value(96));
		power_supply_changed(&chip->batt_psy);
		//lfc add for disable normal charge begin
		if(qpnp_chg_get_charge_en() == 1 && qpnp_get_fast_chg_ing(chip) == 1){
			qpnp_chg_charge_en(chip,false);
		}
		//lfc add for disable normal charge end
		/*update time 6s*/
		schedule_delayed_work(&chip->update_heartbeat_work,
				      round_jiffies_relative(msecs_to_jiffies
							     (BATT_HEARTBEAT_INTERVAL)));
		return;
	}
	if(charge_type == POWER_SUPPLY_TYPE_USB_DCP) {
		switch_fast_chg(chip);
		//pr_info("%s fast chg not started,GPIO96:%d\n",__func__,gpio_get_value(96));
	}
#endif
/* OPPO 2013-12-22 liaofuchun add end*/
	qpnp_check_charger_uovp(chip);
	qpnp_check_charge_timeout(chip);
	qpnp_check_battery_uovp(chip);
	qpnp_check_battery_temp(chip);

	pr_debug("%s current:%d\n", __func__, get_prop_current_now(chip));
	
	qpnp_check_recharging(chip);

	qpnp_check_chg_current(chip);

	power_supply_changed(&chip->batt_psy);

	/*update time 6s*/
	schedule_delayed_work(&chip->update_heartbeat_work,
			      round_jiffies_relative(msecs_to_jiffies
						     (BATT_HEARTBEAT_INTERVAL)));
	//qpnp_dump_info(chip);
}

#ifdef CONFIG_BQ24196_CHARGER
/*OPPO 2013-10-19 liaofuchun add for bq24196 stop charging*/
static void qpnp_start_charge(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
		struct qpnp_chg_chip,start_charge_work);
	
	qpnp_chg_charge_en(chip, 1);
}
static void qpnp_stop_charge(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
		struct qpnp_chg_chip,stop_charge_work);
#ifdef CONFIG_PIC1503_FASTCG
	int ret = 0;
#endif
	
	/* OPPO 2013-12-22 liaofuchun add for fastchg */
	#ifndef CONFIG_PIC1503_FASTCG
	qpnp_chg_charge_en(chip, 0);
	#else
	//when plugged in fastchg,the OVP handle in usbin_valid_irq run
	if(get_prop_fast_chg_started(chip) == false) {
		qpnp_chg_charge_en(chip,0);
		pr_err("%s switch off fastchg\n", __func__);
		gpio_set_value(96, 0);
		ret = gpio_tlmm_config(AP_SWITCH_USB, GPIO_CFG_ENABLE);
		if (ret) {
			pr_err("%s switch usb error %d\n", __func__, ret);
		}
		//lfc modify for mistakenly set fast_switch_to_normal false,fastchg will restart after fastchg full
		//set_switch_to_noraml_false(chip);
	}
	set_fast_switch_to_normal_false(chip);
	set_fast_normal_to_warm_false(chip);
	//whenever charger gone, mask allo fast chg.
	qpnp_set_fast_chg_allow(chip,false);
	qpnp_set_fast_low_temp_full_false(chip);
	#endif
	/* OPPO 2013-12-22 liaofuchun add end */
	
	//solve the problem it can't charge when plug out within 5 minutes after full.
	chip->chg_done = false;
	/* jingchun.wang@Onlinerd.Driver, 2013/12/16  Add for charge timeout */
	chip->time_out = false;
	/* jingchun.wang@Onlinerd.Driver, 2013/12/27  Add for auto adapt current by software. */
	chip->aicl_current = 0;
	chip->chg_display_full = false;//wangjc add for charge full
}

#endif
/*OPPO 2013-10-19 liaofuchun add end*/
static void qpnp_charge_info_init(struct qpnp_chg_chip *chip)
{
	qpnp_battery_temp_region_set(chip, CV_BATTERY_TEMP_REGION__NORMAL);
	chip->mBatteryTempBoundT0 = AUTO_CHARGING_BATT_TEMP_T0;
	chip->mBatteryTempBoundT1 = AUTO_CHARGING_BATT_TEMP_T1;
	chip->mBatteryTempBoundT2 = AUTO_CHARGING_BATT_TEMP_T2;
	chip->mBatteryTempBoundT3 = AUTO_CHARGING_BATT_TEMP_T3;
	chip->mBatteryTempBoundT4 = AUTO_CHARGING_BATT_TEMP_T4;
	chip->charger_status = CHARGER_STATUS_GOOD;
	/* jingchun.wang@Onlinerd.Driver, 2013/12/27  Add for auto adapt current by software. */
	chip->aicl_current = 0;
/* jingchun.wang@Onlinerd.Driver, 2014/03/27  Add for fast charger control of 1+ */
}

static ssize_t test_temp_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size) {
	long val = simple_strtol(buf, NULL, 10);

	if(val == 9898) {
		use_fake_temp = false;
		fake_temp = 300;
	}else {
		use_fake_temp = true;
		fake_temp = val;
	}
		
	return size;
}
static DEVICE_ATTR(test_temp, S_IRUGO | S_IWUSR, NULL, test_temp_store);

static ssize_t test_chg_vol_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size) {
	long val = simple_strtol(buf, NULL, 10);

	if(val == 9898) {
		use_fake_chgvol= false;
		fake_chgvol= 0;
	}else {
		use_fake_chgvol = true;
		fake_chgvol = val;
	}
		
	return size;
}
static DEVICE_ATTR(test_chg_vol, S_IRUGO | S_IWUSR, NULL, test_chg_vol_store);

void qpnp_battery_gauge_register(struct qpnp_battery_gauge *batt_gauge)
{
	if (qpnp_batt_gauge) {
		qpnp_batt_gauge = batt_gauge;
		pr_err("qpnp-charger %s multiple battery gauge called\n",
								__func__);
	} else {
		qpnp_batt_gauge = batt_gauge;
	}
}
EXPORT_SYMBOL(qpnp_battery_gauge_register);

void qpnp_battery_gauge_unregister(struct qpnp_battery_gauge *batt_gauge)
{
	qpnp_batt_gauge = NULL;
}
EXPORT_SYMBOL(qpnp_battery_gauge_unregister);

void qpnp_external_charger_register(struct qpnp_external_charger *external_charger)
{
	if (qpnp_ext_charger) {
		qpnp_ext_charger = external_charger;
		pr_err("qpnp-charger %s multiple external charger called\n",
								__func__);
	} else {
		qpnp_ext_charger = external_charger;
	}
}
EXPORT_SYMBOL(qpnp_external_charger_register);

void qpnp_external_charger_unregister(struct qpnp_external_charger *external_charger)
{
	qpnp_ext_charger = NULL;
}
EXPORT_SYMBOL(qpnp_external_charger_unregister);

#if defined(CONFIG_FB)
/* jingchun.wang@Onlinerd.Driver, 2013/12/14  Add for reset charge current when screen is off */
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct qpnp_chg_chip *chip =
		container_of(self, struct qpnp_chg_chip, fb_notif);
	union power_supply_propval ret = {0,};

	if (evdata && evdata->data && chip) {
		if (event == FB_EVENT_BLANK) {
			blank = evdata->data;
			chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
			/* jingchun.wang@Onlinerd.Driver, 2013/12/30  Add for recognize dcp use 2000mA */
			if(ret.intval / 1000 != 2000) {
				//not DCP charger, don't care
				return 0;
			}
			
			/* jingchun.wang@Onlinerd.Driver, 2013/12/14  Add for reset charge current when temp is normal */
			if(qpnp_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__NORMAL) {
				if (*blank == FB_BLANK_UNBLANK) {
					atomic_set(&chip->suspended, 0);//sjc0522 for Find7s temp rising problem
					/* jingchun.wang@Onlinerd.Driver, 2013/12/27  Add for auto adapt current by software. */
					if(chip->aicl_current != 0) {
						if (chip->aicl_current >= 1500) {
							/* OPPO 2014-05-22 sjc Add for Find7s temp rising problem */
							if (get_pcb_version() >= HW_VERSION__20) {
								if (chip->usbin_counts == USBIN_COUNT_FLAG && !qpnp_get_fast_chg_ing(chip))
									qpnp_chg_iusbmax_set(chip, 900);
							} else {
#ifdef CONFIG_MACH_FIND7OP
/* OPPO 2014-06-03 sjc Modify for Find7op temp rising problem */
								qpnp_chg_iusbmax_set(chip, 1200);
#else
								qpnp_chg_iusbmax_set(chip, 1500);
#endif
							}
						} else {
							qpnp_chg_iusbmax_set(chip, chip->aicl_current);
						}
					}
					qpnp_chg_ibatmax_set(chip, chip->max_bat_chg_current);
				} else if (*blank == FB_BLANK_POWERDOWN) {
					atomic_set(&chip->suspended, 1);//sjc0522 for Find7s temp rising problem
					/* jingchun.wang@Onlinerd.Driver, 2013/12/27  Add for auto adapt current by software. */
					if(chip->aicl_current != 0) {
						qpnp_chg_iusbmax_set(chip, chip->aicl_current);
					}
					qpnp_chg_ibatmax_set(chip, 2496);
				}
			}
		}
	}

	return 0;
}
#endif /*CONFIG_FB*/

#endif /* CONFIG_MACH_OPPO */

static int __devinit
qpnp_charger_probe(struct spmi_device *spmi)
{
	u8 subtype;
	struct qpnp_chg_chip	*chip;
	struct resource *resource;
	struct spmi_resource *spmi_resource;
	int rc = 0;

	chip = devm_kzalloc(&spmi->dev,
			sizeof(struct qpnp_chg_chip), GFP_KERNEL);
	if (chip == NULL) {
		pr_err("kzalloc() failed.\n");
		return -ENOMEM;
	}

	chip->prev_usb_max_ma = -EINVAL;
	chip->fake_battery_soc = -EINVAL;
	chip->dev = &(spmi->dev);
	chip->spmi = spmi;

	chip->usb_psy = power_supply_get_by_name("usb");
	if (!chip->usb_psy) {
		pr_err("usb supply not found deferring probe\n");
		rc = -EPROBE_DEFER;
		goto fail_chg_enable;
	}

	mutex_init(&chip->jeita_configure_lock);
	spin_lock_init(&chip->usbin_health_monitor_lock);
	alarm_init(&chip->reduce_power_stage_alarm, ANDROID_ALARM_RTC_WAKEUP,
			qpnp_chg_reduce_power_stage_callback);
	INIT_WORK(&chip->reduce_power_stage_work,
			qpnp_chg_reduce_power_stage_work);
	mutex_init(&chip->batfet_vreg_lock);
	INIT_WORK(&chip->ocp_clear_work,
			qpnp_chg_ocp_clear_work);
	INIT_WORK(&chip->batfet_lcl_work,
			qpnp_chg_batfet_lcl_work);
	INIT_WORK(&chip->insertion_ocv_work,
			qpnp_chg_insertion_ocv_work);

	/* Get all device tree properties */
	rc = qpnp_charger_read_dt_props(chip);
	if (rc)
		return rc;

	/*
	 * Check if bat_if is set in DT and make sure VADC is present
	 * Also try loading the battery data profile if bat_if exists
	 */
	spmi_for_each_container_dev(spmi_resource, spmi) {
		if (!spmi_resource) {
			pr_err("qpnp_chg: spmi resource absent\n");
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			goto fail_chg_enable;
		}

		if (subtype == SMBB_BAT_IF_SUBTYPE ||
			subtype == SMBBP_BAT_IF_SUBTYPE ||
			subtype == SMBCL_BAT_IF_SUBTYPE) {
			chip->vadc_dev = qpnp_get_vadc(chip->dev, "chg");
			if (IS_ERR(chip->vadc_dev)) {
				rc = PTR_ERR(chip->vadc_dev);
				if (rc != -EPROBE_DEFER)
					pr_err("vadc property missing\n");
				goto fail_chg_enable;
			}

			if (subtype == SMBB_BAT_IF_SUBTYPE) {
				chip->iadc_dev = qpnp_get_iadc(chip->dev,
						"chg");
				if (IS_ERR(chip->iadc_dev)) {
					rc = PTR_ERR(chip->iadc_dev);
					if (rc != -EPROBE_DEFER)
						pr_err("iadc property missing\n");
					goto fail_chg_enable;
				}
			}

			rc = qpnp_chg_load_battery_data(chip);
			if (rc)
				goto fail_chg_enable;
		}
	}

	spmi_for_each_container_dev(spmi_resource, spmi) {
		if (!spmi_resource) {
			pr_err("qpnp_chg: spmi resource absent\n");
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			goto fail_chg_enable;
		}

		switch (subtype) {
		case SMBB_CHGR_SUBTYPE:
		case SMBBP_CHGR_SUBTYPE:
		case SMBCL_CHGR_SUBTYPE:
			chip->chgr_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_BUCK_SUBTYPE:
		case SMBBP_BUCK_SUBTYPE:
		case SMBCL_BUCK_SUBTYPE:
			chip->buck_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}

			rc = qpnp_chg_masked_write(chip,
				chip->buck_base + SEC_ACCESS,
				0xFF,
				0xA5, 1);

			rc = qpnp_chg_masked_write(chip,
				chip->buck_base + BUCK_VCHG_OV,
				0xff,
				0x00, 1);

			if (chip->duty_cycle_100p) {
				rc = qpnp_buck_set_100_duty_cycle_enable(chip,
						1);
				if (rc) {
					pr_err("failed to set duty cycle %d\n",
						rc);
					goto fail_chg_enable;
				}
			}

			break;
		case SMBB_BAT_IF_SUBTYPE:
		case SMBBP_BAT_IF_SUBTYPE:
		case SMBCL_BAT_IF_SUBTYPE:
			chip->bat_if_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_USB_CHGPTH_SUBTYPE:
		case SMBBP_USB_CHGPTH_SUBTYPE:
		case SMBCL_USB_CHGPTH_SUBTYPE:
			chip->usb_chgpth_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				if (rc != -EPROBE_DEFER)
					pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_DC_CHGPTH_SUBTYPE:
			chip->dc_chgpth_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_BOOST_SUBTYPE:
		case SMBBP_BOOST_SUBTYPE:
			chip->boost_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				if (rc != -EPROBE_DEFER)
					pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_MISC_SUBTYPE:
		case SMBBP_MISC_SUBTYPE:
		case SMBCL_MISC_SUBTYPE:
			chip->misc_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype=0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		default:
			pr_err("Invalid peripheral subtype=0x%x\n", subtype);
			rc = -EINVAL;
			goto fail_chg_enable;
		}
	}
	dev_set_drvdata(&spmi->dev, chip);
	device_init_wakeup(&spmi->dev, 1);

	chip->insertion_ocv_uv = -EINVAL;
	chip->batt_present = qpnp_chg_is_batt_present(chip);
	if (chip->bat_if_base) {
		chip->batt_psy.name = "battery";
		chip->batt_psy.type = POWER_SUPPLY_TYPE_BATTERY;
		chip->batt_psy.properties = msm_batt_power_props;
		chip->batt_psy.num_properties =
			ARRAY_SIZE(msm_batt_power_props);
		chip->batt_psy.get_property = qpnp_batt_power_get_property;
		chip->batt_psy.set_property = qpnp_batt_power_set_property;
		chip->batt_psy.property_is_writeable =
				qpnp_batt_property_is_writeable;
		chip->batt_psy.external_power_changed =
				qpnp_batt_external_power_changed;
		chip->batt_psy.supplied_to = pm_batt_supplied_to;
		chip->batt_psy.num_supplicants =
				ARRAY_SIZE(pm_batt_supplied_to);

		rc = power_supply_register(chip->dev, &chip->batt_psy);
		if (rc < 0) {
			pr_err("batt failed to register rc = %d\n", rc);
			goto fail_chg_enable;
		}
		INIT_WORK(&chip->adc_measure_work,
			qpnp_bat_if_adc_measure_work);
		INIT_WORK(&chip->adc_disable_work,
			qpnp_bat_if_adc_disable_work);
	}

	INIT_DELAYED_WORK(&chip->eoc_work, qpnp_eoc_work);
#ifndef CONFIG_BQ24196_CHARGER
	INIT_DELAYED_WORK(&chip->arb_stop_work, qpnp_arb_stop_work);
#endif
	INIT_DELAYED_WORK(&chip->usbin_health_check,
			qpnp_usbin_health_check_work);
	INIT_WORK(&chip->soc_check_work, qpnp_chg_soc_check_work);
	INIT_DELAYED_WORK(&chip->aicl_check_work, qpnp_aicl_check_work);

	if (chip->dc_chgpth_base) {
		chip->dc_psy.name = "qpnp-dc";
		chip->dc_psy.type = POWER_SUPPLY_TYPE_MAINS;
		chip->dc_psy.supplied_to = pm_power_supplied_to;
		chip->dc_psy.num_supplicants = ARRAY_SIZE(pm_power_supplied_to);
		chip->dc_psy.properties = pm_power_props_mains;
		chip->dc_psy.num_properties = ARRAY_SIZE(pm_power_props_mains);
		chip->dc_psy.get_property = qpnp_power_get_property_mains;
		chip->dc_psy.set_property = qpnp_dc_power_set_property;
		chip->dc_psy.property_is_writeable =
				qpnp_dc_property_is_writeable;

		rc = power_supply_register(chip->dev, &chip->dc_psy);
		if (rc < 0) {
			pr_err("power_supply_register dc failed rc=%d\n", rc);
			goto unregister_batt;
		}
	}

	/* Turn on appropriate workaround flags */
	rc = qpnp_chg_setup_flags(chip);
	if (rc < 0) {
		pr_err("failed to setup flags rc=%d\n", rc);
		goto unregister_dc_psy;
	}

	if (chip->maxinput_dc_ma && chip->dc_chgpth_base) {
		rc = qpnp_chg_idcmax_set(chip, chip->maxinput_dc_ma);
		if (rc) {
			pr_err("Error setting idcmax property %d\n", rc);
			goto unregister_dc_psy;
		}
	}

	if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
							&& chip->bat_if_base) {
		chip->adc_param.low_temp = chip->cool_bat_decidegc;
		chip->adc_param.high_temp = chip->warm_bat_decidegc;
		chip->adc_param.timer_interval = ADC_MEAS2_INTERVAL_1S;
		chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
		chip->adc_param.btm_ctx = chip;
		chip->adc_param.threshold_notification =
						qpnp_chg_adc_notification;
		chip->adc_param.channel = LR_MUX1_BATT_THERM;

		if (get_prop_batt_present(chip)) {
			rc = qpnp_adc_tm_channel_measure(chip->adc_tm_dev,
							&chip->adc_param);
			if (rc) {
				pr_err("request ADC error %d\n", rc);
				goto unregister_dc_psy;
			}
		}
	}
	rc = qpnp_chg_bat_if_configure_btc(chip);
	if (rc) {
		pr_err("failed to configure btc %d\n", rc);
		goto unregister_dc_psy;
	}

#ifndef CONFIG_BQ24196_CHARGER
	qpnp_chg_charge_en(chip, !chip->charging_disabled);
#ifdef CONFIG_MACH_OPPO
	if (get_boot_mode() != MSM_BOOT_MODE__NORMAL) {
		qpnp_chg_usb_suspend_enable(chip, 1);
	}
#endif
	qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
#endif /* CONFIG_BQ24196_CHARGER */
	qpnp_chg_set_appropriate_vddmax(chip);

	rc = qpnp_chg_request_irqs(chip);
	if (rc) {
		pr_err("failed to request interrupts %d\n", rc);
		goto unregister_dc_psy;
	}

	qpnp_chg_usb_chg_gone_irq_handler(chip->chg_gone.irq, chip);
	qpnp_chg_usb_usbin_valid_irq_handler(chip->usbin_valid.irq, chip);
#ifndef CONFIG_BQ24196_CHARGER
	qpnp_chg_dc_dcin_valid_irq_handler(chip->dcin_valid.irq, chip);
#endif
	power_supply_set_present(chip->usb_psy,
			qpnp_chg_is_usb_chg_plugged_in(chip));

	/* Set USB psy online to avoid userspace from shutting down if battery
	 * capacity is at zero and no chargers online. */
	if (qpnp_chg_is_usb_chg_plugged_in(chip))
		power_supply_set_online(chip->usb_psy, 1);

#ifndef CONFIG_BQ24196_CHARGER
	schedule_delayed_work(&chip->aicl_check_work,
		msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
#endif
#ifdef CONFIG_MACH_OPPO
	qpnp_charge_info_init(chip);

	INIT_DELAYED_WORK(&chip->update_heartbeat_work,
							update_heartbeat);
	schedule_delayed_work(&chip->update_heartbeat_work,
			      round_jiffies_relative(msecs_to_jiffies
						(BATT_HEARTBEAT_INTERVAL)));
	/*OPPO 2013-10-24 liaofuchun add begin for bq24196 charger*/
#ifdef CONFIG_BQ24196_CHARGER
	INIT_WORK(&chip->stop_charge_work,qpnp_stop_charge);
	INIT_WORK(&chip->start_charge_work,qpnp_start_charge);
	INIT_WORK(&chip->ext_charger_hwinit_work,qpnp_chg_ext_charger_hwinit_work);
#endif
	/*OPPO 2013-10-24 liaofuchun add end*/

#ifdef CONFIG_MACH_OPPO
/* OPPO 2014-05-22 sjc Add for Find7s temp rising problem */
	atomic_set(&chip->suspended, 0);
	chip->usbin_counts = 0;
#endif

#ifdef CONFIG_FB
	/* jingchun.wang@Onlinerd.Driver, 2013/12/14  Add for reset charge current when screen is off */
	chip->fb_notif.notifier_call = fb_notifier_callback;

	rc = fb_register_client(&chip->fb_notif);

	if (rc)
		pr_err("Unable to register fb_notifier: %d\n", rc);
#endif
	rc = device_create_file(chip->dev, &dev_attr_test_temp);
	if (rc < 0) {
		pr_err("%s: creat test temp file failed ret = %d\n",
					__func__, rc);
		device_remove_file(chip->dev, &dev_attr_test_temp);
	}
	rc = device_create_file(chip->dev, &dev_attr_test_chg_vol);
	if (rc < 0) {
		pr_err("%s: creat test charger voltage file failed ret = %d\n",
					__func__, rc);
		device_remove_file(chip->dev, &dev_attr_test_chg_vol);
	}
#endif /* CONFIG_MACH_OPPO */

	pr_info("success chg_dis = %d, bpd = %d, usb = %d, dc = %d b_health = %d batt_present = %d\n",
			chip->charging_disabled,
			chip->bpd_detection,
			qpnp_chg_is_usb_chg_plugged_in(chip),
			qpnp_chg_is_dc_chg_plugged_in(chip),
			get_prop_batt_present(chip),
			get_prop_batt_health(chip));
	return 0;

unregister_dc_psy:
	if (chip->dc_chgpth_base)
		power_supply_unregister(&chip->dc_psy);
unregister_batt:
	if (chip->bat_if_base)
		power_supply_unregister(&chip->batt_psy);
fail_chg_enable:
	regulator_unregister(chip->otg_vreg.rdev);
	regulator_unregister(chip->boost_vreg.rdev);
	return rc;
}

static int __devexit
qpnp_charger_remove(struct spmi_device *spmi)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(&spmi->dev);
	if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
						&& chip->batt_present) {
		qpnp_adc_tm_disable_chan_meas(chip->adc_tm_dev,
							&chip->adc_param);
	}

	cancel_delayed_work_sync(&chip->aicl_check_work);
	power_supply_unregister(&chip->dc_psy);
	cancel_work_sync(&chip->soc_check_work);
	cancel_delayed_work_sync(&chip->usbin_health_check);
	cancel_delayed_work_sync(&chip->arb_stop_work);
	cancel_delayed_work_sync(&chip->eoc_work);
	cancel_work_sync(&chip->adc_disable_work);
	cancel_work_sync(&chip->adc_measure_work);
	power_supply_unregister(&chip->batt_psy);
	cancel_work_sync(&chip->batfet_lcl_work);
	cancel_work_sync(&chip->insertion_ocv_work);
	cancel_work_sync(&chip->reduce_power_stage_work);
	alarm_cancel(&chip->reduce_power_stage_alarm);

	mutex_destroy(&chip->batfet_vreg_lock);
	mutex_destroy(&chip->jeita_configure_lock);

	regulator_unregister(chip->otg_vreg.rdev);
	regulator_unregister(chip->boost_vreg.rdev);

#ifdef CONFIG_FB
	if (fb_unregister_client(&chip->fb_notif))
		pr_err("Error occurred while unregistering fb_notifier.\n");
#endif

	return 0;
}

#ifdef CONFIG_MACH_OPPO
static void qpnp_charger_shutdown(struct spmi_device *spmi)
{
#ifdef CONFIG_BQ24196_CHARGER
	struct qpnp_chg_chip *chip = dev_get_drvdata(&spmi->dev);

	if (qpnp_chg_regulator_otg_is_enabled(chip->otg_vreg.rdev))
		qpnp_chg_regulator_otg_disable(chip->otg_vreg.rdev);
#endif
}
#endif

static int qpnp_chg_resume(struct device *dev)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (chip->bat_if_base) {
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BATT_THERM_FORCE_ON, 1);
		if (rc)
			pr_debug("failed to force on VREF_BAT_THM rc=%d\n", rc);
	}

	return rc;
}

static int qpnp_chg_suspend(struct device *dev)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (chip->bat_if_base) {
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BAT_THM_ENABLED_FSM, 1);
		if (rc)
			pr_debug("failed to set FSM VREF_BAT_THM rc=%d\n", rc);
	}

	return rc;
}

static const struct dev_pm_ops qpnp_chg_pm_ops = {
	.resume		= qpnp_chg_resume,
	.suspend	= qpnp_chg_suspend,
};

static struct spmi_driver qpnp_charger_driver = {
	.probe		= qpnp_charger_probe,
	.remove		= __devexit_p(qpnp_charger_remove),
#ifdef CONFIG_MACH_OPPO
	.shutdown	= qpnp_charger_shutdown,
#endif
	.driver		= {
		.name		= QPNP_CHARGER_DEV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= qpnp_charger_match_table,
		.pm		= &qpnp_chg_pm_ops,
	},
};

/**
 * qpnp_chg_init() - register spmi driver for qpnp-chg
 */
int __init
qpnp_chg_init(void)
{
	return spmi_driver_register(&qpnp_charger_driver);
}
module_init(qpnp_chg_init);

static void __exit
qpnp_chg_exit(void)
{
	spmi_driver_unregister(&qpnp_charger_driver);
}
module_exit(qpnp_chg_exit);


MODULE_DESCRIPTION("QPNP charger driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" QPNP_CHARGER_DEV_NAME);
