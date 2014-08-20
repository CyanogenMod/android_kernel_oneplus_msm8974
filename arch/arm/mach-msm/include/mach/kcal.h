/*
 * arch/arm/mach-msm/include/mach/kcal.h
 *
 * Copyright (c) 2013, LGE Inc. All rights reserved
 * Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014, Paul Reioux <reioux@gmail.com>
 * Copyright (c) 2014, savoca <adeddo27@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

struct kcal_platform_data {
	int (*set_values) (int r, int g, int b);
	int (*get_values) (int *r, int *g, int *b);
	int (*refresh_display) (void);
	int (*set_min) (int min);
	int (*get_min) (int *min);
};

struct kcal_lut_data {
	int r;
	int g;
	int b;
	int min;
	int stat;
};

int update_preset_lcdc_lut(int kr, int kg, int kb);

int __init kcal_ctrl_init(void);
