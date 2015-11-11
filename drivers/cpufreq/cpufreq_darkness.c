/*
 *  drivers/cpufreq/cpufreq_darkness.c
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

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/ktime.h>
#include <linux/sched.h>

/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

static void do_darkness_timer(struct work_struct *work);
static int cpufreq_governor_darkness(struct cpufreq_policy *policy,
				unsigned int event);

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_DARKNESS
static
#endif
struct cpufreq_governor cpufreq_gov_darkness = {
	.name                   = "darkness",
	.governor               = cpufreq_governor_darkness,
	.owner                  = THIS_MODULE,
};

struct cpufreq_darkness_cpuinfo {
	u64 prev_cpu_wall;
	u64 prev_cpu_idle;
	struct cpufreq_frequency_table *freq_table;
	struct delayed_work work;
	struct cpufreq_policy *cur_policy;
	int cpu;
	unsigned int enable:1;
	/*
	 * percpu mutex that serializes governor limit change with
	 * do_dbs_timer invocation. We do not want do_dbs_timer to run
	 * when user is changing the governor or limits.
	 */
	struct mutex timer_mutex;
};
/*
 * mutex that serializes governor limit change with
 * do_darkness_timer invocation. We do not want do_darkness_timer to run
 * when user is changing the governor or limits.
 */
static DEFINE_PER_CPU(struct cpufreq_darkness_cpuinfo, od_darkness_cpuinfo);

static unsigned int darkness_enable;	/* number of CPUs using this policy */
/*
 * darkness_mutex protects darkness_enable in governor start/stop.
 */
static DEFINE_MUTEX(darkness_mutex);

/*static atomic_t min_freq_limit[NR_CPUS];
static atomic_t max_freq_limit[NR_CPUS];*/

/* darkness tuners */
static struct darkness_tuners {
	atomic_t sampling_rate;
} darkness_tuners_ins = {
	.sampling_rate = ATOMIC_INIT(60000),
};


/************************** sysfs interface ************************/

/* cpufreq_darkness Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%d\n", atomic_read(&darkness_tuners_ins.object));		\
}
show_one(sampling_rate, sampling_rate);

static ssize_t show_cpucore_table(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	ssize_t count = 0;
	int i;

	for (i = CONFIG_NR_CPUS; i > 0; i--) {
		count += sprintf(&buf[count], "%d ", i);
	}
	count += sprintf(&buf[count], "\n");

	return count;
}

/**
 * update_sampling_rate - update sampling rate effective immediately if needed.
 * @new_rate: new sampling rate
 *
 * If new rate is smaller than the old, simply updaing
 * darkness_tuners_ins.sampling_rate might not be appropriate. For example,
 * if the original sampling_rate was 1 second and the requested new sampling
 * rate is 10 ms because the user needs immediate reaction from ondemand
 * governor, but not sure if higher frequency will be required or not,
 * then, the governor may change the sampling rate too late; up to 1 second
 * later. Thus, if we are reducing the sampling rate, we need to make the
 * new value effective immediately.
 */
static void update_sampling_rate(unsigned int new_rate)
{
	int cpu;

	atomic_set(&darkness_tuners_ins.sampling_rate,new_rate);

	for_each_online_cpu(cpu) {
		struct cpufreq_policy *policy;
		struct cpufreq_darkness_cpuinfo *darkness_cpuinfo;
		unsigned long next_sampling, appointed_at;

		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		darkness_cpuinfo = &per_cpu(od_darkness_cpuinfo, policy->cpu);
		cpufreq_cpu_put(policy);

		mutex_lock(&darkness_cpuinfo->timer_mutex);

		if (!delayed_work_pending(&darkness_cpuinfo->work)) {
			mutex_unlock(&darkness_cpuinfo->timer_mutex);
			continue;
		}

		next_sampling  = jiffies + usecs_to_jiffies(new_rate);
		appointed_at = darkness_cpuinfo->work.timer.expires;


		if (time_before(next_sampling, appointed_at)) {

			mutex_unlock(&darkness_cpuinfo->timer_mutex);
			cancel_delayed_work_sync(&darkness_cpuinfo->work);
			mutex_lock(&darkness_cpuinfo->timer_mutex);

			#ifdef CONFIG_CPU_EXYNOS4210
				mod_delayed_work_on(darkness_cpuinfo->cpu, system_wq, &darkness_cpuinfo->work, usecs_to_jiffies(new_rate));
			#else
				queue_delayed_work_on(darkness_cpuinfo->cpu, system_wq, &darkness_cpuinfo->work, usecs_to_jiffies(new_rate));
			#endif
		}
		mutex_unlock(&darkness_cpuinfo->timer_mutex);
	}
}

/* sampling_rate */
static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1)
		return -EINVAL;

	input = max(input,10000);

	if (input == atomic_read(&darkness_tuners_ins.sampling_rate))
		return count;

	update_sampling_rate(input);

	return count;
}

define_one_global_rw(sampling_rate);
define_one_global_ro(cpucore_table);

static struct attribute *darkness_attributes[] = {
	&sampling_rate.attr,
	&cpucore_table.attr,
	NULL
};

static struct attribute_group darkness_attr_group = {
	.attrs = darkness_attributes,
	.name = "darkness",
};

/************************** sysfs end ************************/

static void darkness_check_cpu(struct cpufreq_darkness_cpuinfo *this_darkness_cpuinfo)
{
	struct cpufreq_policy *cpu_policy;
	unsigned int min_freq;
	unsigned int max_freq;
	u64 cur_wall_time, cur_idle_time;
	unsigned int wall_time, idle_time;
	unsigned int index = 0;
	unsigned int next_freq = 0;
	int cur_load = -1;
	unsigned int cpu;

	cpu = this_darkness_cpuinfo->cpu;
	cpu_policy = this_darkness_cpuinfo->cur_policy;

	cur_idle_time = get_cpu_idle_time_us(cpu, NULL);
	cur_idle_time += get_cpu_iowait_time_us(cpu, &cur_wall_time);

	wall_time = (unsigned int)
			(cur_wall_time - this_darkness_cpuinfo->prev_cpu_wall);
	this_darkness_cpuinfo->prev_cpu_wall = cur_wall_time;

	idle_time = (unsigned int)
			(cur_idle_time - this_darkness_cpuinfo->prev_cpu_idle);
	this_darkness_cpuinfo->prev_cpu_idle = cur_idle_time;

	/*min_freq = atomic_read(&min_freq_limit[cpu]);
	max_freq = atomic_read(&max_freq_limit[cpu]);*/

	if (!cpu_policy)
		return;

	/*printk(KERN_ERR "TIMER CPU[%u], wall[%u], idle[%u]\n",cpu, wall_time, idle_time);*/
	if (wall_time >= idle_time) { /*if wall_time < idle_time, evaluate cpu load next time*/
		cur_load = wall_time > idle_time ? (100 * (wall_time - idle_time)) / wall_time : 1;/*if wall_time is equal to idle_time cpu_load is equal to 1*/
		/* Checking Frequency Limit */
		/*if (max_freq > cpu_policy->max)
			max_freq = cpu_policy->max;
		if (min_freq < cpu_policy->min)
			min_freq = cpu_policy->min;*/
		min_freq = cpu_policy->min;
		max_freq = cpu_policy->max;
		/* CPUs Online Scale Frequency*/
		next_freq = max(min(cur_load * (max_freq / 100), max_freq), min_freq);
		cpufreq_frequency_table_target(cpu_policy, this_darkness_cpuinfo->freq_table, next_freq,
			CPUFREQ_RELATION_H, &index);
		if (this_darkness_cpuinfo->freq_table[index].frequency != cpu_policy->cur) {
			cpufreq_frequency_table_target(cpu_policy, this_darkness_cpuinfo->freq_table, next_freq,
				CPUFREQ_RELATION_L, &index);
		}
		next_freq = this_darkness_cpuinfo->freq_table[index].frequency;
		if (next_freq != cpu_policy->cur && cpu_online(cpu)) {
			__cpufreq_driver_target(cpu_policy, next_freq, CPUFREQ_RELATION_L);
		}
	}

}

static void do_darkness_timer(struct work_struct *work)
{
	struct cpufreq_darkness_cpuinfo *darkness_cpuinfo;
	int delay;
	unsigned int cpu;

	darkness_cpuinfo =	container_of(work, struct cpufreq_darkness_cpuinfo, work.work);
	cpu = darkness_cpuinfo->cpu;

	mutex_lock(&darkness_cpuinfo->timer_mutex);
	darkness_check_cpu(darkness_cpuinfo);
	/* We want all CPUs to do sampling nearly on
	 * same jiffy
	 */
	delay = usecs_to_jiffies(atomic_read(&darkness_tuners_ins.sampling_rate));
	if (num_online_cpus() > 1) {
		delay -= jiffies % delay;
	}

	queue_delayed_work_on(cpu, system_wq, &darkness_cpuinfo->work, delay);
	mutex_unlock(&darkness_cpuinfo->timer_mutex);
}

static int cpufreq_governor_darkness(struct cpufreq_policy *policy,
				unsigned int event)
{
	unsigned int cpu;
	struct cpufreq_darkness_cpuinfo *this_darkness_cpuinfo;
	int rc, delay;

	cpu = policy->cpu;
	this_darkness_cpuinfo = &per_cpu(od_darkness_cpuinfo, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if (!policy->cur)
			return -EINVAL;

		mutex_lock(&darkness_mutex);

		this_darkness_cpuinfo->cur_policy = policy;

		this_darkness_cpuinfo->prev_cpu_idle = get_cpu_idle_time_us(cpu, NULL);
		this_darkness_cpuinfo->prev_cpu_idle += get_cpu_iowait_time_us(cpu, &this_darkness_cpuinfo->prev_cpu_wall);

		this_darkness_cpuinfo->freq_table = cpufreq_frequency_get_table(cpu);
		this_darkness_cpuinfo->cpu = cpu;

		mutex_init(&this_darkness_cpuinfo->timer_mutex);

		darkness_enable++;
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (darkness_enable == 1) {
			rc = sysfs_create_group(cpufreq_global_kobject,
						&darkness_attr_group);
			if (rc) {
				mutex_unlock(&darkness_mutex);
				return rc;
			}
		}

		/*if (atomic_read(&min_freq_limit[cpu]) == 0)
			atomic_set(&min_freq_limit[cpu], policy->min);

		if (atomic_read(&max_freq_limit[cpu]) == 0)
			atomic_set(&max_freq_limit[cpu], policy->max);*/

		mutex_unlock(&darkness_mutex);

		delay=usecs_to_jiffies(atomic_read(&darkness_tuners_ins.sampling_rate));
		if (num_online_cpus() > 1) {
			delay -= jiffies % delay;
		}

		this_darkness_cpuinfo->enable = 1;
		INIT_DELAYED_WORK_DEFERRABLE(&this_darkness_cpuinfo->work, do_darkness_timer);
		queue_delayed_work_on(this_darkness_cpuinfo->cpu, system_wq, &this_darkness_cpuinfo->work, delay);

		break;

	case CPUFREQ_GOV_STOP:
		this_darkness_cpuinfo->enable = 0;
		cancel_delayed_work_sync(&this_darkness_cpuinfo->work);

		mutex_lock(&darkness_mutex);
		darkness_enable--;
		mutex_destroy(&this_darkness_cpuinfo->timer_mutex);

		if (!darkness_enable)
			sysfs_remove_group(cpufreq_global_kobject,
					   &darkness_attr_group);
		mutex_unlock(&darkness_mutex);

		break;

	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&this_darkness_cpuinfo->timer_mutex);
		if (policy->max < this_darkness_cpuinfo->cur_policy->cur)
			__cpufreq_driver_target(this_darkness_cpuinfo->cur_policy,
				policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > this_darkness_cpuinfo->cur_policy->cur)
			__cpufreq_driver_target(this_darkness_cpuinfo->cur_policy,
				policy->min, CPUFREQ_RELATION_L);
		mutex_unlock(&this_darkness_cpuinfo->timer_mutex);

		break;
	}
	return 0;
}

static int __init cpufreq_gov_darkness_init(void)
{
	return cpufreq_register_governor(&cpufreq_gov_darkness);
}

static void __exit cpufreq_gov_darkness_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_darkness);
}

MODULE_AUTHOR("Alucard24@XDA");
MODULE_DESCRIPTION("'cpufreq_darkness' - A dynamic cpufreq governor");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_DARKNESS
fs_initcall(cpufreq_gov_darkness_init);
#else
module_init(cpufreq_gov_darkness_init);
#endif
module_exit(cpufreq_gov_darkness_exit);
