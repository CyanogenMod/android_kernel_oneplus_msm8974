/*
 * Author: andip71, 05.11.2014
 *
 * Version 1.1
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

extern int ac_level;
extern int usb_level;
extern char charge_info_text[30];
extern int charge_level;
extern int charge_info_level_cur;
extern int charge_info_level_req;

#define AC_CHARGE_LEVEL_DEFAULT 0	// 0 = stock charging logic will apply
#define AC_CHARGE_LEVEL_MIN 0
#define AC_CHARGE_LEVEL_MAX 2200

#define USB_CHARGE_LEVEL_DEFAULT 0	// 0 = stock charging logic will apply
#define USB_CHARGE_LEVEL_MIN 0
#define USB_CHARGE_LEVEL_MAX 1600
