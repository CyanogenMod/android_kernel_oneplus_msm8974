/*
 * MSM CPU Frequency Limiter Driver
 *
 * Copyright (c) 2013-2014, Dorimanx <yuri@bynet.co.il>
 * Copyright (c) 2013-2015, Pranav Vashi <neobuddy89@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_STATE_NOTIFIER
#include <linux/state_notifier.h>
#else
#include <linux/fb.h>
#endif

#include <soc/qcom/limiter.h>

#define MSM_CPUFREQ_LIMIT_MAJOR		3
#define MSM_CPUFREQ_LIMIT_MINOR		6

static unsigned int debug_mask = 0;

#define dprintk(msg...)		\
do { 				\
	if (debug_mask)		\
		pr_info(msg);	\
} while (0)

static struct workqueue_struct *limiter_wq;

static void update_cpu_max_freq(unsigned int cpu)
{
	uint32_t max_freq = 0, min_freq = 0;

	mutex_lock(&limit.msm_limiter_mutex[cpu]);
	if (limit.suspended)
		max_freq = limit.suspend_max_freq;
	else
		max_freq = limit.resume_max_freq[cpu];

	if (limit.suspended && limit.suspend_min_freq[cpu] <= max_freq)
		min_freq = limit.suspend_min_freq[cpu];

	cpufreq_set_freq(max_freq, min_freq, cpu);

	mutex_unlock(&limit.msm_limiter_mutex[cpu]);
}

static void update_cpu_min_freq(unsigned int cpu)
{
	mutex_lock(&limit.msm_limiter_mutex[cpu]);
	cpufreq_set_freq(0, limit.suspend_min_freq[cpu], cpu);
	mutex_unlock(&limit.msm_limiter_mutex[cpu]);
}

static void msm_limit_suspend(struct work_struct *work)
{
	int cpu = 0;

	/* Do not suspend if suspend freq or resume freq not available */
	if (!limit.suspend_max_freq || !limit.resume_max_freq[0])
		return;

	mutex_lock(&limit.resume_suspend_mutex);
	limit.suspended = 1;
	mutex_unlock(&limit.resume_suspend_mutex);

	for_each_possible_cpu(cpu)
		update_cpu_max_freq(cpu);
}

static void msm_limit_resume(struct work_struct *work)
{
	int cpu = 0;

	/* Do not resume if resume freq not available */
	if (!limit.resume_max_freq[0] || !limit.suspended)
		return;

	mutex_lock(&limit.resume_suspend_mutex);
	limit.suspended = 0;
	mutex_unlock(&limit.resume_suspend_mutex);

	/* Restore max allowed freq */
	for_each_possible_cpu(cpu)
		update_cpu_max_freq(cpu);
}

static void __msm_limit_suspend(void)
{
	if (!limit.limiter_enabled || limit.suspended)
		return;

	INIT_DELAYED_WORK(&limit.suspend_work, msm_limit_suspend);
	queue_delayed_work_on(0, limiter_wq, &limit.suspend_work,
			msecs_to_jiffies(limit.suspend_defer_time * 1000));
}

static void __msm_limit_resume(void)
{
	if (!limit.limiter_enabled)
		return;

	flush_workqueue(limiter_wq);
	cancel_delayed_work_sync(&limit.suspend_work);
	queue_work_on(0, limiter_wq, &limit.resume_work);
}

#ifdef CONFIG_STATE_NOTIFIER
static int state_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	switch (event) {
		case STATE_NOTIFIER_ACTIVE:
			__msm_limit_resume();
			break;
		case STATE_NOTIFIER_SUSPEND:
			__msm_limit_suspend();
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
					__msm_limit_resume();
					prev_fb = FB_BLANK_UNBLANK;
				}
				break;
			case FB_BLANK_POWERDOWN:
				if (prev_fb == FB_BLANK_UNBLANK) {
					__msm_limit_suspend();
					prev_fb = FB_BLANK_POWERDOWN;
				}
				break;
		}
	}

	return NOTIFY_OK;
}
#endif

static int msm_cpufreq_limit_start(void)
{
	unsigned int cpu = 0;
	int ret = 0;

	limiter_wq =
	    alloc_workqueue("msm_limiter_wq", WQ_HIGHPRI | WQ_FREEZABLE, 0);
	if (!limiter_wq) {
		pr_err("%s: Failed to allocate limiter workqueue\n",
		       MSM_LIMIT);
		ret = -ENOMEM;
		goto err_out;
	}

#ifdef CONFIG_STATE_NOTIFIER
	limit.notif.notifier_call = state_notifier_callback;
	if (state_register_client(&limit.notif)) {
		pr_err("%s: Failed to register State notifier callback\n",
			MSM_LIMIT);
		goto err_dev;
	}
#else
	limit.notif.notifier_call = fb_notifier_callback;
	if (fb_register_client(&limit.notif)) {
		pr_err("%s: Failed to register FB notifier callback\n",
			MSM_LIMIT);
		goto err_dev;
	}
#endif

	for_each_possible_cpu(cpu)
		mutex_init(&limit.msm_limiter_mutex[cpu]);
	mutex_init(&limit.resume_suspend_mutex);
	INIT_DELAYED_WORK(&limit.suspend_work, msm_limit_suspend);
	INIT_WORK(&limit.resume_work, msm_limit_resume);

	limit.suspended = 1;
	queue_work_on(0, limiter_wq, &limit.resume_work);

	return ret;
err_dev:
	destroy_workqueue(limiter_wq);
err_out:
	limit.limiter_enabled = 0;
	return ret;
}

static void msm_cpufreq_limit_stop(void)
{
	unsigned int cpu = 0;
	limit.suspended = 1;

	flush_workqueue(limiter_wq);

	cancel_work_sync(&limit.resume_work);
	cancel_delayed_work_sync(&limit.suspend_work);
	mutex_destroy(&limit.resume_suspend_mutex);
	for_each_possible_cpu(cpu)	
		mutex_destroy(&limit.msm_limiter_mutex[cpu]);

#ifdef CONFIG_STATE_NOTIFIER
	state_unregister_client(&limit.notif);
#else
	fb_unregister_client(&limit.notif);
#endif
	limit.notif.notifier_call = NULL;
	destroy_workqueue(limiter_wq);
}

static ssize_t limiter_enabled_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", limit.limiter_enabled);
}

static ssize_t limiter_enabled_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = sscanf(buf, "%u\n", &val);
	if (ret != 1 || val < 0 || val > 1)
		return -EINVAL;

	if (val == limit.limiter_enabled)
		return count;

	limit.limiter_enabled = val;

	if (limit.limiter_enabled)
		msm_cpufreq_limit_start();
	else
		msm_cpufreq_limit_stop();

	return count;
}

static ssize_t debug_mask_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", debug_mask);
}

static ssize_t debug_mask_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = sscanf(buf, "%u\n", &val);
	if (ret != 1 || val < 0 || val > 1)
		return -EINVAL;

	if (val == debug_mask)
		return count;

	debug_mask = val;

	return count;
}


static ssize_t suspend_defer_time_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", limit.suspend_defer_time);
}

static ssize_t suspend_defer_time_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = sscanf(buf, "%u\n", &val);
	if (ret != 1)
		return -EINVAL;

	limit.suspend_defer_time = val;

	return count;
}

static ssize_t suspend_max_freq_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", limit.suspend_max_freq);
}

static ssize_t suspend_max_freq_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = sscanf(buf, "%u\n", &val);
	if (ret != 1)
		return -EINVAL;

	if (val == 0)
		goto out;

	if (val == limit.suspend_max_freq)
		return count;

out:
	limit.suspend_max_freq = val;

	return count;
}


#define multi_cpu(cpu)					\
static ssize_t store_resume_max_freq_##cpu		\
(struct kobject *kobj, 					\
 struct kobj_attribute *attr, 				\
 const char *buf, size_t count)				\
{							\
	int ret;					\
	unsigned int val;				\
	ret = sscanf(buf, "%u\n", &val);		\
	if (ret != 1)					\
		return -EINVAL;				\
	if (val == 0)					\
		goto out;				\
	if (val < limit.suspend_min_freq[cpu])		\
		val = limit.suspend_min_freq[cpu];	\
	if (val == limit.resume_max_freq[cpu])		\
		return count;				\
out:							\
	limit.resume_max_freq[cpu] = val;		\
	if (limit.limiter_enabled)			\
		update_cpu_max_freq(cpu);		\
	return count;					\
}							\
static ssize_t show_resume_max_freq_##cpu		\
(struct kobject *kobj,					\
 struct kobj_attribute *attr, char *buf)		\
{							\
	return sprintf(buf, "%u\n",			\
			limit.resume_max_freq[cpu]);	\
}							\
static ssize_t store_suspend_min_freq_##cpu(		\
 struct kobject *kobj,					\
 struct kobj_attribute *attr,				\
 const char *buf, size_t count)				\
{							\
	int ret;					\
	unsigned int val;				\
	ret = sscanf(buf, "%u\n", &val);		\
	if (ret != 1)					\
		return -EINVAL;				\
	if (val == 0)					\
		goto out;				\
	if (val > limit.resume_max_freq[cpu])		\
		val = limit.resume_max_freq[cpu];	\
	if (val == limit.suspend_min_freq[cpu])		\
		return count;				\
out:							\
	limit.suspend_min_freq[cpu] = val;		\
	if (limit.limiter_enabled)			\
		update_cpu_min_freq(cpu);		\
	return count;					\
}							\
static ssize_t show_suspend_min_freq_##cpu(		\
 struct kobject *kobj,					\
 struct kobj_attribute *attr, char *buf)		\
{							\
	return sprintf(buf, "%u\n",			\
		limit.suspend_min_freq[cpu]);		\
}							\
static ssize_t store_scaling_governor_##cpu(		\
 struct kobject *kobj,					\
 struct kobj_attribute *attr,				\
 const char *buf, size_t count)				\
{							\
	int ret;					\
	char val[16];					\
	ret = sscanf(buf, "%s\n", val);			\
	if (ret != 1)					\
		return -EINVAL;				\
	ret = cpufreq_set_gov(val, cpu);		\
	return count;					\
}							\
static ssize_t show_scaling_governor_##cpu(		\
 struct kobject *kobj,					\
 struct kobj_attribute *attr, char *buf)		\
{							\
	return sprintf(buf, "%s\n",			\
	cpufreq_get_gov(cpu));			\
}							\
static ssize_t show_live_max_freq_##cpu(		\
 struct kobject *kobj,					\
 struct kobj_attribute *attr, char *buf)		\
{							\
	return sprintf(buf, "%u\n",			\
	cpufreq_get_max(cpu));				\
}							\
static ssize_t show_live_min_freq_##cpu(		\
 struct kobject *kobj,					\
 struct kobj_attribute *attr, char *buf)		\
{							\
	return sprintf(buf, "%u\n",			\
	cpufreq_get_min(cpu));				\
}							\
static ssize_t show_live_cur_freq_##cpu(		\
 struct kobject *kobj,					\
 struct kobj_attribute *attr, char *buf)		\
{							\
	return sprintf(buf, "%u\n",			\
	cpufreq_quick_get(cpu));			\
}							\
static struct kobj_attribute resume_max_freq_##cpu =	\
	__ATTR(resume_max_freq_##cpu, 0666,		\
		show_resume_max_freq_##cpu,		\
		store_resume_max_freq_##cpu);		\
static struct kobj_attribute suspend_min_freq_##cpu =	\
	__ATTR(suspend_min_freq_##cpu, 0666,		\
		show_suspend_min_freq_##cpu,		\
		store_suspend_min_freq_##cpu);		\
static struct kobj_attribute scaling_governor_##cpu =	\
	__ATTR(scaling_governor_##cpu, 0666,		\
		show_scaling_governor_##cpu,		\
		store_scaling_governor_##cpu);		\
static struct kobj_attribute live_max_freq_##cpu =	\
	__ATTR(live_max_freq_##cpu, 0666,		\
		show_live_max_freq_##cpu,		\
		store_resume_max_freq_##cpu);		\
static struct kobj_attribute live_min_freq_##cpu =	\
	__ATTR(live_min_freq_##cpu, 0666,		\
		show_live_min_freq_##cpu,		\
		store_suspend_min_freq_##cpu);		\
static struct kobj_attribute live_cur_freq_##cpu =	\
	__ATTR(live_cur_freq_##cpu, 0666,		\
		show_live_cur_freq_##cpu, NULL);	\

multi_cpu(0);
multi_cpu(1);
multi_cpu(2);
multi_cpu(3);

static ssize_t msm_cpufreq_limit_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "version: %u.%u\n",
			MSM_CPUFREQ_LIMIT_MAJOR, MSM_CPUFREQ_LIMIT_MINOR);
}

static struct kobj_attribute msm_cpufreq_limit_version_attribute =
	__ATTR(msm_cpufreq_limit_version, 0444,
		msm_cpufreq_limit_version_show,
		NULL);

static struct kobj_attribute limiter_enabled_attribute =
	__ATTR(limiter_enabled, 0666,
		limiter_enabled_show,
		limiter_enabled_store);

static struct kobj_attribute debug_mask_attribute =
	__ATTR(debug_mask, 0666,
		debug_mask_show,
		debug_mask_store);

static struct kobj_attribute suspend_defer_time_attribute =
	__ATTR(suspend_defer_time, 0666,
		suspend_defer_time_show,
		suspend_defer_time_store);

static struct kobj_attribute suspend_max_freq_attribute =
	__ATTR(suspend_max_freq, 0666,
		suspend_max_freq_show,
		suspend_max_freq_store);

static struct attribute *msm_cpufreq_limit_attrs[] =
	{
		&limiter_enabled_attribute.attr,
		&debug_mask_attribute.attr,
		&suspend_defer_time_attribute.attr,
		&suspend_max_freq_attribute.attr,
		&resume_max_freq_0.attr,
		&resume_max_freq_1.attr,
		&resume_max_freq_2.attr,
		&resume_max_freq_3.attr,
		&suspend_min_freq_0.attr,
		&suspend_min_freq_1.attr,
		&suspend_min_freq_2.attr,
		&suspend_min_freq_3.attr,
		&scaling_governor_0.attr,
		&scaling_governor_1.attr,
		&scaling_governor_2.attr,
		&scaling_governor_3.attr,
		&live_max_freq_0.attr,
		&live_max_freq_1.attr,
		&live_max_freq_2.attr,
		&live_max_freq_3.attr,
		&live_min_freq_0.attr,
		&live_min_freq_1.attr,
		&live_min_freq_2.attr,
		&live_min_freq_3.attr,
		&live_cur_freq_0.attr,
		&live_cur_freq_1.attr,
		&live_cur_freq_2.attr,
		&live_cur_freq_3.attr,
		&msm_cpufreq_limit_version_attribute.attr,
		NULL,
	};

static struct attribute_group msm_cpufreq_limit_attr_group =
	{
		.attrs = msm_cpufreq_limit_attrs,
	};

static struct kobject *msm_cpufreq_limit_kobj;

static int msm_cpufreq_limit_init(void)
{
	int ret;

	msm_cpufreq_limit_kobj =
		kobject_create_and_add(MSM_LIMIT, kernel_kobj);
	if (!msm_cpufreq_limit_kobj) {
		pr_err("%s msm_cpufreq_limit_kobj kobject create failed!\n",
			__func__);
		return -ENOMEM;
        }

	ret = sysfs_create_group(msm_cpufreq_limit_kobj,
			&msm_cpufreq_limit_attr_group);

        if (ret) {
		pr_err("%s msm_cpufreq_limit_kobj create failed!\n",
			__func__);
		goto err_dev;
	}

	if (limit.limiter_enabled)
		msm_cpufreq_limit_start();

	return ret;
err_dev:
	if (msm_cpufreq_limit_kobj != NULL)
		kobject_put(msm_cpufreq_limit_kobj);
	return ret;
}

static void msm_cpufreq_limit_exit(void)
{
	if (msm_cpufreq_limit_kobj != NULL)
		kobject_put(msm_cpufreq_limit_kobj);

	if (limit.limiter_enabled)
		msm_cpufreq_limit_stop();

}

late_initcall(msm_cpufreq_limit_init);
module_exit(msm_cpufreq_limit_exit);

MODULE_AUTHOR("Dorimanx <yuri@bynet.co.il>");
MODULE_AUTHOR("Pranav Vashi <neobuddy89@gmail.com>");
MODULE_DESCRIPTION("MSM CPU Frequency Limiter Driver");
MODULE_LICENSE("GPL v2");
