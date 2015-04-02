/*
 * Bricked Hotplug Driver
 *
 * Copyright (c) 2013-2014, Dennis Rassmann <showp1984@gmail.com>
 * Copyright (c) 2013-2014, Pranav Vashi <neobuddy89@gmail.com>
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <asm-generic/cputime.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#ifdef CONFIG_STATE_NOTIFIER
#include <linux/state_notifier.h>
#else
#include <linux/fb.h>
#endif

#define DEBUG 0

#define MPDEC_TAG			"bricked_hotplug"
#define HOTPLUG_ENABLED			0
#define MSM_MPDEC_STARTDELAY		20000
#define MSM_MPDEC_DELAY			130
#define DEFAULT_MIN_CPUS_ONLINE		1
#define DEFAULT_MAX_CPUS_ONLINE		NR_CPUS
#define DEFAULT_MAX_CPUS_ONLINE_SUSP	1
#define DEFAULT_SUSPEND_DEFER_TIME	10
#define DEFAULT_DOWN_LOCK_DUR		500

#define MSM_MPDEC_IDLE_FREQ		499200

enum {
	MSM_MPDEC_DISABLED = 0,
	MSM_MPDEC_IDLE,
	MSM_MPDEC_DOWN,
	MSM_MPDEC_UP,
};

static struct notifier_block notif;
static struct delayed_work hotplug_work;
static struct delayed_work suspend_work;
static struct work_struct resume_work;
static struct workqueue_struct *hotplug_wq;
static struct workqueue_struct *susp_wq;

static struct cpu_hotplug {
	unsigned int startdelay;
	unsigned int suspended;
	unsigned int suspend_defer_time;
	unsigned int min_cpus_online_res;
	unsigned int max_cpus_online_res;
	unsigned int max_cpus_online_susp;
	unsigned int delay;
	unsigned int down_lock_dur;
	unsigned long int idle_freq;
	unsigned int max_cpus_online;
	unsigned int min_cpus_online;
	unsigned int bricked_enabled;
	struct mutex bricked_hotplug_mutex;
	struct mutex bricked_cpu_mutex;
} hotplug = {
	.startdelay = MSM_MPDEC_STARTDELAY,
	.suspended = 0,
	.suspend_defer_time = DEFAULT_SUSPEND_DEFER_TIME,
	.min_cpus_online_res = DEFAULT_MIN_CPUS_ONLINE,
	.max_cpus_online_res = DEFAULT_MAX_CPUS_ONLINE,
	.max_cpus_online_susp = DEFAULT_MAX_CPUS_ONLINE_SUSP,
	.delay = MSM_MPDEC_DELAY,
	.down_lock_dur = DEFAULT_DOWN_LOCK_DUR,
	.idle_freq = MSM_MPDEC_IDLE_FREQ,
	.max_cpus_online = DEFAULT_MAX_CPUS_ONLINE,
	.min_cpus_online = DEFAULT_MIN_CPUS_ONLINE,
	.bricked_enabled = HOTPLUG_ENABLED,
};

static unsigned int NwNs_Threshold[8] = {12, 0, 25, 7, 30, 10, 0, 18};
static unsigned int TwTs_Threshold[8] = {140, 0, 140, 190, 140, 190, 0, 190};

struct down_lock {
	unsigned int locked;
	struct delayed_work lock_rem;
};
static DEFINE_PER_CPU(struct down_lock, lock_info);

static void apply_down_lock(unsigned int cpu)
{
	struct down_lock *dl = &per_cpu(lock_info, cpu);

	dl->locked = 1;
	queue_delayed_work_on(0, hotplug_wq, &dl->lock_rem,
			      msecs_to_jiffies(hotplug.down_lock_dur));
}

static void remove_down_lock(struct work_struct *work)
{
	struct down_lock *dl = container_of(work, struct down_lock,
					    lock_rem.work);
	dl->locked = 0;
}

static int check_down_lock(unsigned int cpu)
{
	struct down_lock *dl = &per_cpu(lock_info, cpu);
	return dl->locked;
}

extern unsigned int get_rq_info(void);

unsigned int state = MSM_MPDEC_DISABLED;

static int get_slowest_cpu(void) {
	unsigned int cpu, slow_cpu = 0, rate, slow_rate = 0;

	for_each_online_cpu(cpu) {
		if (cpu == 0)
			continue;
		rate = cpufreq_quick_get(cpu);
		if (rate > 0 && slow_rate <= rate) {
			slow_rate = rate;
			slow_cpu = cpu;
		}
	}

	return slow_cpu;
}

static unsigned int get_slowest_cpu_rate(void) {
	unsigned int cpu, rate, slow_rate = 0;

	for_each_online_cpu(cpu) {
		rate = cpufreq_quick_get(cpu);
		if (rate > 0 && slow_rate <= rate)
			slow_rate = rate;
	}

	return slow_rate;
}

static int mp_decision(void) {
	static bool first_call = true;
	int new_state = MSM_MPDEC_IDLE;
	int nr_cpu_online;
	int index;
	unsigned int rq_depth;
	static cputime64_t total_time = 0;
	static cputime64_t last_time;
	cputime64_t current_time;
	cputime64_t this_time = 0;

	if (!hotplug.bricked_enabled)
		return MSM_MPDEC_DISABLED;

	current_time = ktime_to_ms(ktime_get());

	if (first_call) {
		first_call = false;
	} else {
		this_time = current_time - last_time;
	}
	total_time += this_time;

	rq_depth = get_rq_info();
	nr_cpu_online = num_online_cpus();

	index = (nr_cpu_online - 1) * 2;
	if ((nr_cpu_online < DEFAULT_MAX_CPUS_ONLINE) && (rq_depth >= NwNs_Threshold[index])) {
		if ((total_time >= TwTs_Threshold[index]) &&
			(nr_cpu_online < hotplug.max_cpus_online)) {
			new_state = MSM_MPDEC_UP;
			if (get_slowest_cpu_rate() <=  hotplug.idle_freq)
				new_state = MSM_MPDEC_IDLE;
		}
	} else if ((nr_cpu_online > 1) && (rq_depth <= NwNs_Threshold[index+1])) {
		if ((total_time >= TwTs_Threshold[index+1]) &&
			(nr_cpu_online > hotplug.min_cpus_online)) {
			new_state = MSM_MPDEC_DOWN;
			if (get_slowest_cpu_rate() > hotplug.idle_freq)
				new_state = MSM_MPDEC_IDLE;
		}
	} else {
		new_state = MSM_MPDEC_IDLE;
		total_time = 0;
	}

	if (new_state != MSM_MPDEC_IDLE) {
		total_time = 0;
	}

	last_time = ktime_to_ms(ktime_get());
#if DEBUG
	pr_info(MPDEC_TAG"[DEBUG] rq: %u, new_state: %i | Mask=[%d%d%d%d]\n",
			rq_depth, new_state, cpu_online(0), cpu_online(1), cpu_online(2), cpu_online(3));
#endif
	return new_state;
}

static void __ref bricked_hotplug_work(struct work_struct *work) {
	unsigned int cpu;

	if (hotplug.suspended && hotplug.max_cpus_online_susp <= 1)
		goto out;

	if (!mutex_trylock(&hotplug.bricked_cpu_mutex))
		goto out;

	state = mp_decision();
	switch (state) {
	case MSM_MPDEC_DISABLED:
	case MSM_MPDEC_IDLE:
		break;
	case MSM_MPDEC_DOWN:
		cpu = get_slowest_cpu();
		if (cpu > 0) {
			if (cpu_online(cpu) && !check_cpuboost(cpu)
					&& !check_down_lock(cpu))
				cpu_down(cpu);
		}
		break;
	case MSM_MPDEC_UP:
		cpu = cpumask_next_zero(0, cpu_online_mask);
		if (cpu < DEFAULT_MAX_CPUS_ONLINE) {
			if (!cpu_online(cpu)) {
				cpu_up(cpu);
				apply_down_lock(cpu);
			}
		}
		break;
	default:
		pr_err(MPDEC_TAG": %s: invalid mpdec hotplug state %d\n",
			__func__, state);
	}
	mutex_unlock(&hotplug.bricked_cpu_mutex);

out:
	if (hotplug.bricked_enabled)
		queue_delayed_work(hotplug_wq, &hotplug_work,
					msecs_to_jiffies(hotplug.delay));
	return;
}

static void bricked_hotplug_suspend(struct work_struct *work)
{
	int cpu;

	mutex_lock(&hotplug.bricked_hotplug_mutex);
	hotplug.suspended = 1;
	hotplug.min_cpus_online_res = hotplug.min_cpus_online;
	hotplug.min_cpus_online = 1;
	hotplug.max_cpus_online_res = hotplug.max_cpus_online;
	hotplug.max_cpus_online = hotplug.max_cpus_online_susp;
	mutex_unlock(&hotplug.bricked_hotplug_mutex);

	if (hotplug.max_cpus_online_susp > 1) {
		pr_info(MPDEC_TAG": Screen -> off\n");
		return;
	}

	/* main work thread can sleep now */
	cancel_delayed_work_sync(&hotplug_work);

	for_each_possible_cpu(cpu) {
		if ((cpu >= 1) && (cpu_online(cpu)))
			cpu_down(cpu);
	}

	pr_info(MPDEC_TAG": Screen -> off. Deactivated bricked hotplug. | Mask=[%d%d%d%d]\n",
			cpu_online(0), cpu_online(1), cpu_online(2), cpu_online(3));
}

static void __ref bricked_hotplug_resume(struct work_struct *work)
{
	int cpu, required_reschedule = 0, required_wakeup = 0;

	if (hotplug.suspended) {
		mutex_lock(&hotplug.bricked_hotplug_mutex);
		hotplug.suspended = 0;
		hotplug.min_cpus_online = hotplug.min_cpus_online_res;
		hotplug.max_cpus_online = hotplug.max_cpus_online_res;
		mutex_unlock(&hotplug.bricked_hotplug_mutex);
		required_wakeup = 1;
		/* Initiate hotplug work if it was cancelled */
		if (hotplug.max_cpus_online_susp <= 1) {
			required_reschedule = 1;
			INIT_DELAYED_WORK(&hotplug_work, bricked_hotplug_work);
		}
	}

	if (wakeup_boost || required_wakeup) {
		/* Fire up all CPUs */
		for_each_cpu_not(cpu, cpu_online_mask) {
			if (cpu == 0)
				continue;
			cpu_up(cpu);
			apply_down_lock(cpu);
		}
	}

	/* Resume hotplug workqueue if required */
	if (required_reschedule) {
		queue_delayed_work(hotplug_wq, &hotplug_work, 0);
		pr_info(MPDEC_TAG": Screen -> on. Activated bricked hotplug. | Mask=[%d%d%d%d]\n",
				cpu_online(0), cpu_online(1), cpu_online(2), cpu_online(3));
	}
}

static void __bricked_hotplug_resume(void)
{
	if (!hotplug.bricked_enabled)
		return;

	flush_workqueue(susp_wq);
	cancel_delayed_work_sync(&suspend_work);
	queue_work_on(0, susp_wq, &resume_work);
}

static void __bricked_hotplug_suspend(void)
{
	if (!hotplug.bricked_enabled || hotplug.suspended)
		return;

	INIT_DELAYED_WORK(&suspend_work, bricked_hotplug_suspend);
	queue_delayed_work_on(0, susp_wq, &suspend_work, 
		msecs_to_jiffies(hotplug.suspend_defer_time * 1000)); 
}

#ifdef CONFIG_STATE_NOTIFIER
static int state_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	switch (event) {
		case STATE_NOTIFIER_ACTIVE:
			__bricked_hotplug_resume();
			break;
		case STATE_NOTIFIER_SUSPEND:
			__bricked_hotplug_suspend();
			break;
		default:
			break;
	}

	return NOTIFY_OK;
}
#else
static int prev_fb = FB_BLANK_UNBLANK;

static int fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;

	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		switch (*blank) {
			case FB_BLANK_UNBLANK:
				if (prev_fb == FB_BLANK_POWERDOWN) {
					__bricked_hotplug_resume();
					prev_fb = FB_BLANK_UNBLANK;
				}
				break;
			case FB_BLANK_POWERDOWN:
				if (prev_fb == FB_BLANK_UNBLANK) {
					__bricked_hotplug_suspend();
					prev_fb = FB_BLANK_POWERDOWN;
				}
				break;
		}
	}

	return NOTIFY_OK;
}
#endif

static int bricked_hotplug_start(void)
{
	int cpu, ret = 0;
	struct down_lock *dl;

	hotplug_wq = alloc_workqueue("bricked_hotplug", WQ_HIGHPRI | WQ_FREEZABLE, 0);
	if (!hotplug_wq) {
		ret = -ENOMEM;
		goto err_out;
	}

	susp_wq =
	    alloc_workqueue("susp_wq", WQ_FREEZABLE, 0);
	if (!susp_wq) {
		pr_err("%s: Failed to allocate suspend workqueue\n",
		       MPDEC_TAG);
		ret = -ENOMEM;
		goto err_dev;
	}

#ifdef CONFIG_STATE_NOTIFIER
	notif.notifier_call = state_notifier_callback;
	if (state_register_client(&notif)) {
		pr_err("%s: Failed to register State notifier callback\n",
			MPDEC_TAG);
		goto err_susp;
	}
#else
	notif.notifier_call = fb_notifier_callback;
	if (fb_register_client(&notif)) {
		pr_err("%s: Failed to register FB notifier callback\n",
			MPDEC_TAG);
		goto err_susp;
	}
#endif

	mutex_init(&hotplug.bricked_cpu_mutex);
	mutex_init(&hotplug.bricked_hotplug_mutex);

	INIT_DELAYED_WORK(&hotplug_work, bricked_hotplug_work);
	INIT_DELAYED_WORK(&suspend_work, bricked_hotplug_suspend);
	INIT_WORK(&resume_work, bricked_hotplug_resume);

	for_each_possible_cpu(cpu) {
		dl = &per_cpu(lock_info, cpu);
		INIT_DELAYED_WORK(&dl->lock_rem, remove_down_lock);
	}

	if (hotplug.bricked_enabled)
		queue_delayed_work(hotplug_wq, &hotplug_work,
					msecs_to_jiffies(hotplug.startdelay));

	return ret;
err_susp:
	destroy_workqueue(susp_wq);
err_dev:
	destroy_workqueue(hotplug_wq);
err_out:
	hotplug.bricked_enabled = 0;
	return ret;
}

static void bricked_hotplug_stop(void)
{
	int cpu;
	struct down_lock *dl;

	for_each_possible_cpu(cpu) {
		dl = &per_cpu(lock_info, cpu);
		cancel_delayed_work_sync(&dl->lock_rem);
	}

	flush_workqueue(susp_wq);
	cancel_work_sync(&resume_work);
	cancel_delayed_work_sync(&suspend_work);
	cancel_delayed_work_sync(&hotplug_work);
	mutex_destroy(&hotplug.bricked_hotplug_mutex);
	mutex_destroy(&hotplug.bricked_cpu_mutex);
#ifdef CONFIG_STATE_NOTIFIER
	state_unregister_client(&notif);
#else
	fb_unregister_client(&notif);
#endif
	notif.notifier_call = NULL;
	destroy_workqueue(susp_wq);
	destroy_workqueue(hotplug_wq);

	/* Put all sibling cores to sleep */
	for_each_online_cpu(cpu) {
		if (cpu == 0)
			continue;
		cpu_down(cpu);
	}
}

/**************************** SYSFS START ****************************/

#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct device *dev, struct device_attribute *bricked_hotplug_attrs,	\
 char *buf)								\
{									\
	return sprintf(buf, "%u\n", hotplug.object);			\
}

show_one(startdelay, startdelay);
show_one(delay, delay);
show_one(down_lock_duration, down_lock_dur);
show_one(min_cpus_online, min_cpus_online);
show_one(max_cpus_online, max_cpus_online);
show_one(max_cpus_online_susp, max_cpus_online_susp);
show_one(suspend_defer_time, suspend_defer_time);
show_one(bricked_enabled, bricked_enabled);

#define define_one_twts(file_name, arraypos)				\
static ssize_t show_##file_name						\
(struct device *dev, struct device_attribute *bricked_hotplug_attrs,	\
 char *buf)								\
{									\
	return sprintf(buf, "%u\n", TwTs_Threshold[arraypos]);		\
}									\
static ssize_t store_##file_name					\
(struct device *dev, struct device_attribute *bricked_hotplug_attrs,	\
 const char *buf, size_t count)						\
{									\
	unsigned int input;						\
	int ret;							\
	ret = sscanf(buf, "%u", &input);				\
	if (ret != 1)							\
		return -EINVAL;						\
	TwTs_Threshold[arraypos] = input;				\
	return count;							\
}									\
static DEVICE_ATTR(file_name, 644, show_##file_name, store_##file_name);
define_one_twts(twts_threshold_0, 0);
define_one_twts(twts_threshold_1, 1);
define_one_twts(twts_threshold_2, 2);
define_one_twts(twts_threshold_3, 3);
define_one_twts(twts_threshold_4, 4);
define_one_twts(twts_threshold_5, 5);
define_one_twts(twts_threshold_6, 6);
define_one_twts(twts_threshold_7, 7);

#define define_one_nwns(file_name, arraypos)				\
static ssize_t show_##file_name						\
(struct device *dev, struct device_attribute *bricked_hotplug_attrs,	\
 char *buf)								\
{									\
	return sprintf(buf, "%u\n", NwNs_Threshold[arraypos]);		\
}									\
static ssize_t store_##file_name					\
(struct device *dev, struct device_attribute *bricked_hotplug_attrs,	\
 const char *buf, size_t count)						\
{									\
	unsigned int input;						\
	int ret;							\
	ret = sscanf(buf, "%u", &input);				\
	if (ret != 1)							\
		return -EINVAL;						\
	NwNs_Threshold[arraypos] = input;				\
	return count;							\
}									\
static DEVICE_ATTR(file_name, 644, show_##file_name, store_##file_name);
define_one_nwns(nwns_threshold_0, 0);
define_one_nwns(nwns_threshold_1, 1);
define_one_nwns(nwns_threshold_2, 2);
define_one_nwns(nwns_threshold_3, 3);
define_one_nwns(nwns_threshold_4, 4);
define_one_nwns(nwns_threshold_5, 5);
define_one_nwns(nwns_threshold_6, 6);
define_one_nwns(nwns_threshold_7, 7);

static ssize_t show_idle_freq (struct device *dev,
				struct device_attribute *bricked_hotplug_attrs,
				char *buf)
{
	return sprintf(buf, "%lu\n", hotplug.idle_freq);
}

static ssize_t store_startdelay(struct device *dev,
				struct device_attribute *bricked_hotplug_attrs,
				const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	hotplug.startdelay = input;

	return count;
}

static ssize_t store_delay(struct device *dev,
				struct device_attribute *bricked_hotplug_attrs,
				const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	hotplug.delay = input;

	return count;
}

static ssize_t store_down_lock_duration(struct device *dev,
				struct device_attribute *bricked_hotplug_attrs,
				const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = sscanf(buf, "%u", &val);
	if (ret != 1)
		return -EINVAL;

	hotplug.down_lock_dur = val;

	return count;
}

static ssize_t store_idle_freq(struct device *dev,
				struct device_attribute *bricked_hotplug_attrs,
				const char *buf, size_t count)
{
	long unsigned int input;
	int ret;
	ret = sscanf(buf, "%lu", &input);
	if (ret != 1)
		return -EINVAL;

	hotplug.idle_freq = input;

	return count;
}

static ssize_t __ref store_min_cpus_online(struct device *dev,
				struct device_attribute *bricked_hotplug_attrs,
				const char *buf, size_t count)
{
	unsigned int input, cpu;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if ((ret != 1) || input < 1 || input > DEFAULT_MAX_CPUS_ONLINE)
		return -EINVAL;

	if (hotplug.max_cpus_online < input)
		hotplug.max_cpus_online = input;

	hotplug.min_cpus_online = input;

	if (!hotplug.bricked_enabled)
		return count;

	if (num_online_cpus() < hotplug.min_cpus_online) {
		for (cpu = 1; cpu < DEFAULT_MAX_CPUS_ONLINE; cpu++) {
			if (num_online_cpus() >= hotplug.min_cpus_online)
				break;
			if (cpu_online(cpu))
				continue;
			cpu_up(cpu);
		}
		pr_info(MPDEC_TAG": min_cpus_online set to %u. Affected CPUs were hotplugged!\n", input);
	}

	return count;
}

static ssize_t store_max_cpus_online(struct device *dev,
				struct device_attribute *bricked_hotplug_attrs,
				const char *buf, size_t count)
{
	unsigned int input, cpu;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if ((ret != 1) || input < 1 || input > DEFAULT_MAX_CPUS_ONLINE)
			return -EINVAL;

	if (hotplug.min_cpus_online > input)
		hotplug.min_cpus_online = input;

	hotplug.max_cpus_online = input;

	if (!hotplug.bricked_enabled)
		return count;

	if (num_online_cpus() > hotplug.max_cpus_online) {
		for (cpu = DEFAULT_MAX_CPUS_ONLINE; cpu > 0; cpu--) {
			if (num_online_cpus() <= hotplug.max_cpus_online)
				break;
			if (!cpu_online(cpu))
				continue;
			cpu_down(cpu);
		}
		pr_info(MPDEC_TAG": max_cpus set to %u. Affected CPUs were unplugged!\n", input);
	}

	return count;
}

static ssize_t store_max_cpus_online_susp(struct device *dev,
				struct device_attribute *bricked_hotplug_attrs,
				const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if ((ret != 1) || input < 1 || input > DEFAULT_MAX_CPUS_ONLINE)
			return -EINVAL;

	hotplug.max_cpus_online_susp = input;

	return count;
}

static ssize_t store_suspend_defer_time(struct device *dev,
				    struct device_attribute *bricked_hotplug_attrs,
				    const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = sscanf(buf, "%u", &val);
	if (ret != 1)
		return -EINVAL;

	hotplug.suspend_defer_time = val;

	return count;
}

static ssize_t store_bricked_enabled(struct device *dev,
				struct device_attribute *bricked_hotplug_attrs,
				const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == hotplug.bricked_enabled)
		return count;

	hotplug.bricked_enabled = input;

	if (!hotplug.bricked_enabled) {
		state = MSM_MPDEC_DISABLED;
		bricked_hotplug_stop();
		pr_info(MPDEC_TAG": Disabled\n");
	} else {
		state = MSM_MPDEC_IDLE;
		bricked_hotplug_start();
		pr_info(MPDEC_TAG": Enabled\n");
	}

	return count;
}

static DEVICE_ATTR(startdelay, 644, show_startdelay, store_startdelay);
static DEVICE_ATTR(delay, 644, show_delay, store_delay);
static DEVICE_ATTR(down_lock_duration, 644, show_down_lock_duration, store_down_lock_duration);
static DEVICE_ATTR(idle_freq, 644, show_idle_freq, store_idle_freq);
static DEVICE_ATTR(min_cpus, 644, show_min_cpus_online, store_min_cpus_online);
static DEVICE_ATTR(max_cpus, 644, show_max_cpus_online, store_max_cpus_online);
static DEVICE_ATTR(min_cpus_online, 644, show_min_cpus_online, store_min_cpus_online);
static DEVICE_ATTR(max_cpus_online, 644, show_max_cpus_online, store_max_cpus_online);
static DEVICE_ATTR(max_cpus_online_susp, 644, show_max_cpus_online_susp, store_max_cpus_online_susp);
static DEVICE_ATTR(suspend_defer_time, 644, show_suspend_defer_time, store_suspend_defer_time);
static DEVICE_ATTR(enabled, 644, show_bricked_enabled, store_bricked_enabled);

static struct attribute *bricked_hotplug_attrs[] = {
	&dev_attr_startdelay.attr,
	&dev_attr_delay.attr,
	&dev_attr_down_lock_duration.attr,
	&dev_attr_idle_freq.attr,
	&dev_attr_min_cpus.attr,
	&dev_attr_max_cpus.attr,
	&dev_attr_min_cpus_online.attr,
	&dev_attr_max_cpus_online.attr,
	&dev_attr_max_cpus_online_susp.attr,
	&dev_attr_suspend_defer_time.attr,
	&dev_attr_enabled.attr,
	&dev_attr_twts_threshold_0.attr,
	&dev_attr_twts_threshold_1.attr,
	&dev_attr_twts_threshold_2.attr,
	&dev_attr_twts_threshold_3.attr,
	&dev_attr_twts_threshold_4.attr,
	&dev_attr_twts_threshold_5.attr,
	&dev_attr_twts_threshold_6.attr,
	&dev_attr_twts_threshold_7.attr,
	&dev_attr_nwns_threshold_0.attr,
	&dev_attr_nwns_threshold_1.attr,
	&dev_attr_nwns_threshold_2.attr,
	&dev_attr_nwns_threshold_3.attr,
	&dev_attr_nwns_threshold_4.attr,
	&dev_attr_nwns_threshold_5.attr,
	&dev_attr_nwns_threshold_6.attr,
	&dev_attr_nwns_threshold_7.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = bricked_hotplug_attrs,
	.name = "conf",
};

/**************************** SYSFS END ****************************/

static int bricked_hotplug_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct kobject *bricked_kobj;

	bricked_kobj =
		kobject_create_and_add("msm_mpdecision", kernel_kobj);
	if (!bricked_kobj) {
		pr_err("%s kobject create failed!\n",
			__func__);
		return -ENOMEM;
        }

	ret = sysfs_create_group(bricked_kobj,
			&attr_group);

        if (ret) {
		pr_err("%s bricked_kobj create failed!\n",
			__func__);
		goto err_dev;
	}

	if (hotplug.bricked_enabled) {
		ret = bricked_hotplug_start();
		if (ret != 0)
			goto err_dev;
	}

	return ret;
err_dev:
	if (bricked_kobj != NULL)
		kobject_put(bricked_kobj);
	return ret;
}

static struct platform_device bricked_hotplug_device = {
	.name = MPDEC_TAG,
	.id = -1,
};

static int bricked_hotplug_remove(struct platform_device *pdev)
{
	if (hotplug.bricked_enabled)
		bricked_hotplug_stop();

	return 0;
}

static struct platform_driver bricked_hotplug_driver = {
	.probe = bricked_hotplug_probe,
	.remove = bricked_hotplug_remove,
	.driver = {
		.name = MPDEC_TAG,
		.owner = THIS_MODULE,
	},
};

static int __init msm_mpdec_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&bricked_hotplug_driver);
	if (ret) {
		pr_err("%s: Driver register failed: %d\n", MPDEC_TAG, ret);
		return ret;
	}

	ret = platform_device_register(&bricked_hotplug_device);
	if (ret) {
		pr_err("%s: Device register failed: %d\n", MPDEC_TAG, ret);
		return ret;
	}

	pr_info(MPDEC_TAG": %s init complete.", __func__);

	return ret;
}

void msm_mpdec_exit(void)
{
	platform_device_unregister(&bricked_hotplug_device);
	platform_driver_unregister(&bricked_hotplug_driver);
}

late_initcall(msm_mpdec_init);
module_exit(msm_mpdec_exit);

MODULE_AUTHOR("Pranav Vashi <neobuddy89@gmail.com>");
MODULE_DESCRIPTION("Bricked Hotplug Driver");
MODULE_LICENSE("GPLv2");
