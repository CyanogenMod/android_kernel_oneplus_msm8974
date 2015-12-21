/* linux/arch/arm/mach-msm/msm_zen_decision.c
 *
 * In-kernel solution to replace CPU hotplug work that breaks from
 * disabling certain MSM userspace applications.
 *
 * Copyright (c) 2015 Brandon Berhent
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>

#define ZEN_DECISION "zen_decision"

/*
 * Enable/Disable driver
 */
unsigned int enabled = 0;

/*
 * How long to wait to enable cores on wake (in ms)
 */
#define WAKE_WAIT_TIME_MAX 60000 // 1 minute maximum
unsigned int wake_wait_time = 1000;

/*
 * Battery level threshold to ignore UP operations.
 * Only do CPU_UP work when battery level is above this value.
 *
 * Setting to 0 will do CPU_UP work regardless of battery level.
 */
unsigned int bat_threshold_ignore = 15;

/* FB Notifier */
static struct notifier_block fb_notifier;

/* Worker Stuff */
static struct workqueue_struct *zen_wake_wq;
static struct delayed_work wake_work;

/* Sysfs stuff */
struct kobject *zendecision_kobj;

/* Power supply information */
static struct power_supply *psy;
union power_supply_propval current_charge;

/*
 * Some devices may have a different name power_supply device representing the battery.
 *
 * If we can't find the "battery" device, then we ignore all battery work, which means
 * we do the CPU_UP work regardless of the battery level.
 */
const char ps_name[] = "battery";

static int get_power_supply_level(void)
{
	int ret;
	if (!psy) {
		ret = -ENXIO;
		return ret;
	}

	/*
	 * On at least some MSM devices POWER_SUPPLY_PROP_CAPACITY represents current
	 * battery level as an integer between 0 and 100.
	 *
	 * Unknown if other devices use this property or a different one.
	 */
	ret = psy->get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &current_charge);
	if (ret)
		return ret;

	return current_charge.intval;
}

/*
 * msm_zd_online_cpus
 *
 * Core wake work function.
 * Brings all CPUs online. Called from worker thread.
 */
static void __ref msm_zd_online_all_cpus(struct work_struct *work)
{
	int cpu;

	for_each_cpu_not(cpu, cpu_online_mask) {
		cpu_up(cpu);
	}
}

/*
 * msm_zd_queue_online_work
 *
 * Call msm_zd_online_all_cpus as a delayed worker thread on wake_wq.
 * Delayed by wake_wait_time.
 */
static void msm_zd_queue_online_work(void)
{
	queue_delayed_work(zen_wake_wq, &wake_work,
			msecs_to_jiffies(wake_wait_time));
}

/** Use FB notifiers to detect screen off/on and do the work **/
static int fb_notifier_callback(struct notifier_block *nb,
	unsigned long event, void *data)
{
	int *blank;
	struct fb_event *evdata = data;

	/* If driver is disabled just leave here */
	if (!enabled)
		return 0;

	/* Clear wake workqueue of any pending threads */
	flush_workqueue(zen_wake_wq);
	cancel_delayed_work_sync(&wake_work);

	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK) {
			/* Always queue work if PS device doesn't exist or bat_threshold_ignore == 0 */
			if (psy && bat_threshold_ignore) {
				/* If current level > ignore threshold, then queue UP work */
				if (get_power_supply_level() > bat_threshold_ignore)
					msm_zd_queue_online_work();
			} else
				msm_zd_queue_online_work();
		}
	}

	return 0;
}

/* Sysfs Start */
static ssize_t enable_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", enabled);
}

static ssize_t enable_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	int ret;
	unsigned long new_val;

	ret = kstrtoul(buf, 0, &new_val);
	if (ret < 0)
		return ret;

	if (new_val > 0)
		enabled = 1;
	else
		enabled = 0;

	return size;
}

static ssize_t wake_delay_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", wake_wait_time);
}

static ssize_t wake_delay_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	int ret;
	unsigned long new_val;
	ret = kstrtoul(buf, 0, &new_val);
	if (ret < 0)
		return ret;

	/* Restrict value between 0 and WAKE_WAIT_TIME_MAX */
	wake_wait_time = new_val > WAKE_WAIT_TIME_MAX ? WAKE_WAIT_TIME_MAX : new_val;

	return size;
}

static ssize_t bat_threshold_ignore_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", bat_threshold_ignore);
}

static ssize_t bat_threshold_ignore_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	int ret;
	unsigned long new_val;
	ret = kstrtoul(buf, 0, &new_val);
	if (ret < 0)
		return ret;

	/* Restrict between 0 and 100 */
	if (new_val > 100)
		bat_threshold_ignore = 100;
	else
		bat_threshold_ignore = new_val;

	return size;
}

static struct kobj_attribute kobj_enabled =
	__ATTR(enabled, 0644, enable_show,
		enable_store);

static struct kobj_attribute kobj_wake_wait =
	__ATTR(wake_wait_time, 0644, wake_delay_show,
		wake_delay_store);

static struct kobj_attribute kobj_bat_threshold_ignore =
	__ATTR(bat_threshold_ignore, 0644, bat_threshold_ignore_show,
		bat_threshold_ignore_store);

static struct attribute *zd_attrs[] = {
	&kobj_enabled.attr,
	&kobj_wake_wait.attr,
	&kobj_bat_threshold_ignore.attr,
	NULL,
};

static struct attribute_group zd_option_group = {
	.attrs = zd_attrs,
};

/* Sysfs End */

static int zd_probe(struct platform_device *pdev)
{
	int ret;

	/* Setup sysfs */
	zendecision_kobj = kobject_create_and_add("zen_decision", kernel_kobj);
	if (zendecision_kobj == NULL) {
		pr_err("[%s]: subsystem register failed. \n", ZEN_DECISION);
		return -ENOMEM;
	}

	ret = sysfs_create_group(zendecision_kobj, &zd_option_group);
	if (ret) {
		pr_info("[%s]: sysfs interface failed to initialize\n", ZEN_DECISION);
		return -EINVAL;
	}

	/* Setup Workqueues */
	zen_wake_wq = alloc_workqueue("zen_wake_wq", WQ_FREEZABLE | WQ_UNBOUND, 1);
	if (!zen_wake_wq) {
		pr_err("[%s]: Failed to allocate suspend workqueue\n", ZEN_DECISION);
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&wake_work, msm_zd_online_all_cpus);

	/* Setup FB Notifier */
	fb_notifier.notifier_call = fb_notifier_callback;
	if (fb_register_client(&fb_notifier)) {
		pr_err("[%s]: failed to register FB notifier\n", ZEN_DECISION);
		return -ENOMEM;
	}

	/* Setup power supply */
	psy = power_supply_get_by_name(ps_name);
	// We can continue without finding PS info, print debug info
	if (!psy)
		pr_warn("[%s]: power supply '%s' not found, continuing without \n", ZEN_DECISION, ps_name);
	else
		pr_info("[%s]: power supply '%s' found\n", ZEN_DECISION, ps_name);

	/* Everything went well, lets say we loaded successfully */
	pr_info("[%s]: driver initialized successfully \n", ZEN_DECISION);

	return ret;
}

static int zd_remove(struct platform_device *pdev)
{
	kobject_put(zendecision_kobj);

	flush_workqueue(zen_wake_wq);
	cancel_delayed_work_sync(&wake_work);
	destroy_workqueue(zen_wake_wq);

	fb_unregister_client(&fb_notifier);
	fb_notifier.notifier_call = NULL;

	return 0;
}

static struct platform_driver zd_driver = {
	.probe = zd_probe,
	.remove = zd_remove,
	.driver = {
		.name = ZEN_DECISION,
		.owner = THIS_MODULE,
	}
};

static struct platform_device zd_device = {
	.name = ZEN_DECISION,
	.id = -1
};

static int __init zd_init(void)
{
	int ret = platform_driver_register(&zd_driver);
	if (ret)
		pr_err("[%s]: platform_driver_register failed: %d\n", ZEN_DECISION, ret);
	else
		pr_info("[%s]: platform_driver_register succeeded\n", ZEN_DECISION);


	ret = platform_device_register(&zd_device);
	if (ret)
		pr_err("[%s]: platform_device_register failed: %d\n", ZEN_DECISION, ret);
	else
		pr_info("[%s]: platform_device_register succeeded\n", ZEN_DECISION);

	return ret;
}

static void __exit zd_exit(void)
{
	platform_driver_unregister(&zd_driver);
	platform_device_unregister(&zd_device);
}

late_initcall(zd_init);
module_exit(zd_exit);

MODULE_VERSION("2.0");
MODULE_DESCRIPTION("Zen Decision - Kernel MSM Userspace Handler");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Brandon Berhent <bbedward@gmail.com>");
