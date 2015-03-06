/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
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
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/memory.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/krait-regulator.h>
#include <linux/msm_tsens.h>
#include <linux/msm_thermal.h>
#include <asm/mach/map.h>
#include <asm/hardware/gic.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <mach/board.h>
#include <mach/gpiomux.h>
#include <mach/msm_iomap.h>
#ifdef CONFIG_ION_MSM
#include <linux/msm_ion.h>
#endif
#include <mach/msm_memtypes.h>
#include <mach/msm_smd.h>
#include <mach/restart.h>
#include <mach/rpm-smd.h>
#include <mach/rpm-regulator-smd.h>
#include <mach/socinfo.h>
#include <mach/msm_smem.h>
#include "board-dt.h"
#include "clock.h"
#include "devices.h"
#include "spm.h"
#include "pm.h"
#include "modem_notifier.h"
#include "platsmp.h"

#include <linux/persistent_ram.h>
#include <linux/gpio.h>

#include <linux/pcb_version.h>

#ifdef CONFIG_KEXEC_HARDBOOT
#include <asm/setup.h>
#include <asm/memory.h>
#include <linux/memblock.h>
#define OPPO_PERSISTENT_RAM_SIZE	(SZ_1M)
#endif

static struct platform_device *ram_console_dev;

static struct persistent_ram_descriptor msm_prd[] __initdata = {
	{
		.name = "ram_console",
		.size = SZ_1M,
	},
};

static struct persistent_ram msm_pr __initdata = {
	.descs = msm_prd,
	.num_descs = ARRAY_SIZE(msm_prd),
	.start = PLAT_PHYS_OFFSET + SZ_1G + SZ_256M,
	.size = SZ_1M,
};

void __init msm_8974_reserve(void)
{
#ifdef CONFIG_KEXEC_HARDBOOT
	// Reserve space for hardboot page - just after ram_console,
	// at the start of second memory bank
	int ret;
	phys_addr_t start;
	struct membank* bank;
	
	if (meminfo.nr_banks < 2) {
		pr_err("%s: not enough membank\n", __func__);
		return;
	}
	
	bank = &meminfo.bank[1];
	start = bank->start + SZ_1M + OPPO_PERSISTENT_RAM_SIZE;
	ret = memblock_remove(start, SZ_1M);
	if(!ret)
		pr_info("Hardboot page reserved at 0x%X\n", start);
	else
		pr_err("Failed to reserve space for hardboot page at 0x%X!\n", start);
#endif
	persistent_ram_early_init(&msm_pr);
	of_scan_flat_dt(dt_scan_for_memory_reserve, NULL);
}

/*
 * Used to satisfy dependencies for devices that need to be
 * run early or in a particular order. Most likely your device doesn't fall
 * into this category, and thus the driver should not be added here. The
 * EPROBE_DEFER can satisfy most dependency problems.
 */
void __init msm8974_add_drivers(void)
{
	msm_smem_init();
	msm_init_modem_notifier_list();
	msm_smd_init();
	msm_rpm_driver_init();
	msm_pm_sleep_status_init();
	rpm_regulator_smd_driver_init();
	msm_spm_device_init();
	krait_power_init();
	if (of_board_is_rumi())
		msm_clock_init(&msm8974_rumi_clock_init_data);
	else
		msm_clock_init(&msm8974_clock_init_data);
	tsens_tm_init_driver();
	msm_thermal_device_init();
}

#define DISP_ESD_GPIO 28
#define DISP_LCD_UNK_GPIO 62
static void __init oppo_config_display(void)
{
	int rc;

	rc = gpio_request(DISP_ESD_GPIO, "disp_esd");
	if (rc) {
		pr_err("%s: request DISP_ESD GPIO failed, rc: %d",
				__func__, rc);
		return;
	}

	rc = gpio_tlmm_config(GPIO_CFG(DISP_ESD_GPIO, 0,
				GPIO_CFG_INPUT,
				GPIO_CFG_PULL_DOWN,
				GPIO_CFG_2MA),
			GPIO_CFG_ENABLE);
	if (rc) {
		pr_err("%s: unable to configure DISP_ESD GPIO, rc: %d",
				__func__, rc);
		gpio_free(DISP_ESD_GPIO);
		return;
	}

	rc = gpio_direction_input(DISP_ESD_GPIO);
	if (rc) {
		pr_err("%s: set direction for DISP_ESD GPIO failed, rc: %d",
				__func__, rc);
		gpio_free(DISP_ESD_GPIO);
		return;
	}

	if (get_pcb_version() >= HW_VERSION__20) {
		rc = gpio_request(DISP_LCD_UNK_GPIO, "lcd_unk");
		if (rc) {
			pr_err("%s: request DISP_UNK GPIO failed, rc: %d",
					__func__, rc);
			return;
		}

		rc = gpio_direction_output(DISP_LCD_UNK_GPIO, 0);
		if (rc) {
			pr_err("%s: set direction for DISP_LCD_UNK GPIO failed, rc: %d",
					__func__, rc);
			gpio_free(DISP_LCD_UNK_GPIO);
			return;
		}
	}
}

static void __init oppo_config_ramconsole(void)
{
	int ret;

	ram_console_dev = platform_device_alloc("ram_console", -1);
	if (!ram_console_dev) {
		pr_err("%s: Unable to allocate memory for RAM console device",
				__func__);
		return;
	}

	ret = platform_device_add(ram_console_dev);
	if (ret) {
		pr_err("%s: Unable to add RAM console device", __func__);
		return;
	}
}

static struct rpm_regulator* sns_reg = 0;
static struct delayed_work sns_dwork;

static void oppo_config_sns_reg_release(struct work_struct *work)
{
	int ret;
	pr_info("Releasing sensor regulator\n");
	if (sns_reg) {
		ret = rpm_regulator_disable(sns_reg);
		if (ret)
			pr_err("8941_l18 rpm_regulator_disable failed (%d)\n", ret);
		rpm_regulator_put(sns_reg);
		sns_reg = 0;
	}
}

static void __init oppo_config_sns_power(void)
{
	int ret;

	sns_reg = rpm_regulator_get(NULL, "8941_l18");
	if (IS_ERR_OR_NULL(sns_reg)) {
		ret = PTR_ERR(sns_reg);
		pr_err("8941_l18 rpm_regulator_get failed (%d)\n", ret);
		sns_reg = 0;
	} else {
		ret = rpm_regulator_enable(sns_reg);
		if (ret)
			pr_err("8941_l18 rpm_regulator_enable failed (%d)\n", ret);

		INIT_DELAYED_WORK(&sns_dwork, oppo_config_sns_reg_release);
		schedule_delayed_work(&sns_dwork, msecs_to_jiffies(30000));
	}
}

static struct of_dev_auxdata msm_hsic_host_adata[] = {
	OF_DEV_AUXDATA("qcom,hsic-host", 0xF9A00000, "msm_hsic_host", NULL),
	{}
};

static struct of_dev_auxdata msm8974_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("qcom,hsusb-otg", 0xF9A55000, \
			"msm_otg", NULL),
	OF_DEV_AUXDATA("qcom,ehci-host", 0xF9A55000, \
			"msm_ehci_host", NULL),
	OF_DEV_AUXDATA("qcom,dwc-usb3-msm", 0xF9200000, \
			"msm_dwc3", NULL),
	OF_DEV_AUXDATA("qcom,usb-bam-msm", 0xF9304000, \
			"usb_bam", NULL),
	OF_DEV_AUXDATA("qcom,spi-qup-v2", 0xF9924000, \
			"spi_qsd.1", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9824000, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF98A4000, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9864000, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF98E4000, \
			"msm_sdcc.4", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9824900, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF98A4900, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9864900, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF98E4900, \
			"msm_sdcc.4", NULL),
	OF_DEV_AUXDATA("qcom,msm-rng", 0xF9BFF000, \
			"msm_rng", NULL),
	OF_DEV_AUXDATA("qcom,qseecom", 0xFE806000, \
			"qseecom", NULL),
	OF_DEV_AUXDATA("qcom,mdss_mdp", 0xFD900000, "mdp.0", NULL),
	OF_DEV_AUXDATA("qcom,msm-tsens", 0xFC4A8000, \
			"msm-tsens", NULL),
	OF_DEV_AUXDATA("qcom,qcedev", 0xFD440000, \
			"qcedev.0", NULL),
	OF_DEV_AUXDATA("qcom,hsic-host", 0xF9A00000, \
			"msm_hsic_host", NULL),
	OF_DEV_AUXDATA("qcom,hsic-smsc-hub", 0, "msm_smsc_hub",
			msm_hsic_host_adata),
	{}
};

static void __init msm8974_map_io(void)
{
	msm_map_8974_io();
}

void __init msm8974_init(void)
{
	struct of_dev_auxdata *adata = msm8974_auxdata_lookup;

	if (socinfo_init() < 0)
		pr_err("%s: socinfo_init() failed\n", __func__);

	msm_8974_init_gpiomux();
	regulator_has_full_constraints();
	board_dt_populate(adata);
	msm8974_add_drivers();
	oppo_config_display();
	oppo_config_ramconsole();
	oppo_config_sns_power();
}

static const char *msm8974_dt_match[] __initconst = {
	"qcom,msm8974",
	"qcom,apq8074",
	NULL
};

DT_MACHINE_START(MSM8974_DT, "Qualcomm MSM 8974 (Flattened Device Tree)")
	.map_io = msm8974_map_io,
	.init_irq = msm_dt_init_irq,
	.init_machine = msm8974_init,
	.handle_irq = gic_handle_irq,
	.timer = &msm_dt_timer,
	.dt_compat = msm8974_dt_match,
	.reserve = msm_8974_reserve,
	.restart = msm_restart,
	.smp = &msm8974_smp_ops,
MACHINE_END
