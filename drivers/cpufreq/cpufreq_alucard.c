/*
 *  drivers/cpufreq/cpufreq_alucard.c
 *
 *  Copyright (C)  2011 Samsung Electronics co. ltd
 *    ByungChang Cha <bc.cha@samsung.com>
 *
 *  Based on ondemand governor
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Created by Alucard_24@xda
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/slab.h>
/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

/* Tuning Interface */
#ifdef CONFIG_MACH_LGE
#define FREQ_RESPONSIVENESS		2265600
#else
#define FREQ_RESPONSIVENESS		1134000
#endif

#define CPUS_DOWN_RATE			2
#define CPUS_UP_RATE			2

#define DEC_CPU_LOAD			70
#define DEC_CPU_LOAD_AT_MIN_FREQ	70

#define INC_CPU_LOAD			70
#define INC_CPU_LOAD_AT_MIN_FREQ	70

/* Pump Inc/Dec for all cores */
#define PUMP_INC_STEP_AT_MIN_FREQ	4
#define PUMP_INC_STEP			1
#define PUMP_DEC_STEP			2

/* sample rate */
#define MIN_SAMPLING_RATE		10000
#define SAMPLING_RATE			50000

static void do_alucard_timer(struct work_struct *work);

struct cpufreq_alucard_cpuinfo {
	u64 prev_cpu_wall;
	u64 prev_cpu_idle;
	struct cpufreq_frequency_table *freq_table;
	struct delayed_work work;
	struct cpufreq_policy *cur_policy;
	int cpu;
	int min_index;
	int max_index;
	int pump_inc_step;
	int pump_inc_step_at_min_freq;
	int pump_dec_step;
	unsigned int index;
	bool governor_enabled;
	unsigned int up_rate;
	unsigned int down_rate;
	/*
	 * mutex that serializes governor limit change with
	 * do_alucard_timer invocation. We do not want do_alucard_timer to run
	 * when user is changing the governor or limits.
	 */
	struct mutex timer_mutex;
};

static DEFINE_PER_CPU(struct cpufreq_alucard_cpuinfo, od_alucard_cpuinfo);

static unsigned int alucard_enable;	/* number of CPUs using this policy */
/*
 * alucard_mutex protects alucard_enable in governor start/stop.
 */
static DEFINE_MUTEX(alucard_mutex);

/* alucard tuners */
static struct alucard_tuners {
	unsigned int sampling_rate;
	int inc_cpu_load_at_min_freq;
	int inc_cpu_load;
	int dec_cpu_load_at_min_freq;
	int dec_cpu_load;
	int freq_responsiveness;
	unsigned int io_is_busy;
	unsigned int cpus_up_rate;
	unsigned int cpus_down_rate;
} alucard_tuners_ins = {
	.sampling_rate = SAMPLING_RATE,
	.inc_cpu_load_at_min_freq = INC_CPU_LOAD_AT_MIN_FREQ,
	.inc_cpu_load = INC_CPU_LOAD,
	.dec_cpu_load_at_min_freq = DEC_CPU_LOAD_AT_MIN_FREQ,
	.dec_cpu_load = DEC_CPU_LOAD,
	.freq_responsiveness = FREQ_RESPONSIVENESS,
	.io_is_busy = 0,
	.cpus_up_rate = CPUS_UP_RATE,
	.cpus_down_rate = CPUS_DOWN_RATE,
};

/************************** sysfs interface ************************/

/* cpufreq_alucard Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%d\n", alucard_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(inc_cpu_load_at_min_freq, inc_cpu_load_at_min_freq);
show_one(inc_cpu_load, inc_cpu_load);
show_one(dec_cpu_load_at_min_freq, dec_cpu_load_at_min_freq);
show_one(dec_cpu_load, dec_cpu_load);
show_one(freq_responsiveness, freq_responsiveness);
show_one(io_is_busy, io_is_busy);
show_one(cpus_up_rate, cpus_up_rate);
show_one(cpus_down_rate, cpus_down_rate);

#define show_pcpu_param(file_name, num_core)		\
static ssize_t show_##file_name##_##num_core		\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	struct cpufreq_alucard_cpuinfo *this_alucard_cpuinfo = &per_cpu(od_alucard_cpuinfo, num_core - 1); \
	return sprintf(buf, "%d\n", \
			this_alucard_cpuinfo->file_name);		\
}

show_pcpu_param(pump_inc_step_at_min_freq, 1);
show_pcpu_param(pump_inc_step_at_min_freq, 2);
show_pcpu_param(pump_inc_step_at_min_freq, 3);
show_pcpu_param(pump_inc_step_at_min_freq, 4);
show_pcpu_param(pump_inc_step, 1);
show_pcpu_param(pump_inc_step, 2);
show_pcpu_param(pump_inc_step, 3);
show_pcpu_param(pump_inc_step, 4);
show_pcpu_param(pump_dec_step, 1);
show_pcpu_param(pump_dec_step, 2);
show_pcpu_param(pump_dec_step, 3);
show_pcpu_param(pump_dec_step, 4);

#define store_pcpu_param(file_name, num_core)		\
static ssize_t store_##file_name##_##num_core		\
(struct kobject *kobj, struct attribute *attr,				\
	const char *buf, size_t count)					\
{									\
	int input;						\
	struct cpufreq_alucard_cpuinfo *this_alucard_cpuinfo; \
	int ret;							\
														\
	ret = sscanf(buf, "%d", &input);					\
	if (ret != 1)											\
		return -EINVAL;										\
														\
	this_alucard_cpuinfo = &per_cpu(od_alucard_cpuinfo, num_core - 1); \
														\
	if (input == this_alucard_cpuinfo->file_name) {		\
		return count;						\
	}								\
										\
	this_alucard_cpuinfo->file_name = input;			\
	return count;							\
}


#define store_pcpu_pump_param(file_name, num_core)		\
static ssize_t store_##file_name##_##num_core		\
(struct kobject *kobj, struct attribute *attr,				\
	const char *buf, size_t count)					\
{									\
	int input;						\
	struct cpufreq_alucard_cpuinfo *this_alucard_cpuinfo; \
	int ret;							\
														\
	ret = sscanf(buf, "%d", &input);					\
	if (ret != 1)											\
		return -EINVAL;										\
														\
	input = min(max(1, input), 6);							\
														\
	this_alucard_cpuinfo = &per_cpu(od_alucard_cpuinfo, num_core - 1); \
														\
	if (input == this_alucard_cpuinfo->file_name) {		\
		return count;						\
	}								\
										\
	this_alucard_cpuinfo->file_name = input;			\
	return count;							\
}

store_pcpu_pump_param(pump_inc_step_at_min_freq, 1);
store_pcpu_pump_param(pump_inc_step_at_min_freq, 2);
store_pcpu_pump_param(pump_inc_step_at_min_freq, 3);
store_pcpu_pump_param(pump_inc_step_at_min_freq, 4);
store_pcpu_pump_param(pump_inc_step, 1);
store_pcpu_pump_param(pump_inc_step, 2);
store_pcpu_pump_param(pump_inc_step, 3);
store_pcpu_pump_param(pump_inc_step, 4);
store_pcpu_pump_param(pump_dec_step, 1);
store_pcpu_pump_param(pump_dec_step, 2);
store_pcpu_pump_param(pump_dec_step, 3);
store_pcpu_pump_param(pump_dec_step, 4);

define_one_global_rw(pump_inc_step_at_min_freq_1);
define_one_global_rw(pump_inc_step_at_min_freq_2);
define_one_global_rw(pump_inc_step_at_min_freq_3);
define_one_global_rw(pump_inc_step_at_min_freq_4);
define_one_global_rw(pump_inc_step_1);
define_one_global_rw(pump_inc_step_2);
define_one_global_rw(pump_inc_step_3);
define_one_global_rw(pump_inc_step_4);
define_one_global_rw(pump_dec_step_1);
define_one_global_rw(pump_dec_step_2);
define_one_global_rw(pump_dec_step_3);
define_one_global_rw(pump_dec_step_4);

/* sampling_rate */
static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	input = max(input, 10000);

	if (input == alucard_tuners_ins.sampling_rate)
		return count;

	alucard_tuners_ins.sampling_rate = input;

	return count;
}

/* inc_cpu_load_at_min_freq */
static ssize_t store_inc_cpu_load_at_min_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1) {
		return -EINVAL;
	}

	input = min(input, alucard_tuners_ins.inc_cpu_load);

	if (input == alucard_tuners_ins.inc_cpu_load_at_min_freq)
		return count;

	alucard_tuners_ins.inc_cpu_load_at_min_freq = input;

	return count;
}

/* inc_cpu_load */
static ssize_t store_inc_cpu_load(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1)
		return -EINVAL;

	input = max(min(input, 100),0);

	if (input == alucard_tuners_ins.inc_cpu_load)
		return count;

	alucard_tuners_ins.inc_cpu_load = input;

	return count;
}

/* dec_cpu_load_at_min_freq */
static ssize_t store_dec_cpu_load_at_min_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1) {
		return -EINVAL;
	}

	input = min(input, alucard_tuners_ins.dec_cpu_load);

	if (input == alucard_tuners_ins.dec_cpu_load_at_min_freq)
		return count;

	alucard_tuners_ins.dec_cpu_load_at_min_freq = input;

	return count;
}

/* dec_cpu_load */
static ssize_t store_dec_cpu_load(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1)
		return -EINVAL;

	input = max(min(input, 95),5);

	if (input == alucard_tuners_ins.dec_cpu_load)
		return count;

	alucard_tuners_ins.dec_cpu_load = input;

	return count;
}

/* freq_responsiveness */
static ssize_t store_freq_responsiveness(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1)
		return -EINVAL;

	if (input == alucard_tuners_ins.freq_responsiveness)
		return count;

	alucard_tuners_ins.freq_responsiveness = input;

	return count;
}

/* io_is_busy */
static ssize_t store_io_is_busy(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input, j;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == alucard_tuners_ins.io_is_busy)
		return count;

	alucard_tuners_ins.io_is_busy = !!input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct cpufreq_alucard_cpuinfo *j_alucard_cpuinfo;

		j_alucard_cpuinfo = &per_cpu(od_alucard_cpuinfo, j);

		j_alucard_cpuinfo->prev_cpu_idle = get_cpu_idle_time(j,
			&j_alucard_cpuinfo->prev_cpu_wall, alucard_tuners_ins.io_is_busy);
	}
	return count;
}

/* cpus_up_rate */
static ssize_t store_cpus_up_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input == alucard_tuners_ins.cpus_up_rate)
		return count;

	alucard_tuners_ins.cpus_up_rate = input;

	return count;
}

/* cpus_down_rate */
static ssize_t store_cpus_down_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input == alucard_tuners_ins.cpus_down_rate)
		return count;

	alucard_tuners_ins.cpus_down_rate = input;

	return count;
}

define_one_global_rw(sampling_rate);
define_one_global_rw(inc_cpu_load_at_min_freq);
define_one_global_rw(inc_cpu_load);
define_one_global_rw(dec_cpu_load_at_min_freq);
define_one_global_rw(dec_cpu_load);
define_one_global_rw(freq_responsiveness);
define_one_global_rw(io_is_busy);
define_one_global_rw(cpus_up_rate);
define_one_global_rw(cpus_down_rate);

static struct attribute *alucard_attributes[] = {
	&sampling_rate.attr,
	&inc_cpu_load_at_min_freq.attr,
	&inc_cpu_load.attr,
	&dec_cpu_load_at_min_freq.attr,
	&dec_cpu_load.attr,
	&freq_responsiveness.attr,
	&io_is_busy.attr,
	&pump_inc_step_at_min_freq_1.attr,
	&pump_inc_step_at_min_freq_2.attr,
	&pump_inc_step_at_min_freq_3.attr,
	&pump_inc_step_at_min_freq_4.attr,
	&pump_inc_step_1.attr,
	&pump_inc_step_2.attr,
	&pump_inc_step_3.attr,
	&pump_inc_step_4.attr,
	&pump_dec_step_1.attr,
	&pump_dec_step_2.attr,
	&pump_dec_step_3.attr,
	&pump_dec_step_4.attr,
	&cpus_up_rate.attr,
	&cpus_down_rate.attr,
	NULL
};

static struct attribute_group alucard_attr_group = {
	.attrs = alucard_attributes,
	.name = "alucard",
};

/************************** sysfs end ************************/

static void alucard_check_cpu(struct cpufreq_alucard_cpuinfo *this_alucard_cpuinfo)
{
	struct cpufreq_policy *cpu_policy;
	unsigned int freq_responsiveness = alucard_tuners_ins.freq_responsiveness;
	int dec_cpu_load = alucard_tuners_ins.dec_cpu_load;
	int inc_cpu_load = alucard_tuners_ins.inc_cpu_load;
	int pump_inc_step = this_alucard_cpuinfo->pump_inc_step;
	int pump_dec_step = this_alucard_cpuinfo->pump_dec_step;
	u64 cur_wall_time, cur_idle_time;
	unsigned int wall_time, idle_time;
	unsigned int hi_index = 0;
	int cur_load = -1;
	unsigned int cpu;
	int io_busy = alucard_tuners_ins.io_is_busy;
	unsigned int cpus_up_rate = alucard_tuners_ins.cpus_up_rate;
	unsigned int cpus_down_rate = alucard_tuners_ins.cpus_down_rate;

	cpu = this_alucard_cpuinfo->cpu;
	cpu_policy = this_alucard_cpuinfo->cur_policy;
	if (cpu_policy == NULL)
		return;

	cur_idle_time = get_cpu_idle_time(cpu, &cur_wall_time, io_busy);

	wall_time = (unsigned int)
			(cur_wall_time - this_alucard_cpuinfo->prev_cpu_wall);
	this_alucard_cpuinfo->prev_cpu_wall = cur_wall_time;

	idle_time = (unsigned int)
			(cur_idle_time - this_alucard_cpuinfo->prev_cpu_idle);
	this_alucard_cpuinfo->prev_cpu_idle = cur_idle_time;

	/*if wall_time < idle_time or wall_time == 0, evaluate cpu load next time*/
	if (unlikely(!wall_time || wall_time < idle_time))
		return;

	cur_load = 100 * (wall_time - idle_time) / wall_time;

	cpufreq_notify_utilization(cpu_policy, cur_load);

	/* Maximum increasing frequency possible */
	cpufreq_frequency_table_target(cpu_policy, this_alucard_cpuinfo->freq_table, max(cur_load * (cpu_policy->max / 100), cpu_policy->min),
			CPUFREQ_RELATION_L, &hi_index);

	/* CPUs Online Scale Frequency*/
	if (cpu_policy->cur < freq_responsiveness) {
		inc_cpu_load = alucard_tuners_ins.inc_cpu_load_at_min_freq;
		dec_cpu_load = alucard_tuners_ins.dec_cpu_load_at_min_freq;
		pump_inc_step = this_alucard_cpuinfo->pump_inc_step_at_min_freq;
		hi_index = this_alucard_cpuinfo->max_index;
	}
	/* Check for frequency increase or for frequency decrease */
	if (cur_load >= inc_cpu_load && this_alucard_cpuinfo->index < hi_index) {
		if (this_alucard_cpuinfo->up_rate % cpus_up_rate == 0) {
			if ((this_alucard_cpuinfo->index + pump_inc_step) <= hi_index)
				this_alucard_cpuinfo->index += pump_inc_step;
			else
				this_alucard_cpuinfo->index = hi_index;

			this_alucard_cpuinfo->up_rate = 1;
			this_alucard_cpuinfo->down_rate = 1;
		} else {
			if (this_alucard_cpuinfo->up_rate < cpus_up_rate)
				++this_alucard_cpuinfo->up_rate;
			else
				this_alucard_cpuinfo->up_rate = 1;
		}
	} else if (cur_load < dec_cpu_load && this_alucard_cpuinfo->index > this_alucard_cpuinfo->min_index) {
		if (this_alucard_cpuinfo->down_rate % cpus_down_rate == 0) {
			if ((this_alucard_cpuinfo->index - this_alucard_cpuinfo->min_index) >= pump_dec_step)
				this_alucard_cpuinfo->index -= pump_dec_step;
			else
				this_alucard_cpuinfo->index = this_alucard_cpuinfo->min_index;

			this_alucard_cpuinfo->up_rate = 1;
			this_alucard_cpuinfo->down_rate = 1;
		} else {
			if (this_alucard_cpuinfo->down_rate < cpus_down_rate)
				++this_alucard_cpuinfo->down_rate;
			else
				this_alucard_cpuinfo->down_rate = 1;
		}
	}

	if (this_alucard_cpuinfo->freq_table[this_alucard_cpuinfo->index].frequency != cpu_policy->cur) {
		__cpufreq_driver_target(cpu_policy, this_alucard_cpuinfo->freq_table[this_alucard_cpuinfo->index].frequency, CPUFREQ_RELATION_C);
	}
}

static void do_alucard_timer(struct work_struct *work)
{
	struct cpufreq_alucard_cpuinfo *alucard_cpuinfo;
	int delay;
	unsigned int cpu;

	alucard_cpuinfo = container_of(work, struct cpufreq_alucard_cpuinfo, work.work);
	cpu = alucard_cpuinfo->cpu;

	mutex_lock(&alucard_cpuinfo->timer_mutex);

	alucard_check_cpu(alucard_cpuinfo);

	delay = usecs_to_jiffies(alucard_tuners_ins.sampling_rate);

	/* We want all CPUs to do sampling nearly on same jiffy */
	if (num_online_cpus() > 1)
		delay -= jiffies % delay;

	if (delay <= 0)
		delay = usecs_to_jiffies(MIN_SAMPLING_RATE);

	mod_delayed_work_on(cpu, system_wq, &alucard_cpuinfo->work, delay);

	mutex_unlock(&alucard_cpuinfo->timer_mutex);
}

static int cpufreq_governor_alucard(struct cpufreq_policy *policy,
				unsigned int event)
{
	unsigned int cpu;
	struct cpufreq_alucard_cpuinfo *this_alucard_cpuinfo;
	int rc, delay;
	int io_busy;

	cpu = policy->cpu;
	io_busy = alucard_tuners_ins.io_is_busy;
	this_alucard_cpuinfo = &per_cpu(od_alucard_cpuinfo, cpu);
	this_alucard_cpuinfo->freq_table = cpufreq_frequency_get_table(cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) ||
			(!policy->cur) ||
			(cpu != this_alucard_cpuinfo->cpu))
			return -EINVAL;

		mutex_lock(&alucard_mutex);

		this_alucard_cpuinfo->cur_policy = policy;

		this_alucard_cpuinfo->prev_cpu_idle = get_cpu_idle_time(cpu, &this_alucard_cpuinfo->prev_cpu_wall, io_busy);

		cpufreq_frequency_table_target(policy, this_alucard_cpuinfo->freq_table, policy->min,
			CPUFREQ_RELATION_L, &this_alucard_cpuinfo->min_index);

		cpufreq_frequency_table_target(policy, this_alucard_cpuinfo->freq_table, policy->max,
			CPUFREQ_RELATION_H, &this_alucard_cpuinfo->max_index);

		cpufreq_frequency_table_target(policy, this_alucard_cpuinfo->freq_table, policy->cur,
				CPUFREQ_RELATION_C, &this_alucard_cpuinfo->index);

		alucard_enable++;
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (alucard_enable == 1) {
			rc = sysfs_create_group(cpufreq_global_kobject,
						&alucard_attr_group);
			if (rc) {
				alucard_enable--;
				mutex_unlock(&alucard_mutex);
				return rc;
			}
		}
		this_alucard_cpuinfo->up_rate = 1;
		this_alucard_cpuinfo->down_rate = 1;
		this_alucard_cpuinfo->governor_enabled = true;
		mutex_unlock(&alucard_mutex);

		mutex_init(&this_alucard_cpuinfo->timer_mutex);

		delay = usecs_to_jiffies(alucard_tuners_ins.sampling_rate);
		/* We want all CPUs to do sampling nearly on same jiffy */
		if (num_online_cpus() > 1)
			delay -= jiffies % delay;

		if (delay <= 0)
			delay = usecs_to_jiffies(MIN_SAMPLING_RATE);

		INIT_DELAYED_WORK_DEFERRABLE(&this_alucard_cpuinfo->work, do_alucard_timer);

		mod_delayed_work_on(this_alucard_cpuinfo->cpu, system_wq, &this_alucard_cpuinfo->work, delay);

		break;

	case CPUFREQ_GOV_STOP:
		cancel_delayed_work_sync(&this_alucard_cpuinfo->work);

		mutex_lock(&alucard_mutex);
		mutex_destroy(&this_alucard_cpuinfo->timer_mutex);

		this_alucard_cpuinfo->governor_enabled = false;

		this_alucard_cpuinfo->cur_policy = NULL;

		this_alucard_cpuinfo->index = 0;

		alucard_enable--;
		if (!alucard_enable) {
			sysfs_remove_group(cpufreq_global_kobject,
					   &alucard_attr_group);
		}

		mutex_unlock(&alucard_mutex);

		break;

	case CPUFREQ_GOV_LIMITS:
		if (!this_alucard_cpuinfo->cur_policy) {
			pr_debug("Unable to limit cpu freq due to cur_policy == NULL\n");
			return -EPERM;
		}
		mutex_lock(&this_alucard_cpuinfo->timer_mutex);
		cpufreq_frequency_table_target(policy, this_alucard_cpuinfo->freq_table, policy->min,
			CPUFREQ_RELATION_L, &this_alucard_cpuinfo->min_index);

		cpufreq_frequency_table_target(policy, this_alucard_cpuinfo->freq_table, policy->max,
			CPUFREQ_RELATION_H, &this_alucard_cpuinfo->max_index);

		if (policy->max < this_alucard_cpuinfo->cur_policy->cur) {
			__cpufreq_driver_target(this_alucard_cpuinfo->cur_policy,
				policy->max, CPUFREQ_RELATION_H);
			cpufreq_frequency_table_target(policy, this_alucard_cpuinfo->freq_table, policy->max,
				CPUFREQ_RELATION_C, &this_alucard_cpuinfo->index);
		} else if (policy->min > this_alucard_cpuinfo->cur_policy->cur) {
			__cpufreq_driver_target(this_alucard_cpuinfo->cur_policy,
				policy->min, CPUFREQ_RELATION_L);
			cpufreq_frequency_table_target(policy, this_alucard_cpuinfo->freq_table, policy->min,
				CPUFREQ_RELATION_C, &this_alucard_cpuinfo->index);
		}

		mutex_unlock(&this_alucard_cpuinfo->timer_mutex);

		break;
	}
	return 0;
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_ALUCARD
static
#endif
struct cpufreq_governor cpufreq_gov_alucard = {
	.name                   = "alucard",
	.governor               = cpufreq_governor_alucard,
	.owner                  = THIS_MODULE,
};


static int __init cpufreq_gov_alucard_init(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		struct cpufreq_alucard_cpuinfo *this_alucard_cpuinfo = &per_cpu(od_alucard_cpuinfo, cpu);

		this_alucard_cpuinfo->cpu = cpu;
		this_alucard_cpuinfo->pump_inc_step_at_min_freq = PUMP_INC_STEP_AT_MIN_FREQ;
		this_alucard_cpuinfo->pump_inc_step = PUMP_INC_STEP;
		this_alucard_cpuinfo->pump_dec_step = PUMP_DEC_STEP;
	}

	return cpufreq_register_governor(&cpufreq_gov_alucard);
}

static void __exit cpufreq_gov_alucard_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_alucard);
}

MODULE_AUTHOR("Alucard24@XDA");
MODULE_DESCRIPTION("'cpufreq_alucard' - A dynamic cpufreq governor v1.1 (SnapDragon)");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ALUCARD
fs_initcall(cpufreq_gov_alucard_init);
#else
module_init(cpufreq_gov_alucard_init);
#endif
module_exit(cpufreq_gov_alucard_exit);

