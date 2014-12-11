/*
 * Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2013, LGE Inc. All rights reserved
 * Copyright (c) 2014, savoca <adeddo27@gmail.com>
 * Copyright (c) 2014, Paul Reioux <reioux@gmail.com>
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

#ifndef __MDSS_MDP_KCAL_CTRL_H
#define __MDSS_MDP_KCAL_CTRL_H

#define KCAL_DATA_R 0x01
#define KCAL_DATA_G 0x02
#define KCAL_DATA_B 0x03

#define NUM_QLUT 256
#define MAX_KCAL (NUM_QLUT - 1)

#define SCALED_BY_KCAL(rgb, kcal) \
	(((((unsigned int)(rgb) * (unsigned int)(kcal)) << 10) / \
		(unsigned int)MAX_KCAL) >> 10)

struct kcal_lut_data {
	int red;
	int green;
	int blue;
	int minimum;
};

void update_preset_lcdc_lut(int kr, int kg, int kb);

int mdss_mdp_pp_get_kcal(int data);

#endif
