/* arch/arm/mach-msm/include/mach/memory.h
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2013, The Linux Foundation. All rights reserved.
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
#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H
#include <linux/types.h>

/* physical offset of RAM */
#define PLAT_PHYS_OFFSET UL(CONFIG_PHYS_OFFSET)

#if defined(CONFIG_KEXEC_HARDBOOT)
#if defined(CONFIG_MACH_APQ8064_FLO)
#define KEXEC_HB_PAGE_ADDR		UL(0x88C00000)
#elif defined(CONFIG_MACH_APQ8064_MAKO)
#define KEXEC_HB_PAGE_ADDR		UL(0x88600000)
#elif defined(CONFIG_MACH_MSM8974_HAMMERHEAD)
#define KEXEC_HB_PAGE_ADDR		UL(0x10100000)
#define KEXEC_HB_KERNEL_LOC		UL(0x3208000)
#elif defined(CONFIG_MACH_FIND7OP)
#define KEXEC_HB_PAGE_ADDR		UL(0x2F600000)
#define KEXEC_HB_KERNEL_LOC 		UL(0x22000000)
#else
#error "Adress for kexec hardboot page not defined"
#endif
#endif

#ifndef __ASSEMBLY__
void clean_and_invalidate_caches(unsigned long, unsigned long, unsigned long);
void clean_caches(unsigned long, unsigned long, unsigned long);
void invalidate_caches(unsigned long, unsigned long, unsigned long);
int msm_get_memory_type_from_name(const char *memtype_name);

#ifdef CONFIG_CACHE_L2X0
extern void l2x0_cache_sync(void);
#define finish_arch_switch(prev)     do { l2x0_cache_sync(); } while (0)
#endif

#define MAX_HOLE_ADDRESS    (PHYS_OFFSET + 0x10000000)
/*
 * Need a temporary unique variable that no one will ever see to
 * hold the compat string. Line number gives this easily.
 * Need another layer of indirection to get __LINE__ to expand
 * properly as opposed to appending and ending up with
 * __compat___LINE__
 */
#define __CONCAT(a, b)	___CONCAT(a, b)
#define ___CONCAT(a, b)	a ## b

#define EXPORT_COMPAT(com)	\
static char *__CONCAT(__compat_, __LINE__)  __used \
	__attribute((__section__(".exportcompat.init"))) = com

extern char *__compat_exports_start[];
extern char *__compat_exports_end[];

#endif

#if defined CONFIG_ARCH_MSM_SCORPION || defined CONFIG_ARCH_MSM_KRAIT
#define arch_has_speculative_dfetch()	1
#endif

#endif

#define CONSISTENT_DMA_SIZE	(SZ_1M * 14)
