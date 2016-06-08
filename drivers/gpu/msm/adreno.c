/* Copyright (c) 2002,2007-2014, The Linux Foundation. All rights reserved.
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
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/of_coresight.h>
#include <linux/input.h>

#include <mach/socinfo.h>
#include <mach/msm_bus_board.h>
#include <mach/msm_bus.h>

#include "kgsl.h"
#include "kgsl_pwrscale.h"
#include "kgsl_cffdump.h"
#include "kgsl_sharedmem.h"
#include "kgsl_iommu.h"

#include "adreno.h"
#include "adreno_pm4types.h"
#include "adreno_trace.h"

#include "a3xx_reg.h"

#define DRIVER_VERSION_MAJOR   3
#define DRIVER_VERSION_MINOR   1

/* Number of times to try hard reset */
#define NUM_TIMES_RESET_RETRY 5

/* Adreno MH arbiter config*/
#define ADRENO_CFG_MHARB \
	(0x10 \
		| (0 << MH_ARBITER_CONFIG__SAME_PAGE_GRANULARITY__SHIFT) \
		| (1 << MH_ARBITER_CONFIG__L1_ARB_ENABLE__SHIFT) \
		| (1 << MH_ARBITER_CONFIG__L1_ARB_HOLD_ENABLE__SHIFT) \
		| (0 << MH_ARBITER_CONFIG__L2_ARB_CONTROL__SHIFT) \
		| (1 << MH_ARBITER_CONFIG__PAGE_SIZE__SHIFT) \
		| (1 << MH_ARBITER_CONFIG__TC_REORDER_ENABLE__SHIFT) \
		| (1 << MH_ARBITER_CONFIG__TC_ARB_HOLD_ENABLE__SHIFT) \
		| (0 << MH_ARBITER_CONFIG__IN_FLIGHT_LIMIT_ENABLE__SHIFT) \
		| (0x8 << MH_ARBITER_CONFIG__IN_FLIGHT_LIMIT__SHIFT) \
		| (1 << MH_ARBITER_CONFIG__CP_CLNT_ENABLE__SHIFT) \
		| (1 << MH_ARBITER_CONFIG__VGT_CLNT_ENABLE__SHIFT) \
		| (1 << MH_ARBITER_CONFIG__TC_CLNT_ENABLE__SHIFT) \
		| (1 << MH_ARBITER_CONFIG__RB_CLNT_ENABLE__SHIFT) \
		| (1 << MH_ARBITER_CONFIG__PA_CLNT_ENABLE__SHIFT))

#define ADRENO_MMU_CONFIG						\
	(0x01								\
	 | (MMU_CONFIG << MH_MMU_CONFIG__RB_W_CLNT_BEHAVIOR__SHIFT)	\
	 | (MMU_CONFIG << MH_MMU_CONFIG__CP_W_CLNT_BEHAVIOR__SHIFT)	\
	 | (MMU_CONFIG << MH_MMU_CONFIG__CP_R0_CLNT_BEHAVIOR__SHIFT)	\
	 | (MMU_CONFIG << MH_MMU_CONFIG__CP_R1_CLNT_BEHAVIOR__SHIFT)	\
	 | (MMU_CONFIG << MH_MMU_CONFIG__CP_R2_CLNT_BEHAVIOR__SHIFT)	\
	 | (MMU_CONFIG << MH_MMU_CONFIG__CP_R3_CLNT_BEHAVIOR__SHIFT)	\
	 | (MMU_CONFIG << MH_MMU_CONFIG__CP_R4_CLNT_BEHAVIOR__SHIFT)	\
	 | (MMU_CONFIG << MH_MMU_CONFIG__VGT_R0_CLNT_BEHAVIOR__SHIFT)	\
	 | (MMU_CONFIG << MH_MMU_CONFIG__VGT_R1_CLNT_BEHAVIOR__SHIFT)	\
	 | (MMU_CONFIG << MH_MMU_CONFIG__TC_R_CLNT_BEHAVIOR__SHIFT)	\
	 | (MMU_CONFIG << MH_MMU_CONFIG__PA_W_CLNT_BEHAVIOR__SHIFT))

#define KGSL_LOG_LEVEL_DEFAULT 3

static void adreno_input_work(struct work_struct *work);

static struct devfreq_msm_adreno_tz_data adreno_tz_data = {
	.bus = {
		.max = 350,
	},
	.device_id = KGSL_DEVICE_3D0,
};

static const struct kgsl_functable adreno_functable;

static struct adreno_device device_3d0 = {
	.dev = {
		KGSL_DEVICE_COMMON_INIT(device_3d0.dev),
		.pwrscale = KGSL_PWRSCALE_INIT(&adreno_tz_data),
		.name = DEVICE_3D0_NAME,
		.id = KGSL_DEVICE_3D0,
		.mmu = {
			.config = ADRENO_MMU_CONFIG,
		},
		.pwrctrl = {
			.irq_name = KGSL_3D0_IRQ,
		},
		.iomemname = KGSL_3D0_REG_MEMORY,
		.shadermemname = KGSL_3D0_SHADER_MEMORY,
		.ftbl = &adreno_functable,
		.cmd_log = KGSL_LOG_LEVEL_DEFAULT,
		.ctxt_log = KGSL_LOG_LEVEL_DEFAULT,
		.drv_log = KGSL_LOG_LEVEL_DEFAULT,
		.mem_log = KGSL_LOG_LEVEL_DEFAULT,
		.pwr_log = KGSL_LOG_LEVEL_DEFAULT,
	},
	.gmem_base = 0,
	.gmem_size = SZ_256K,
	.pfp_fw = NULL,
	.pm4_fw = NULL,
	.wait_timeout = 0, /* in milliseconds, 0 means disabled */
	.ib_check_level = 0,
	.ft_policy = KGSL_FT_DEFAULT_POLICY,
	.ft_pf_policy = KGSL_FT_PAGEFAULT_DEFAULT_POLICY,
	.fast_hang_detect = 1,
	.long_ib_detect = 1,
	.input_work = __WORK_INITIALIZER(device_3d0.input_work,
		adreno_input_work),
};

unsigned int ft_detect_regs[FT_DETECT_REGS_COUNT];

/*
 * This is the master list of all GPU cores that are supported by this
 * driver.
 */

#define ANY_ID (~0)
#define NO_VER (~0)

static const struct {
	enum adreno_gpurev gpurev;
	unsigned int core, major, minor, patchid;
	const char *pm4fw;
	const char *pfpfw;
	struct adreno_gpudev *gpudev;
	unsigned int istore_size;
	unsigned int pix_shader_start;
	/* Size of an instruction in dwords */
	unsigned int instruction_size;
	/* size of gmem for gpu*/
	unsigned int gmem_size;
	/* version of pm4 microcode that supports sync_lock
	   between CPU and GPU for IOMMU-v0 programming */
	unsigned int sync_lock_pm4_ver;
	/* version of pfp microcode that supports sync_lock
	   between CPU and GPU for IOMMU-v0 programming */
	unsigned int sync_lock_pfp_ver;
	/* PM4 jump table index */
	unsigned int pm4_jt_idx;
	/* PM4 jump table load addr */
	unsigned int pm4_jt_addr;
	/* PFP jump table index */
	unsigned int pfp_jt_idx;
	/* PFP jump table load addr */
	unsigned int pfp_jt_addr;
	/* PM4 bootstrap loader size */
	unsigned int pm4_bstrp_size;
	/* PFP bootstrap loader size */
	unsigned int pfp_bstrp_size;
	/* PFP bootstrap loader supported version */
	unsigned int pfp_bstrp_ver;

} adreno_gpulist[] = {
	/* A3XX doesn't use the pix_shader_start */
	{ ADRENO_REV_A305, 3, 0, 5, 0,
		"a300_pm4.fw", "a300_pfp.fw", &adreno_a3xx_gpudev,
		512, 0, 2, SZ_256K, 0x3FF037, 0x3FF016 },
	/* A3XX doesn't use the pix_shader_start */
	{ ADRENO_REV_A320, 3, 2, ANY_ID, ANY_ID,
		"a300_pm4.fw", "a300_pfp.fw", &adreno_a3xx_gpudev,
		512, 0, 2, SZ_512K, 0x3FF037, 0x3FF016 },
	{ ADRENO_REV_A330, 3, 3, 0, ANY_ID,
		"a330_pm4.fw", "a330_pfp.fw", &adreno_a3xx_gpudev,
		512, 0, 2, SZ_1M, NO_VER, NO_VER, 0x8AD, 0x2E4, 0x201, 0x200,
		0x6, 0x20, 0x330020 },
	{ ADRENO_REV_A305B, 3, 0, 5, 0x10,
		"a330_pm4.fw", "a330_pfp.fw", &adreno_a3xx_gpudev,
		512, 0, 2, SZ_128K, NO_VER, NO_VER, 0x8AD, 0x2E4,
		0x201, 0x200 },
	/* 8226v2 */
	{ ADRENO_REV_A305B, 3, 0, 5, 0x12,
		"a330_pm4.fw", "a330_pfp.fw", &adreno_a3xx_gpudev,
		512, 0, 2, SZ_128K, NO_VER, NO_VER, 0x8AD, 0x2E4,
		0x201, 0x200 },
	{ ADRENO_REV_A305C, 3, 0, 5, 0x20,
		"a300_pm4.fw", "a300_pfp.fw", &adreno_a3xx_gpudev,
		512, 0, 2, SZ_128K, 0x3FF037, 0x3FF016 },
};

/* Nice level for the higher priority GPU start thread */
static int _wake_nice = -7;

/* Number of milliseconds to stay active active after a wake on touch */
static unsigned int _wake_timeout = 100;

/*
 * A workqueue callback responsible for actually turning on the GPU after a
 * touch event. kgsl_pwrctrl_wake() is used without any active_count protection
 * to avoid the need to maintain state.  Either somebody will start using the
 * GPU or the idle timer will fire and put the GPU back into slumber
 */
static void adreno_input_work(struct work_struct *work)
{
	struct adreno_device *adreno_dev = container_of(work,
			struct adreno_device, input_work);
	struct kgsl_device *device = &adreno_dev->dev;

	kgsl_mutex_lock(&device->mutex, &device->mutex_owner);

	device->flags |= KGSL_FLAG_WAKE_ON_TOUCH;

	/*
	 * Don't schedule adreno_start in a high priority workqueue, we are
	 * already in a workqueue which should be sufficient
	 */
	kgsl_pwrctrl_wake(device, 0);

	/*
	 * When waking up from a touch event we want to stay active long enough
	 * for the user to send a draw command.  The default idle timer timeout
	 * is shorter than we want so go ahead and push the idle timer out
	 * further for this special case
	 */
	mod_timer(&device->idle_timer,
		jiffies + msecs_to_jiffies(_wake_timeout));
	kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);
}

/*
 * Process input events and schedule work if needed.  At this point we are only
 * interested in groking EV_ABS touchscreen events
 */
static void adreno_input_event(struct input_handle *handle, unsigned int type,
		unsigned int code, int value)
{
	struct kgsl_device *device = handle->handler->private;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	/* Only consider EV_ABS (touch) events */
	if (type != EV_ABS)
		return;

	/*
	 * Don't do anything if anything hasn't been rendered since we've been
	 * here before
	 */

	if (device->flags & KGSL_FLAG_WAKE_ON_TOUCH)
		return;

	/*
	 * If the device is in nap, kick the idle timer to make sure that we
	 * don't go into slumber before the first render. If the device is
	 * already in slumber schedule the wake.
	 */

	if (device->state == KGSL_STATE_NAP) {
		/*
		 * Set the wake on touch bit to keep from coming back here and
		 * keeping the device in nap without rendering
		 */

		device->flags |= KGSL_FLAG_WAKE_ON_TOUCH;

		mod_timer(&device->idle_timer,
			jiffies + device->pwrctrl.interval_timeout);
	} else if (device->state == KGSL_STATE_SLUMBER) {
		schedule_work(&adreno_dev->input_work);
	}
}

static int adreno_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int ret;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (handle == NULL)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = handler->name;

	ret = input_register_handle(handle);
	if (ret) {
		kfree(handle);
		return ret;
	}

	ret = input_open_device(handle);
	if (ret) {
		input_unregister_handle(handle);
		kfree(handle);
	}

	return ret;
}

static void adreno_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

/*
 * We are only interested in EV_ABS events so only register handlers for those
 * input devices that have EV_ABS events
 */
static const struct input_device_id adreno_input_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		/* assumption: MT_.._X & MT_.._Y are in the same long */
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
				BIT_MASK(ABS_MT_POSITION_X) |
				BIT_MASK(ABS_MT_POSITION_Y) },
	},
	{ },
};

static struct input_handler adreno_input_handler = {
	.event = adreno_input_event,
	.connect = adreno_input_connect,
	.disconnect = adreno_input_disconnect,
	.name = "kgsl",
	.id_table = adreno_input_ids,
};

/**
 * adreno_perfcounter_init: Reserve kernel performance counters
 * @device: device to configure
 *
 * The kernel needs/wants a certain group of performance counters for
 * its own activities.  Reserve these performance counters at init time
 * to ensure that they are always reserved for the kernel.  The performance
 * counters used by the kernel can be obtained by the user, but these
 * performance counters will remain active as long as the device is alive.
 */

static int adreno_perfcounter_init(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	if (adreno_dev->gpudev->perfcounter_init)
		return adreno_dev->gpudev->perfcounter_init(adreno_dev);
	return 0;
};

/**
 * adreno_perfcounter_close: Release counters initialized by
 * adreno_perfcounter_init
 * @device: device to realease counters for
 *
 */
static void adreno_perfcounter_close(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	if (adreno_dev->gpudev->perfcounter_close)
		return adreno_dev->gpudev->perfcounter_close(adreno_dev);
}

/**
 * adreno_perfcounter_start: Enable performance counters
 * @adreno_dev: Adreno device to configure
 *
 * Ensure all performance counters are enabled that are allocated.  Since
 * the device was most likely stopped, we can't trust that the counters
 * are still valid so make it so.
 * Returns 0 on success else error code
 */

static int adreno_perfcounter_start(struct adreno_device *adreno_dev)
{
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct adreno_perfcount_group *group;
	unsigned int i, j;
	int ret = 0;

	if (NULL == counters)
		return 0;

	/* group id iter */
	for (i = 0; i < counters->group_count; i++) {
		group = &(counters->groups[i]);

		/* countable iter */
		for (j = 0; j < group->reg_count; j++) {
			if (group->regs[j].countable ==
					KGSL_PERFCOUNTER_NOT_USED ||
					group->regs[j].countable ==
					KGSL_PERFCOUNTER_BROKEN)
				continue;

			if (adreno_dev->gpudev->perfcounter_enable)
				ret = adreno_dev->gpudev->perfcounter_enable(
					adreno_dev, i, j,
					group->regs[j].countable);
				if (ret)
					goto done;
		}
	}
done:
	return ret;
}

/**
 * adreno_perfcounter_read_group() - Determine which countables are in counters
 * @adreno_dev: Adreno device to configure
 * @reads: List of kgsl_perfcounter_read_groups
 * @count: Length of list
 *
 * Read the performance counters for the groupid/countable pairs and return
 * the 64 bit result for each pair
 */

int adreno_perfcounter_read_group(struct adreno_device *adreno_dev,
	struct kgsl_perfcounter_read_group __user *reads, unsigned int count)
{
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcount_group *group;
	struct kgsl_perfcounter_read_group *list = NULL;
	unsigned int i, j;
	int ret = 0;

	if (NULL == counters)
		return -EINVAL;

	/* sanity check for later */
	if (!adreno_dev->gpudev->perfcounter_read)
		return -EINVAL;

	/* sanity check params passed in */
	if (reads == NULL || count == 0 || count > 100)
		return -EINVAL;

	list = kmalloc(sizeof(struct kgsl_perfcounter_read_group) * count,
			GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	if (copy_from_user(list, reads,
			sizeof(struct kgsl_perfcounter_read_group) * count)) {
		ret = -EFAULT;
		goto done;
	}

	kgsl_mutex_lock(&device->mutex, &device->mutex_owner);
	ret = kgsl_active_count_get(device);
	if (ret) {
		kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);
		goto done;
	}

	/* list iterator */
	for (j = 0; j < count; j++) {

		list[j].value = 0;

		/* Verify that the group ID is within range */
		if (list[j].groupid >= counters->group_count) {
			ret = -EINVAL;
			break;
		}

		group = &(counters->groups[list[j].groupid]);

		/* group/counter iterator */
		for (i = 0; i < group->reg_count; i++) {
			if (group->regs[i].countable == list[j].countable) {
				list[j].value =
					adreno_dev->gpudev->perfcounter_read(
					adreno_dev, list[j].groupid, i);
				break;
			}
		}
	}

	kgsl_active_count_put(device);
	kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);

	/* write the data */
	if (ret == 0)
		ret = copy_to_user(reads, list,
			sizeof(struct kgsl_perfcounter_read_group) * count);

done:
	kfree(list);
	return ret;
}

/**
 * adreno_perfcounter_get_groupid() - Get the performance counter ID
 * @adreno_dev: Adreno device
 * @name: Performance counter group name string
 *
 * Get the groupid based on the name and return this ID
 */

int adreno_perfcounter_get_groupid(struct adreno_device *adreno_dev,
					const char *name)
{

	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct adreno_perfcount_group *group;
	int i;

	if (name == NULL)
		return -EINVAL;

	if (NULL == counters)
		return -EINVAL;

	for (i = 0; i < counters->group_count; ++i) {
		group = &(counters->groups[i]);
		if (!strcmp(group->name, name))
			return i;
	}

	return -EINVAL;
}

/**
 * adreno_perfcounter_get_name() - Get the group name
 * @adreno_dev: Adreno device
 * @groupid: Desired performance counter groupid
 *
 * Get the name based on the groupid and return it
 */

const char *adreno_perfcounter_get_name(struct adreno_device *adreno_dev,
		unsigned int groupid)
{
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;

	if (NULL == counters)
		return NULL;

	if (groupid >= counters->group_count)
		return NULL;

	return counters->groups[groupid].name;
}

/**
 * adreno_perfcounter_query_group: Determine which countables are in counters
 * @adreno_dev: Adreno device to configure
 * @groupid: Desired performance counter group
 * @countables: Return list of all countables in the groups counters
 * @count: Max length of the array
 * @max_counters: max counters for the groupid
 *
 * Query the current state of counters for the group.
 */

int adreno_perfcounter_query_group(struct adreno_device *adreno_dev,
	unsigned int groupid, unsigned int *countables, unsigned int count,
	unsigned int *max_counters)
{
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct kgsl_device *device = &adreno_dev->dev;
	struct adreno_perfcount_group *group;
	unsigned int i, t;
	int ret;
	unsigned int *buf;

	*max_counters = 0;

	if (NULL == counters)
		return -EINVAL;

	if (groupid >= counters->group_count)
		return -EINVAL;

	kgsl_mutex_lock(&device->mutex, &device->mutex_owner);

	group = &(counters->groups[groupid]);
	*max_counters = group->reg_count;

	/*
	 * if NULL countable or *count of zero, return max reg_count in
	 * *max_counters and return success
	 */
	if (countables == NULL || count == 0) {
		kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);
		return 0;
	}

	t = min_t(unsigned int, group->reg_count, count);

	buf = kmalloc(t * sizeof(unsigned int), GFP_KERNEL);
	if (buf == NULL) {
		kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);
		return -ENOMEM;
	}

	for (i = 0; i < t; i++)
		buf[i] = group->regs[i].countable;

	kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);

	ret = copy_to_user(countables, buf, sizeof(unsigned int) * t);
	kfree(buf);

	return ret;
}

static inline void refcount_group(struct adreno_perfcount_group *group,
	unsigned int reg, unsigned int flags,
	unsigned int *lo, unsigned int *hi)
{
	if (flags & PERFCOUNTER_FLAG_KERNEL)
		group->regs[reg].kernelcount++;
	else
		group->regs[reg].usercount++;

	if (lo)
		*lo = group->regs[reg].offset;

	if (hi)
		*hi = group->regs[reg].offset_hi;
}

/**
 * adreno_perfcounter_get: Try to put a countable in an available counter
 * @adreno_dev: Adreno device to configure
 * @groupid: Desired performance counter group
 * @countable: Countable desired to be in a counter
 * @offset: Return offset of the LO counter assigned
 * @offset_hi: Return offset of the HI counter assigned
 * @flags: Used to setup kernel perf counters
 *
 * Try to place a countable in an available counter.  If the countable is
 * already in a counter, reference count the counter/countable pair resource
 * and return success
 */

int adreno_perfcounter_get(struct adreno_device *adreno_dev,
	unsigned int groupid, unsigned int countable, unsigned int *offset,
	unsigned int *offset_hi, unsigned int flags)
{
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct adreno_perfcount_group *group;
	unsigned int empty = -1;
	int ret = 0;

	/* always clear return variables */
	if (offset)
		*offset = 0;
	if (offset_hi)
		*offset_hi = 0;

	if (NULL == counters)
		return -EINVAL;

	if (groupid >= counters->group_count)
		return -EINVAL;

	group = &(counters->groups[groupid]);

	if (group->flags & ADRENO_PERFCOUNTER_GROUP_FIXED) {
		/*
		 * In fixed groups the countable equals the fixed register the
		 * user wants. First make sure it is in range
		 */

		if (countable >= group->reg_count)
			return -EINVAL;

		/* If it is already reserved, just increase the refcounts */
		if ((group->regs[countable].kernelcount != 0) ||
			(group->regs[countable].usercount != 0)) {
				refcount_group(group, countable, flags,
					offset, offset_hi);
				return 0;
		}

		empty = countable;
	} else {
		unsigned int i;

		/*
		 * Check if the countable is already associated with a counter.
		 * Refcount and return the offset, otherwise, try and find an
		 * empty counter and assign the countable to it.
		 */

		for (i = 0; i < group->reg_count; i++) {
			if (group->regs[i].countable == countable) {
				refcount_group(group, i, flags,
					offset, offset_hi);
				return 0;
			} else if (group->regs[i].countable ==
			KGSL_PERFCOUNTER_NOT_USED) {
				/* keep track of unused counter */
				empty = i;
			}
		}
	}

	/* no available counters, so do nothing else */
	if (empty == -1)
		return -EBUSY;

	/* enable the new counter */
	ret = adreno_dev->gpudev->perfcounter_enable(adreno_dev, groupid, empty,
		countable);
	if (ret)
		return ret;
	/* initialize the new counter */
	group->regs[empty].countable = countable;

	/* set initial kernel and user count */
	if (flags & PERFCOUNTER_FLAG_KERNEL) {
		group->regs[empty].kernelcount = 1;
		group->regs[empty].usercount = 0;
	} else {
		group->regs[empty].kernelcount = 0;
		group->regs[empty].usercount = 1;
	}

	if (offset)
		*offset = group->regs[empty].offset;
	if (offset_hi)
		*offset_hi = group->regs[empty].offset_hi;

	return ret;
}


/**
 * adreno_perfcounter_put: Release a countable from counter resource
 * @adreno_dev: Adreno device to configure
 * @groupid: Desired performance counter group
 * @countable: Countable desired to be freed from a  counter
 * @flags: Flag to determine if kernel or user space request
 *
 * Put a performance counter/countable pair that was previously received.  If
 * noone else is using the countable, free up the counter for others.
 */
int adreno_perfcounter_put(struct adreno_device *adreno_dev,
	unsigned int groupid, unsigned int countable, unsigned int flags)
{
	struct adreno_perfcounters *counters = adreno_dev->gpudev->perfcounters;
	struct adreno_perfcount_group *group;

	unsigned int i;

	if (NULL == counters)
		return -EINVAL;

	if (groupid >= counters->group_count)
		return -EINVAL;

	group = &(counters->groups[groupid]);

	/*
	 * Find if the counter/countable pair is used currently.
	 * Start cycling through registers in the bank.
	 */
	for (i = 0; i < group->reg_count; i++) {
		/* check if countable assigned is what we are looking for */
		if (group->regs[i].countable == countable) {
			/* found pair, book keep count based on request type */
			if (flags & PERFCOUNTER_FLAG_KERNEL &&
					group->regs[i].kernelcount > 0)
				group->regs[i].kernelcount--;
			else if (group->regs[i].usercount > 0)
				group->regs[i].usercount--;
			else
				break;

			/* mark available if not used anymore */
			if (group->regs[i].kernelcount == 0 &&
					group->regs[i].usercount == 0)
				group->regs[i].countable =
					KGSL_PERFCOUNTER_NOT_USED;

			return 0;
		}
	}

	return -EINVAL;
}

/**
 * adreno_perfcounter_restore() - Restore performance counters
 * @adreno_dev: adreno device to configure
 *
 * Load the physical performance counters with 64 bit value which are
 * saved on GPU power collapse.
 */
static inline void adreno_perfcounter_restore(struct adreno_device *adreno_dev)
{
	if (adreno_dev->gpudev->perfcounter_restore)
		adreno_dev->gpudev->perfcounter_restore(adreno_dev);
}

/**
 * adreno_perfcounter_save() - Save performance counters
 * @adreno_dev: adreno device to configure
 *
 * Save the performance counter values before GPU power collapse.
 * The saved values are restored on restart.
 * This ensures physical counters are coherent across power-collapse.
 */
static inline void adreno_perfcounter_save(struct adreno_device *adreno_dev)
{
	if (adreno_dev->gpudev->perfcounter_save)
		adreno_dev->gpudev->perfcounter_save(adreno_dev);
}

static irqreturn_t adreno_irq_handler(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	return adreno_dev->gpudev->irq_handler(adreno_dev);
}

static void adreno_cleanup_pt(struct kgsl_device *device,
			struct kgsl_pagetable *pagetable)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct adreno_ringbuffer *rb = &adreno_dev->ringbuffer;

	kgsl_mmu_unmap(pagetable, &rb->buffer_desc);

	kgsl_mmu_unmap(pagetable, &device->memstore);

	kgsl_mmu_unmap(pagetable, &adreno_dev->pwron_fixup);

	kgsl_mmu_unmap(pagetable, &device->mmu.setstate_memory);

	kgsl_mmu_unmap(pagetable, &adreno_dev->profile.shared_buffer);
}

static int adreno_setup_pt(struct kgsl_device *device,
			struct kgsl_pagetable *pagetable)
{
	int result;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct adreno_ringbuffer *rb = &adreno_dev->ringbuffer;

	result = kgsl_mmu_map_global(pagetable, &rb->buffer_desc);

	/*
	 * ALERT: Order of these mapping is important to
	 * Keep the most used entries like memstore
	 * and mmu setstate memory by TLB prefetcher.
	 */

	if (!result)
		result = kgsl_mmu_map_global(pagetable, &device->memstore);

	if (!result)
		result = kgsl_mmu_map_global(pagetable,
			&adreno_dev->pwron_fixup);

	if (!result)
		result = kgsl_mmu_map_global(pagetable,
			&device->mmu.setstate_memory);

	if (!result)
		result = kgsl_mmu_map_global(pagetable,
			&adreno_dev->profile.shared_buffer);

	if (result) {
		/* On error clean up what we have wrought */
		adreno_cleanup_pt(device, pagetable);
		return result;
	}

	/*
	 * Set the mpu end to the last "normal" global memory we use.
	 * For the IOMMU, this will be used to restrict access to the
	 * mapped registers.
	 */
	device->mh.mpu_range = adreno_dev->profile.shared_buffer.gpuaddr +
				adreno_dev->profile.shared_buffer.size;

	return 0;
}

static unsigned int _adreno_iommu_setstate_v0(struct kgsl_device *device,
					unsigned int *cmds_orig,
					phys_addr_t pt_val,
					int num_iommu_units, uint32_t flags)
{
	phys_addr_t reg_pt_val;
	unsigned int *cmds = cmds_orig;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	int i;

	if (cpu_is_msm8960())
		cmds += adreno_add_change_mh_phys_limit_cmds(cmds, 0xFFFFF000,
					device->mmu.setstate_memory.gpuaddr +
					KGSL_IOMMU_SETSTATE_NOP_OFFSET);
	else
		cmds += adreno_add_bank_change_cmds(cmds,
					KGSL_IOMMU_CONTEXT_USER,
					device->mmu.setstate_memory.gpuaddr +
					KGSL_IOMMU_SETSTATE_NOP_OFFSET);

	cmds += adreno_add_idle_cmds(adreno_dev, cmds);

	/* Acquire GPU-CPU sync Lock here */
	cmds += kgsl_mmu_sync_lock(&device->mmu, cmds);

	if (flags & KGSL_MMUFLAGS_PTUPDATE) {
		/*
		 * We need to perfrom the following operations for all
		 * IOMMU units
		 */
		for (i = 0; i < num_iommu_units; i++) {
			reg_pt_val = kgsl_mmu_get_default_ttbr0(&device->mmu,
						i, KGSL_IOMMU_CONTEXT_USER);
			reg_pt_val &= ~KGSL_IOMMU_CTX_TTBR0_ADDR_MASK;
			reg_pt_val |= (pt_val & KGSL_IOMMU_CTX_TTBR0_ADDR_MASK);
			/*
			 * Set address of the new pagetable by writng to IOMMU
			 * TTBR0 register
			 */
			*cmds++ = cp_type3_packet(CP_MEM_WRITE, 2);
			*cmds++ = kgsl_mmu_get_reg_gpuaddr(&device->mmu, i,
				KGSL_IOMMU_CONTEXT_USER, KGSL_IOMMU_CTX_TTBR0);
			*cmds++ = reg_pt_val;
			*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
			*cmds++ = 0x00000000;

			/*
			 * Read back the ttbr0 register as a barrier to ensure
			 * above writes have completed
			 */
			cmds += adreno_add_read_cmds(device, cmds,
				kgsl_mmu_get_reg_gpuaddr(&device->mmu, i,
				KGSL_IOMMU_CONTEXT_USER, KGSL_IOMMU_CTX_TTBR0),
				reg_pt_val,
				device->mmu.setstate_memory.gpuaddr +
				KGSL_IOMMU_SETSTATE_NOP_OFFSET);
		}
	}
	if (flags & KGSL_MMUFLAGS_TLBFLUSH) {
		/*
		 * tlb flush
		 */
		for (i = 0; i < num_iommu_units; i++) {
			reg_pt_val = (pt_val + kgsl_mmu_get_default_ttbr0(
						&device->mmu,
						i, KGSL_IOMMU_CONTEXT_USER));
			reg_pt_val &= ~KGSL_IOMMU_CTX_TTBR0_ADDR_MASK;
			reg_pt_val |= (pt_val & KGSL_IOMMU_CTX_TTBR0_ADDR_MASK);

			*cmds++ = cp_type3_packet(CP_MEM_WRITE, 2);
			*cmds++ = kgsl_mmu_get_reg_gpuaddr(&device->mmu, i,
				KGSL_IOMMU_CONTEXT_USER,
				KGSL_IOMMU_CTX_TLBIALL);
			*cmds++ = 1;

			cmds += __adreno_add_idle_indirect_cmds(cmds,
			device->mmu.setstate_memory.gpuaddr +
			KGSL_IOMMU_SETSTATE_NOP_OFFSET);

			cmds += adreno_add_read_cmds(device, cmds,
				kgsl_mmu_get_reg_gpuaddr(&device->mmu, i,
					KGSL_IOMMU_CONTEXT_USER,
					KGSL_IOMMU_CTX_TTBR0),
				reg_pt_val,
				device->mmu.setstate_memory.gpuaddr +
				KGSL_IOMMU_SETSTATE_NOP_OFFSET);
		}
	}

	/* Release GPU-CPU sync Lock here */
	cmds += kgsl_mmu_sync_unlock(&device->mmu, cmds);

	if (cpu_is_msm8960())
		cmds += adreno_add_change_mh_phys_limit_cmds(cmds,
			kgsl_mmu_get_reg_gpuaddr(&device->mmu, 0,
						0, KGSL_IOMMU_GLOBAL_BASE),
			device->mmu.setstate_memory.gpuaddr +
			KGSL_IOMMU_SETSTATE_NOP_OFFSET);
	else
		cmds += adreno_add_bank_change_cmds(cmds,
			KGSL_IOMMU_CONTEXT_PRIV,
			device->mmu.setstate_memory.gpuaddr +
			KGSL_IOMMU_SETSTATE_NOP_OFFSET);

	cmds += adreno_add_idle_cmds(adreno_dev, cmds);

	return cmds - cmds_orig;
}

static unsigned int _adreno_iommu_setstate_v1(struct kgsl_device *device,
					unsigned int *cmds_orig,
					phys_addr_t pt_val,
					int num_iommu_units, uint32_t flags)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	phys_addr_t ttbr0_val;
	unsigned int reg_pt_val;
	unsigned int *cmds = cmds_orig;
	int i;
	unsigned int ttbr0, tlbiall, tlbstatus, tlbsync, mmu_ctrl;

	cmds += adreno_add_idle_cmds(adreno_dev, cmds);

	for (i = 0; i < num_iommu_units; i++) {
		ttbr0_val = kgsl_mmu_get_default_ttbr0(&device->mmu,
				i, KGSL_IOMMU_CONTEXT_USER);
		ttbr0_val &= ~KGSL_IOMMU_CTX_TTBR0_ADDR_MASK;
		ttbr0_val |= (pt_val & KGSL_IOMMU_CTX_TTBR0_ADDR_MASK);
		if (flags & KGSL_MMUFLAGS_PTUPDATE) {
			mmu_ctrl = kgsl_mmu_get_reg_ahbaddr(
				&device->mmu, i,
				KGSL_IOMMU_CONTEXT_USER,
				KGSL_IOMMU_IMPLDEF_MICRO_MMU_CTRL) >> 2;

			ttbr0 = kgsl_mmu_get_reg_ahbaddr(&device->mmu, i,
						KGSL_IOMMU_CONTEXT_USER,
						KGSL_IOMMU_CTX_TTBR0) >> 2;

			if (kgsl_mmu_hw_halt_supported(&device->mmu, i)) {
				*cmds++ = cp_type3_packet(CP_WAIT_FOR_IDLE, 1);
				*cmds++ = 0;
				/*
				 * glue commands together until next
				 * WAIT_FOR_ME
				 */
				cmds += adreno_wait_reg_eq(cmds,
					adreno_getreg(adreno_dev,
						ADRENO_REG_CP_WFI_PEND_CTR),
					1, 0xFFFFFFFF, 0xF);

				/* set the iommu lock bit */
				*cmds++ = cp_type3_packet(CP_REG_RMW, 3);
				*cmds++ = mmu_ctrl;
				/* AND to unmask the lock bit */
				*cmds++ =
				 ~(KGSL_IOMMU_IMPLDEF_MICRO_MMU_CTRL_HALT);
				/* OR to set the IOMMU lock bit */
				*cmds++ =
				   KGSL_IOMMU_IMPLDEF_MICRO_MMU_CTRL_HALT;
				/* wait for smmu to lock */
				cmds += adreno_wait_reg_eq(cmds, mmu_ctrl,
				KGSL_IOMMU_IMPLDEF_MICRO_MMU_CTRL_IDLE,
				KGSL_IOMMU_IMPLDEF_MICRO_MMU_CTRL_IDLE, 0xF);
			}
			/* set ttbr0 */
			if (sizeof(phys_addr_t) > sizeof(unsigned long)) {
				reg_pt_val = ttbr0_val & 0xFFFFFFFF;
				*cmds++ = cp_type0_packet(ttbr0, 1);
				*cmds++ = reg_pt_val;
				reg_pt_val = (unsigned int)
				((ttbr0_val & 0xFFFFFFFF00000000ULL) >> 32);
				*cmds++ = cp_type0_packet(ttbr0 + 1, 1);
				*cmds++ = reg_pt_val;
			} else {
				reg_pt_val = ttbr0_val;
				*cmds++ = cp_type0_packet(ttbr0, 1);
				*cmds++ = reg_pt_val;
			}
			if (kgsl_mmu_hw_halt_supported(&device->mmu, i)) {
				/* unlock the IOMMU lock */
				*cmds++ = cp_type3_packet(CP_REG_RMW, 3);
				*cmds++ = mmu_ctrl;
				/* AND to unmask the lock bit */
				*cmds++ =
				   ~(KGSL_IOMMU_IMPLDEF_MICRO_MMU_CTRL_HALT);
				/* OR with 0 so lock bit is unset */
				*cmds++ = 0;
				/* release all commands with wait_for_me */
				*cmds++ = cp_type3_packet(CP_WAIT_FOR_ME, 1);
				*cmds++ = 0;
			}
		}
		if (flags & KGSL_MMUFLAGS_TLBFLUSH) {
			tlbiall = kgsl_mmu_get_reg_ahbaddr(&device->mmu, i,
						KGSL_IOMMU_CONTEXT_USER,
						KGSL_IOMMU_CTX_TLBIALL) >> 2;
			*cmds++ = cp_type0_packet(tlbiall, 1);
			*cmds++ = 1;

			tlbsync = kgsl_mmu_get_reg_ahbaddr(&device->mmu, i,
						KGSL_IOMMU_CONTEXT_USER,
						KGSL_IOMMU_CTX_TLBSYNC) >> 2;
			*cmds++ = cp_type0_packet(tlbsync, 1);
			*cmds++ = 0;

			tlbstatus = kgsl_mmu_get_reg_ahbaddr(&device->mmu, i,
					KGSL_IOMMU_CONTEXT_USER,
					KGSL_IOMMU_CTX_TLBSTATUS) >> 2;
			cmds += adreno_wait_reg_eq(cmds, tlbstatus, 0,
					KGSL_IOMMU_CTX_TLBSTATUS_SACTIVE, 0xF);
			/* release all commands with wait_for_me */
			*cmds++ = cp_type3_packet(CP_WAIT_FOR_ME, 1);
			*cmds++ = 0;
		}
	}

	cmds += adreno_add_idle_cmds(adreno_dev, cmds);

	return cmds - cmds_orig;
}

/**
 * adreno_use_default_setstate() - Use CPU instead of the GPU to manage the mmu?
 * @adreno_dev: the device
 *
 * In many cases it is preferable to poke the iommu directly rather
 * than using the GPU command stream. If we are idle or trying to go to a low
 * power state, using the command stream will be slower and asynchronous, which
 * needlessly complicates the power state transitions. Additionally,
 * the hardware simulators do not support command stream MMU operations so
 * the command stream can never be used if we are capturing CFF data.
 *
 */
static bool adreno_use_default_setstate(struct adreno_device *adreno_dev)
{
	return (adreno_isidle(&adreno_dev->dev) ||
		KGSL_STATE_ACTIVE != adreno_dev->dev.state ||
		atomic_read(&adreno_dev->dev.active_cnt) == 0 ||
		adreno_dev->dev.cff_dump_enable);
}

static int adreno_iommu_setstate(struct kgsl_device *device,
					unsigned int context_id,
					uint32_t flags)
{
	phys_addr_t pt_val;
	unsigned int *link = NULL, *cmds;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	int num_iommu_units;
	struct kgsl_context *context;
	struct adreno_context *adreno_ctx = NULL;
	struct adreno_ringbuffer *rb = &adreno_dev->ringbuffer;
	unsigned int result;

	if (adreno_use_default_setstate(adreno_dev)) {
		kgsl_mmu_device_setstate(&device->mmu, flags);
		return 0;
	}
	num_iommu_units = kgsl_mmu_get_num_iommu_units(&device->mmu);

	context = kgsl_context_get(device, context_id);
	if (!context) {
		kgsl_mmu_device_setstate(&device->mmu, flags);
		return 0;
	}
	adreno_ctx = ADRENO_CONTEXT(context);

	link = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (link == NULL) {
		result = -ENOMEM;
		goto done;
	}

	cmds = link;

	result = kgsl_mmu_enable_clk(&device->mmu, KGSL_IOMMU_CONTEXT_USER);

	if (result)
		goto done;

	pt_val = kgsl_mmu_get_pt_base_addr(&device->mmu,
				device->mmu.hwpagetable);

	cmds += __adreno_add_idle_indirect_cmds(cmds,
		device->mmu.setstate_memory.gpuaddr +
		KGSL_IOMMU_SETSTATE_NOP_OFFSET);

	if (msm_soc_version_supports_iommu_v0())
		cmds += _adreno_iommu_setstate_v0(device, cmds, pt_val,
						num_iommu_units, flags);
	else
		cmds += _adreno_iommu_setstate_v1(device, cmds, pt_val,
						num_iommu_units, flags);

	/* invalidate all base pointers */
	*cmds++ = cp_type3_packet(CP_INVALIDATE_STATE, 1);
	*cmds++ = 0x7fff;

	if ((unsigned int) (cmds - link) > (PAGE_SIZE / sizeof(unsigned int))) {
		KGSL_DRV_ERR(device, "Temp command buffer overflow\n");
		BUG();
	}
	/*
	 * This returns the per context timestamp but we need to
	 * use the global timestamp for iommu clock disablement
	 */
	result = adreno_ringbuffer_issuecmds(device, adreno_ctx,
			KGSL_CMD_FLAGS_PMODE, link,
			(unsigned int)(cmds - link));

	/*
	 * On error disable the IOMMU clock right away otherwise turn it off
	 * after the command has been retired
	 */
	if (result)
		kgsl_mmu_disable_clk(&device->mmu,
						KGSL_IOMMU_CONTEXT_USER);
	else
		kgsl_mmu_disable_clk_on_ts(&device->mmu, rb->global_ts,
						KGSL_IOMMU_CONTEXT_USER);

done:
	kfree(link);
	kgsl_context_put(context);
	return result;
}

static unsigned int
adreno_getchipid(struct kgsl_device *device)
{
	struct kgsl_device_platform_data *pdata =
		kgsl_device_get_drvdata(device);

	/* All A3XX and newer chipsets will specify the chipid in pdata */
	return pdata->chipid;
}

static inline bool _rev_match(unsigned int id, unsigned int entry)
{
	return (entry == ANY_ID || entry == id);
}

static void
adreno_identify_gpu(struct adreno_device *adreno_dev)
{
	unsigned int i, core, major, minor, patchid;

	adreno_dev->chip_id = adreno_getchipid(&adreno_dev->dev);

	core = ADRENO_CHIPID_CORE(adreno_dev->chip_id);
	major = ADRENO_CHIPID_MAJOR(adreno_dev->chip_id);
	minor = ADRENO_CHIPID_MINOR(adreno_dev->chip_id);
	patchid = ADRENO_CHIPID_PATCH(adreno_dev->chip_id);

	for (i = 0; i < ARRAY_SIZE(adreno_gpulist); i++) {
		if (core == adreno_gpulist[i].core &&
		    _rev_match(major, adreno_gpulist[i].major) &&
		    _rev_match(minor, adreno_gpulist[i].minor) &&
		    _rev_match(patchid, adreno_gpulist[i].patchid))
			break;
	}

	if (i == ARRAY_SIZE(adreno_gpulist)) {
		adreno_dev->gpurev = ADRENO_REV_UNKNOWN;
		return;
	}

	adreno_dev->gpurev = adreno_gpulist[i].gpurev;
	adreno_dev->gpudev = adreno_gpulist[i].gpudev;
	adreno_dev->pfp_fwfile = adreno_gpulist[i].pfpfw;
	adreno_dev->pm4_fwfile = adreno_gpulist[i].pm4fw;
	adreno_dev->istore_size = adreno_gpulist[i].istore_size;
	adreno_dev->pix_shader_start = adreno_gpulist[i].pix_shader_start;
	adreno_dev->instruction_size = adreno_gpulist[i].instruction_size;
	adreno_dev->gmem_size = adreno_gpulist[i].gmem_size;
	adreno_dev->pm4_jt_idx = adreno_gpulist[i].pm4_jt_idx;
	adreno_dev->pm4_jt_addr = adreno_gpulist[i].pm4_jt_addr;
	adreno_dev->pm4_bstrp_size = adreno_gpulist[i].pm4_bstrp_size;
	adreno_dev->pfp_jt_idx = adreno_gpulist[i].pfp_jt_idx;
	adreno_dev->pfp_jt_addr = adreno_gpulist[i].pfp_jt_addr;
	adreno_dev->pfp_bstrp_size = adreno_gpulist[i].pfp_bstrp_size;
	adreno_dev->pfp_bstrp_ver = adreno_gpulist[i].pfp_bstrp_ver;
	adreno_dev->gpulist_index = i;
	/*
	 * Initialize uninitialzed gpu registers, only needs to be done once
	 * Make all offsets that are not initialized to ADRENO_REG_UNUSED
	 */
	for (i = 0; i < ADRENO_REG_REGISTER_MAX; i++) {
		if (adreno_dev->gpudev->reg_offsets->offset_0 != i &&
			!adreno_dev->gpudev->reg_offsets->offsets[i]) {
			adreno_dev->gpudev->reg_offsets->offsets[i] =
						ADRENO_REG_UNUSED;
		}
	}
}

static struct platform_device_id adreno_id_table[] = {
	{ DEVICE_3D0_NAME, (kernel_ulong_t)&device_3d0.dev, },
	{},
};

MODULE_DEVICE_TABLE(platform, adreno_id_table);

static struct of_device_id adreno_match_table[] = {
	{ .compatible = "qcom,kgsl-3d0", },
	{}
};

static inline int adreno_of_read_property(struct device_node *node,
	const char *prop, unsigned int *ptr)
{
	int ret = of_property_read_u32(node, prop, ptr);
	if (ret)
		KGSL_CORE_ERR("Unable to read '%s'\n", prop);
	return ret;
}

static struct device_node *adreno_of_find_subnode(struct device_node *parent,
	const char *name)
{
	struct device_node *child;

	for_each_child_of_node(parent, child) {
		if (of_device_is_compatible(child, name))
			return child;
	}

	return NULL;
}

static int adreno_of_get_pwrlevels(struct device_node *parent,
	struct kgsl_device_platform_data *pdata)
{
	struct device_node *node, *child;
	int ret = -EINVAL;

	node = adreno_of_find_subnode(parent, "qcom,gpu-pwrlevels");

	if (node == NULL) {
		KGSL_CORE_ERR("Unable to find 'qcom,gpu-pwrlevels'\n");
		return -EINVAL;
	}

	pdata->num_levels = 0;

	for_each_child_of_node(node, child) {
		unsigned int index;
		struct kgsl_pwrlevel *level;

		if (adreno_of_read_property(child, "reg", &index))
			goto done;

		if (index >= KGSL_MAX_PWRLEVELS) {
			KGSL_CORE_ERR("Pwrlevel index %d is out of range\n",
				index);
			continue;
		}

		if (index >= pdata->num_levels)
			pdata->num_levels = index + 1;

		level = &pdata->pwrlevel[index];

		if (adreno_of_read_property(child, "qcom,gpu-freq",
			&level->gpu_freq))
			goto done;

		if (adreno_of_read_property(child, "qcom,bus-freq",
			&level->bus_freq))
			goto done;

		if (adreno_of_read_property(child, "qcom,io-fraction",
			&level->io_fraction))
			level->io_fraction = 0;
	}

	if (adreno_of_read_property(parent, "qcom,initial-pwrlevel",
		&pdata->init_level))
		pdata->init_level = 1;

	if (pdata->init_level < 0 || pdata->init_level > pdata->num_levels) {
		KGSL_CORE_ERR("Initial power level out of range\n");
		pdata->init_level = 1;
	}

	ret = 0;
done:
	return ret;

}

static int adreno_of_get_iommu(struct device_node *parent,
	struct kgsl_device_platform_data *pdata)
{
	struct device_node *node, *child;
	struct kgsl_device_iommu_data *data = NULL;
	struct kgsl_iommu_ctx *ctxs = NULL;
	u32 reg_val[2];
	int ctx_index = 0;

	node = of_parse_phandle(parent, "iommu", 0);
	if (node == NULL)
		return -EINVAL;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		KGSL_CORE_ERR("kzalloc(%d) failed\n", sizeof(*data));
		goto err;
	}

	if (of_property_read_u32_array(node, "reg", reg_val, 2))
		goto err;

	data->physstart = reg_val[0];
	data->physend = data->physstart + reg_val[1] - 1;
	data->iommu_halt_enable = of_property_read_bool(node,
					"qcom,iommu-enable-halt");

	data->iommu_ctx_count = 0;

	for_each_child_of_node(node, child)
		data->iommu_ctx_count++;

	ctxs = kzalloc(data->iommu_ctx_count * sizeof(struct kgsl_iommu_ctx),
		GFP_KERNEL);

	if (ctxs == NULL) {
		KGSL_CORE_ERR("kzalloc(%d) failed\n",
			data->iommu_ctx_count * sizeof(struct kgsl_iommu_ctx));
		goto err;
	}

	for_each_child_of_node(node, child) {
		int ret = of_property_read_string(child, "label",
				&ctxs[ctx_index].iommu_ctx_name);

		if (ret) {
			KGSL_CORE_ERR("Unable to read KGSL IOMMU 'label'\n");
			goto err;
		}

		if (!strcmp("gfx3d_user", ctxs[ctx_index].iommu_ctx_name)) {
			ctxs[ctx_index].ctx_id = 0;
		} else if (!strcmp("gfx3d_priv",
					ctxs[ctx_index].iommu_ctx_name)) {
			ctxs[ctx_index].ctx_id = 1;
		} else if (!strcmp("gfx3d_spare",
					ctxs[ctx_index].iommu_ctx_name)) {
			ctxs[ctx_index].ctx_id = 2;
		} else {
			KGSL_CORE_ERR("dt: IOMMU context %s is invalid\n",
				ctxs[ctx_index].iommu_ctx_name);
			goto err;
		}

		ctx_index++;
	}

	data->iommu_ctxs = ctxs;

	pdata->iommu_data = data;
	pdata->iommu_count = 1;

	return 0;

err:
	kfree(ctxs);
	kfree(data);

	return -EINVAL;
}

static int adreno_of_get_pdata(struct platform_device *pdev)
{
	struct kgsl_device_platform_data *pdata = NULL;
	struct kgsl_device *device;
	int ret = -EINVAL;

	pdev->id_entry = adreno_id_table;

	pdata = pdev->dev.platform_data;
	if (pdata)
		return 0;

	if (of_property_read_string(pdev->dev.of_node, "label", &pdev->name)) {
		KGSL_CORE_ERR("Unable to read 'label'\n");
		goto err;
	}

	if (adreno_of_read_property(pdev->dev.of_node, "qcom,id", &pdev->id))
		goto err;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL) {
		KGSL_CORE_ERR("kzalloc(%d) failed\n", sizeof(*pdata));
		ret = -ENOMEM;
		goto err;
	}

	if (adreno_of_read_property(pdev->dev.of_node, "qcom,chipid",
		&pdata->chipid))
		goto err;

	/* pwrlevel Data */
	ret = adreno_of_get_pwrlevels(pdev->dev.of_node, pdata);
	if (ret)
		goto err;

	/* get pm-qos-latency from target, set it to default if not found */
	if (adreno_of_read_property(pdev->dev.of_node, "qcom,pm-qos-latency",
		&pdata->pm_qos_latency))
		pdata->pm_qos_latency = 501;


	if (adreno_of_read_property(pdev->dev.of_node, "qcom,idle-timeout",
		&pdata->idle_timeout))
		pdata->idle_timeout = 80;

	pdata->strtstp_sleepwake = of_property_read_bool(pdev->dev.of_node,
						"qcom,strtstp-sleepwake");

	pdata->bus_control = of_property_read_bool(pdev->dev.of_node,
					"qcom,bus-control");

	if (adreno_of_read_property(pdev->dev.of_node, "qcom,clk-map",
		&pdata->clk_map))
		goto err;

	device = (struct kgsl_device *)pdev->id_entry->driver_data;

	if (device->id != KGSL_DEVICE_3D0)
		goto err;

	/* Bus Scale Data */

	pdata->bus_scale_table = msm_bus_cl_get_pdata(pdev);
	if (IS_ERR_OR_NULL(pdata->bus_scale_table)) {
		ret = PTR_ERR(pdata->bus_scale_table);
		if (!ret)
			ret = -EINVAL;
		goto err;
	}

	ret = adreno_of_get_iommu(pdev->dev.of_node, pdata);
	if (ret)
		goto err;

	pdata->coresight_pdata = of_get_coresight_platform_data(&pdev->dev,
			pdev->dev.of_node);

	pdev->dev.platform_data = pdata;
	return 0;

err:
	if (pdata) {
		if (pdata->iommu_data)
			kfree(pdata->iommu_data->iommu_ctxs);

		kfree(pdata->iommu_data);
	}

	kfree(pdata);

	return ret;
}

#ifdef CONFIG_MSM_OCMEM
static int
adreno_ocmem_gmem_malloc(struct adreno_device *adreno_dev)
{
	if (!(adreno_is_a330(adreno_dev) ||
		adreno_is_a305b(adreno_dev)))
		return 0;

	/* OCMEM is only needed once, do not support consective allocation */
	if (adreno_dev->ocmem_hdl != NULL)
		return 0;

	adreno_dev->ocmem_hdl =
		ocmem_allocate(OCMEM_GRAPHICS, adreno_dev->gmem_size);
	if (adreno_dev->ocmem_hdl == NULL)
		return -ENOMEM;

	adreno_dev->gmem_size = adreno_dev->ocmem_hdl->len;
	adreno_dev->ocmem_base = adreno_dev->ocmem_hdl->addr;

	return 0;
}

static void
adreno_ocmem_gmem_free(struct adreno_device *adreno_dev)
{
	if (!(adreno_is_a330(adreno_dev) ||
		adreno_is_a305b(adreno_dev)))
		return;

	if (adreno_dev->ocmem_hdl == NULL)
		return;

	ocmem_free(OCMEM_GRAPHICS, adreno_dev->ocmem_hdl);
	adreno_dev->ocmem_hdl = NULL;
}
#else
static int
adreno_ocmem_gmem_malloc(struct adreno_device *adreno_dev)
{
	return 0;
}

static void
adreno_ocmem_gmem_free(struct adreno_device *adreno_dev)
{
}
#endif

static int __devinit
adreno_probe(struct platform_device *pdev)
{
	struct kgsl_device *device;
	struct kgsl_device_platform_data *pdata = NULL;
	struct adreno_device *adreno_dev;
	int status = -EINVAL;
	bool is_dt;

	is_dt = of_match_device(adreno_match_table, &pdev->dev);

	if (is_dt && pdev->dev.of_node) {
		status = adreno_of_get_pdata(pdev);
		if (status)
			goto error_return;
	}

	device = (struct kgsl_device *)pdev->id_entry->driver_data;
	adreno_dev = ADRENO_DEVICE(device);
	device->parentdev = &pdev->dev;

	status = kgsl_device_platform_probe(device);
	if (status) {
		goto error;
	}

	status = adreno_ringbuffer_init(device);
	if (status != 0)
		goto error;

	if (status)
		goto error_close_rb;

	status = adreno_dispatcher_init(adreno_dev);
	if (status)
		goto error_close_device;

	adreno_debugfs_init(device);
	adreno_profile_init(device);

	adreno_ft_init_sysfs(device);

	kgsl_pwrscale_init(&pdev->dev, CONFIG_MSM_ADRENO_DEFAULT_GOVERNOR);


	device->flags &= ~KGSL_FLAGS_SOFT_RESET;
	pdata = kgsl_device_get_drvdata(device);

	adreno_coresight_init(pdev);

	adreno_input_handler.private = device;

	/*
	 * It isn't fatal if we cannot register the input handler.  Sad,
	 * perhaps, but not fatal
	 */
	if (input_register_handler(&adreno_input_handler))
		KGSL_DRV_ERR(device, "Unable to register the input handler\n");

	return 0;

error_close_device:
	kgsl_device_platform_remove(device);
error_close_rb:
	adreno_ringbuffer_close(&adreno_dev->ringbuffer);
error:
	device->parentdev = NULL;
error_return:
	return status;
}

static int __devexit adreno_remove(struct platform_device *pdev)
{
	struct kgsl_device *device;
	struct adreno_device *adreno_dev;

	device = (struct kgsl_device *)pdev->id_entry->driver_data;
	adreno_dev = ADRENO_DEVICE(device);

	input_unregister_handler(&adreno_input_handler);

	adreno_coresight_remove(pdev);
	adreno_profile_close(device);

	kgsl_pwrscale_close(device);

	adreno_dispatcher_close(adreno_dev);
	adreno_ringbuffer_close(&adreno_dev->ringbuffer);
	adreno_perfcounter_close(device);
	kgsl_device_platform_remove(device);

	clear_bit(ADRENO_DEVICE_INITIALIZED, &adreno_dev->priv);

	return 0;
}

static int adreno_init(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	int i;
	int ret;

	kgsl_pwrctrl_set_state(device, KGSL_STATE_INIT);
	/*
	 * initialization only needs to be done once initially until
	 * device is shutdown
	 */
	if (test_bit(ADRENO_DEVICE_INITIALIZED, &adreno_dev->priv))
		return 0;

	/* Power up the device */
	kgsl_pwrctrl_enable(device);

	/* Identify the specific GPU */
	adreno_identify_gpu(adreno_dev);

	if (adreno_ringbuffer_read_pm4_ucode(device)) {
		KGSL_DRV_ERR(device, "Reading pm4 microcode failed %s\n",
			adreno_dev->pm4_fwfile);
		BUG_ON(1);
	}

	if (adreno_ringbuffer_read_pfp_ucode(device)) {
		KGSL_DRV_ERR(device, "Reading pfp microcode failed %s\n",
			adreno_dev->pfp_fwfile);
		BUG_ON(1);
	}

	if (adreno_dev->gpurev == ADRENO_REV_UNKNOWN) {
		KGSL_DRV_ERR(device, "Unknown chip ID %x\n",
			adreno_dev->chip_id);
		BUG_ON(1);
	}

	kgsl_pwrctrl_set_state(device, KGSL_STATE_INIT);
	/*
	 * Check if firmware supports the sync lock PM4 packets needed
	 * for IOMMUv1
	 */

	if ((adreno_dev->pm4_fw_version >=
		adreno_gpulist[adreno_dev->gpulist_index].sync_lock_pm4_ver) &&
		(adreno_dev->pfp_fw_version >=
		adreno_gpulist[adreno_dev->gpulist_index].sync_lock_pfp_ver))
		device->mmu.flags |= KGSL_MMU_FLAGS_IOMMU_SYNC;

	/* Initialize ft detection register offsets */
	ft_detect_regs[0] = adreno_getreg(adreno_dev,
						ADRENO_REG_RBBM_STATUS);
	ft_detect_regs[1] = adreno_getreg(adreno_dev,
						ADRENO_REG_CP_RB_RPTR);
	ft_detect_regs[2] = adreno_getreg(adreno_dev,
						ADRENO_REG_CP_IB1_BASE);
	ft_detect_regs[3] = adreno_getreg(adreno_dev,
						ADRENO_REG_CP_IB1_BUFSZ);
	ft_detect_regs[4] = adreno_getreg(adreno_dev,
						ADRENO_REG_CP_IB2_BASE);
	ft_detect_regs[5] = adreno_getreg(adreno_dev,
						ADRENO_REG_CP_IB2_BUFSZ);
	for (i = 6; i < FT_DETECT_REGS_COUNT; i++)
		ft_detect_regs[i] = 0;

	/* turn on hang interrupt for a330v2 by default */
	if (adreno_is_a330v2(adreno_dev))
		set_bit(ADRENO_DEVICE_HANG_INTR, &adreno_dev->priv);

	ret = adreno_perfcounter_init(device);
	if (ret)
		goto done;

	/* Power down the device */
	kgsl_pwrctrl_disable(device);

	/* Enable the power on shader corruption fix for all A3XX targets */
	if (adreno_is_a3xx(adreno_dev))
		adreno_a3xx_pwron_fixup_init(adreno_dev);

	set_bit(ADRENO_DEVICE_INITIALIZED, &adreno_dev->priv);
done:
	return ret;
}

/**
 * _adreno_start - Power up the GPU and prepare to accept commands
 * @adreno_dev: Pointer to an adreno_device structure
 *
 * The core function that powers up and initalizes the GPU.  This function is
 * called at init and after coming out of SLUMBER
 */
static int _adreno_start(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = &adreno_dev->dev;
	int status = -EINVAL;
	unsigned int state = device->state;
	unsigned int regulator_left_on = 0;

	kgsl_cffdump_open(device);

	kgsl_pwrctrl_set_state(device, KGSL_STATE_INIT);

	regulator_left_on = (regulator_is_enabled(device->pwrctrl.gpu_reg) ||
				(device->pwrctrl.gpu_cx &&
				regulator_is_enabled(device->pwrctrl.gpu_cx)));

	/* Clear any GPU faults that might have been left over */
	adreno_clear_gpu_fault(adreno_dev);

	/* Power up the device */
	kgsl_pwrctrl_enable(device);

	/* Set the bit to indicate that we've just powered on */
	set_bit(ADRENO_DEVICE_PWRON, &adreno_dev->priv);

	status = kgsl_mmu_start(device);
	if (status)
		goto error_clk_off;

	status = adreno_ocmem_gmem_malloc(adreno_dev);
	if (status) {
		KGSL_DRV_ERR(device, "OCMEM malloc failed\n");
		goto error_mmu_off;
	}

	if (regulator_left_on && adreno_dev->gpudev->soft_reset) {
		/*
		 * Reset the GPU for A3xx. A2xx does a soft reset in
		 * the start function.
		 */
		adreno_dev->gpudev->soft_reset(adreno_dev);
	}

	/* Restore performance counter registers with saved values */
	adreno_perfcounter_restore(adreno_dev);

	/* Start the GPU */
	adreno_dev->gpudev->start(adreno_dev);

	kgsl_pwrctrl_irq(device, KGSL_PWRFLAGS_ON);
	device->ftbl->irqctrl(device, 1);

	status = adreno_ringbuffer_cold_start(&adreno_dev->ringbuffer);
	if (status)
		goto error_irq_off;

	status = adreno_perfcounter_start(adreno_dev);
	if (status)
		goto error_rb_stop;

	/* Start the dispatcher */
	adreno_dispatcher_start(device);

	device->reset_counter++;

	set_bit(ADRENO_DEVICE_STARTED, &adreno_dev->priv);

	return 0;

error_rb_stop:
	adreno_ringbuffer_stop(&adreno_dev->ringbuffer);
error_irq_off:
	kgsl_pwrctrl_irq(device, KGSL_PWRFLAGS_OFF);

error_mmu_off:
	kgsl_mmu_stop(&device->mmu);

error_clk_off:
	kgsl_pwrctrl_disable(device);
	/* set the state back to original state */
	kgsl_pwrctrl_set_state(device, state);

	return status;
}

/**
 * adreno_start() - Power up and initialize the GPU
 * @device: Pointer to the KGSL device to power up
 * @priority:  Boolean flag to specify of the start should be scheduled in a low
 * latency work queue
 *
 * Power up the GPU and initialize it.  If priority is specified then elevate
 * the thread priority for the duration of the start operation
 */
static int adreno_start(struct kgsl_device *device, int priority)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	int nice = task_nice(current);
	int ret;

	if (priority && (_wake_nice < nice))
		set_user_nice(current, _wake_nice);

	ret = _adreno_start(adreno_dev);

	if (priority)
		set_user_nice(current, nice);

	return ret;
}

static int adreno_stop(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	if (adreno_dev->drawctxt_active)
		kgsl_context_put(&adreno_dev->drawctxt_active->base);

	adreno_dev->drawctxt_active = NULL;

	adreno_dispatcher_stop(adreno_dev);
	adreno_ringbuffer_stop(&adreno_dev->ringbuffer);

	kgsl_mmu_stop(&device->mmu);

	device->ftbl->irqctrl(device, 0);
	kgsl_pwrctrl_irq(device, KGSL_PWRFLAGS_OFF);
	del_timer_sync(&device->idle_timer);

	adreno_ocmem_gmem_free(adreno_dev);

	/* Save physical performance counter values before GPU power down*/
	adreno_perfcounter_save(adreno_dev);

	/* Power down the device */
	kgsl_pwrctrl_disable(device);

	kgsl_cffdump_close(device);

	clear_bit(ADRENO_DEVICE_STARTED, &adreno_dev->priv);

	return 0;
}

/**
 * adreno_reset() - Helper function to reset the GPU
 * @device: Pointer to the KGSL device structure for the GPU
 *
 * Try to reset the GPU to recover from a fault.  First, try to do a low latency
 * soft reset.  If the soft reset fails for some reason, then bring out the big
 * guns and toggle the footswitch.
 */
int adreno_reset(struct kgsl_device *device)
{
	int ret = -EINVAL;
	struct kgsl_mmu *mmu = &device->mmu;
	int i = 0;

	/* Try soft reset first, for non mmu fault case only */
	if (!atomic_read(&mmu->fault)) {
		ret = adreno_soft_reset(device);
		if (ret)
			KGSL_DEV_ERR_ONCE(device, "Device soft reset failed\n");
	}
	if (ret) {
		/* If soft reset failed/skipped, then pull the power */
		adreno_stop(device);

		/* Keep trying to start the device until it works */
		for (i = 0; i < NUM_TIMES_RESET_RETRY; i++) {
			ret = adreno_start(device, 0);
			if (!ret)
				break;

			msleep(20);
		}
	}
	if (ret)
		return ret;

	if (0 != i)
		KGSL_DRV_WARN(device, "Device hard reset tried %d tries\n", i);

	/*
	 * If active_cnt is non-zero then the system was active before
	 * going into a reset - put it back in that state
	 */

	if (atomic_read(&device->active_cnt))
		kgsl_pwrctrl_set_state(device, KGSL_STATE_ACTIVE);

	/* Set the page table back to the default page table */
	kgsl_mmu_setstate(&device->mmu, device->mmu.defaultpagetable,
			KGSL_MEMSTORE_GLOBAL);

	return ret;
}

/**
 * _get_adreno_dev() -  Routine to get a pointer to adreno dev
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value to write
 * @count: size of the value to write
 */
struct adreno_device *_get_adreno_dev(struct device *dev)
{
	struct kgsl_device *device = kgsl_device_from_dev(dev);
	return device ? ADRENO_DEVICE(device) : NULL;
}

/**
 * _ft_policy_store() -  Routine to configure FT policy
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value to write
 * @count: size of the value to write
 *
 * FT policy can be set to any of the options below.
 * KGSL_FT_DISABLE -> BIT(0) Set to disable FT
 * KGSL_FT_REPLAY  -> BIT(1) Set to enable replay
 * KGSL_FT_SKIPIB  -> BIT(2) Set to skip IB
 * KGSL_FT_SKIPFRAME -> BIT(3) Set to skip frame
 * by default set FT policy to KGSL_FT_DEFAULT_POLICY
 */
static int _ft_policy_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct adreno_device *adreno_dev = _get_adreno_dev(dev);
	int ret;
	if (adreno_dev == NULL)
		return 0;

	mutex_lock(&adreno_dev->dev.mutex);
	ret = kgsl_sysfs_store(buf, &adreno_dev->ft_policy);
	mutex_unlock(&adreno_dev->dev.mutex);

	return ret < 0 ? ret : count;
}

/**
 * _ft_policy_show() -  Routine to read FT policy
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value read
 *
 * This is a routine to read current FT policy
 */
static int _ft_policy_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct adreno_device *adreno_dev = _get_adreno_dev(dev);
	if (adreno_dev == NULL)
		return 0;
	return snprintf(buf, PAGE_SIZE, "0x%X\n", adreno_dev->ft_policy);
}

/**
 * _ft_pagefault_policy_store() -  Routine to configure FT
 * pagefault policy
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value to write
 * @count: size of the value to write
 *
 * FT pagefault policy can be set to any of the options below.
 * KGSL_FT_PAGEFAULT_INT_ENABLE -> BIT(0) set to enable pagefault INT
 * KGSL_FT_PAGEFAULT_GPUHALT_ENABLE  -> BIT(1) Set to enable GPU HALT on
 * pagefaults. This stalls the GPU on a pagefault on IOMMU v1 HW.
 * KGSL_FT_PAGEFAULT_LOG_ONE_PER_PAGE  -> BIT(2) Set to log only one
 * pagefault per page.
 * KGSL_FT_PAGEFAULT_LOG_ONE_PER_INT -> BIT(3) Set to log only one
 * pagefault per INT.
 */
static int _ft_pagefault_policy_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct adreno_device *adreno_dev = _get_adreno_dev(dev);
	int ret = 0;
	unsigned int policy = 0;
	if (adreno_dev == NULL)
		return 0;

	mutex_lock(&adreno_dev->dev.mutex);

	ret = kgsl_sysfs_store(buf, &policy);
	if (ret)
		goto out;

	policy &= (KGSL_FT_PAGEFAULT_INT_ENABLE |
			KGSL_FT_PAGEFAULT_GPUHALT_ENABLE |
			KGSL_FT_PAGEFAULT_LOG_ONE_PER_PAGE |
			KGSL_FT_PAGEFAULT_LOG_ONE_PER_INT);
	ret = kgsl_mmu_set_pagefault_policy(&(adreno_dev->dev.mmu),
			adreno_dev->ft_pf_policy);
	if (!ret)
		adreno_dev->ft_pf_policy = policy;

out:
	mutex_unlock(&adreno_dev->dev.mutex);
	return ret < 0 ? ret : count;
}

/**
 * _ft_pagefault_policy_show() -  Routine to read FT pagefault
 * policy
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value read
 *
 * This is a routine to read current FT pagefault policy
 */
static int _ft_pagefault_policy_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct adreno_device *adreno_dev = _get_adreno_dev(dev);
	if (adreno_dev == NULL)
		return 0;
	return snprintf(buf, PAGE_SIZE, "0x%X\n", adreno_dev->ft_pf_policy);
}

/**
 * _ft_fast_hang_detect_store() -  Routine to configure FT fast
 * hang detect policy
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value to write
 * @count: size of the value to write
 *
 * 0x1 - Enable fast hang detection
 * 0x0 - Disable fast hang detection
 */
static int _ft_fast_hang_detect_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct adreno_device *adreno_dev = _get_adreno_dev(dev);
	int ret, tmp;

	if (adreno_dev == NULL)
		return 0;

	mutex_lock(&adreno_dev->dev.mutex);

	tmp = adreno_dev->fast_hang_detect;

	ret = kgsl_sysfs_store(buf, &adreno_dev->fast_hang_detect);

	if (tmp != adreno_dev->fast_hang_detect) {
		if (adreno_dev->fast_hang_detect) {
			if (adreno_dev->gpudev->fault_detect_start &&
				!kgsl_active_count_get(&adreno_dev->dev)) {
				adreno_dev->gpudev->fault_detect_start(
					adreno_dev);
				kgsl_active_count_put(&adreno_dev->dev);
			}
		} else {
			if (adreno_dev->gpudev->fault_detect_stop)
				adreno_dev->gpudev->fault_detect_stop(
					adreno_dev);
		}
	}

	mutex_unlock(&adreno_dev->dev.mutex);

	return ret < 0 ? ret : count;

}

/**
 * _ft_fast_hang_detect_show() -  Routine to read FT fast
 * hang detect policy
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value read
 */
static int _ft_fast_hang_detect_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct adreno_device *adreno_dev = _get_adreno_dev(dev);
	if (adreno_dev == NULL)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n",
				(adreno_dev->fast_hang_detect ? 1 : 0));
}

/**
 * _ft_long_ib_detect_store() -  Routine to configure FT long IB
 * detect policy
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value to write
 * @count: size of the value to write
 *
 * 0x0 - Enable long IB detection
 * 0x1 - Disable long IB detection
 */
static int _ft_long_ib_detect_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct adreno_device *adreno_dev = _get_adreno_dev(dev);
	int ret;
	if (adreno_dev == NULL)
		return 0;

	mutex_lock(&adreno_dev->dev.mutex);
	ret = kgsl_sysfs_store(buf, &adreno_dev->long_ib_detect);
	mutex_unlock(&adreno_dev->dev.mutex);

	return ret < 0 ? ret : count;

}

/**
 * _ft_long_ib_detect_show() -  Routine to read FT long IB
 * detect policy
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value read
 */
static int _ft_long_ib_detect_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct adreno_device *adreno_dev = _get_adreno_dev(dev);
	if (adreno_dev == NULL)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n",
				(adreno_dev->long_ib_detect ? 1 : 0));
}

/**
 * _wake_timeout_store() - Store the amount of time to extend idle check after
 * wake on touch
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value to write
 * @count: size of the value to write
 *
 */
static ssize_t _wake_timeout_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int ret = kgsl_sysfs_store(buf, &_wake_timeout);
	return ret < 0 ? ret : count;
}

/**
 * _wake_timeout_show() -  Show the amount of time idle check gets extended
 * after wake on touch
 * detect policy
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value read
 */
static ssize_t _wake_timeout_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", _wake_timeout);
}

/**
 * _ft_hang_intr_status_store -  Routine to enable/disable h/w hang interrupt
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value to write
 * @count: size of the value to write
 */
static ssize_t _ft_hang_intr_status_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned int new_setting, old_setting;
	struct kgsl_device *device = kgsl_device_from_dev(dev);
	struct adreno_device *adreno_dev;
	int ret;
	if (device == NULL)
		return 0;
	adreno_dev = ADRENO_DEVICE(device);

	mutex_lock(&device->mutex);
	ret = kgsl_sysfs_store(buf, &new_setting);
	if (ret)
		goto done;
	if (new_setting)
		new_setting = 1;
	old_setting =
		(test_bit(ADRENO_DEVICE_HANG_INTR, &adreno_dev->priv) ? 1 : 0);
	if (new_setting != old_setting) {
		if (new_setting)
			set_bit(ADRENO_DEVICE_HANG_INTR, &adreno_dev->priv);
		else
			clear_bit(ADRENO_DEVICE_HANG_INTR, &adreno_dev->priv);
		/* Set the new setting based on device state */
		switch (device->state) {
		case KGSL_STATE_NAP:
		case KGSL_STATE_SLEEP:
			kgsl_pwrctrl_wake(device, 0);
		case KGSL_STATE_ACTIVE:
			adreno_dev->gpudev->irq_control(adreno_dev, 1);
		/*
		 * For following states setting will be picked up on device
		 * start. Still need them in switch statement to differentiate
		 * from default
		 */
		case KGSL_STATE_SLUMBER:
		case KGSL_STATE_SUSPEND:
			break;
		default:
			ret = -EACCES;
			/* reset back to old setting on error */
			if (new_setting)
				clear_bit(ADRENO_DEVICE_HANG_INTR,
					&adreno_dev->priv);
			else
				set_bit(ADRENO_DEVICE_HANG_INTR,
					&adreno_dev->priv);
			goto done;
		}
	}
done:
	mutex_unlock(&device->mutex);
	return ret < 0 ? ret : count;
}

/**
 * _ft_hang_intr_status_show() -  Routine to read hardware hang interrupt
 * enablement
 * @dev: device ptr
 * @attr: Device attribute
 * @buf: value read
 */
static ssize_t _ft_hang_intr_status_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct adreno_device *adreno_dev = _get_adreno_dev(dev);
	if (adreno_dev == NULL)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n",
		test_bit(ADRENO_DEVICE_HANG_INTR, &adreno_dev->priv) ? 1 : 0);
}

#define FT_DEVICE_ATTR(name) \
	DEVICE_ATTR(name, 0644,	_ ## name ## _show, _ ## name ## _store);

FT_DEVICE_ATTR(ft_policy);
FT_DEVICE_ATTR(ft_pagefault_policy);
FT_DEVICE_ATTR(ft_fast_hang_detect);
FT_DEVICE_ATTR(ft_long_ib_detect);
FT_DEVICE_ATTR(ft_hang_intr_status);

static DEVICE_INT_ATTR(wake_nice, 0644, _wake_nice);
static FT_DEVICE_ATTR(wake_timeout);

const struct device_attribute *ft_attr_list[] = {
	&dev_attr_ft_policy,
	&dev_attr_ft_pagefault_policy,
	&dev_attr_ft_fast_hang_detect,
	&dev_attr_ft_long_ib_detect,
	&dev_attr_wake_nice.attr,
	&dev_attr_wake_timeout,
	&dev_attr_ft_hang_intr_status,
	NULL,
};

int adreno_ft_init_sysfs(struct kgsl_device *device)
{
	return kgsl_create_device_sysfs_files(device->dev, ft_attr_list);
}

void adreno_ft_uninit_sysfs(struct kgsl_device *device)
{
	kgsl_remove_device_sysfs_files(device->dev, ft_attr_list);
}

static int adreno_getproperty(struct kgsl_device *device,
				enum kgsl_property_type type,
				void *value,
				unsigned int sizebytes)
{
	int status = -EINVAL;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	switch (type) {
	case KGSL_PROP_DEVICE_INFO:
		{
			struct kgsl_devinfo devinfo;

			if (sizebytes != sizeof(devinfo)) {
				status = -EINVAL;
				break;
			}

			memset(&devinfo, 0, sizeof(devinfo));
			devinfo.device_id = device->id+1;
			devinfo.chip_id = adreno_dev->chip_id;
			devinfo.mmu_enabled = kgsl_mmu_enabled();
			devinfo.gpu_id = adreno_dev->gpurev;
			devinfo.gmem_gpubaseaddr = adreno_dev->gmem_base;
			devinfo.gmem_sizebytes = adreno_dev->gmem_size;

			if (copy_to_user(value, &devinfo, sizeof(devinfo)) !=
					0) {
				status = -EFAULT;
				break;
			}
			status = 0;
		}
		break;
	case KGSL_PROP_DEVICE_SHADOW:
		{
			struct kgsl_shadowprop shadowprop;

			if (sizebytes != sizeof(shadowprop)) {
				status = -EINVAL;
				break;
			}
			memset(&shadowprop, 0, sizeof(shadowprop));
			if (device->memstore.hostptr) {
				/*NOTE: with mmu enabled, gpuaddr doesn't mean
				 * anything to mmap().
				 */
				shadowprop.gpuaddr = device->memstore.gpuaddr;
				shadowprop.size = device->memstore.size;
				/* GSL needs this to be set, even if it
				   appears to be meaningless */
				shadowprop.flags = KGSL_FLAGS_INITIALIZED |
					KGSL_FLAGS_PER_CONTEXT_TIMESTAMPS;
			}
			if (copy_to_user(value, &shadowprop,
				sizeof(shadowprop))) {
				status = -EFAULT;
				break;
			}
			status = 0;
		}
		break;
	case KGSL_PROP_MMU_ENABLE:
		{
			int mmu_prop = kgsl_mmu_enabled();

			if (sizebytes != sizeof(int)) {
				status = -EINVAL;
				break;
			}
			if (copy_to_user(value, &mmu_prop, sizeof(mmu_prop))) {
				status = -EFAULT;
				break;
			}
			status = 0;
		}
		break;
	case KGSL_PROP_INTERRUPT_WAITS:
		{
			int int_waits = 1;
			if (sizebytes != sizeof(int)) {
				status = -EINVAL;
				break;
			}
			if (copy_to_user(value, &int_waits, sizeof(int))) {
				status = -EFAULT;
				break;
			}
			status = 0;
		}
		break;
	default:
		status = -EINVAL;
	}

	return status;
}

static int adreno_set_constraint(struct kgsl_device *device,
				struct kgsl_context *context,
				struct kgsl_device_constraint *constraint)
{
	int status = 0;

	switch (constraint->type) {
	case KGSL_CONSTRAINT_PWRLEVEL: {
		struct kgsl_device_constraint_pwrlevel pwr;

		if (constraint->size != sizeof(pwr)) {
			status = -EINVAL;
			break;
		}

		if (copy_from_user(&pwr,
				(void __user *)constraint->data,
				sizeof(pwr))) {
			status = -EFAULT;
			break;
		}
		if (pwr.level >= KGSL_CONSTRAINT_PWR_MAXLEVELS) {
			status = -EINVAL;
			break;
		}

		context->pwr_constraint.type =
				KGSL_CONSTRAINT_PWRLEVEL;
		context->pwr_constraint.sub_type = pwr.level;
		}
		break;
	case KGSL_CONSTRAINT_NONE:
		context->pwr_constraint.type = KGSL_CONSTRAINT_NONE;
		break;

	default:
		status = -EINVAL;
		break;
	}

	return status;
}

static int adreno_setproperty(struct kgsl_device_private *dev_priv,
				enum kgsl_property_type type,
				void *value,
				unsigned int sizebytes)
{
	int status = -EINVAL;
	struct kgsl_device *device = dev_priv->device;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	switch (type) {
	case KGSL_PROP_PWRCTRL: {
			unsigned int enable;

			if (sizebytes != sizeof(enable))
				break;

			if (copy_from_user(&enable, (void __user *) value,
				sizeof(enable))) {
				status = -EFAULT;
				break;
			}

			kgsl_mutex_lock(&device->mutex, &device->mutex_owner);

			if (enable) {
				device->pwrctrl.ctrl_flags = 0;
				adreno_dev->fast_hang_detect = 1;

				if (adreno_dev->gpudev->fault_detect_start)
					adreno_dev->gpudev->fault_detect_start(
						adreno_dev);

				kgsl_pwrscale_enable(device);
			} else {
				kgsl_pwrctrl_wake(device, 0);
				device->pwrctrl.ctrl_flags = KGSL_PWR_ON;
				adreno_dev->fast_hang_detect = 0;
				if (adreno_dev->gpudev->fault_detect_stop)
					adreno_dev->gpudev->fault_detect_stop(
						adreno_dev);
				kgsl_pwrscale_disable(device);
			}

			kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);
			status = 0;
		}
		break;
	case KGSL_PROP_PWR_CONSTRAINT: {
			struct kgsl_device_constraint constraint;
			struct kgsl_context *context;

			if (sizebytes != sizeof(constraint))
				break;

			if (copy_from_user(&constraint, value,
				sizeof(constraint))) {
				status = -EFAULT;
				break;
			}

			context = kgsl_context_get_owner(dev_priv,
							constraint.context_id);

			if (context == NULL)
				break;

			status = adreno_set_constraint(device, context,
								&constraint);

			kgsl_context_put(context);
		}
		break;
	default:
		break;
	}

	return status;
}

/**
 * adreno_hw_isidle() - Check if the GPU core is idle
 * @device: Pointer to the KGSL device structure for the GPU
 *
 * Return true if the RBBM status register for the GPU type indicates that the
 * hardware is idle
 */
bool adreno_hw_isidle(struct kgsl_device *device)
{
	unsigned int reg_rbbm_status;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	/* Don't consider ourselves idle if there is an IRQ pending */
	if (adreno_dev->gpudev->irq_pending(adreno_dev))
		return false;

	adreno_readreg(adreno_dev, ADRENO_REG_RBBM_STATUS,
		&reg_rbbm_status);

	if (!(reg_rbbm_status & 0x80000000))
		return true;

	return false;
}

/**
 * adreno_soft_reset() -  Do a soft reset of the GPU hardware
 * @device: KGSL device to soft reset
 *
 * "soft reset" the GPU hardware - this is a fast path GPU reset
 * The GPU hardware is reset but we never pull power so we can skip
 * a lot of the standard adreno_stop/adreno_start sequence
 */
int adreno_soft_reset(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	int ret;

	if (!adreno_dev->gpudev->soft_reset) {
		dev_WARN_ONCE(device->dev, 1, "Soft reset not supported");
		return -EINVAL;
	}

	if (adreno_dev->drawctxt_active)
		kgsl_context_put(&adreno_dev->drawctxt_active->base);

	adreno_dev->drawctxt_active = NULL;

	/* Stop the ringbuffer */
	adreno_ringbuffer_stop(&adreno_dev->ringbuffer);

	if (kgsl_pwrctrl_isenabled(device))
		device->ftbl->irqctrl(device, 0);

	kgsl_pwrctrl_irq(device, KGSL_PWRFLAGS_OFF);

	adreno_clear_gpu_fault(adreno_dev);

	/* Delete the idle timer */
	del_timer_sync(&device->idle_timer);

	/* Make sure we are totally awake */
	kgsl_pwrctrl_enable(device);

	/* save physical performance counter values before GPU soft reset */
	adreno_perfcounter_save(adreno_dev);

	/* Reset the GPU */
	adreno_dev->gpudev->soft_reset(adreno_dev);

	/* Restore physical performance counter values after soft reset */
	adreno_perfcounter_restore(adreno_dev);

	/* Reinitialize the GPU */
	adreno_dev->gpudev->start(adreno_dev);

	/* Enable IRQ */
	kgsl_pwrctrl_irq(device, KGSL_PWRFLAGS_ON);
	device->ftbl->irqctrl(device, 1);

	/*
	 * If we have offsets for the jump tables we can try to do a warm start,
	 * otherwise do a full ringbuffer restart
	 */

	if (adreno_dev->pm4_jt_idx)
		ret = adreno_ringbuffer_warm_start(&adreno_dev->ringbuffer);
	else
		ret = adreno_ringbuffer_cold_start(&adreno_dev->ringbuffer);

	if (ret)
		return ret;

	device->reset_counter++;

	return 0;
}

/*
 * adreno_isidle() - return true if the GPU hardware is idle
 * @device: Pointer to the KGSL device structure for the GPU
 *
 * Return true if the GPU hardware is idle and there are no commands pending in
 * the ringbuffer
 */
bool adreno_isidle(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	unsigned int rptr;

	if (!kgsl_pwrctrl_isenabled(device))
		return true;

	rptr = adreno_get_rptr(&adreno_dev->ringbuffer);

	/*
	 * wptr is updated when we add commands to ringbuffer, add a barrier
	 * to make sure updated wptr is compared to rptr
	 */
	smp_mb();

	if (rptr == adreno_dev->ringbuffer.wptr)
		return adreno_hw_isidle(device);

	return false;
}

/**
 * adreno_idle() - wait for the GPU hardware to go idle
 * @device: Pointer to the KGSL device structure for the GPU
 *
 * Wait up to ADRENO_IDLE_TIMEOUT milliseconds for the GPU hardware to go quiet.
 */

int adreno_idle(struct kgsl_device *device)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	unsigned long wait = jiffies + msecs_to_jiffies(ADRENO_IDLE_TIMEOUT);

	/*
	 * Make sure the device mutex is held so the dispatcher can't send any
	 * more commands to the hardware
	 */

	BUG_ON(!mutex_is_locked(&device->mutex));

	if (adreno_is_a3xx(adreno_dev))
		kgsl_cffdump_regpoll(device,
			adreno_getreg(adreno_dev, ADRENO_REG_RBBM_STATUS) << 2,
			0x00000000, 0x80000000);
	else
		kgsl_cffdump_regpoll(device,
			adreno_getreg(adreno_dev, ADRENO_REG_RBBM_STATUS) << 2,
			0x110, 0x110);

	while (time_before(jiffies, wait)) {
		/*
		 * If we fault, stop waiting and return an error. The dispatcher
		 * will clean up the fault from the work queue, but we need to
		 * make sure we don't block it by waiting for an idle that
		 * will never come.
		 */

		if (adreno_gpu_fault(adreno_dev) != 0)
			return -EDEADLK;

		if (adreno_isidle(device))
			return 0;
	}

	return -ETIMEDOUT;
}

/**
 * adreno_drain() - Drain the dispatch queue
 * @device: Pointer to the KGSL device structure for the GPU
 *
 * Drain the dispatcher of existing command batches.  This halts
 * additional commands from being issued until the gate is completed.
 */
static int adreno_drain(struct kgsl_device *device)
{
	INIT_COMPLETION(device->cmdbatch_gate);

	return 0;
}

/* Caller must hold the device mutex. */
static int adreno_suspend_context(struct kgsl_device *device)
{
	int status = 0;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	/* process any profiling results that are available */
	adreno_profile_process_results(device);

	/* switch to NULL ctxt */
	if (adreno_dev->drawctxt_active != NULL) {
		adreno_drawctxt_switch(adreno_dev, NULL, 0);
		status = adreno_idle(device);
	}

	return status;
}

/*
 * adreno_find_region() - Find corresponding allocation for a given address
 * @device: Device on which address operates
 * @pt_base: The pagetable in which address is mapped
 * @gpuaddr: The gpu address
 * @size: Size in bytes of the address
 * @entry: If the allocation is part of user space allocation then the mem
 * entry is returned in this parameter. Caller is supposed to decrement
 * refcount on this entry after its done using it.
 *
 * Finds an allocation descriptor for a given gpu address range
 *
 * Returns the descriptor on success else NULL
 */
struct kgsl_memdesc *adreno_find_region(struct kgsl_device *device,
						phys_addr_t pt_base,
						unsigned int gpuaddr,
						unsigned int size,
						struct kgsl_mem_entry **entry)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct adreno_ringbuffer *ringbuffer = &adreno_dev->ringbuffer;

	*entry = NULL;
	if (kgsl_gpuaddr_in_memdesc(&ringbuffer->buffer_desc, gpuaddr, size))
		return &ringbuffer->buffer_desc;

	if (kgsl_gpuaddr_in_memdesc(&device->memstore, gpuaddr, size))
		return &device->memstore;

	if (kgsl_gpuaddr_in_memdesc(&adreno_dev->pwron_fixup, gpuaddr, size))
		return &adreno_dev->pwron_fixup;

	if (kgsl_gpuaddr_in_memdesc(&device->mmu.setstate_memory, gpuaddr,
					size))
		return &device->mmu.setstate_memory;

	*entry = kgsl_get_mem_entry(device, pt_base, gpuaddr, size);

	if (*entry)
		return &((*entry)->memdesc);

	return NULL;
}

/*
 * adreno_convertaddr() - Convert a gpu address to kernel mapped address
 * @device: Device on which the address operates
 * @pt_base: The pagetable in which address is mapped
 * @gpuaddr: The start address
 * @size: The length of address range
 * @entry: If the allocation is part of user space allocation then the mem
 * entry is returned in this parameter. Caller is supposed to decrement
 * refcount on this entry after its done using it.
 *
 * Returns the converted host pointer on success else NULL
 */
uint8_t *adreno_convertaddr(struct kgsl_device *device, phys_addr_t pt_base,
			    unsigned int gpuaddr, unsigned int size,
				struct kgsl_mem_entry **entry)
{
	struct kgsl_memdesc *memdesc;

	memdesc = adreno_find_region(device, pt_base, gpuaddr, size, entry);

	return memdesc ? kgsl_gpuaddr_to_vaddr(memdesc, gpuaddr) : NULL;
}


/**
 * adreno_read - General read function to read adreno device memory
 * @device - Pointer to the GPU device struct (for adreno device)
 * @base - Base address (kernel virtual) where the device memory is mapped
 * @offsetwords - Offset in words from the base address, of the memory that
 * is to be read
 * @value - Value read from the device memory
 * @mem_len - Length of the device memory mapped to the kernel
 */
static void adreno_read(struct kgsl_device *device, void *base,
		unsigned int offsetwords, unsigned int *value,
		unsigned int mem_len)
{

	unsigned int *reg;
	BUG_ON(offsetwords*sizeof(uint32_t) >= mem_len);
	reg = (unsigned int *)(base + (offsetwords << 2));

	if (!in_interrupt())
		kgsl_pre_hwaccess(device);

	/*ensure this read finishes before the next one.
	 * i.e. act like normal readl() */
	*value = __raw_readl(reg);
	rmb();
}

/**
 * adreno_regread - Used to read adreno device registers
 * @offsetwords - Word (4 Bytes) offset to the register to be read
 * @value - Value read from device register
 */
static void adreno_regread(struct kgsl_device *device, unsigned int offsetwords,
	unsigned int *value)
{
	adreno_read(device, device->reg_virt, offsetwords, value,
						device->reg_len);
}

/**
 * adreno_shadermem_regread - Used to read GPU (adreno) shader memory
 * @device - GPU device whose shader memory is to be read
 * @offsetwords - Offset in words, of the shader memory address to be read
 * @value - Pointer to where the read shader mem value is to be stored
 */
void adreno_shadermem_regread(struct kgsl_device *device,
	unsigned int offsetwords, unsigned int *value)
{
	adreno_read(device, device->shader_mem_virt, offsetwords, value,
					device->shader_mem_len);
}

static void adreno_regwrite(struct kgsl_device *device,
				unsigned int offsetwords,
				unsigned int value)
{
	unsigned int *reg;

	BUG_ON(offsetwords*sizeof(uint32_t) >= device->reg_len);

	if (!in_interrupt())
		kgsl_pre_hwaccess(device);

	kgsl_trace_regwrite(device, offsetwords, value);

	kgsl_cffdump_regwrite(device, offsetwords << 2, value);
	reg = (unsigned int *)(device->reg_virt + (offsetwords << 2));

	/*ensure previous writes post before this one,
	 * i.e. act like normal writel() */
	wmb();
	__raw_writel(value, reg);
}

/**
 * adreno_waittimestamp - sleep while waiting for the specified timestamp
 * @device - pointer to a KGSL device structure
 * @context - pointer to the active kgsl context
 * @timestamp - GPU timestamp to wait for
 * @msecs - amount of time to wait (in milliseconds)
 *
 * Wait up to 'msecs' milliseconds for the specified timestamp to expire.
 */
static int adreno_waittimestamp(struct kgsl_device *device,
		struct kgsl_context *context,
		unsigned int timestamp,
		unsigned int msecs)
{
	int ret;
	struct adreno_context *drawctxt;

	if (context == NULL) {
		/* If they are doing then complain once */
		dev_WARN_ONCE(device->dev, 1,
			"IOCTL_KGSL_DEVICE_WAITTIMESTAMP is deprecated\n");
		return -ENOTTY;
	}

	/* Return -EINVAL if the context has been detached */
	if (kgsl_context_detached(context))
		return -EINVAL;

	ret = adreno_drawctxt_wait(ADRENO_DEVICE(device), context,
		timestamp, msecs);

	/* If the context got invalidated then return a specific error */
	drawctxt = ADRENO_CONTEXT(context);

	if (drawctxt->state == ADRENO_CONTEXT_STATE_INVALID)
		ret = -EDEADLK;

	/*
	 * Return -EPROTO if the device has faulted since the last time we
	 * checked.  Userspace uses this as a marker for performing post
	 * fault activities
	 */

	if (!ret && test_and_clear_bit(ADRENO_CONTEXT_FAULT, &drawctxt->priv))
		ret = -EPROTO;

	return ret;
}

static unsigned int adreno_readtimestamp(struct kgsl_device *device,
		struct kgsl_context *context, enum kgsl_timestamp_type type)
{
	unsigned int timestamp = 0;
	unsigned int id = context ? context->id : KGSL_MEMSTORE_GLOBAL;

	switch (type) {
	case KGSL_TIMESTAMP_QUEUED: {
		struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

		timestamp = adreno_context_timestamp(context,
				&adreno_dev->ringbuffer);
		break;
	}
	case KGSL_TIMESTAMP_CONSUMED:
		kgsl_sharedmem_readl(&device->memstore, &timestamp,
			KGSL_MEMSTORE_OFFSET(id, soptimestamp));
		break;
	case KGSL_TIMESTAMP_RETIRED:
		kgsl_sharedmem_readl(&device->memstore, &timestamp,
			KGSL_MEMSTORE_OFFSET(id, eoptimestamp));
		break;
	}

	return timestamp;
}

static long adreno_ioctl(struct kgsl_device_private *dev_priv,
			      unsigned int cmd, void *data)
{
	struct kgsl_device *device = dev_priv->device;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	int result = 0;

	switch (cmd) {
	case IOCTL_KGSL_PERFCOUNTER_GET: {
		struct kgsl_perfcounter_get *get = data;
		kgsl_mutex_lock(&device->mutex, &device->mutex_owner);
		/*
		 * adreno_perfcounter_get() is called by kernel clients
		 * during start(), so it is not safe to take an
		 * active count inside this function.
		 */
		result = kgsl_active_count_get(device);
		if (result == 0) {
			result = adreno_perfcounter_get(adreno_dev,
				get->groupid, get->countable, &get->offset,
				&get->offset_hi, PERFCOUNTER_FLAG_NONE);
			kgsl_active_count_put(device);
		}
		kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);
		break;
	}
	case IOCTL_KGSL_PERFCOUNTER_PUT: {
		struct kgsl_perfcounter_put *put = data;
		kgsl_mutex_lock(&device->mutex, &device->mutex_owner);
		result = adreno_perfcounter_put(adreno_dev, put->groupid,
			put->countable, PERFCOUNTER_FLAG_NONE);
		kgsl_mutex_unlock(&device->mutex, &device->mutex_owner);
		break;
	}
	case IOCTL_KGSL_PERFCOUNTER_QUERY: {
		struct kgsl_perfcounter_query *query = data;
		result = adreno_perfcounter_query_group(adreno_dev,
			query->groupid, query->countables,
			query->count, &query->max_counters);
		break;
	}
	case IOCTL_KGSL_PERFCOUNTER_READ: {
		struct kgsl_perfcounter_read *read = data;
		result = adreno_perfcounter_read_group(adreno_dev,
			read->reads, read->count);
		break;
	}
	default:
		KGSL_DRV_INFO(dev_priv->device,
			"invalid ioctl code %08x\n", cmd);
		result = -ENOIOCTLCMD;
		break;
	}
	return result;

}

static inline s64 adreno_ticks_to_us(u32 ticks, u32 freq)
{
	freq /= 1000000;
	return ticks / freq;
}

static void adreno_power_stats(struct kgsl_device *device,
				struct kgsl_power_stats *stats)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;
	struct adreno_busy_data busy_data;

	memset(stats, 0, sizeof(*stats));

	/*
	 * If we're not currently active, there shouldn't have been
	 * any cycles since the last time this function was called.
	 */

	if (device->state != KGSL_STATE_ACTIVE)
		return;

	/* Get the busy cycles counted since the counter was last reset */
	adreno_dev->gpudev->busy_cycles(adreno_dev, &busy_data);

	stats->busy_time = adreno_ticks_to_us(busy_data.gpu_busy,
					      kgsl_pwrctrl_active_freq(pwr));
	stats->ram_time = busy_data.vbif_ram_cycles;
	stats->ram_wait = busy_data.vbif_starved_ram;
}

void adreno_irqctrl(struct kgsl_device *device, int state)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	adreno_dev->gpudev->irq_control(adreno_dev, state);
}

static unsigned int adreno_gpuid(struct kgsl_device *device,
	unsigned int *chipid)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	/* Some applications need to know the chip ID too, so pass
	 * that as a parameter */

	if (chipid != NULL)
		*chipid = adreno_dev->chip_id;

	/* Standard KGSL gpuid format:
	 * top word is 0x0002 for 2D or 0x0003 for 3D
	 * Bottom word is core specific identifer
	 */

	return (0x0003 << 16) | ((int) adreno_dev->gpurev);
}

static const struct kgsl_functable adreno_functable = {
	/* Mandatory functions */
	.regread = adreno_regread,
	.regwrite = adreno_regwrite,
	.idle = adreno_idle,
	.isidle = adreno_isidle,
	.suspend_context = adreno_suspend_context,
	.init = adreno_init,
	.start = adreno_start,
	.stop = adreno_stop,
	.getproperty = adreno_getproperty,
	.waittimestamp = adreno_waittimestamp,
	.readtimestamp = adreno_readtimestamp,
	.issueibcmds = adreno_ringbuffer_issueibcmds,
	.ioctl = adreno_ioctl,
	.setup_pt = adreno_setup_pt,
	.cleanup_pt = adreno_cleanup_pt,
	.power_stats = adreno_power_stats,
	.irqctrl = adreno_irqctrl,
	.gpuid = adreno_gpuid,
	.snapshot = adreno_snapshot,
	.irq_handler = adreno_irq_handler,
	.drain = adreno_drain,
	/* Optional functions */
	.setstate = adreno_iommu_setstate,
	.drawctxt_create = adreno_drawctxt_create,
	.drawctxt_detach = adreno_drawctxt_detach,
	.drawctxt_destroy = adreno_drawctxt_destroy,
	.drawctxt_dump = adreno_drawctxt_dump,
	.setproperty = adreno_setproperty,
	.drawctxt_sched = adreno_drawctxt_sched,
	.resume = adreno_dispatcher_start,
};

static struct platform_driver adreno_platform_driver = {
	.probe = adreno_probe,
	.remove = __devexit_p(adreno_remove),
	.suspend = kgsl_suspend_driver,
	.resume = kgsl_resume_driver,
	.id_table = adreno_id_table,
	.driver = {
		.owner = THIS_MODULE,
		.name = DEVICE_3D_NAME,
		.pm = &kgsl_pm_ops,
		.of_match_table = adreno_match_table,
	}
};

static int __init kgsl_3d_init(void)
{
	return platform_driver_register(&adreno_platform_driver);
}

static void __exit kgsl_3d_exit(void)
{
	platform_driver_unregister(&adreno_platform_driver);
}

module_init(kgsl_3d_init);
module_exit(kgsl_3d_exit);

MODULE_DESCRIPTION("3D Graphics driver");
MODULE_VERSION("1.2");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:kgsl_3d");
