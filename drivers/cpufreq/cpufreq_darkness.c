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

#define MIN_SAMPLING_RATE	10000

/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

static void do_darkness_timer(struct work_struct *work);

struct cpufreq_darkness_cpuinfo {
	u64 prev_cpu_wall;
	u64 prev_cpu_idle;
	struct cpufreq_frequency_table *freq_table;
	struct delayed_work work;
	struct cpufreq_policy *cur_policy;
	bool governor_enabled;
	unsigned int cpu;
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

static struct workqueue_struct *darkness_wq;

/* darkness tuners */
static struct darkness_tuners {
	unsigned int sampling_rate;
} darkness_tuners_ins = {
	.sampling_rate = 60000,
};

/************************** sysfs interface ************************/

/* cpufreq_darkness Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%d\n", darkness_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);

/* sampling_rate */
static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	int input;
	int ret = 0;
	int mpd = strcmp(current->comm, "mpdecision");

	if (mpd == 0)
		return ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	input = max(input,10000);

	if (input == darkness_tuners_ins.sampling_rate)
		return count;

	darkness_tuners_ins.sampling_rate = input;

	return count;
}

define_one_global_rw(sampling_rate);

static struct attribute *darkness_attributes[] = {
	&sampling_rate.attr,
	NULL
};

static struct attribute_group darkness_attr_group = {
	.attrs = darkness_attributes,
	.name = "darkness",
};

/************************** sysfs end ************************/

static unsigned int adjust_cpufreq_frequency_target(struct cpufreq_policy *policy,
					struct cpufreq_frequency_table *table,
					unsigned int tmp_freq)
{
	unsigned int i = 0, l_freq = 0, h_freq = 0, target_freq = 0;

	if (tmp_freq < policy->min)
		tmp_freq = policy->min;
	if (tmp_freq > policy->max)
		tmp_freq = policy->max;

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID) {
			continue;
		}
		if (freq < tmp_freq) {
			h_freq = freq;
		}
		if (freq == tmp_freq) {
			target_freq = freq;
			break;
		}
		if (freq > tmp_freq) {
			l_freq = freq;
			break;
		}
	}
	if (!target_freq) {
		if (policy->cur >= h_freq
			 && policy->cur <= l_freq)
			target_freq = policy->cur;
		else
			target_freq = l_freq;
	}

	return target_freq;
}

static void darkness_check_cpu(struct cpufreq_darkness_cpuinfo *this_darkness_cpuinfo)
{
	struct cpufreq_policy *policy;
	unsigned int max_load = 0;
	unsigned int next_freq = 0;
	unsigned int j;

	policy = this_darkness_cpuinfo->cur_policy;
	if (!policy)
		return;

	for_each_cpu(j, policy->cpus) {
		struct cpufreq_darkness_cpuinfo *j_darkness_cpuinfo = &per_cpu(od_darkness_cpuinfo, j);
		u64 cur_wall_time, cur_idle_time;
		unsigned int idle_time, wall_time;
		unsigned int load;
		
		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time, 0);

		wall_time = (unsigned int)
			(cur_wall_time - j_darkness_cpuinfo->prev_cpu_wall);
		j_darkness_cpuinfo->prev_cpu_wall = cur_wall_time;

		idle_time = (unsigned int)
			(cur_idle_time - j_darkness_cpuinfo->prev_cpu_idle);
		j_darkness_cpuinfo->prev_cpu_idle = cur_idle_time;

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		load = 100 * (wall_time - idle_time) / wall_time;

		if (load > max_load)
			max_load = load;
	}

	cpufreq_notify_utilization(policy, max_load);

	/* CPUs Online Scale Frequency*/
	next_freq = adjust_cpufreq_frequency_target(policy, this_darkness_cpuinfo->freq_table, 
												max_load * (policy->max / 100));
	if (next_freq != policy->cur && next_freq > 0)
		__cpufreq_driver_target(policy, next_freq, CPUFREQ_RELATION_L);
}

static void do_darkness_timer(struct work_struct *work)
{
	struct cpufreq_darkness_cpuinfo *this_darkness_cpuinfo = 
		container_of(work, struct cpufreq_darkness_cpuinfo, work.work);
	int delay;

	if (unlikely(!cpu_online(this_darkness_cpuinfo->cpu) ||
				!this_darkness_cpuinfo->cur_policy))
		return;

	mutex_lock(&this_darkness_cpuinfo->timer_mutex);

	darkness_check_cpu(this_darkness_cpuinfo);

	delay = usecs_to_jiffies(darkness_tuners_ins.sampling_rate);
	/* We want all CPUs to do sampling nearly on
	 * same jiffy
	 */
	if (num_online_cpus() > 1) {
		delay -= jiffies % delay;
	}

	queue_delayed_work_on(this_darkness_cpuinfo->cpu, darkness_wq,
			&this_darkness_cpuinfo->work, delay);
	mutex_unlock(&this_darkness_cpuinfo->timer_mutex);
}

static int cpufreq_governor_darkness(struct cpufreq_policy *policy,
				unsigned int event)
{
	struct cpufreq_darkness_cpuinfo *this_darkness_cpuinfo;
	unsigned int cpu = policy->cpu, j;
	int rc, delay;

	this_darkness_cpuinfo = &per_cpu(od_darkness_cpuinfo, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!policy))
			return -EINVAL;

		mutex_lock(&darkness_mutex);
		this_darkness_cpuinfo->freq_table = cpufreq_frequency_get_table(cpu);
		if (!this_darkness_cpuinfo->freq_table) {
			mutex_unlock(&darkness_mutex);
			return -EINVAL;
		}

		for_each_cpu(j, policy->cpus) {
			struct cpufreq_darkness_cpuinfo *j_darkness_cpuinfo = &per_cpu(od_darkness_cpuinfo, j);

			j_darkness_cpuinfo->prev_cpu_idle = get_cpu_idle_time(j,
				&j_darkness_cpuinfo->prev_cpu_wall, 0);
		}

		darkness_enable++;
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (darkness_enable == 1) {
			rc = sysfs_create_group(cpufreq_global_kobject,
						&darkness_attr_group);
			if (rc) {
				darkness_enable--;
				mutex_unlock(&darkness_mutex);
				return rc;
			}
		}
		cpu = policy->cpu;
		this_darkness_cpuinfo->cpu = cpu;
		this_darkness_cpuinfo->cur_policy = policy;
		this_darkness_cpuinfo->governor_enabled = true;
		mutex_unlock(&darkness_mutex);

		mutex_init(&this_darkness_cpuinfo->timer_mutex);

		delay = usecs_to_jiffies(darkness_tuners_ins.sampling_rate);
		/* We want all CPUs to do sampling nearly on same jiffy */
		if (num_online_cpus() > 1) {
			delay -= jiffies % delay;
		}

		INIT_DELAYED_WORK_DEFERRABLE(&this_darkness_cpuinfo->work, do_darkness_timer);
		queue_delayed_work_on(cpu,
			darkness_wq, &this_darkness_cpuinfo->work, delay);

		break;
	case CPUFREQ_GOV_STOP:
		cancel_delayed_work_sync(&this_darkness_cpuinfo->work);

		mutex_lock(&darkness_mutex);
		mutex_destroy(&this_darkness_cpuinfo->timer_mutex);

		this_darkness_cpuinfo->governor_enabled = false;

		this_darkness_cpuinfo->cur_policy = NULL;

		darkness_enable--;
		if (!darkness_enable) {
			sysfs_remove_group(cpufreq_global_kobject,
					   &darkness_attr_group);
		}
		mutex_unlock(&darkness_mutex);

		break;
	case CPUFREQ_GOV_LIMITS:
		if (!this_darkness_cpuinfo->cur_policy
			 || !policy) {
			pr_debug("Unable to limit cpu freq due to cur_policy == NULL\n");
			return -EPERM;
		}
		mutex_lock(&this_darkness_cpuinfo->timer_mutex);
		__cpufreq_driver_target(this_darkness_cpuinfo->cur_policy,
				policy->cur, CPUFREQ_RELATION_L);
		mutex_unlock(&this_darkness_cpuinfo->timer_mutex);

		break;
	}
	return 0;
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_DARKNESS
static
#endif
struct cpufreq_governor cpufreq_gov_darkness = {
	.name                   = "darkness",
	.governor               = cpufreq_governor_darkness,
	.owner                  = THIS_MODULE,
};

static int __init cpufreq_gov_darkness_init(void)
{
	darkness_wq = alloc_workqueue("darkness_wq", WQ_HIGHPRI, 0);
	if (!darkness_wq) {
		printk(KERN_ERR "Failed to create darkness_wq workqueue\n");
		return -EFAULT;
	}

	return cpufreq_register_governor(&cpufreq_gov_darkness);
}

static void __exit cpufreq_gov_darkness_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_darkness);
}

MODULE_AUTHOR("Alucard24@XDA");
MODULE_DESCRIPTION("'cpufreq_darkness' - A dynamic cpufreq/cpuhotplug governor v5.0 (SnapDragon)");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_DARKNESS
fs_initcall(cpufreq_gov_darkness_init);
#else
module_init(cpufreq_gov_darkness_init);
#endif
module_exit(cpufreq_gov_darkness_exit);
