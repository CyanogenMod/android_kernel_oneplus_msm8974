/* FPC1020 Touch sensor driver
 *
 * Copyright (c) 2013 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#ifndef LINUX_SPI_FPC1020_H
#define LINUX_SPI_FPC1020_H

struct fpc1020_platform_data {
	int irq_gpio;
	int reset_gpio;
#ifndef CONFIG_MACH_OPPO
	//Lycan.Wang@Prd.BasicDrv, 2014-09-12 Modify for Hw Config
	int cs_gpio;
#else /* CONFIG_MACH_OPPO */
	int vdden_gpio;
#endif /* CONFIG_MACH_OPPO */
	int external_supply_mv;
	int txout_boost;
};

typedef enum {
	FPC1020_MODE_IDLE			= 0,
	FPC1020_MODE_WAIT_AND_CAPTURE		= 1,
	FPC1020_MODE_SINGLE_CAPTURE		= 2,
	FPC1020_MODE_CHECKERBOARD_TEST_NORM	= 3,
	FPC1020_MODE_CHECKERBOARD_TEST_INV	= 4,
	FPC1020_MODE_BOARD_TEST_ONE		= 5,
	FPC1020_MODE_BOARD_TEST_ZERO		= 6,
	FPC1020_MODE_WAIT_FINGER_DOWN		= 7,
	FPC1020_MODE_WAIT_FINGER_UP		= 8,
} fpc1020_capture_mode_t;

typedef enum {
	FPC1020_CHIP_NONE  = 0,
	FPC1020_CHIP_1020A = 1,
	FPC1020_CHIP_1021A = 2,
	FPC1020_CHIP_1021B = 3,
	FPC1020_CHIP_1150A = 4,
	FPC1020_CHIP_1150B = 5,
	FPC1020_CHIP_1150F = 6
} fpc1020_chip_t;

#endif
