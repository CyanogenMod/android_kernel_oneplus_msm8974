/************************************************************ 
** Copyright (C), 2013-2016, OPPO Mobile Comm Corp., Ltd
** All rights reserved. 
** Author: He Wei
************************************************************/
#ifndef _BOOT_MODE_H
#define _BOOT_MODE_H

enum{
	MSM_BOOT_MODE__NORMAL,
	MSM_BOOT_MODE__RECOVERY = 2, //the number adapt system/core/init/init.c
	MSM_BOOT_MODE__FACTORY,
	MSM_BOOT_MODE__RF,
	MSM_BOOT_MODE__WLAN,
};

int get_boot_mode(void);
char *get_boot_mode_str(void);
char *get_start_reason(void);

#endif /* _BOOT_MODE_H */
