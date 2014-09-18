/*
 * Author: andip71, 18.09.2014
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

#define AC_CHARGE_LEVEL_DEFAULT 2000

#define AC_CHARGE_LEVEL_MIN 100
#define AC_CHARGE_LEVEL_MAX 2200

extern int usb_level;

#define USB_CHARGE_LEVEL_DEFAULT 500

#define USB_CHARGE_LEVEL_MIN 0
#define USB_CHARGE_LEVEL_MAX 1600

extern char charge_info_text[30];
extern int charge_info_level;
