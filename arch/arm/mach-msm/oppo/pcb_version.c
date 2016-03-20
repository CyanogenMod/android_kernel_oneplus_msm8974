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
 *
 */

#include <asm/setup.h>

#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <linux/pcb_version.h>

static int  current_pcb_version_num = PCB_VERSION_UNKNOWN;
int get_pcb_version(void)
{
	return current_pcb_version_num;
}
EXPORT_SYMBOL(get_pcb_version);

static int  current_rf_version_num = RF_VERSION_UNKNOWN;
int get_rf_version(void)
{
	return current_rf_version_num;
}
EXPORT_SYMBOL(get_rf_version);

static char *saved_command_line_pcb_version = NULL;
int __init board_pcb_version_init(char *s)
{
	saved_command_line_pcb_version = s;

	if (!strncmp(s, "10", 2))
		current_pcb_version_num = HW_VERSION__10;
	else if (!strncmp(s, "11", 2))
		current_pcb_version_num = HW_VERSION__11;
	else if (!strncmp(s, "12", 2))
		current_pcb_version_num = HW_VERSION__12;
	else if (!strncmp(s, "13", 2))
		current_pcb_version_num = HW_VERSION__13;
	else if (!strncmp(s, "20", 2))
		current_pcb_version_num = HW_VERSION__20;
	else if (!strncmp(s, "21", 2))
		current_pcb_version_num = HW_VERSION__21;
	else if (!strncmp(s, "22", 2))
		current_pcb_version_num = HW_VERSION__22;
	else if (!strncmp(s, "23", 2))
		current_pcb_version_num = HW_VERSION__23;
	else if (!strncmp(s, "30", 2))
		current_pcb_version_num = HW_VERSION__30;
	else if (!strncmp(s, "31", 2))
		current_pcb_version_num = HW_VERSION__31;
	else if (!strncmp(s, "32", 2))
		current_pcb_version_num = HW_VERSION__32;
	else if (!strncmp(s, "33", 2))
		current_pcb_version_num = HW_VERSION__33;
	else if (!strncmp(s, "34", 2))
		current_pcb_version_num = HW_VERSION__34;
	else if (!strncmp(s, "40", 2))
		current_pcb_version_num = HW_VERSION__40;
	else if (!strncmp(s, "41", 2))
		current_pcb_version_num = HW_VERSION__41;
	else if (!strncmp(s, "42", 2))
		current_pcb_version_num = HW_VERSION__42;
	else if (!strncmp(s, "43", 2))
		current_pcb_version_num = HW_VERSION__43;
	else if (!strncmp(s, "44", 2))
		current_pcb_version_num = HW_VERSION__44;

	return 0;
}
__setup("oppo.pcb_version=", board_pcb_version_init);

static char *saved_command_line_rf_version = NULL;
int __init board_rf_version_init(char *s)
{
	saved_command_line_rf_version = s;

	if (!strncmp(s, "11", 2))
		current_rf_version_num = RF_VERSION__11;
	else if (!strncmp(s, "12", 2))
		current_rf_version_num = RF_VERSION__12;
	else if (!strncmp(s, "13", 2))
		current_rf_version_num = RF_VERSION__13;
	else if (!strncmp(s, "21", 2))
		current_rf_version_num = RF_VERSION__21;
	else if (!strncmp(s, "22", 2))
		current_rf_version_num = RF_VERSION__22;
	else if (!strncmp(s, "23", 2))
		current_rf_version_num = RF_VERSION__23;
	else if (!strncmp(s, "31", 2))
		current_rf_version_num = RF_VERSION__31;
	else if (!strncmp(s, "32", 2))
		current_rf_version_num = RF_VERSION__32;
	else if (!strncmp(s, "33", 2))
		current_rf_version_num = RF_VERSION__33;
	else if (!strncmp(s, "44", 2))
		current_rf_version_num = RF_VERSION__44;
	else if (!strncmp(s, "66", 2))
		current_rf_version_num = RF_VERSION__66;
	else if (!strncmp(s, "67", 2))
		current_rf_version_num = RF_VERSION__67;
	else if (!strncmp(s, "76", 2))
		current_rf_version_num = RF_VERSION__76;
	else if (!strncmp(s, "77", 2))
		current_rf_version_num = RF_VERSION__77;
	else if (!strncmp(s, "87", 2))
		current_rf_version_num = RF_VERSION__87;
	else if (!strncmp(s, "88", 2))
		current_rf_version_num = RF_VERSION__88;
	else if (!strncmp(s, "89", 2))
		current_rf_version_num = RF_VERSION__89;
	else if (!strncmp(s, "90", 2))
		current_rf_version_num = RF_VERSION__90_CHINA_MOBILE;
	else if (!strncmp(s, "91", 2))
		current_rf_version_num = RF_VERSION__91_UNICOM;
	else if (!strncmp(s, "92", 2))
		current_rf_version_num = RF_VERSION__92_CHINA_RESERVED1;
	else if (!strncmp(s, "93", 2))
		current_rf_version_num = RF_VERSION__93_CHINA_RESERVED2;
	else if (!strncmp(s, "94", 2))
		current_rf_version_num = RF_VERSION__94_CHINA_RESERVED3;
	else if (!strncmp(s, "95", 2))
		current_rf_version_num = RF_VERSION__95_EUROPE;
	else if (!strncmp(s, "96", 2))
		current_rf_version_num = RF_VERSION__96_AMERICA;
	else if (!strncmp(s, "97", 2))
		current_rf_version_num = RF_VERSION__97_TAIWAN;
	else if (!strncmp(s, "98", 2))
		current_rf_version_num = RF_VERSION__98_INDONESIA;
	else if (!strncmp(s, "99", 2))
		current_rf_version_num = RF_VERSION__99_OVERSEA_RESERVED1;

	return 0;
}
__setup("oppo.rf_version=", board_rf_version_init);
