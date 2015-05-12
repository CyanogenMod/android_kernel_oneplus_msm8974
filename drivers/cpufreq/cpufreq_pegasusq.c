/*
 *  drivers/cpufreq/cpufreq_pegasusq.c
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/reboot.h>

/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

#define DEF_SAMPLING_DOWN_FACTOR		(2)
#define MAX_SAMPLING_DOWN_FACTOR		(100000)
#define DEF_FREQUENCY_DOWN_DIFFERENTIAL		(5)
#define DEF_FREQUENCY_UP_THRESHOLD		(85)

/* for multiple freq_step */
#define DEF_UP_THRESHOLD_DIFF	(5)

#define DEF_FREQUENCY_MIN_SAMPLE_RATE		(10000)
#define MIN_FREQUENCY_UP_THRESHOLD		(11)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)
#define DEF_SAMPLING_RATE			(50000)
#define MIN_SAMPLING_RATE			(10000)

#define DEF_FREQ_STEP				(37)
/* for multiple freq_step */
#define DEF_FREQ_STEP_DEC			(13)

#define DEF_START_DELAY				(0)

#define UP_THRESHOLD_AT_MIN_FREQ		(40)
#define FREQ_FOR_RESPONSIVENESS			(2265600)
/* for fast decrease */
#define FREQ_FOR_FAST_DOWN			(1574400)
#define UP_THRESHOLD_AT_FAST_DOWN		(80)

static unsigned int min_sampling_rate;

static void do_dbs_timer(struct work_struct *work);
static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				unsigned int event);

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_PEGASUSQ
static
#endif
struct cpufreq_governor cpufreq_gov_pegasusq = {
	.name                   = "pegasusq",
	.governor               = cpufreq_governor_dbs,
	.owner                  = THIS_MODULE,
};

/* Sampling types */
enum {DBS_NORMAL_SAMPLE, DBS_SUB_SAMPLE};

struct cpu_dbs_info_s {
	u64 prev_cpu_idle;
	u64 prev_cpu_iowait;
	u64 prev_cpu_wall;
	unsigned int prev_cpu_wall_delta;
	cputime64_t prev_cpu_nice;
	struct cpufreq_policy *cur_policy;
	struct delayed_work work;
	struct cpufreq_frequency_table *freq_table;
	unsigned int rate_mult;
	int cpu;
	/*
	 * percpu mutex that serializes governor limit change with
	 * do_dbs_timer invocation. We do not want do_dbs_timer to run
	 * when user is changing the governor or limits.
	 */
	struct mutex timer_mutex;
};
static DEFINE_PER_CPU(struct cpu_dbs_info_s, od_cpu_dbs_info);

static unsigned int dbs_enable;	/* number of CPUs using this policy */

/*
 * dbs_mutex protects dbs_enable in governor start/stop.
 */
static DEFINE_MUTEX(dbs_mutex);

static struct dbs_tuners {
	unsigned int sampling_rate;
	unsigned int up_threshold;
	unsigned int down_differential;
	unsigned int ignore_nice;
	unsigned int sampling_down_factor;
	unsigned int io_is_busy;
	/* pegasusq tuners */
	unsigned int freq_step;
	unsigned int max_freq;
	unsigned int min_freq;
	unsigned int up_threshold_at_min_freq;
	unsigned int freq_for_responsiveness;
} dbs_tuners_ins = {
	.up_threshold = DEF_FREQUENCY_UP_THRESHOLD,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,
	.down_differential = DEF_FREQUENCY_DOWN_DIFFERENTIAL,
	.ignore_nice = 0,
	.freq_step = DEF_FREQ_STEP,
	.up_threshold_at_min_freq = UP_THRESHOLD_AT_MIN_FREQ,
	.freq_for_responsiveness = FREQ_FOR_RESPONSIVENESS,
};

static inline u64 get_cpu_idle_time_jiffy(unsigned int cpu, u64 *wall)
{
	u64 idle_time;
	u64 cur_wall_time;
	u64 busy_time;

	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());

	busy_time  = kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

	idle_time = cur_wall_time - busy_time;
	if (wall)
		*wall = jiffies_to_usecs(cur_wall_time);

	return jiffies_to_usecs(idle_time);
}

/* function has been moved to cpufreq.c
static inline cputime64_t get_cpu_idle_time(unsigned int cpu, cputime64_t *wall)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, NULL);

	if (idle_time == -1ULL)
		return get_cpu_idle_time_jiffy(cpu, wall);
	else
		idle_time += get_cpu_iowait_time_us(cpu, wall);

	return idle_time;
}
*/

static inline cputime64_t get_cpu_iowait_time(unsigned int cpu, cputime64_t *wall)
{
	u64 iowait_time = get_cpu_iowait_time_us(cpu, wall);

	if (iowait_time == -1ULL)
		return 0;

	return iowait_time;
}

/************************** sysfs interface ************************/

static ssize_t show_sampling_rate_min(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", min_sampling_rate);
}

define_one_global_ro(sampling_rate_min);

/* cpufreq_pegasusq Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)              \
{									\
	return sprintf(buf, "%u\n", dbs_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(io_is_busy, io_is_busy);
show_one(up_threshold, up_threshold);
show_one(sampling_down_factor, sampling_down_factor);
show_one(ignore_nice_load, ignore_nice);
show_one(down_differential, down_differential);
show_one(freq_step, freq_step);
show_one(up_threshold_at_min_freq, up_threshold_at_min_freq);
show_one(freq_for_responsiveness, freq_for_responsiveness);

static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.sampling_rate = max(input, min_sampling_rate);

	return count;
}

/* io_is_busy */
static ssize_t store_io_is_busy(struct kobject *a, struct attribute *b,
				const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.io_is_busy = !!input;

	return count;
}

static ssize_t store_up_threshold(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold = input;

	return count;
}

static ssize_t store_sampling_down_factor(struct kobject *a,
					  struct attribute *b,
					  const char *buf, size_t count)
{
	unsigned int input, j;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;

	dbs_tuners_ins.sampling_down_factor = input;

	/* Reset down sampling multiplier in case it was active */
	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(od_cpu_dbs_info, j);
		dbs_info->rate_mult = 1;
	}
	return count;
}

/* ignore_nice_load */
static ssize_t store_ignore_nice_load(struct kobject *a, struct attribute *b,
				      const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	unsigned int j;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == dbs_tuners_ins.ignore_nice)  {/* nothing to do */
		return count;
	}
	dbs_tuners_ins.ignore_nice = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(od_cpu_dbs_info, j);
		dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&dbs_info->prev_cpu_wall, dbs_tuners_ins.io_is_busy);
		if (dbs_tuners_ins.ignore_nice)
			dbs_info->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];
	}
	return count;
}

static ssize_t store_down_differential(struct kobject *a, struct attribute *b,
				    const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.down_differential = min(input, 100u);

	return count;
}

static ssize_t store_freq_step(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.freq_step = min(input, 100u);
	return count;
}

static ssize_t store_up_threshold_at_min_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
	    input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold_at_min_freq = input;
	return count;
}

static ssize_t store_freq_for_responsiveness(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.freq_for_responsiveness = input;
	return count;
}

define_one_global_rw(sampling_rate);
define_one_global_rw(io_is_busy);
define_one_global_rw(up_threshold);
define_one_global_rw(sampling_down_factor);
define_one_global_rw(ignore_nice_load);
define_one_global_rw(down_differential);
define_one_global_rw(freq_step);
define_one_global_rw(up_threshold_at_min_freq);
define_one_global_rw(freq_for_responsiveness);

static struct attribute *dbs_attributes[] = {
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&sampling_down_factor.attr,
	&ignore_nice_load.attr,
	&io_is_busy.attr,
	&down_differential.attr,
	&freq_step.attr,
	&up_threshold_at_min_freq.attr,
	&freq_for_responsiveness.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "pegasusq",
};

/************************** sysfs end ************************/

static void dbs_freq_increase(struct cpufreq_policy *p, unsigned int freq)
{
#if !defined(CONFIG_ARCH_EXYNOS4) && !defined(CONFIG_ARCH_EXYNOS5)
	if (p->cur == p->max)
		return;
#endif

	__cpufreq_driver_target(p, freq, CPUFREQ_RELATION_L);
}

static void dbs_check_cpu(struct cpu_dbs_info_s *this_dbs_info)
{
	unsigned int max_load_freq;

	struct cpufreq_policy *policy;
	unsigned int j;
	int up_threshold = dbs_tuners_ins.up_threshold;

	policy = this_dbs_info->cur_policy;

	/* Get Absolute Load - in terms of freq */
	max_load_freq = 0;

	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info_s *j_dbs_info;
		u64 cur_wall_time, cur_idle_time, cur_iowait_time;
		u64 prev_wall_time, prev_idle_time, prev_iowait_time;
		unsigned int idle_time, wall_time, iowait_time;
		unsigned int load, load_freq;
		int freq_avg;

		j_dbs_info = &per_cpu(od_cpu_dbs_info, j);
		prev_wall_time = j_dbs_info->prev_cpu_wall;
		prev_idle_time = j_dbs_info->prev_cpu_idle;
		prev_iowait_time = j_dbs_info->prev_cpu_iowait;

		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time, dbs_tuners_ins.io_is_busy);
		cur_iowait_time = get_cpu_iowait_time(j, &cur_wall_time);

		wall_time = (unsigned int)
				(cur_wall_time - prev_wall_time);
		j_dbs_info->prev_cpu_wall = cur_wall_time;

		idle_time = (unsigned int)
				(cur_idle_time - prev_idle_time);
		j_dbs_info->prev_cpu_idle = cur_idle_time;

		iowait_time = (unsigned int)
				(cur_iowait_time - prev_iowait_time);
		j_dbs_info->prev_cpu_iowait = cur_iowait_time;

		if (dbs_tuners_ins.ignore_nice) {
			u64 cur_nice;
			unsigned long cur_nice_jiffies;

			cur_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE] -
					 j_dbs_info->prev_cpu_nice;
			/*
			 * Assumption: nice time between sampling periods will
			 * be less than 2^32 jiffies for 32 bit sys
			 */
			cur_nice_jiffies = (unsigned long)
					cputime64_to_jiffies64(cur_nice);

			j_dbs_info->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];
			idle_time += jiffies_to_usecs(cur_nice_jiffies);
		}

		if (dbs_tuners_ins.io_is_busy && idle_time >= iowait_time)
			idle_time -= iowait_time;

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		load = 100 * (wall_time - idle_time) / wall_time;

		freq_avg = __cpufreq_driver_getavg(policy, j);
		if (freq_avg <= 0)
			freq_avg = policy->cur;

		load_freq = load * freq_avg;
		if (load_freq > max_load_freq)
			max_load_freq = load_freq;
	}

	/* Check for frequency increase */
	if (policy->cur < dbs_tuners_ins.freq_for_responsiveness)
		up_threshold = dbs_tuners_ins.up_threshold_at_min_freq;
	/* for fast frequency decrease */
	else
		up_threshold = dbs_tuners_ins.up_threshold;

	if (max_load_freq > up_threshold * policy->cur) {
		/* for multiple freq_step */
		int inc = policy->max * (dbs_tuners_ins.freq_step
					- DEF_FREQ_STEP_DEC * 2) / 100;
		int target = 0;

		/* for multiple freq_step */
		if (max_load_freq > (up_threshold + DEF_UP_THRESHOLD_DIFF * 2)
			* policy->cur)
			inc = policy->max * dbs_tuners_ins.freq_step / 100;
		else if (max_load_freq > (up_threshold + DEF_UP_THRESHOLD_DIFF)
			* policy->cur)
			inc = policy->max * (dbs_tuners_ins.freq_step
					- DEF_FREQ_STEP_DEC) / 100;

		target = min(policy->max, policy->cur + inc);

		/* If switching to max speed, apply sampling_down_factor */
		if (policy->cur < policy->max && target == policy->max)
			this_dbs_info->rate_mult =
				dbs_tuners_ins.sampling_down_factor;
		dbs_freq_increase(policy, target);
		return;
	}

	/* Check for frequency decrease */
#if !defined(CONFIG_ARCH_EXYNOS4) && !defined(CONFIG_ARCH_EXYNOS5)
	/* if we cannot reduce the frequency anymore, break out early */
	if (policy->cur == policy->min)
		return;
#endif

	/*
	 * The optimal frequency is the frequency that is the lowest that
	 * can support the current CPU usage without triggering the up
	 * policy. To be safe, we focus DOWN_DIFFERENTIAL points under
	 * the threshold.
	 */
	if (max_load_freq <
	    (dbs_tuners_ins.up_threshold - dbs_tuners_ins.down_differential) *
			policy->cur) {
		unsigned int freq_next;
		unsigned int down_thres;

		freq_next = max_load_freq /
			(dbs_tuners_ins.up_threshold -
				dbs_tuners_ins.down_differential);

		/* No longer fully busy, reset rate_mult */
		this_dbs_info->rate_mult = 1;

		if (freq_next < policy->min)
			freq_next = policy->min;

		down_thres = dbs_tuners_ins.up_threshold_at_min_freq
			- dbs_tuners_ins.down_differential;

		if (freq_next < dbs_tuners_ins.freq_for_responsiveness
			&& (max_load_freq / freq_next) > down_thres)
			freq_next = dbs_tuners_ins.freq_for_responsiveness;

		if (policy->cur == freq_next)
			return;

		__cpufreq_driver_target(policy, freq_next,
					CPUFREQ_RELATION_L);
	}
}

static void do_dbs_timer(struct work_struct *work)
{
	struct cpu_dbs_info_s *dbs_info =
		container_of(work, struct cpu_dbs_info_s, work.work);
	unsigned int cpu = dbs_info->cpu;
	int delay;

	mutex_lock(&dbs_info->timer_mutex);

	dbs_check_cpu(dbs_info);
	/* We want all CPUs to do sampling nearly on
	 * same jiffy
	 */
	delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate
				 * dbs_info->rate_mult);

	if (num_online_cpus() > 1)
		delay -= jiffies % delay;

	schedule_delayed_work_on(cpu, &dbs_info->work, delay);
	mutex_unlock(&dbs_info->timer_mutex);
}

static inline void dbs_timer_init(struct cpu_dbs_info_s *dbs_info)
{
	/* We want all CPUs to do sampling nearly on same jiffy */
	int delay = usecs_to_jiffies(DEF_START_DELAY * 1000 * 1000
				     + dbs_tuners_ins.sampling_rate);

	if (num_online_cpus() > 1)
		delay -= jiffies % delay;

	INIT_DELAYED_WORK_DEFERRABLE(&dbs_info->work, do_dbs_timer);

	schedule_delayed_work_on(dbs_info->cpu, &dbs_info->work, delay);
}

static inline void dbs_timer_exit(struct cpu_dbs_info_s *dbs_info)
{
	cancel_delayed_work_sync(&dbs_info->work);
}

static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				unsigned int event)
{
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info_s *this_dbs_info;
	unsigned int j;
	int rc;

	this_dbs_info = &per_cpu(od_cpu_dbs_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:

		if ((!cpu_online(cpu)) || (!policy->cur))
			return -EINVAL;

		dbs_tuners_ins.max_freq = policy->max;
		dbs_tuners_ins.min_freq = policy->min;

		mutex_lock(&dbs_mutex);

		dbs_enable++;
		for_each_cpu(j, policy->cpus) {
			struct cpu_dbs_info_s *j_dbs_info;
			j_dbs_info = &per_cpu(od_cpu_dbs_info, j);
			j_dbs_info->cur_policy = policy;

			j_dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&j_dbs_info->prev_cpu_wall, dbs_tuners_ins.io_is_busy);
			if (dbs_tuners_ins.ignore_nice) {
				j_dbs_info->prev_cpu_nice =
						kcpustat_cpu(j).cpustat[CPUTIME_NICE];
			}
		}
		this_dbs_info->cpu = cpu;
		this_dbs_info->rate_mult = 1;
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 1) {
			rc = sysfs_create_group(cpufreq_global_kobject,
						&dbs_attr_group);
			if (rc) {
				mutex_unlock(&dbs_mutex);
				return rc;
			}

			min_sampling_rate = MIN_SAMPLING_RATE;
			dbs_tuners_ins.sampling_rate = DEF_SAMPLING_RATE;
			dbs_tuners_ins.io_is_busy = 0;
		}
		mutex_unlock(&dbs_mutex);

		mutex_init(&this_dbs_info->timer_mutex);

		dbs_timer_init(this_dbs_info);

		break;

	case CPUFREQ_GOV_STOP:

		dbs_timer_exit(this_dbs_info);

		mutex_destroy(&this_dbs_info->timer_mutex);

		mutex_lock(&dbs_mutex);

		dbs_enable--;

		if (!dbs_enable) {
			sysfs_remove_group(cpufreq_global_kobject,
					   &dbs_attr_group);
		}
		mutex_unlock(&dbs_mutex);

		break;

	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&this_dbs_info->timer_mutex);
		if (policy->max < this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(this_dbs_info->cur_policy,
				policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(this_dbs_info->cur_policy,
				policy->min, CPUFREQ_RELATION_L);
		dbs_check_cpu(this_dbs_info);
		mutex_unlock(&this_dbs_info->timer_mutex);

		break;
	}
	return 0;
}

static int __init cpufreq_gov_dbs_init(void)
{
	int ret;

	ret = cpufreq_register_governor(&cpufreq_gov_pegasusq);
	if (ret)
		goto err_reg;

	return ret;

err_reg:
	kfree(&dbs_tuners_ins);
	return ret;
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_pegasusq);
	kfree(&dbs_tuners_ins);
}

MODULE_AUTHOR("ByungChang Cha <bc.cha@samsung.com>");
MODULE_DESCRIPTION("'cpufreq_pegasusq' - A dynamic cpufreq governor");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_PEGASUSQ
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);


