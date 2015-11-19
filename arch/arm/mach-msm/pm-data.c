/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
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


#include "pm.h"

struct msm_pm_platform_data msm_pm_sleep_modes[] = {
	[MSM_PM_MODE(0, MSM_PM_SLEEP_MODE_POWER_COLLAPSE)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(0, MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(0, MSM_PM_SLEEP_MODE_RETENTION)] = {
		.idle_supported = 1,
		.suspend_supported = 0,
		.idle_enabled = 0,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(0, MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 1,
		.suspend_enabled = 1,
	},

	[MSM_PM_MODE(1, MSM_PM_SLEEP_MODE_POWER_COLLAPSE)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 1,
	},

	[MSM_PM_MODE(1, MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(1, MSM_PM_SLEEP_MODE_RETENTION)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(1, MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT)] = {
		.idle_supported = 1,
		.suspend_supported = 0,
		.idle_enabled = 1,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(2, MSM_PM_SLEEP_MODE_POWER_COLLAPSE)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 1,
	},

	[MSM_PM_MODE(2, MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(2, MSM_PM_SLEEP_MODE_RETENTION)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(2, MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT)] = {
		.idle_supported = 1,
		.suspend_supported = 0,
		.idle_enabled = 1,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(3, MSM_PM_SLEEP_MODE_POWER_COLLAPSE)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 1,
	},

	[MSM_PM_MODE(3, MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(3, MSM_PM_SLEEP_MODE_RETENTION)] = {
		.idle_supported = 1,
		.suspend_supported = 1,
		.idle_enabled = 0,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(3, MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT)] = {
		.idle_supported = 1,
		.suspend_supported = 0,
		.idle_enabled = 1,
		.suspend_enabled = 0,
	},

	[MSM_PM_MODE(3, MSM_PM_SLEEP_MODE_NR)] = {
		.idle_supported = 0,
		.suspend_supported = 0,
		.idle_enabled = 0,
		.suspend_enabled = 0,
	},
};

// add enable/disable sleep mode
// mode : msm_pm_sleep_mode @ /android/kernel/arch/arm/mach-msm/pm.h
// Usage>>>
//		enter_mode : msm_pm_sleep_mode_enable(MSM_PM_SLEEP_MODE_RETENTION, 0);
//		exit_mode : msm_pm_sleep_mode_enable(MSM_PM_SLEEP_MODE_RETENTION, 1);
void msm_pm_sleep_mode_enable(unsigned int mode, bool enable)
{
	unsigned int cpu;
	int idx;

	for_each_possible_cpu(cpu) {
		idx = MSM_PM_MODE(cpu, mode);
		msm_pm_sleep_modes[idx].idle_enabled = enable;
	}

}

// add MSM_PM_SLEEP_MODE_RETENTION enable/disable sleep mode
// mode : msm_pm_sleep_mode @ /android/kernel/arch/arm/mach-msm/pm.h
// Usage>>>
//		enter_mode : msm_pm_sleep_mode_enable(0);
//		exit_mode : msm_pm_sleep_mode_enable(1);
void msm_pm_retention_mode_enable(bool enable)
{
	msm_pm_sleep_mode_enable(MSM_PM_SLEEP_MODE_RETENTION, enable);
}
