/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
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

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/ratelimit.h>
#include <mach/socinfo.h>

#include "kgsl.h"
#include "adreno.h"
#include "kgsl_sharedmem.h"
#include "kgsl_cffdump.h"
#include "a3xx_reg.h"
#include "adreno_a3xx_trace.h"

/*
 * Set of registers to dump for A3XX on snapshot.
 * Registers in pairs - first value is the start offset, second
 * is the stop offset (inclusive)
 */

const unsigned int a3xx_registers[] = {
	0x0000, 0x0002, 0x0010, 0x0012, 0x0018, 0x0018, 0x0020, 0x0027,
	0x0029, 0x002b, 0x002e, 0x0033, 0x0040, 0x0042, 0x0050, 0x005c,
	0x0060, 0x006c, 0x0080, 0x0082, 0x0084, 0x0088, 0x0090, 0x00e5,
	0x00ea, 0x00ed, 0x0100, 0x0100, 0x0110, 0x0123, 0x01c0, 0x01c1,
	0x01c3, 0x01c5, 0x01c7, 0x01c7, 0x01d5, 0x01d9, 0x01dc, 0x01dd,
	0x01ea, 0x01ea, 0x01ee, 0x01f1, 0x01f5, 0x01f5, 0x01fc, 0x01ff,
	0x0440, 0x0440, 0x0443, 0x0443, 0x0445, 0x0445, 0x044d, 0x044f,
	0x0452, 0x0452, 0x0454, 0x046f, 0x047c, 0x047c, 0x047f, 0x047f,
	0x0578, 0x057f, 0x0600, 0x0602, 0x0605, 0x0607, 0x060a, 0x060e,
	0x0612, 0x0614, 0x0c01, 0x0c02, 0x0c06, 0x0c1d, 0x0c3d, 0x0c3f,
	0x0c48, 0x0c4b, 0x0c80, 0x0c80, 0x0c88, 0x0c8b, 0x0ca0, 0x0cb7,
	0x0cc0, 0x0cc1, 0x0cc6, 0x0cc7, 0x0ce4, 0x0ce5,
	0x0e41, 0x0e45, 0x0e64, 0x0e65,
	0x0e80, 0x0e82, 0x0e84, 0x0e89, 0x0ea0, 0x0ea1, 0x0ea4, 0x0ea7,
	0x0ec4, 0x0ecb, 0x0ee0, 0x0ee0, 0x0f00, 0x0f01, 0x0f03, 0x0f09,
	0x2040, 0x2040, 0x2044, 0x2044, 0x2048, 0x204d, 0x2068, 0x2069,
	0x206c, 0x206d, 0x2070, 0x2070, 0x2072, 0x2072, 0x2074, 0x2075,
	0x2079, 0x207a, 0x20c0, 0x20d3, 0x20e4, 0x20ef, 0x2100, 0x2109,
	0x210c, 0x210c, 0x210e, 0x210e, 0x2110, 0x2111, 0x2114, 0x2115,
	0x21e4, 0x21e4, 0x21ea, 0x21ea, 0x21ec, 0x21ed, 0x21f0, 0x21f0,
	0x2240, 0x227e,
	0x2280, 0x228b, 0x22c0, 0x22c0, 0x22c4, 0x22ce, 0x22d0, 0x22d8,
	0x22df, 0x22e6, 0x22e8, 0x22e9, 0x22ec, 0x22ec, 0x22f0, 0x22f7,
	0x22ff, 0x22ff, 0x2340, 0x2343,
	0x2440, 0x2440, 0x2444, 0x2444, 0x2448, 0x244d,
	0x2468, 0x2469, 0x246c, 0x246d, 0x2470, 0x2470, 0x2472, 0x2472,
	0x2474, 0x2475, 0x2479, 0x247a, 0x24c0, 0x24d3, 0x24e4, 0x24ef,
	0x2500, 0x2509, 0x250c, 0x250c, 0x250e, 0x250e, 0x2510, 0x2511,
	0x2514, 0x2515, 0x25e4, 0x25e4, 0x25ea, 0x25ea, 0x25ec, 0x25ed,
	0x25f0, 0x25f0,
	0x2640, 0x267e, 0x2680, 0x268b, 0x26c0, 0x26c0, 0x26c4, 0x26ce,
	0x26d0, 0x26d8, 0x26df, 0x26e6, 0x26e8, 0x26e9, 0x26ec, 0x26ec,
	0x26f0, 0x26f7, 0x26ff, 0x26ff, 0x2740, 0x2743,
	0x300C, 0x300E, 0x301C, 0x301D,
	0x302A, 0x302A, 0x302C, 0x302D, 0x3030, 0x3031, 0x3034, 0x3036,
	0x303C, 0x303C, 0x305E, 0x305F,
};

const unsigned int a3xx_registers_count = ARRAY_SIZE(a3xx_registers) / 2;

/* Removed the following HLSQ register ranges from being read during
 * fault tolerance since reading the registers may cause the device to hang:
 */
const unsigned int a3xx_hlsq_registers[] = {
	0x0e00, 0x0e05, 0x0e0c, 0x0e0c, 0x0e22, 0x0e23,
	0x2200, 0x2212, 0x2214, 0x2217, 0x221a, 0x221a,
	0x2600, 0x2612, 0x2614, 0x2617, 0x261a, 0x261a,
};

const unsigned int a3xx_hlsq_registers_count =
			ARRAY_SIZE(a3xx_hlsq_registers) / 2;

/* The set of additional registers to be dumped for A330 */

const unsigned int a330_registers[] = {
	0x1d0, 0x1d0, 0x1d4, 0x1d4, 0x453, 0x453,
};

const unsigned int a330_registers_count = ARRAY_SIZE(a330_registers) / 2;

unsigned int adreno_a3xx_rbbm_clock_ctl_default(struct adreno_device
							*adreno_dev)
{
	if (adreno_is_a305(adreno_dev))
		return A305_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a305c(adreno_dev))
		return A305C_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a320(adreno_dev))
		return A320_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a330v2(adreno_dev))
		return A330v2_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a330(adreno_dev))
		return A330_RBBM_CLOCK_CTL_DEFAULT;
	else if (adreno_is_a305b(adreno_dev))
		return A305B_RBBM_CLOCK_CTL_DEFAULT;

	BUG_ON(1);
}

static const unsigned int _a3xx_pwron_fixup_fs_instructions[] = {
	0x00000000, 0x302CC300, 0x00000000, 0x302CC304,
	0x00000000, 0x302CC308, 0x00000000, 0x302CC30C,
	0x00000000, 0x302CC310, 0x00000000, 0x302CC314,
	0x00000000, 0x302CC318, 0x00000000, 0x302CC31C,
	0x00000000, 0x302CC320, 0x00000000, 0x302CC324,
	0x00000000, 0x302CC328, 0x00000000, 0x302CC32C,
	0x00000000, 0x302CC330, 0x00000000, 0x302CC334,
	0x00000000, 0x302CC338, 0x00000000, 0x302CC33C,
	0x00000000, 0x00000400, 0x00020000, 0x63808003,
	0x00060004, 0x63828007, 0x000A0008, 0x6384800B,
	0x000E000C, 0x6386800F, 0x00120010, 0x63888013,
	0x00160014, 0x638A8017, 0x001A0018, 0x638C801B,
	0x001E001C, 0x638E801F, 0x00220020, 0x63908023,
	0x00260024, 0x63928027, 0x002A0028, 0x6394802B,
	0x002E002C, 0x6396802F, 0x00320030, 0x63988033,
	0x00360034, 0x639A8037, 0x003A0038, 0x639C803B,
	0x003E003C, 0x639E803F, 0x00000000, 0x00000400,
	0x00000003, 0x80D60003, 0x00000007, 0x80D60007,
	0x0000000B, 0x80D6000B, 0x0000000F, 0x80D6000F,
	0x00000013, 0x80D60013, 0x00000017, 0x80D60017,
	0x0000001B, 0x80D6001B, 0x0000001F, 0x80D6001F,
	0x00000023, 0x80D60023, 0x00000027, 0x80D60027,
	0x0000002B, 0x80D6002B, 0x0000002F, 0x80D6002F,
	0x00000033, 0x80D60033, 0x00000037, 0x80D60037,
	0x0000003B, 0x80D6003B, 0x0000003F, 0x80D6003F,
	0x00000000, 0x03000000, 0x00000000, 0x00000000,
};

/**
 * adreno_a3xx_pwron_fixup_init() - Initalize a special command buffer to run a
 * post-power collapse shader workaround
 * @adreno_dev: Pointer to a adreno_device struct
 *
 * Some targets require a special workaround shader to be executed after
 * power-collapse.  Construct the IB once at init time and keep it
 * handy
 *
 * Returns: 0 on success or negative on error
 */
int adreno_a3xx_pwron_fixup_init(struct adreno_device *adreno_dev)
{
	unsigned int *cmds;
	int count = ARRAY_SIZE(_a3xx_pwron_fixup_fs_instructions);
	int ret;

	/* Return if the fixup is already in place */
	if (test_bit(ADRENO_DEVICE_PWRON_FIXUP, &adreno_dev->priv))
		return 0;

	ret = kgsl_allocate_contiguous(&adreno_dev->dev,
				&adreno_dev->pwron_fixup, PAGE_SIZE);

	if (ret)
		return ret;

	adreno_dev->pwron_fixup.flags |= KGSL_MEMFLAGS_GPUREADONLY;

	cmds = adreno_dev->pwron_fixup.hostptr;

	*cmds++ = cp_type0_packet(A3XX_UCHE_CACHE_INVALIDATE0_REG, 2);
	*cmds++ = 0x00000000;
	*cmds++ = 0x90000000;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_REG_RMW, 3);
	*cmds++ = A3XX_RBBM_CLOCK_CTL;
	*cmds++ = 0xFFFCFFFF;
	*cmds++ = 0x00010000;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_0_REG, 1);
	*cmds++ = 0x1E000150;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_SET_CONSTANT, 2);
	*cmds++ = CP_REG(A3XX_HLSQ_CONTROL_0_REG);
	*cmds++ = 0x1E000150;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_0_REG, 1);
	*cmds++ = 0x1E000150;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_1_REG, 1);
	*cmds++ = 0x00000040;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_2_REG, 1);
	*cmds++ = 0x80000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_3_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_VS_CONTROL_REG, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_FS_CONTROL_REG, 1);
	*cmds++ = 0x0D001002;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONST_VSPRESV_RANGE_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONST_FSPRESV_RANGE_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_0_REG, 1);
	*cmds++ = 0x00401101;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_1_REG, 1);
	*cmds++ = 0x00000400;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_2_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_3_REG, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_4_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_5_REG, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_NDRANGE_6_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_CONTROL_0_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_CONTROL_1_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_KERNEL_CONST_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_KERNEL_GROUP_X_REG, 1);
	*cmds++ = 0x00000010;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_KERNEL_GROUP_Y_REG, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_KERNEL_GROUP_Z_REG, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_WG_OFFSET_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_SP_CTRL_REG, 1);
	*cmds++ = 0x00040000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_CTRL_REG0, 1);
	*cmds++ = 0x0000000A;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_CTRL_REG1, 1);
	*cmds++ = 0x00000001;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_PARAM_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_4, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_5, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_6, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OUT_REG_7, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_VPC_DST_REG_0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_VPC_DST_REG_1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_VPC_DST_REG_2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_VPC_DST_REG_3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OBJ_OFFSET_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_OBJ_START_REG, 1);
	*cmds++ = 0x00000004;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_PVT_MEM_PARAM_REG, 1);
	*cmds++ = 0x04008001;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_PVT_MEM_ADDR_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_PVT_MEM_SIZE_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_VS_LENGTH_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_CTRL_REG0, 1);
	*cmds++ = 0x0DB0400A;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_CTRL_REG1, 1);
	*cmds++ = 0x00300402;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_OBJ_OFFSET_REG, 1);
	*cmds++ = 0x00010000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_OBJ_START_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_PVT_MEM_PARAM_REG, 1);
	*cmds++ = 0x04008001;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_PVT_MEM_ADDR_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_PVT_MEM_SIZE_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_FLAT_SHAD_MODE_REG_0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_FLAT_SHAD_MODE_REG_1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_OUTPUT_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_MRT_REG_0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_MRT_REG_1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_MRT_REG_2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_MRT_REG_3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_IMAGE_OUTPUT_REG_0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_IMAGE_OUTPUT_REG_1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_IMAGE_OUTPUT_REG_2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_IMAGE_OUTPUT_REG_3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_SP_FS_LENGTH_REG, 1);
	*cmds++ = 0x0000000D;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_CLIP_CNTL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_GB_CLIP_ADJ, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_XOFFSET, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_XSCALE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_YOFFSET, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_YSCALE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_ZOFFSET, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_VPORT_ZSCALE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X4, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y4, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z4, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W4, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_X5, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Y5, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_Z5, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_CL_USER_PLANE_W5, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SU_POINT_MINMAX, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SU_POINT_SIZE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SU_POLY_OFFSET_OFFSET, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SU_POLY_OFFSET_SCALE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SU_MODE_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SC_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SC_SCREEN_SCISSOR_TL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SC_SCREEN_SCISSOR_BR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SC_WINDOW_SCISSOR_BR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_SC_WINDOW_SCISSOR_TL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_TSE_DEBUG_ECO, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_PERFCOUNTER0_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_PERFCOUNTER1_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_PERFCOUNTER2_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_GRAS_PERFCOUNTER3_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MODE_CONTROL, 1);
	*cmds++ = 0x00008000;
	*cmds++ = cp_type0_packet(A3XX_RB_RENDER_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MSAA_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_ALPHA_REFERENCE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_CONTROL0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_CONTROL1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_CONTROL2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_CONTROL3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_INFO0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_INFO1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_INFO2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_INFO3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_BASE0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_BASE1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_BASE2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BUF_BASE3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BLEND_CONTROL0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BLEND_CONTROL1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BLEND_CONTROL2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_MRT_BLEND_CONTROL3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_BLEND_RED, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_BLEND_GREEN, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_BLEND_BLUE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_BLEND_ALPHA, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_CLEAR_COLOR_DW0, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_CLEAR_COLOR_DW1, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_CLEAR_COLOR_DW2, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_CLEAR_COLOR_DW3, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_COPY_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_COPY_DEST_BASE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_COPY_DEST_PITCH, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_COPY_DEST_INFO, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_DEPTH_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_DEPTH_CLEAR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_DEPTH_BUF_INFO, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_DEPTH_BUF_PITCH, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_CLEAR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_BUF_INFO, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_BUF_PITCH, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_REF_MASK, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_STENCIL_REF_MASK_BF, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_LRZ_VSC_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_WINDOW_OFFSET, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_SAMPLE_COUNT_CONTROL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_SAMPLE_COUNT_ADDR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_Z_CLAMP_MIN, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_Z_CLAMP_MAX, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_GMEM_BASE_ADDR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_DEBUG_ECO_CONTROLS_ADDR, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_PERFCOUNTER0_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_PERFCOUNTER1_SELECT, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_RB_FRAME_BUFFER_DIMENSION, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_LOAD_STATE, 4);
	*cmds++ = (1 << CP_LOADSTATE_DSTOFFSET_SHIFT) |
		(0 << CP_LOADSTATE_STATESRC_SHIFT) |
		(6 << CP_LOADSTATE_STATEBLOCKID_SHIFT) |
		(1 << CP_LOADSTATE_NUMOFUNITS_SHIFT);
	*cmds++ = (1 << CP_LOADSTATE_STATETYPE_SHIFT) |
		(0 << CP_LOADSTATE_EXTSRCADDR_SHIFT);
	*cmds++ = 0x00400000;
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_LOAD_STATE, 4);
	*cmds++ = (2 << CP_LOADSTATE_DSTOFFSET_SHIFT) |
		(6 << CP_LOADSTATE_STATEBLOCKID_SHIFT) |
		(1 << CP_LOADSTATE_NUMOFUNITS_SHIFT);
	*cmds++ = (1 << CP_LOADSTATE_STATETYPE_SHIFT);
	*cmds++ = 0x00400220;
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_LOAD_STATE, 4);
	*cmds++ = (6 << CP_LOADSTATE_STATEBLOCKID_SHIFT) |
		(1 << CP_LOADSTATE_NUMOFUNITS_SHIFT);
	*cmds++ = (1 << CP_LOADSTATE_STATETYPE_SHIFT);
	*cmds++ = 0x00000000;
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_LOAD_STATE, 2 + count);
	*cmds++ = (6 << CP_LOADSTATE_STATEBLOCKID_SHIFT) |
		(13 << CP_LOADSTATE_NUMOFUNITS_SHIFT);
	*cmds++ = 0x00000000;

	memcpy(cmds, _a3xx_pwron_fixup_fs_instructions, count << 2);

	cmds += count;

	*cmds++ = cp_type3_packet(CP_EXEC_CL, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CL_CONTROL_0_REG, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type0_packet(A3XX_HLSQ_CONTROL_0_REG, 1);
	*cmds++ = 0x1E000150;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_SET_CONSTANT, 2);
	*cmds++ = CP_REG(A3XX_HLSQ_CONTROL_0_REG);
	*cmds++ = 0x1E000050;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_REG_RMW, 3);
	*cmds++ = A3XX_RBBM_CLOCK_CTL;
	*cmds++ = 0xFFFCFFFF;
	*cmds++ = 0x00000000;
	*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
	*cmds++ = 0x00000000;

	/*
	 * Remember the number of dwords in the command buffer for when we
	 * program the indirect buffer call in the ringbuffer
	 */
	adreno_dev->pwron_fixup_dwords =
		(cmds - (unsigned int *) adreno_dev->pwron_fixup.hostptr);

	/* Mark the flag in ->priv to show that we have the fix */
	set_bit(ADRENO_DEVICE_PWRON_FIXUP, &adreno_dev->priv);
	return 0;
}

static int a3xx_rb_init(struct adreno_device *adreno_dev,
			 struct adreno_ringbuffer *rb)
{
	unsigned int *cmds, cmds_gpu;
	cmds = adreno_ringbuffer_allocspace(rb, NULL, 18);
	if (cmds == NULL)
		return -ENOMEM;

	cmds_gpu = rb->buffer_desc.gpuaddr + sizeof(uint) * (rb->wptr - 18);

	GSL_RB_WRITE(rb->device, cmds, cmds_gpu,
			cp_type3_packet(CP_ME_INIT, 17));
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x000003f7);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000000);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000000);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000000);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000080);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000100);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000180);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00006600);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000150);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x0000014e);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000154);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000001);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000000);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000000);
	/* Enable protected mode */
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x20000000);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000000);
	GSL_RB_WRITE(rb->device, cmds, cmds_gpu, 0x00000000);

	adreno_ringbuffer_submit(rb);

	return 0;
}

static void a3xx_fatal_err_callback(struct adreno_device *adreno_dev, int bit)
{
	struct kgsl_device *device = &adreno_dev->dev;
	const char *err = "";

	switch (bit) {
	case A3XX_INT_MISC_HANG_DETECT:
		err = "MISC: GPU hang detected\n";
		break;
	default:
		return;
	}
	KGSL_DRV_CRIT(device, "%s\n", err);
	kgsl_pwrctrl_irq(device, KGSL_PWRFLAGS_OFF);

	/* Trigger a fault in the dispatcher - this will effect a restart */
	adreno_dispatcher_irq_fault(device);
}

static void a3xx_err_callback(struct adreno_device *adreno_dev, int bit)
{
	struct kgsl_device *device = &adreno_dev->dev;

	switch (bit) {
	case A3XX_INT_RBBM_AHB_ERROR: {
		unsigned int reg;

		kgsl_regread(device, A3XX_RBBM_AHB_ERROR_STATUS, &reg);

		/*
		 * Return the word address of the erroring register so that it
		 * matches the register specification
		 */

		KGSL_DRV_CRIT(device,
			"RBBM | AHB bus error | %s | addr=%x | ports=%x:%x\n",
			reg & (1 << 28) ? "WRITE" : "READ",
			(reg & 0xFFFFF) >> 2, (reg >> 20) & 0x3,
			(reg >> 24) & 0x3);

		/* Clear the error */
		kgsl_regwrite(device, A3XX_RBBM_AHB_CMD, (1 << 3));
		break;
	}
	case A3XX_INT_RBBM_REG_TIMEOUT:
		KGSL_DRV_CRIT_RATELIMIT(device, "RBBM: AHB register timeout\n");
		break;
	case A3XX_INT_RBBM_ME_MS_TIMEOUT:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"RBBM: ME master split timeout\n");
		break;
	case A3XX_INT_RBBM_PFP_MS_TIMEOUT:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"RBBM: PFP master split timeout\n");
		break;
	case A3XX_INT_RBBM_ATB_BUS_OVERFLOW:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"RBBM: ATB bus oveflow\n");
		break;
	case A3XX_INT_VFD_ERROR:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"VFD: Out of bounds access\n");
		break;
	case A3XX_INT_CP_T0_PACKET_IN_IB:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"ringbuffer TO packet in IB interrupt\n");
		break;
	case A3XX_INT_CP_OPCODE_ERROR:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"ringbuffer opcode error interrupt\n");
		break;
	case A3XX_INT_CP_RESERVED_BIT_ERROR:
		KGSL_DRV_CRIT_RATELIMIT(device,
				"ringbuffer reserved bit error interrupt\n");
		break;
	case A3XX_INT_CP_HW_FAULT: {
		unsigned int reg;
		adreno_readreg(adreno_dev, ADRENO_REG_CP_HW_FAULT, &reg);
		KGSL_DRV_CRIT_RATELIMIT(device,
			"CP | Ringbuffer HW fault | status=%x\n", reg);
		break;
	}
	case A3XX_INT_CP_REG_PROTECT_FAULT: {
		unsigned int reg;
		kgsl_regread(device, A3XX_CP_PROTECT_STATUS, &reg);

		KGSL_DRV_CRIT(device,
			"CP | Protected mode error| %s | addr=%x\n",
			reg & (1 << 24) ? "WRITE" : "READ",
			(reg & 0x1FFFF) >> 2);
		break;
	}
	case A3XX_INT_CP_AHB_ERROR_HALT:
		KGSL_DRV_CRIT(device, "ringbuffer AHB error interrupt\n");
		break;
	case A3XX_INT_UCHE_OOB_ACCESS:
		KGSL_DRV_CRIT_RATELIMIT(device,
			"UCHE:  Out of bounds access\n");
		break;
	}
}

static void a3xx_cp_callback(struct adreno_device *adreno_dev, int irq)
{
	struct kgsl_device *device = &adreno_dev->dev;

	queue_work(device->work_queue, &device->ts_expired_ws);
	adreno_dispatcher_schedule(device);
}


static int a3xx_perfcounter_enable_pwr(struct kgsl_device *device,
	unsigned int counter)
{
	unsigned int in, out;

	if (counter > 1)
		return -EINVAL;

	kgsl_regread(device, A3XX_RBBM_RBBM_CTL, &in);

	if (counter == 0)
		out = in | RBBM_RBBM_CTL_RESET_PWR_CTR0;
	else
		out = in | RBBM_RBBM_CTL_RESET_PWR_CTR1;

	kgsl_regwrite(device, A3XX_RBBM_RBBM_CTL, out);

	if (counter == 0)
		out = in | RBBM_RBBM_CTL_ENABLE_PWR_CTR0;
	else
		out = in | RBBM_RBBM_CTL_ENABLE_PWR_CTR1;

	kgsl_regwrite(device, A3XX_RBBM_RBBM_CTL, out);

	return 0;
}

static int a3xx_perfcounter_enable_vbif(struct kgsl_device *device,
					 unsigned int counter,
					 unsigned int countable)
{
	unsigned int in, out, bit, sel;

	if (counter > 1 || countable > 0x7f)
		return -EINVAL;

	kgsl_regread(device, A3XX_VBIF_PERF_CNT_EN, &in);
	kgsl_regread(device, A3XX_VBIF_PERF_CNT_SEL, &sel);

	if (counter == 0) {
		bit = VBIF_PERF_CNT_0;
		sel = (sel & ~VBIF_PERF_CNT_0_SEL_MASK) | countable;
	} else {
		bit = VBIF_PERF_CNT_1;
		sel = (sel & ~VBIF_PERF_CNT_1_SEL_MASK)
			| (countable << VBIF_PERF_CNT_1_SEL);
	}

	out = in | bit;

	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_SEL, sel);

	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_CLR, bit);
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_CLR, 0);

	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, out);
	return 0;
}

static int a3xx_perfcounter_enable_vbif_pwr(struct kgsl_device *device,
					     unsigned int counter)
{
	unsigned int in, out, bit;

	if (counter > 2)
		return -EINVAL;

	kgsl_regread(device, A3XX_VBIF_PERF_CNT_EN, &in);
	if (counter == 0)
		bit = VBIF_PERF_PWR_CNT_0;
	else if (counter == 1)
		bit = VBIF_PERF_PWR_CNT_1;
	else
		bit = VBIF_PERF_PWR_CNT_2;

	out = in | bit;

	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_CLR, bit);
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_CLR, 0);

	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, out);
	return 0;
}

/*
 * a3xx_perfcounter_enable - Configure a performance counter for a countable
 * @adreno_dev -  Adreno device to configure
 * @group - Desired performance counter group
 * @counter - Desired performance counter in the group
 * @countable - Desired countable
 *
 * Physically set up a counter within a group with the desired countable
 * Return 0 on success else error code
 */

static int a3xx_perfcounter_enable(struct adreno_device *adreno_dev,
	unsigned int group, unsigned int counter, unsigned int countable)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcount_register *reg;

	/* Special cases */
	if (group == KGSL_PERFCOUNTER_GROUP_PWR)
		return a3xx_perfcounter_enable_pwr(device, counter);
	else if (group == KGSL_PERFCOUNTER_GROUP_VBIF)
		return a3xx_perfcounter_enable_vbif(device, counter,
							countable);
	else if (group == KGSL_PERFCOUNTER_GROUP_VBIF_PWR)
		return a3xx_perfcounter_enable_vbif_pwr(device, counter);

	if (group >= adreno_dev->gpudev->perfcounters->group_count)
		return -EINVAL;

	if ((0 == adreno_dev->gpudev->perfcounters->groups[group].reg_count) ||
		(counter >=
		adreno_dev->gpudev->perfcounters->groups[group].reg_count))
		return -EINVAL;

	reg = &(adreno_dev->gpudev->perfcounters->groups[group].regs[counter]);

	/* Select the desired perfcounter */
	kgsl_regwrite(device, reg->select, countable);

	return 0;
}

static uint64_t a3xx_perfcounter_read_pwr(struct adreno_device *adreno_dev,
				unsigned int counter)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct adreno_perfcount_register *reg;
	unsigned int in, out, lo = 0, hi = 0;
	unsigned int enable_bit;

	if (counter > 1)
		return 0;
	if (0 == counter)
		enable_bit = RBBM_RBBM_CTL_ENABLE_PWR_CTR0;
	else
		enable_bit = RBBM_RBBM_CTL_ENABLE_PWR_CTR1;
	/* freeze counter */
	adreno_readreg(adreno_dev, ADRENO_REG_RBBM_RBBM_CTL, &in);
	out = (in & ~enable_bit);
	adreno_writereg(adreno_dev, ADRENO_REG_RBBM_RBBM_CTL, out);

	reg = &counters->groups[KGSL_PERFCOUNTER_GROUP_PWR].regs[counter];
	kgsl_regread(device, reg->offset, &lo);
	kgsl_regread(device, reg->offset + 1, &hi);

	/* restore the counter control value */
	adreno_writereg(adreno_dev, ADRENO_REG_RBBM_RBBM_CTL, in);

	return (((uint64_t) hi) << 32) | lo;
}

static uint64_t a3xx_perfcounter_read_vbif(struct adreno_device *adreno_dev,
				unsigned int counter)
{
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcount_register *reg;
	unsigned int in, out, lo = 0, hi = 0;

	if (counter > 1)
		return 0;

	/* freeze counter */
	kgsl_regread(device, A3XX_VBIF_PERF_CNT_EN, &in);
	if (counter == 0)
		out = (in & ~VBIF_PERF_CNT_0);
	else
		out = (in & ~VBIF_PERF_CNT_1);
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, out);

	reg = &counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF].regs[counter];
	kgsl_regread(device, reg->offset, &lo);
	kgsl_regread(device, reg->offset + 1, &hi);

	/* restore the perfcounter value */
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, in);

	return (((uint64_t) hi) << 32) | lo;
}

static uint64_t a3xx_perfcounter_read_vbif_pwr(struct adreno_device *adreno_dev,
				unsigned int counter)
{
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcount_register *reg;
	unsigned int in, out, lo = 0, hi = 0;

	if (counter > 2)
		return 0;

	/* freeze counter */
	kgsl_regread(device, A3XX_VBIF_PERF_CNT_EN, &in);
	if (0 == counter)
		out = (in & ~VBIF_PERF_PWR_CNT_0);
	else
		out = (in & ~VBIF_PERF_PWR_CNT_2);
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, out);

	reg = &counters->groups[KGSL_PERFCOUNTER_GROUP_VBIF_PWR].regs[counter];
	kgsl_regread(device, reg->offset, &lo);
	kgsl_regread(device, reg->offset + 1, &hi);
	/* restore the perfcounter value */
	kgsl_regwrite(device, A3XX_VBIF_PERF_CNT_EN, in);

	return (((uint64_t) hi) << 32) | lo;
}

static uint64_t a3xx_perfcounter_read(struct adreno_device *adreno_dev,
	unsigned int group, unsigned int counter)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcount_register *reg;
	unsigned int lo = 0, hi = 0;
	unsigned int offset;
	unsigned int in, out;

	if (group == KGSL_PERFCOUNTER_GROUP_VBIF_PWR)
		return a3xx_perfcounter_read_vbif_pwr(adreno_dev, counter);

	if (group == KGSL_PERFCOUNTER_GROUP_VBIF)
		return a3xx_perfcounter_read_vbif(adreno_dev, counter);

	if (group == KGSL_PERFCOUNTER_GROUP_PWR)
		return a3xx_perfcounter_read_pwr(adreno_dev, counter);

	if (group >= adreno_dev->gpudev->perfcounters->group_count)
		return 0;

	if ((0 == adreno_dev->gpudev->perfcounters->groups[group].reg_count) ||
		(counter >=
		adreno_dev->gpudev->perfcounters->groups[group].reg_count))
		return 0;

	reg = &(adreno_dev->gpudev->perfcounters->groups[group].regs[counter]);

	/* Freeze the counter */
	kgsl_regread(device, A3XX_RBBM_PERFCTR_CTL, &in);
	out = in & ~RBBM_PERFCTR_CTL_ENABLE;
	kgsl_regwrite(device, A3XX_RBBM_PERFCTR_CTL, out);

	offset = reg->offset;
	/* Read the values */
	kgsl_regread(device, offset, &lo);
	kgsl_regread(device, offset + 1, &hi);

	/* Re-Enable the counter */
	kgsl_regwrite(device, A3XX_RBBM_PERFCTR_CTL, in);
	return (((uint64_t) hi) << 32) | lo;
}

/*
 * values cannot be loaded into physical performance
 * counters belonging to these groups.
 */
static inline int loadable_perfcounter_group(unsigned int groupid)
{
	return ((groupid == KGSL_PERFCOUNTER_GROUP_VBIF_PWR) ||
		(groupid == KGSL_PERFCOUNTER_GROUP_VBIF) ||
		(groupid == KGSL_PERFCOUNTER_GROUP_PWR)) ? 0 : 1;
}

/*
 * Return true if the countable is used and not broken
 */
static inline int active_countable(unsigned int countable)
{
	return ((countable != KGSL_PERFCOUNTER_NOT_USED) &&
		(countable != KGSL_PERFCOUNTER_BROKEN));
}

/**
 * a3xx_perfcounter_save() - Save the physical performance counter values
 * @adreno_dev -  Adreno device whose registers need to be saved
 *
 * Read all the physical performance counter's values and save them
 * before GPU power collapse.
 */
static void a3xx_perfcounter_save(struct adreno_device *adreno_dev)
{
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct adreno_perfcount_group *group;
	unsigned int regid, groupid;

	for (groupid = 0; groupid < counters->group_count; groupid++) {
		if (!loadable_perfcounter_group(groupid))
			continue;

		group = &(counters->groups[groupid]);

		/* group/counter iterator */
		for (regid = 0; regid < group->reg_count; regid++) {
			if (!active_countable(group->regs[regid].countable))
				continue;

			group->regs[regid].value =
				adreno_dev->gpudev->perfcounter_read(
				adreno_dev, groupid, regid);
		}
	}
}

/**
 * a3xx_perfcounter_write() - Write the physical performance counter values.
 * @adreno_dev -  Adreno device whose registers are to be written to.
 * @group - group to which the physical counter belongs to.
 * @counter - register id of the physical counter to which the value is
 *		written to.
 *
 * This function loads the 64 bit saved value into the particular physical
 * counter by enabling the corresponding bit in A3XX_RBBM_PERFCTR_LOAD_CMD*
 * register.
 */
static void a3xx_perfcounter_write(struct adreno_device *adreno_dev,
				unsigned int group, unsigned int counter)
{
	struct kgsl_device *device = &(adreno_dev->dev);
	struct adreno_perfcount_register *reg;
	unsigned int val;

	reg = &(adreno_dev->gpudev->perfcounters->groups[group].regs[counter]);

	/* Clear the load cmd registers */
	kgsl_regwrite(device, A3XX_RBBM_PERFCTR_LOAD_CMD0, 0);
	kgsl_regwrite(device, A3XX_RBBM_PERFCTR_LOAD_CMD1, 0);

	/* Write the saved value to PERFCTR_LOAD_VALUE* registers. */
	kgsl_regwrite(device, A3XX_RBBM_PERFCTR_LOAD_VALUE_LO,
			(uint32_t)reg->value);
	kgsl_regwrite(device, A3XX_RBBM_PERFCTR_LOAD_VALUE_HI,
			(uint32_t)(reg->value >> 32));

	/*
	 * Set the load bit in PERFCTR_LOAD_CMD for the physical counter
	 * we want to restore. The value in PERFCTR_LOAD_VALUE* is loaded
	 * into the corresponding physical counter.
	 */
	if (reg->load_bit < 32)	{
		val = 1 << reg->load_bit;
		kgsl_regwrite(device, A3XX_RBBM_PERFCTR_LOAD_CMD0, val);
	} else {
		val  = 1 << (reg->load_bit - 32);
		kgsl_regwrite(device, A3XX_RBBM_PERFCTR_LOAD_CMD1, val);
	}
}

/**
 * a3xx_perfcounter_restore() - Restore the physical performance counter values.
 * @adreno_dev -  Adreno device whose registers are to be restored.
 *
 * This function together with a3xx_perfcounter_save make sure that performance
 * counters are coherent across GPU power collapse.
 */
static void a3xx_perfcounter_restore(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct adreno_perfcount_group *group;
	unsigned int regid, groupid;

	for (groupid = 0; groupid < counters->group_count; groupid++) {
		if (!loadable_perfcounter_group(groupid))
			continue;

		group = &(counters->groups[groupid]);

		/* group/counter iterator */
		for (regid = 0; regid < group->reg_count; regid++) {
			if (!active_countable(group->regs[regid].countable))
				continue;

			a3xx_perfcounter_write(adreno_dev, groupid, regid);
		}
	}

	/* Clear the load cmd registers */
	kgsl_regwrite(device, A3XX_RBBM_PERFCTR_LOAD_CMD0, 0);
	kgsl_regwrite(device, A3XX_RBBM_PERFCTR_LOAD_CMD1, 0);

}

#define A3XX_IRQ_CALLBACK(_c) { .func = _c }

#define A3XX_INT_MASK \
	((1 << A3XX_INT_RBBM_AHB_ERROR) |        \
	 (1 << A3XX_INT_RBBM_ATB_BUS_OVERFLOW) | \
	 (1 << A3XX_INT_CP_T0_PACKET_IN_IB) |    \
	 (1 << A3XX_INT_CP_OPCODE_ERROR) |       \
	 (1 << A3XX_INT_CP_RESERVED_BIT_ERROR) | \
	 (1 << A3XX_INT_CP_HW_FAULT) |           \
	 (1 << A3XX_INT_CP_IB1_INT) |            \
	 (1 << A3XX_INT_CP_IB2_INT) |            \
	 (1 << A3XX_INT_CP_RB_INT) |             \
	 (1 << A3XX_INT_CP_REG_PROTECT_FAULT) |  \
	 (1 << A3XX_INT_CP_AHB_ERROR_HALT) |     \
	 (1 << A3XX_INT_UCHE_OOB_ACCESS))

static struct {
	void (*func)(struct adreno_device *, int);
} a3xx_irq_funcs[] = {
	A3XX_IRQ_CALLBACK(NULL),               /* 0 - RBBM_GPU_IDLE */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 1 - RBBM_AHB_ERROR */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 2 - RBBM_REG_TIMEOUT */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 3 - RBBM_ME_MS_TIMEOUT */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 4 - RBBM_PFP_MS_TIMEOUT */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 5 - RBBM_ATB_BUS_OVERFLOW */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 6 - RBBM_VFD_ERROR */
	A3XX_IRQ_CALLBACK(NULL),	       /* 7 - CP_SW */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 8 - CP_T0_PACKET_IN_IB */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 9 - CP_OPCODE_ERROR */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 10 - CP_RESERVED_BIT_ERROR */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 11 - CP_HW_FAULT */
	A3XX_IRQ_CALLBACK(NULL),	       /* 12 - CP_DMA */
	A3XX_IRQ_CALLBACK(a3xx_cp_callback),   /* 13 - CP_IB2_INT */
	A3XX_IRQ_CALLBACK(a3xx_cp_callback),   /* 14 - CP_IB1_INT */
	A3XX_IRQ_CALLBACK(a3xx_cp_callback),   /* 15 - CP_RB_INT */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 16 - CP_REG_PROTECT_FAULT */
	A3XX_IRQ_CALLBACK(NULL),	       /* 17 - CP_RB_DONE_TS */
	A3XX_IRQ_CALLBACK(NULL),	       /* 18 - CP_VS_DONE_TS */
	A3XX_IRQ_CALLBACK(NULL),	       /* 19 - CP_PS_DONE_TS */
	A3XX_IRQ_CALLBACK(NULL),	       /* 20 - CP_CACHE_FLUSH_TS */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 21 - CP_AHB_ERROR_FAULT */
	A3XX_IRQ_CALLBACK(NULL),	       /* 22 - Unused */
	A3XX_IRQ_CALLBACK(NULL),	       /* 23 - Unused */
	A3XX_IRQ_CALLBACK(a3xx_fatal_err_callback),/* 24 - MISC_HANG_DETECT */
	A3XX_IRQ_CALLBACK(a3xx_err_callback),  /* 25 - UCHE_OOB_ACCESS */
	/* 26 to 31 - Unused */
};

static irqreturn_t a3xx_irq_handler(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = &adreno_dev->dev;
	irqreturn_t ret = IRQ_NONE;
	unsigned int status, tmp;
	int i;

	kgsl_regread(&adreno_dev->dev, A3XX_RBBM_INT_0_STATUS, &status);

	for (tmp = status, i = 0; tmp && i < ARRAY_SIZE(a3xx_irq_funcs); i++) {
		if (tmp & 1) {
			if (a3xx_irq_funcs[i].func != NULL) {
				a3xx_irq_funcs[i].func(adreno_dev, i);
				ret = IRQ_HANDLED;
			} else {
				KGSL_DRV_CRIT(device,
					"Unhandled interrupt bit %x\n", i);
			}
		}

		tmp >>= 1;
	}

	trace_kgsl_a3xx_irq_status(device, status);

	if (status)
		kgsl_regwrite(&adreno_dev->dev, A3XX_RBBM_INT_CLEAR_CMD,
			status);
	return ret;
}

static void a3xx_irq_control(struct adreno_device *adreno_dev, int state)
{
	struct kgsl_device *device = &adreno_dev->dev;

	if (state)
		kgsl_regwrite(device, A3XX_RBBM_INT_0_MASK, A3XX_INT_MASK |
			(test_bit(ADRENO_DEVICE_HANG_INTR, &adreno_dev->priv) ?
				(1 << A3XX_INT_MISC_HANG_DETECT) : 0));
	else
		kgsl_regwrite(device, A3XX_RBBM_INT_0_MASK, 0);
}

static unsigned int a3xx_irq_pending(struct adreno_device *adreno_dev)
{
	unsigned int status;

	kgsl_regread(&adreno_dev->dev, A3XX_RBBM_INT_0_STATUS, &status);

	return (status & A3XX_INT_MASK) ? 1 : 0;
}

static unsigned int counter_delta(struct adreno_device *adreno_dev,
			unsigned int reg, unsigned int *counter)
{
	struct kgsl_device *device = &adreno_dev->dev;
	unsigned int val;
	unsigned int ret = 0;

	/* Read the value */
	if (reg == ADRENO_REG_RBBM_PERFCTR_PWR_1_LO)
		adreno_readreg(adreno_dev, reg, &val);
	else
		kgsl_regread(device, reg, &val);

	/* Return 0 for the first read */
	if (*counter != 0) {
		if (val < *counter)
			ret = (0xFFFFFFFF - *counter) + val;
		else
			ret = val - *counter;
	}

	*counter = val;
	return ret;
}

/*
 * a3xx_busy_cycles() - Returns number of gpu cycles
 * @adreno_dev: Pointer to device ehose cycles are checked
 *
 * Returns number of busy cycles since the last time this function is called
 * Function is common between a3xx and a4xx devices
 */
void a3xx_busy_cycles(struct adreno_device *adreno_dev,
				struct adreno_busy_data *data)
{
	struct adreno_busy_data *busy = &adreno_dev->busy_data;
	struct kgsl_device *device = &adreno_dev->dev;

	memset(data, 0, sizeof(*data));

	data->gpu_busy = counter_delta(adreno_dev,
					ADRENO_REG_RBBM_PERFCTR_PWR_1_LO,
					&busy->gpu_busy);
	if (device->pwrctrl.bus_control) {
		data->vbif_ram_cycles = counter_delta(adreno_dev,
					adreno_dev->ram_cycles_lo,
					&busy->vbif_ram_cycles);
		data->vbif_starved_ram = counter_delta(adreno_dev,
					A3XX_VBIF_PERF_PWR_CNT0_LO,
					&busy->vbif_starved_ram);
	}
}

struct a3xx_vbif_data {
	unsigned int reg;
	unsigned int val;
};
/* VBIF registers start after 0x3000 so use 0x0 as end of list marker */
static struct a3xx_vbif_data a305_vbif[] = {
	/* Set up 16 deep read/write request queues */
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_IN_RD_LIM_CONF1, 0x10101010 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303 },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_IN_WR_LIM_CONF1, 0x10101010 },
	/* Enable WR-REQ */
	{ A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x0000FF },
	/* Set up round robin arbitration between both AXI ports */
	{ A3XX_VBIF_ARB_CTL, 0x00000030 },
	/* Set up AOOO */
	{ A3XX_VBIF_OUT_AXI_AOOO_EN, 0x0000003C },
	{ A3XX_VBIF_OUT_AXI_AOOO, 0x003C003C },
	{0, 0},
};

static struct a3xx_vbif_data a305b_vbif[] = {
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x00181818 },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x00181818 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x00000018 },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x00000018 },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x00000303 },
	{ A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0003 },
	{0, 0},
};

static struct a3xx_vbif_data a305c_vbif[] = {
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x00101010 },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x00101010 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x00000010 },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x00000010 },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x00000101 },
	{ A3XX_VBIF_ARB_CTL, 0x00000010 },
	/* Set up AOOO */
	{ A3XX_VBIF_OUT_AXI_AOOO_EN, 0x00000007 },
	{ A3XX_VBIF_OUT_AXI_AOOO, 0x00070007 },
	{0, 0},
};

static struct a3xx_vbif_data a320_vbif[] = {
	/* Set up 16 deep read/write request queues */
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_IN_RD_LIM_CONF1, 0x10101010 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303 },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x10101010 },
	{ A3XX_VBIF_IN_WR_LIM_CONF1, 0x10101010 },
	/* Enable WR-REQ */
	{ A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x0000FF },
	/* Set up round robin arbitration between both AXI ports */
	{ A3XX_VBIF_ARB_CTL, 0x00000030 },
	/* Set up AOOO */
	{ A3XX_VBIF_OUT_AXI_AOOO_EN, 0x0000003C },
	{ A3XX_VBIF_OUT_AXI_AOOO, 0x003C003C },
	/* Enable 1K sort */
	{ A3XX_VBIF_ABIT_SORT, 0x000000FF },
	{ A3XX_VBIF_ABIT_SORT_CONF, 0x000000A4 },
	{0, 0},
};

static struct a3xx_vbif_data a330_vbif[] = {
	/* Set up 16 deep read/write request queues */
	{ A3XX_VBIF_IN_RD_LIM_CONF0, 0x18181818 },
	{ A3XX_VBIF_IN_RD_LIM_CONF1, 0x00001818 },
	{ A3XX_VBIF_OUT_RD_LIM_CONF0, 0x00001818 },
	{ A3XX_VBIF_OUT_WR_LIM_CONF0, 0x00001818 },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303 },
	{ A3XX_VBIF_IN_WR_LIM_CONF0, 0x18181818 },
	{ A3XX_VBIF_IN_WR_LIM_CONF1, 0x00001818 },
	/* Enable WR-REQ */
	{ A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x00003F },
	/* Set up round robin arbitration between both AXI ports */
	{ A3XX_VBIF_ARB_CTL, 0x00000030 },
	/* Set up VBIF_ROUND_ROBIN_QOS_ARB */
	{ A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0001 },
	/* Set up AOOO */
	{ A3XX_VBIF_OUT_AXI_AOOO_EN, 0x0000003F },
	{ A3XX_VBIF_OUT_AXI_AOOO, 0x003F003F },
	/* Enable 1K sort */
	{ A3XX_VBIF_ABIT_SORT, 0x0001003F },
	{ A3XX_VBIF_ABIT_SORT_CONF, 0x000000A4 },
	/* Disable VBIF clock gating. This is to enable AXI running
	 * higher frequency than GPU.
	 */
	{ A3XX_VBIF_CLKON, 1 },
	{0, 0},
};

/*
 * Most of the VBIF registers on 8974v2 have the correct values at power on, so
 * we won't modify those if we don't need to
 */
static struct a3xx_vbif_data a330v2_vbif[] = {
	/* Enable 1k sort */
	{ A3XX_VBIF_ABIT_SORT, 0x0001003F },
	{ A3XX_VBIF_ABIT_SORT_CONF, 0x000000A4 },
	/* Enable WR-REQ */
	{ A3XX_VBIF_GATE_OFF_WRREQ_EN, 0x00003F },
	{ A3XX_VBIF_DDR_OUT_MAX_BURST, 0x0000303 },
	/* Set up VBIF_ROUND_ROBIN_QOS_ARB */
	{ A3XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x0003 },
	{0, 0},
};

static struct {
	int(*devfunc)(struct adreno_device *);
	struct a3xx_vbif_data *vbif;
} a3xx_vbif_platforms[] = {
	{ adreno_is_a305, a305_vbif },
	{ adreno_is_a305c, a305c_vbif },
	{ adreno_is_a320, a320_vbif },
	/* A330v2 needs to be ahead of A330 so the right device matches */
	{ adreno_is_a330v2, a330v2_vbif },
	{ adreno_is_a330, a330_vbif },
	{ adreno_is_a305b, a305b_vbif },
};

/*
 * Define the available perfcounter groups - these get used by
 * adreno_perfcounter_get and adreno_perfcounter_put
 */

static struct adreno_perfcount_register a3xx_perfcounters_cp[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_CP_0_LO,
		A3XX_RBBM_PERFCTR_CP_0_HI, 0, A3XX_CP_PERFCOUNTER_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_rbbm[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RBBM_0_LO,
		A3XX_RBBM_PERFCTR_RBBM_0_HI, 1, A3XX_RBBM_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RBBM_1_LO,
		A3XX_RBBM_PERFCTR_RBBM_1_HI, 2, A3XX_RBBM_PERFCOUNTER1_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_pc[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PC_0_LO,
		A3XX_RBBM_PERFCTR_PC_0_HI, 3, A3XX_PC_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PC_1_LO,
		A3XX_RBBM_PERFCTR_PC_1_HI, 4, A3XX_PC_PERFCOUNTER1_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PC_2_LO,
		A3XX_RBBM_PERFCTR_PC_2_HI, 5, A3XX_PC_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PC_3_LO,
		A3XX_RBBM_PERFCTR_PC_3_HI, 6, A3XX_PC_PERFCOUNTER3_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_vfd[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_VFD_0_LO,
		A3XX_RBBM_PERFCTR_VFD_0_HI, 7, A3XX_VFD_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_VFD_1_LO,
		A3XX_RBBM_PERFCTR_VFD_1_HI, 8, A3XX_VFD_PERFCOUNTER1_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_hlsq[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_0_LO,
		A3XX_RBBM_PERFCTR_HLSQ_0_HI, 9,
		A3XX_HLSQ_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_1_LO,
		A3XX_RBBM_PERFCTR_HLSQ_1_HI, 10,
		A3XX_HLSQ_PERFCOUNTER1_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_2_LO,
		A3XX_RBBM_PERFCTR_HLSQ_2_HI, 11,
		A3XX_HLSQ_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_3_LO,
		A3XX_RBBM_PERFCTR_HLSQ_3_HI, 12,
		A3XX_HLSQ_PERFCOUNTER3_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_4_LO,
		A3XX_RBBM_PERFCTR_HLSQ_4_HI, 13,
		A3XX_HLSQ_PERFCOUNTER4_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_HLSQ_5_LO,
		A3XX_RBBM_PERFCTR_HLSQ_5_HI, 14,
		A3XX_HLSQ_PERFCOUNTER5_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_vpc[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_VPC_0_LO,
		A3XX_RBBM_PERFCTR_VPC_0_HI, 15, A3XX_VPC_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_VPC_1_LO,
		A3XX_RBBM_PERFCTR_VPC_1_HI, 16, A3XX_VPC_PERFCOUNTER1_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_tse[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TSE_0_LO,
		A3XX_RBBM_PERFCTR_TSE_0_HI, 17, A3XX_GRAS_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TSE_1_LO,
		A3XX_RBBM_PERFCTR_TSE_1_HI, 18, A3XX_GRAS_PERFCOUNTER1_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_ras[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RAS_0_LO,
		A3XX_RBBM_PERFCTR_RAS_0_HI, 19, A3XX_GRAS_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RAS_1_LO,
		A3XX_RBBM_PERFCTR_RAS_1_HI, 20, A3XX_GRAS_PERFCOUNTER3_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_uche[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_0_LO,
		A3XX_RBBM_PERFCTR_UCHE_0_HI, 21,
		A3XX_UCHE_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_1_LO,
		A3XX_RBBM_PERFCTR_UCHE_1_HI, 22,
		A3XX_UCHE_PERFCOUNTER1_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_2_LO,
		A3XX_RBBM_PERFCTR_UCHE_2_HI, 23,
		A3XX_UCHE_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_3_LO,
		A3XX_RBBM_PERFCTR_UCHE_3_HI, 24,
		A3XX_UCHE_PERFCOUNTER3_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_4_LO,
		A3XX_RBBM_PERFCTR_UCHE_4_HI, 25,
		A3XX_UCHE_PERFCOUNTER4_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_UCHE_5_LO,
		A3XX_RBBM_PERFCTR_UCHE_5_HI, 26,
		A3XX_UCHE_PERFCOUNTER5_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_tp[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_0_LO,
		A3XX_RBBM_PERFCTR_TP_0_HI, 27, A3XX_TP_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_1_LO,
		A3XX_RBBM_PERFCTR_TP_1_HI, 28, A3XX_TP_PERFCOUNTER1_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_2_LO,
		A3XX_RBBM_PERFCTR_TP_2_HI, 29, A3XX_TP_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_3_LO,
		A3XX_RBBM_PERFCTR_TP_3_HI, 30, A3XX_TP_PERFCOUNTER3_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_4_LO,
		A3XX_RBBM_PERFCTR_TP_4_HI, 31, A3XX_TP_PERFCOUNTER4_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_TP_5_LO,
		A3XX_RBBM_PERFCTR_TP_5_HI, 32, A3XX_TP_PERFCOUNTER5_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_sp[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_0_LO,
		A3XX_RBBM_PERFCTR_SP_0_HI, 33, A3XX_SP_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_1_LO,
		A3XX_RBBM_PERFCTR_SP_1_HI, 34, A3XX_SP_PERFCOUNTER1_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_2_LO,
		A3XX_RBBM_PERFCTR_SP_2_HI, 35, A3XX_SP_PERFCOUNTER2_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_3_LO,
		A3XX_RBBM_PERFCTR_SP_3_HI, 36, A3XX_SP_PERFCOUNTER3_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_4_LO,
		A3XX_RBBM_PERFCTR_SP_4_HI, 37, A3XX_SP_PERFCOUNTER4_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_5_LO,
		A3XX_RBBM_PERFCTR_SP_5_HI, 38, A3XX_SP_PERFCOUNTER5_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_6_LO,
		A3XX_RBBM_PERFCTR_SP_6_HI, 39, A3XX_SP_PERFCOUNTER6_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_SP_7_LO,
		A3XX_RBBM_PERFCTR_SP_7_HI, 40, A3XX_SP_PERFCOUNTER7_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_rb[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RB_0_LO,
		A3XX_RBBM_PERFCTR_RB_0_HI, 41, A3XX_RB_PERFCOUNTER0_SELECT },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_RB_1_LO,
		A3XX_RBBM_PERFCTR_RB_1_HI, 42, A3XX_RB_PERFCOUNTER1_SELECT },
};

static struct adreno_perfcount_register a3xx_perfcounters_pwr[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PWR_0_LO,
		A3XX_RBBM_PERFCTR_PWR_0_HI, -1, 0 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_RBBM_PERFCTR_PWR_1_LO,
		A3XX_RBBM_PERFCTR_PWR_1_HI, -1, 0 },
};

static struct adreno_perfcount_register a3xx_perfcounters_vbif[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF_PERF_CNT0_LO,
		A3XX_VBIF_PERF_CNT0_HI, -1, 0 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF_PERF_CNT1_LO,
		A3XX_VBIF_PERF_CNT1_HI, -1, 0 },
};
static struct adreno_perfcount_register a3xx_perfcounters_vbif_pwr[] = {
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF_PERF_PWR_CNT0_LO,
		A3XX_VBIF_PERF_PWR_CNT0_HI, -1, 0 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF_PERF_PWR_CNT1_LO,
		A3XX_VBIF_PERF_PWR_CNT1_HI, -1, 0 },
	{ KGSL_PERFCOUNTER_NOT_USED, 0, 0, A3XX_VBIF_PERF_PWR_CNT2_LO,
		A3XX_VBIF_PERF_PWR_CNT2_HI, -1, 0 },
};

static struct adreno_perfcount_group a3xx_perfcounter_groups[] = {
	ADRENO_PERFCOUNTER_GROUP(a3xx, cp),
	ADRENO_PERFCOUNTER_GROUP(a3xx, rbbm),
	ADRENO_PERFCOUNTER_GROUP(a3xx, pc),
	ADRENO_PERFCOUNTER_GROUP(a3xx, vfd),
	ADRENO_PERFCOUNTER_GROUP(a3xx, hlsq),
	ADRENO_PERFCOUNTER_GROUP(a3xx, vpc),
	ADRENO_PERFCOUNTER_GROUP(a3xx, tse),
	ADRENO_PERFCOUNTER_GROUP(a3xx, ras),
	ADRENO_PERFCOUNTER_GROUP(a3xx, uche),
	ADRENO_PERFCOUNTER_GROUP(a3xx, tp),
	ADRENO_PERFCOUNTER_GROUP(a3xx, sp),
	ADRENO_PERFCOUNTER_GROUP(a3xx, rb),
	ADRENO_PERFCOUNTER_GROUP_FLAGS(a3xx, pwr,
		ADRENO_PERFCOUNTER_GROUP_FIXED),
	ADRENO_PERFCOUNTER_GROUP(a3xx, vbif),
	ADRENO_PERFCOUNTER_GROUP_FLAGS(a3xx, vbif_pwr,
		ADRENO_PERFCOUNTER_GROUP_FIXED),
};

static struct adreno_perfcounters a3xx_perfcounters = {
	a3xx_perfcounter_groups,
	ARRAY_SIZE(a3xx_perfcounter_groups),
};

static inline int _get_counter(struct adreno_device *adreno_dev,
		int group, int countable, unsigned int *lo,
		unsigned int *hi)
{
	int ret = 0;

	if (*lo == 0) {

		ret = adreno_perfcounter_get(adreno_dev, group, countable,
			lo, hi, PERFCOUNTER_FLAG_KERNEL);

		if (ret) {
			struct kgsl_device *device = &adreno_dev->dev;

			KGSL_DRV_ERR(device,
				"Unable to allocate fault detect performance counter %d/%d\n",
				group, countable);
			KGSL_DRV_ERR(device,
				"GPU fault detect will be less reliable\n");
		}
	}

	return ret;
}

static inline void _put_counter(struct adreno_device *adreno_dev,
		int group, int countable, unsigned int *lo,
		unsigned int *hi)
{
	if (*lo != 0) {
		adreno_perfcounter_put(adreno_dev, group, countable,
			PERFCOUNTER_FLAG_KERNEL);
	}

	*lo = 0;
	*hi = 0;
}

/**
 * a3xx_fault_detect_start() - Allocate performance counters used for fast fault
 * detection
 * @adreno_dev: Pointer to an adreno_device structure
 *
 * Allocate the series of performance counters that should be periodically
 * checked to verify that the GPU is still moving
 */
void a3xx_fault_detect_start(struct adreno_device *adreno_dev)
{
	_get_counter(adreno_dev, KGSL_PERFCOUNTER_GROUP_SP,
		SP_ALU_ACTIVE_CYCLES,
		&ft_detect_regs[6], &ft_detect_regs[7]);

	_get_counter(adreno_dev, KGSL_PERFCOUNTER_GROUP_SP,
		SP0_ICL1_MISSES,
		&ft_detect_regs[8], &ft_detect_regs[9]);

	_get_counter(adreno_dev, KGSL_PERFCOUNTER_GROUP_SP,
		SP_FS_CFLOW_INSTRUCTIONS,
		&ft_detect_regs[10], &ft_detect_regs[11]);

	_get_counter(adreno_dev, KGSL_PERFCOUNTER_GROUP_TSE,
		TSE_INPUT_PRIM_NUM,
		&ft_detect_regs[12], &ft_detect_regs[13]);
}
/**
 * a3xx_fault_detect_stop() - Release performance counters used for fast fault
 * detection
 * @adreno_dev: Pointer to an adreno_device structure
 *
 * Release the counters allocated in a3xx_fault_detect_start
 */
void a3xx_fault_detect_stop(struct adreno_device *adreno_dev)
{
	_put_counter(adreno_dev, KGSL_PERFCOUNTER_GROUP_SP,
		SP_ALU_ACTIVE_CYCLES,
		&ft_detect_regs[6], &ft_detect_regs[7]);

	_put_counter(adreno_dev, KGSL_PERFCOUNTER_GROUP_SP,
		SP0_ICL1_MISSES,
		&ft_detect_regs[8], &ft_detect_regs[9]);

	_put_counter(adreno_dev, KGSL_PERFCOUNTER_GROUP_SP,
		SP_FS_CFLOW_INSTRUCTIONS,
		&ft_detect_regs[10], &ft_detect_regs[11]);

	_put_counter(adreno_dev, KGSL_PERFCOUNTER_GROUP_TSE,
		TSE_INPUT_PRIM_NUM,
		&ft_detect_regs[12], &ft_detect_regs[13]);
}

/**
 * a3xx_perfcounter_close() - Put counters that were initialized in
 * a3xx_perfcounter_init
 * @adreno_dev: Pointer to an adreno_device structure
 */
static void a3xx_perfcounter_close(struct adreno_device *adreno_dev)
{
	adreno_perfcounter_put(adreno_dev, KGSL_PERFCOUNTER_GROUP_PWR, 1,
		PERFCOUNTER_FLAG_KERNEL);

	adreno_perfcounter_put(adreno_dev, KGSL_PERFCOUNTER_GROUP_VBIF_PWR, 0,
		PERFCOUNTER_FLAG_KERNEL);

	adreno_perfcounter_put(adreno_dev, KGSL_PERFCOUNTER_GROUP_VBIF,
		VBIF_AXI_TOTAL_BEATS, PERFCOUNTER_FLAG_KERNEL);

	if (adreno_dev->fast_hang_detect)
		a3xx_fault_detect_stop(adreno_dev);
}

/**
 * a3xx_perfcounter_init() - Allocate performance counters for use in the kernel
 * @adreno_dev: Pointer to an adreno_device structure
 */
static int a3xx_perfcounter_init(struct adreno_device *adreno_dev)
{
	int ret;
	struct kgsl_device *device = &adreno_dev->dev;
	/* SP[3] counter is broken on a330 so disable it if a330 device */
	if (adreno_is_a330(adreno_dev))
		a3xx_perfcounters_sp[3].countable = KGSL_PERFCOUNTER_BROKEN;

	if (adreno_dev->fast_hang_detect)
		a3xx_fault_detect_start(adreno_dev);

	/* Reserve and start countable 1 in the PWR perfcounter group */
	ret = adreno_perfcounter_get(adreno_dev, KGSL_PERFCOUNTER_GROUP_PWR, 1,
			NULL, NULL, PERFCOUNTER_FLAG_KERNEL);

	if (device->pwrctrl.bus_control) {
		/* VBIF waiting for RAM */
		ret |= adreno_perfcounter_get(adreno_dev,
				KGSL_PERFCOUNTER_GROUP_VBIF_PWR, 0,
				NULL, NULL, PERFCOUNTER_FLAG_KERNEL);
		/* VBIF DDR cycles */
		ret |= adreno_perfcounter_get(adreno_dev,
				KGSL_PERFCOUNTER_GROUP_VBIF,
				VBIF_AXI_TOTAL_BEATS,
				&adreno_dev->ram_cycles_lo, NULL,
				PERFCOUNTER_FLAG_KERNEL);
	}

	/* Default performance counter profiling to false */
	adreno_dev->profile.enabled = false;
	return ret;
}

/**
 * a3xx_protect_init() - Initializes register protection on a3xx
 * @device: Pointer to the device structure
 * Performs register writes to enable protected access to sensitive
 * registers
 */
static void a3xx_protect_init(struct kgsl_device *device)
{
	int index = 0;

	/* enable access protection to privileged registers */
	kgsl_regwrite(device, A3XX_CP_PROTECT_CTRL, 0x00000007);

	/* RBBM registers */
	adreno_set_protected_registers(device, &index, 0x18, 0);
	adreno_set_protected_registers(device, &index, 0x20, 2);
	adreno_set_protected_registers(device, &index, 0x33, 0);
	adreno_set_protected_registers(device, &index, 0x42, 0);
	adreno_set_protected_registers(device, &index, 0x50, 4);
	adreno_set_protected_registers(device, &index, 0x63, 0);
	adreno_set_protected_registers(device, &index, 0x100, 4);

	/* CP registers */
	adreno_set_protected_registers(device, &index, 0x1C0, 5);
	adreno_set_protected_registers(device, &index, 0x1EC, 1);
	adreno_set_protected_registers(device, &index, 0x1F6, 1);
	adreno_set_protected_registers(device, &index, 0x1F8, 2);
	adreno_set_protected_registers(device, &index, 0x45E, 2);
	adreno_set_protected_registers(device, &index, 0x460, 4);

	/* RB registers */
	adreno_set_protected_registers(device, &index, 0xCC0, 0);

	/* VBIF registers */
	adreno_set_protected_registers(device, &index, 0x3000, 6);

	/* SMMU registers */
	adreno_set_protected_registers(device, &index, 0x4000, 14);
}

static void a3xx_start(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = &adreno_dev->dev;
	struct a3xx_vbif_data *vbif = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(a3xx_vbif_platforms); i++) {
		if (a3xx_vbif_platforms[i].devfunc(adreno_dev)) {
			vbif = a3xx_vbif_platforms[i].vbif;
			break;
		}
	}

	BUG_ON(vbif == NULL);

	while (vbif->reg != 0) {
		kgsl_regwrite(device, vbif->reg, vbif->val);
		vbif++;
	}

	/* Make all blocks contribute to the GPU BUSY perf counter */
	kgsl_regwrite(device, A3XX_RBBM_GPU_BUSY_MASKED, 0xFFFFFFFF);

	/* Tune the hystersis counters for SP and CP idle detection */
	kgsl_regwrite(device, A3XX_RBBM_SP_HYST_CNT, 0x10);
	kgsl_regwrite(device, A3XX_RBBM_WAIT_IDLE_CLOCKS_CTL, 0x10);

	/* Enable the RBBM error reporting bits.  This lets us get
	   useful information on failure */

	kgsl_regwrite(device, A3XX_RBBM_AHB_CTL0, 0x00000001);

	/* Enable AHB error reporting */
	kgsl_regwrite(device, A3XX_RBBM_AHB_CTL1, 0xA6FFFFFF);

	/* Turn on the power counters */
	kgsl_regwrite(device, A3XX_RBBM_RBBM_CTL, 0x00030000);

	/* Turn on hang detection - this spews a lot of useful information
	 * into the RBBM registers on a hang */

	kgsl_regwrite(device, A3XX_RBBM_INTERFACE_HANG_INT_CTL,
			(1 << 16) | 0xFFF);

	/* Enable 64-byte cacheline size. HW Default is 32-byte (0x000000E0). */
	kgsl_regwrite(device, A3XX_UCHE_CACHE_MODE_CONTROL_REG, 0x00000001);

	/* Enable Clock gating */
	kgsl_regwrite(device, A3XX_RBBM_CLOCK_CTL,
		adreno_a3xx_rbbm_clock_ctl_default(adreno_dev));

	if (adreno_is_a330v2(adreno_dev))
		kgsl_regwrite(device, A3XX_RBBM_GPR0_CTL,
			A330v2_RBBM_GPR0_CTL_DEFAULT);
	else if (adreno_is_a330(adreno_dev))
		kgsl_regwrite(device, A3XX_RBBM_GPR0_CTL,
			A330_RBBM_GPR0_CTL_DEFAULT);

	/* Set the OCMEM base address for A330 */
	if (adreno_is_a330(adreno_dev) ||
		adreno_is_a305b(adreno_dev)) {
		kgsl_regwrite(device, A3XX_RB_GMEM_BASE_ADDR,
			(unsigned int)(adreno_dev->ocmem_base >> 14));
	}
	/* Turn on protection */
	a3xx_protect_init(device);

	/* Turn on performance counters */
	kgsl_regwrite(device, A3XX_RBBM_PERFCTR_CTL, 0x01);

	kgsl_regwrite(device, A3XX_CP_DEBUG, A3XX_CP_DEBUG_DEFAULT);
	memset(&adreno_dev->busy_data, 0, sizeof(adreno_dev->busy_data));
}

/**
 * a3xx_coresight_enable() - Enables debugging through coresight
 * debug bus for adreno a3xx devices.
 * @device: Pointer to GPU device structure
 */
int a3xx_coresight_enable(struct kgsl_device *device)
{
	kgsl_mutex_lock(&device->mutex, &device->mutex_owner);
	if (!kgsl_active_count_get(device)) {
		kgsl_regwrite(device, A3XX_RBBM_DEBUG_BUS_CTL, 0x0001093F);
		kgsl_regwrite(device, A3XX_RBBM_DEBUG_BUS_STB_CTL0,
				0x00000000);
		kgsl_regwrite(device, A3XX_RBBM_DEBUG_BUS_STB_CTL1,
				0xFFFFFFFE);
		kgsl_regwrite(device, A3XX_RBBM_INT_TRACE_BUS_CTL,
				0x00201111);
		kgsl_regwrite(device, A3XX_RBBM_EXT_TRACE_BUS_CTL,
				0x89100010);
		kgsl_regwrite(device, A3XX_RBBM_EXT_TRACE_STOP_CNT,
				0x00017fff);
		kgsl_regwrite(device, A3XX_RBBM_EXT_TRACE_START_CNT,
				0x0001000f);
		kgsl_regwrite(device, A3XX_RBBM_EXT_TRACE_PERIOD_CNT ,
				0x0001ffff);
		kgsl_regwrite(device, A3XX_RBBM_EXT_TRACE_CMD,
				0x00000001);
		kgsl_active_count_put(device);
	}
	kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);
	return 0;
}

/**
 * a3xx_coresight_disable() - Disables debugging through coresight
 * debug bus for adreno a3xx devices.
 * @device: Pointer to GPU device structure
 */
void a3xx_coresight_disable(struct kgsl_device *device)
{
	kgsl_mutex_lock(&device->mutex, &device->mutex_owner);
	if (!kgsl_active_count_get(device)) {
		kgsl_regwrite(device, A3XX_RBBM_DEBUG_BUS_CTL, 0x0);
		kgsl_regwrite(device, A3XX_RBBM_DEBUG_BUS_STB_CTL0, 0x0);
		kgsl_regwrite(device, A3XX_RBBM_DEBUG_BUS_STB_CTL1, 0x0);
		kgsl_regwrite(device, A3XX_RBBM_INT_TRACE_BUS_CTL, 0x0);
		kgsl_regwrite(device, A3XX_RBBM_EXT_TRACE_BUS_CTL, 0x0);
		kgsl_regwrite(device, A3XX_RBBM_EXT_TRACE_STOP_CNT, 0x0);
		kgsl_regwrite(device, A3XX_RBBM_EXT_TRACE_START_CNT, 0x0);
		kgsl_regwrite(device, A3XX_RBBM_EXT_TRACE_PERIOD_CNT , 0x0);
		kgsl_regwrite(device, A3XX_RBBM_EXT_TRACE_CMD, 0x0);
		kgsl_active_count_put(device);
	}
	kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);
}

static void a3xx_coresight_write_reg(struct kgsl_device *device,
		unsigned int wordoffset, unsigned int val)
{
	kgsl_mutex_lock(&device->mutex, &device->mutex_owner);
	if (!kgsl_active_count_get(device)) {
		kgsl_regwrite(device, wordoffset, val);
		kgsl_active_count_put(device);
	}
	kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);
}

void a3xx_coresight_config_debug_reg(struct kgsl_device *device,
		int debug_reg, unsigned int val)
{
	switch (debug_reg) {

	case DEBUG_BUS_CTL:
		a3xx_coresight_write_reg(device, A3XX_RBBM_DEBUG_BUS_CTL, val);
		break;

	case TRACE_STOP_CNT:
		a3xx_coresight_write_reg(device, A3XX_RBBM_EXT_TRACE_STOP_CNT,
				val);
		break;

	case TRACE_START_CNT:
		a3xx_coresight_write_reg(device, A3XX_RBBM_EXT_TRACE_START_CNT,
				val);
		break;

	case TRACE_PERIOD_CNT:
		a3xx_coresight_write_reg(device, A3XX_RBBM_EXT_TRACE_PERIOD_CNT,
				val);
		break;

	case TRACE_CMD:
		a3xx_coresight_write_reg(device, A3XX_RBBM_EXT_TRACE_CMD, val);
		break;

	case TRACE_BUS_CTL:
		a3xx_coresight_write_reg(device, A3XX_RBBM_EXT_TRACE_BUS_CTL,
				val);
		break;
	}

}

/*
 * a3xx_soft_reset() - Soft reset GPU
 * @adreno_dev: Pointer to adreno device
 *
 * Soft reset the GPU by doing a AHB write of value 1 to RBBM_SW_RESET
 * register. This is used when we want to reset the GPU without
 * turning off GFX power rail. The reset when asserted resets
 * all the HW logic, restores GPU registers to default state and
 * flushes out pending VBIF transactions.
 */
static void a3xx_soft_reset(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = &adreno_dev->dev;
	unsigned int reg;

	kgsl_regwrite(device, A3XX_RBBM_SW_RESET_CMD, 1);
	/*
	 * Do a dummy read to get a brief read cycle delay for the reset to take
	 * effect
	 */
	kgsl_regread(device, A3XX_RBBM_SW_RESET_CMD, &reg);
	kgsl_regwrite(device, A3XX_RBBM_SW_RESET_CMD, 0);
}

/* Defined in adreno_a3xx_snapshot.c */
void *a3xx_snapshot(struct adreno_device *adreno_dev, void *snapshot,
	int *remain, int hang);

/* Register offset defines for A3XX */
static unsigned int a3xx_register_offsets[ADRENO_REG_REGISTER_MAX] = {
	ADRENO_REG_DEFINE(ADRENO_REG_CP_DEBUG, A3XX_CP_DEBUG),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_ME_RAM_WADDR, A3XX_CP_ME_RAM_WADDR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_ME_RAM_DATA, A3XX_CP_ME_RAM_DATA),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_PFP_UCODE_DATA, A3XX_CP_PFP_UCODE_DATA),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_PFP_UCODE_ADDR, A3XX_CP_PFP_UCODE_ADDR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_WFI_PEND_CTR, A3XX_CP_WFI_PEND_CTR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_RB_BASE, A3XX_CP_RB_BASE),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_RB_RPTR_ADDR, A3XX_CP_RB_RPTR_ADDR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_RB_RPTR, A3XX_CP_RB_RPTR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_RB_WPTR, A3XX_CP_RB_WPTR),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_PROTECT_CTRL, A3XX_CP_PROTECT_CTRL),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_ME_CNTL, A3XX_CP_ME_CNTL),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_RB_CNTL, A3XX_CP_RB_CNTL),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_IB1_BASE, A3XX_CP_IB1_BASE),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_IB1_BUFSZ, A3XX_CP_IB1_BUFSZ),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_IB2_BASE, A3XX_CP_IB2_BASE),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_IB2_BUFSZ, A3XX_CP_IB2_BUFSZ),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_TIMESTAMP, A3XX_CP_SCRATCH_REG0),
	ADRENO_REG_DEFINE(ADRENO_REG_SCRATCH_ADDR, A3XX_CP_SCRATCH_ADDR),
	ADRENO_REG_DEFINE(ADRENO_REG_SCRATCH_UMSK, A3XX_CP_SCRATCH_UMSK),
	ADRENO_REG_DEFINE(ADRENO_REG_CP_HW_FAULT, A3XX_CP_HW_FAULT),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_STATUS, A3XX_RBBM_STATUS),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PERFCTR_CTL, A3XX_RBBM_PERFCTR_CTL),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PERFCTR_LOAD_CMD0,
					A3XX_RBBM_PERFCTR_LOAD_CMD0),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PERFCTR_LOAD_CMD1,
					A3XX_RBBM_PERFCTR_LOAD_CMD1),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PERFCTR_PWR_1_LO,
					A3XX_RBBM_PERFCTR_PWR_1_LO),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_INT_0_MASK, A3XX_RBBM_INT_0_MASK),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_INT_0_STATUS, A3XX_RBBM_INT_0_STATUS),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_AHB_ERROR_STATUS,
					A3XX_RBBM_AHB_ERROR_STATUS),
	ADRENO_REG_DEFINE(ADRENO_REG_VPC_VPC_DEBUG_RAM_SEL,
					A3XX_VPC_VPC_DEBUG_RAM_SEL),
	ADRENO_REG_DEFINE(ADRENO_REG_VPC_VPC_DEBUG_RAM_READ,
					A3XX_VPC_VPC_DEBUG_RAM_READ),
	ADRENO_REG_DEFINE(ADRENO_REG_VSC_PIPE_DATA_ADDRESS_0,
				A3XX_VSC_PIPE_DATA_ADDRESS_0),
	ADRENO_REG_DEFINE(ADRENO_REG_VSC_PIPE_DATA_LENGTH_7,
					A3XX_VSC_PIPE_DATA_LENGTH_7),
	ADRENO_REG_DEFINE(ADRENO_REG_VSC_SIZE_ADDRESS, A3XX_VSC_SIZE_ADDRESS),
	ADRENO_REG_DEFINE(ADRENO_REG_VFD_CONTROL_0, A3XX_VFD_CONTROL_0),
	ADRENO_REG_DEFINE(ADRENO_REG_VFD_FETCH_INSTR_0_0,
					A3XX_VFD_FETCH_INSTR_0_0),
	ADRENO_REG_DEFINE(ADRENO_REG_VFD_FETCH_INSTR_1_F,
					A3XX_VFD_FETCH_INSTR_1_F),
	ADRENO_REG_DEFINE(ADRENO_REG_VFD_INDEX_MAX, A3XX_VFD_INDEX_MAX),
	ADRENO_REG_DEFINE(ADRENO_REG_SP_VS_PVT_MEM_ADDR_REG,
				A3XX_SP_VS_PVT_MEM_ADDR_REG),
	ADRENO_REG_DEFINE(ADRENO_REG_SP_FS_PVT_MEM_ADDR_REG,
				A3XX_SP_FS_PVT_MEM_ADDR_REG),
	ADRENO_REG_DEFINE(ADRENO_REG_SP_VS_OBJ_START_REG,
				A3XX_SP_VS_OBJ_START_REG),
	ADRENO_REG_DEFINE(ADRENO_REG_SP_FS_OBJ_START_REG,
				A3XX_SP_FS_OBJ_START_REG),
	ADRENO_REG_DEFINE(ADRENO_REG_PA_SC_AA_CONFIG, A3XX_PA_SC_AA_CONFIG),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_PM_OVERRIDE2, A3XX_RBBM_PM_OVERRIDE2),
	ADRENO_REG_DEFINE(ADRENO_REG_SCRATCH_REG2, A3XX_CP_SCRATCH_REG2),
	ADRENO_REG_DEFINE(ADRENO_REG_SQ_GPR_MANAGEMENT, A3XX_SQ_GPR_MANAGEMENT),
	ADRENO_REG_DEFINE(ADRENO_REG_SQ_INST_STORE_MANAGMENT,
				A3XX_SQ_INST_STORE_MANAGMENT),
	ADRENO_REG_DEFINE(ADRENO_REG_TP0_CHICKEN, A3XX_TP0_CHICKEN),
	ADRENO_REG_DEFINE(ADRENO_REG_RBBM_RBBM_CTL, A3XX_RBBM_RBBM_CTL),
	ADRENO_REG_DEFINE(ADRENO_REG_UCHE_INVALIDATE0,
			A3XX_UCHE_CACHE_INVALIDATE0_REG),
};

struct adreno_reg_offsets a3xx_reg_offsets = {
	.offsets = a3xx_register_offsets,
	.offset_0 = ADRENO_REG_REGISTER_MAX,
};

struct adreno_gpudev adreno_a3xx_gpudev = {
	.reg_offsets = &a3xx_reg_offsets,
	.perfcounters = &a3xx_perfcounters,

	.rb_init = a3xx_rb_init,
	.perfcounter_init = a3xx_perfcounter_init,
	.perfcounter_close = a3xx_perfcounter_close,
	.perfcounter_save = a3xx_perfcounter_save,
	.perfcounter_restore = a3xx_perfcounter_restore,
	.irq_control = a3xx_irq_control,
	.irq_handler = a3xx_irq_handler,
	.irq_pending = a3xx_irq_pending,
	.busy_cycles = a3xx_busy_cycles,
	.start = a3xx_start,
	.snapshot = a3xx_snapshot,
	.perfcounter_enable = a3xx_perfcounter_enable,
	.perfcounter_read = a3xx_perfcounter_read,
	.perfcounter_write = a3xx_perfcounter_write,
	.coresight_enable = a3xx_coresight_enable,
	.coresight_disable = a3xx_coresight_disable,
	.coresight_config_debug_reg = a3xx_coresight_config_debug_reg,
	.fault_detect_start = a3xx_fault_detect_start,
	.fault_detect_stop = a3xx_fault_detect_stop,
	.soft_reset = a3xx_soft_reset,
};
