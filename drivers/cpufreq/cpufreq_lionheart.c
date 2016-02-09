/*
 * drivers/cpufreq/cpufreq_lionheart.c
 *
 * Patched & tweaked: knzo
 *
 * Based on the Conservative governor by:
 *
 *    Copyright (C)  2001 Russell King
 *              (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                        Jun Nakajima <jun.nakajima@intel.com>
 *              (C)  2009 Alexander Clouter <alex@digriz.org.uk>
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

#include <asm/cputime.h>
#include <linux/kthread.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/cpumask.h>
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
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/earlysuspend.h>

#define DEF_FREQUENCY_UP_THRESHOLD		(65)
#define DEF_FREQUENCY_DOWN_THRESHOLD		(30)
#define MIN_SAMPLING_RATE_RATIO			(2)

static unsigned int min_sampling_rate;

#define LATENCY_MULTIPLIER			(1000)
#define MIN_LATENCY_MULTIPLIER			(100)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(10)
#define TRANSITION_LATENCY_LIMIT		(10 * 1000 * 1000)

static void do_dbs_timer(struct work_struct *work);

struct cpu_dbs_info_s {
	cputime64_t prev_cpu_idle;
	cputime64_t prev_cpu_wall;
	cputime64_t prev_cpu_nice;
	struct cpufreq_policy *cur_policy;
	struct delayed_work work;
	unsigned int down_skip;
	unsigned int requested_freq;
	int cpu;
	unsigned int enable:1;

	struct mutex timer_mutex;
};
static DEFINE_PER_CPU(struct cpu_dbs_info_s, cs_cpu_dbs_info);

static unsigned int dbs_enable;	

static DEFINE_MUTEX(dbs_mutex);

static struct dbs_tuners {
	unsigned int sampling_rate;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int down_threshold;
	unsigned int ignore_nice;
	unsigned int freq_step;
} dbs_tuners_ins = {
	.up_threshold = DEF_FREQUENCY_UP_THRESHOLD,
	.down_threshold = DEF_FREQUENCY_DOWN_THRESHOLD,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,
	.ignore_nice = 0,
	.freq_step = 5,
};

static inline cputime64_t get_cpu_idle_time_jiffy(unsigned int cpu,
							cputime64_t *wall)
{
	cputime64_t idle_time;
	cputime64_t cur_wall_time;
	cputime64_t busy_time;

	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());

	busy_time  = kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

	idle_time = (cur_wall_time - busy_time);
	if (wall)
		*wall = (cputime64_t)jiffies_to_usecs(cur_wall_time);

	return (cputime64_t)jiffies_to_usecs(idle_time);
}

/* function has been moved to cpufreq.c
static inline cputime64_t get_cpu_idle_time(unsigned int cpu, cputime64_t *wall)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, wall);

	if (idle_time == -1ULL)
		return get_cpu_idle_time_jiffy(cpu, wall);

	return idle_time;
}
* */

static int
dbs_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
		     void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpu_dbs_info_s *this_dbs_info = &per_cpu(cs_cpu_dbs_info,
							freq->cpu);

	struct cpufreq_policy *policy;

	if (!this_dbs_info->enable)
		return 0;

	policy = this_dbs_info->cur_policy;

	if (this_dbs_info->requested_freq > policy->max
			|| this_dbs_info->requested_freq < policy->min)
		this_dbs_info->requested_freq = freq->new;

	return 0;
}

static struct notifier_block dbs_cpufreq_notifier_block = {
	.notifier_call = dbs_cpufreq_notifier
};

static ssize_t show_sampling_rate_min(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", min_sampling_rate);
}

define_one_global_ro(sampling_rate_min);

#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%u\n", dbs_tuners_ins.object);		\
}

show_one(sampling_rate, sampling_rate);
show_one(sampling_down_factor, sampling_down_factor);
show_one(up_threshold, up_threshold);
show_one(down_threshold, down_threshold);
show_one(ignore_nice_load, ignore_nice);
show_one(freq_step, freq_step);

static ssize_t store_sampling_down_factor(struct kobject *a,
					  struct attribute *b,
					  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;

	dbs_tuners_ins.sampling_down_factor = input;
	return count;
}

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

static ssize_t store_up_threshold(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > 100 ||
			input <= dbs_tuners_ins.down_threshold)
		return -EINVAL;

	dbs_tuners_ins.up_threshold = input;
	return count;
}

static ssize_t store_down_threshold(struct kobject *a, struct attribute *b,
				    const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input < 11 || input > 100 ||
			input >= dbs_tuners_ins.up_threshold)
		return -EINVAL;

	dbs_tuners_ins.down_threshold = input;
	return count;
}

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

	if (input == dbs_tuners_ins.ignore_nice)
		return count;

	dbs_tuners_ins.ignore_nice = input;

	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(cs_cpu_dbs_info, j);
		dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&dbs_info->prev_cpu_wall, 0);
		if (dbs_tuners_ins.ignore_nice)
			dbs_info->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];
	}
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

	if (input > 100)
		input = 100;

	dbs_tuners_ins.freq_step = input;
	return count;
}

define_one_global_rw(sampling_rate);
define_one_global_rw(sampling_down_factor);
define_one_global_rw(up_threshold);
define_one_global_rw(down_threshold);
define_one_global_rw(ignore_nice_load);
define_one_global_rw(freq_step);

static struct attribute *dbs_attributes[] = {
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&sampling_down_factor.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&ignore_nice_load.attr,
	&freq_step.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "lionheart",
};

static void dbs_check_cpu(struct cpu_dbs_info_s *this_dbs_info)
{
	unsigned int load = 0;
	unsigned int max_load = 0;
	unsigned int freq_target;

	struct cpufreq_policy *policy;
	unsigned int j;

	policy = this_dbs_info->cur_policy;

	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info_s *j_dbs_info;
		cputime64_t cur_wall_time, cur_idle_time;
		unsigned int idle_time, wall_time;

		j_dbs_info = &per_cpu(cs_cpu_dbs_info, j);

		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time, 0);

		wall_time = (unsigned int) (cur_wall_time - j_dbs_info->prev_cpu_wall);
		j_dbs_info->prev_cpu_wall = cur_wall_time;

		idle_time = (unsigned int) (cur_idle_time - j_dbs_info->prev_cpu_idle);
		j_dbs_info->prev_cpu_idle = cur_idle_time;

		if (dbs_tuners_ins.ignore_nice) {
			cputime64_t cur_nice;
			unsigned long cur_nice_jiffies;

			cur_nice = (kcpustat_cpu(j).cpustat[CPUTIME_NICE] - j_dbs_info->prev_cpu_nice);

			cur_nice_jiffies = (unsigned long)
					cputime64_to_jiffies64(cur_nice);

			j_dbs_info->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];
			idle_time += jiffies_to_usecs(cur_nice_jiffies);
		}

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		load = 100 * (wall_time - idle_time) / wall_time;

		if (load > max_load)
			max_load = load;
	}

	if (dbs_tuners_ins.freq_step == 0)
		return;

	if (max_load > dbs_tuners_ins.up_threshold) {
		this_dbs_info->down_skip = 0;

		if (this_dbs_info->requested_freq == policy->max)
			return;

		freq_target = (dbs_tuners_ins.freq_step * policy->max) / 100;

		if (unlikely(freq_target == 0))
			freq_target = 5;

		this_dbs_info->requested_freq += freq_target;
		if (this_dbs_info->requested_freq > policy->max)
			this_dbs_info->requested_freq = policy->max;

		__cpufreq_driver_target(policy, this_dbs_info->requested_freq,
			CPUFREQ_RELATION_H);
		return;
	}

	if (max_load < (dbs_tuners_ins.down_threshold - 10)) {
		freq_target = (dbs_tuners_ins.freq_step * policy->max) / 100;

		this_dbs_info->requested_freq -= freq_target;
		if (this_dbs_info->requested_freq < policy->min)
			this_dbs_info->requested_freq = policy->min;

		if (policy->cur == policy->min)
			return;

		__cpufreq_driver_target(policy, this_dbs_info->requested_freq,
				CPUFREQ_RELATION_H);
		return;
	}
}

static void do_dbs_timer(struct work_struct *work)
{
	struct cpu_dbs_info_s *dbs_info =
		container_of(work, struct cpu_dbs_info_s, work.work);
	unsigned int cpu = dbs_info->cpu;

	int delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);

	// delay -= jiffies % delay;

	mutex_lock(&dbs_info->timer_mutex);

	dbs_check_cpu(dbs_info);

	schedule_delayed_work_on(cpu, &dbs_info->work, delay);
	mutex_unlock(&dbs_info->timer_mutex);
}

static inline void dbs_timer_init(struct cpu_dbs_info_s *dbs_info)
{
	int delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);

	// delay -= jiffies % delay;

	dbs_info->enable = 1;
	INIT_DELAYED_WORK_DEFERRABLE(&dbs_info->work, do_dbs_timer);
	schedule_delayed_work_on(dbs_info->cpu, &dbs_info->work, delay);
}

static inline void dbs_timer_exit(struct cpu_dbs_info_s *dbs_info)
{
	dbs_info->enable = 0;
	cancel_delayed_work_sync(&dbs_info->work);
}

static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				   unsigned int event)
{
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info_s *this_dbs_info;
	unsigned int j;
	int rc;

	this_dbs_info = &per_cpu(cs_cpu_dbs_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!policy->cur))
			return -EINVAL;

		mutex_lock(&dbs_mutex);

		for_each_cpu(j, policy->cpus) {
			struct cpu_dbs_info_s *j_dbs_info;
			j_dbs_info = &per_cpu(cs_cpu_dbs_info, j);
			j_dbs_info->cur_policy = policy;

			j_dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&j_dbs_info->prev_cpu_wall, 0);
			if (dbs_tuners_ins.ignore_nice) {
				j_dbs_info->prev_cpu_nice =
						kcpustat_cpu(j).cpustat[CPUTIME_NICE];
			}
		}
		this_dbs_info->down_skip = 0;
		this_dbs_info->requested_freq = policy->cur;

		mutex_init(&this_dbs_info->timer_mutex);
		dbs_enable++;

		if (dbs_enable == 1) {
			unsigned int latency;

			latency = policy->cpuinfo.transition_latency / 1000;
			if (latency == 0)
				latency = 1;

			rc = sysfs_create_group(cpufreq_global_kobject,
						&dbs_attr_group);
			if (rc) {
				mutex_unlock(&dbs_mutex);
				return rc;
			}

			min_sampling_rate = 10000;
			dbs_tuners_ins.sampling_rate = 10000;

			cpufreq_register_notifier(
					&dbs_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
		}
		mutex_unlock(&dbs_mutex);

		dbs_timer_init(this_dbs_info);

		break;

	case CPUFREQ_GOV_STOP:
		dbs_timer_exit(this_dbs_info);

		mutex_lock(&dbs_mutex);
		dbs_enable--;
		mutex_destroy(&this_dbs_info->timer_mutex);

		if (dbs_enable == 0)
			cpufreq_unregister_notifier(
					&dbs_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);

		mutex_unlock(&dbs_mutex);
		if (!dbs_enable)
			sysfs_remove_group(cpufreq_global_kobject,
					   &dbs_attr_group);

		break;

	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&this_dbs_info->timer_mutex);
		if (policy->max < this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(
					this_dbs_info->cur_policy,
					policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(
					this_dbs_info->cur_policy,
					policy->min, CPUFREQ_RELATION_L);
		mutex_unlock(&this_dbs_info->timer_mutex);

		break;
	}
	return 0;
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_LIONHEART
static
#endif
struct cpufreq_governor cpufreq_gov_lionheart = {
	.name			= "lionheart",
	.governor		= cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};

static int __init cpufreq_gov_dbs_init(void)
{
	return cpufreq_register_governor(&cpufreq_gov_lionheart);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_lionheart);
}

MODULE_AUTHOR("knzo");
MODULE_DESCRIPTION("'cpufreq_lionheart' - A brave and agile conservative-based governor.");
MODULE_LICENSE("GPL");

fs_initcall(cpufreq_gov_dbs_init);
module_exit(cpufreq_gov_dbs_exit);



