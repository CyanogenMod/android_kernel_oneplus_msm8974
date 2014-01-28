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

	return 0;
}
__setup("oppo.rf_version", board_rf_version_init);
