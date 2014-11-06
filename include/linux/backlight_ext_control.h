/*
 * include/linux/backlight_ext_control.h
 *
 * Backlight External Control v1.0
 *
 * Author: Zane Zaminsky <cyxman@yahoo.com>
 *         Credits to andip71 (alias Lord Boeffla) for original dynamic
 *         fsync control varaint.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _BACKLIGHT_EXT_CONTROL_H
#define _BACKLIGHT_EXT_CONTROL_H

/*
 * Below 'hooks' for this version are in the status function of the
 * TI LM3630 backlight driver for One Plus One devices
 * (drivers/video/backlight/lm3630_bl.c)
 */

/*
 * General flag for external usage
 * (true = backlight on, false = backlight off)
 */
extern bool backlight_on;

/*
 * External functions:
 */

// controls file sync depending on screen state in fsync driver
#ifdef CONFIG_DYNAMIC_FSYNC
extern void dyn_fsync_resume(void);
extern void dyn_fsync_suspend(void);
#endif

// controls suspend/resume state in zzmoove governor
#ifdef CONFIG_CPU_FREQ_GOV_ZZMOOVE
extern void zzmoove_suspend(void);
extern void zzmoove_resume(void);
#endif
#endif
