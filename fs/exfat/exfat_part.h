/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _EXFAT_PART_H
#define _EXFAT_PART_H

#include "exfat_config.h"
#include "exfat_global.h"
#include "exfat_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MBR_SIGNATURE           0xAA55
	typedef struct {
		UINT8       boot_code[446];
		UINT8       partition[64];
		UINT8       signature[2];
	} MBR_SECTOR_T;

	typedef struct {
		UINT8       def_boot;
		UINT8       bgn_chs[3];
		UINT8       sys_type;
		UINT8       end_chs[3];
		UINT8       start_sector[4];
		UINT8       num_sectors[4];
	} PART_ENTRY_T;

	INT32 ffsSetPartition(INT32 dev, INT32 num_vol, PART_INFO_T *vol_spec);
	INT32 ffsGetPartition(INT32 dev, INT32 *num_vol, PART_INFO_T *vol_spec);
	INT32 ffsGetDevInfo(INT32 dev, DEV_INFO_T *info);

#ifdef __cplusplus
}
#endif

#endif
