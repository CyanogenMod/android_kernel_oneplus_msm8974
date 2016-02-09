/*
 * CPU subsystem support
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/device.h>
#include <linux/node.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/percpu.h>

#include "base.h"

#define ONL_CONT_MODE_SYSFS 	0	// core online status controlled by sysfs (mpdecision)
#define ONL_CONT_MODE_ONLINE 	1	// core is forced online
#define ONL_CONT_MODE_OFFLINE	2	// core is forced offline 
#define ONL_CONT_MODE_LOCK4_3	3	// core 4 is forced offline but locked to core 3 (only allowed for core 4 !)
#define ID_CPU_CORE_3			2	// internal id of CPU core 3
#define ID_CPU_CORE_4			3	// internal id of CPU core 4

int online_control_mode[4] = {ONL_CONT_MODE_SYSFS, 
							  ONL_CONT_MODE_SYSFS, 
							  ONL_CONT_MODE_SYSFS, 
							  ONL_CONT_MODE_SYSFS};

struct bus_type cpu_subsys = {
	.name = "cpu",
	.dev_name = "cpu",
};
EXPORT_SYMBOL_GPL(cpu_subsys);

static DEFINE_PER_CPU(struct device *, cpu_sys_devices);

#ifdef CONFIG_HOTPLUG_CPU
static ssize_t show_online(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);

	return sprintf(buf, "%u\n", !!cpu_online(cpu->dev.id));
}

static ssize_t __ref store_online(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);
	struct device *dev3;
	ssize_t ret;

	cpu_hotplug_driver_lock();
	switch (buf[0]) {
	case '0':
		ret = cpu_down(cpu->dev.id);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
		// handling if core lock4_3 is active for fourth core
		if ((cpu->dev.id == ID_CPU_CORE_3) && 
			(online_control_mode[ID_CPU_CORE_4] == ONL_CONT_MODE_LOCK4_3))
		{
			dev3 = get_cpu_device(ID_CPU_CORE_4);
			ret = cpu_down(ID_CPU_CORE_4);
			if (!ret)
				kobject_uevent(&dev3->kobj, KOBJ_OFFLINE);
		}
		break;
	case '1':
		ret = cpu_up(cpu->dev.id);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_ONLINE);

		// handling if core lock4_3 is active for fourth core
		if ((cpu->dev.id == ID_CPU_CORE_3) && 
			(online_control_mode[3] == ONL_CONT_MODE_LOCK4_3))
		{
			dev3 = get_cpu_device(ID_CPU_CORE_4);
			ret = cpu_up(ID_CPU_CORE_4);
			if (!ret)
				kobject_uevent(&dev3->kobj, KOBJ_ONLINE);
		}
		break;
	default:
		ret = -EINVAL;
	}
	cpu_hotplug_driver_unlock();

	if (ret >= 0)
		ret = count;
	return ret;
}
static DEVICE_ATTR(online, 0644, show_online, store_online);

static ssize_t show_online_control(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	
	struct cpu *cpu = container_of(dev, struct cpu, dev);

	switch (online_control_mode[cpu->dev.id])
	{
		case ONL_CONT_MODE_SYSFS:
			return sprintf(buf, "0: sysfs controlled\n");
			break;
		case ONL_CONT_MODE_ONLINE:
			return sprintf(buf, "1: forced online\n");
			break;
		case ONL_CONT_MODE_OFFLINE:
			return sprintf(buf, "2: forced offline\n");
			break;
	}
	
	return sprintf(buf, "Core online control invalid status\n");
}

static ssize_t __ref store_online_control(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);
	ssize_t ret;

	cpu_hotplug_driver_lock();
	switch (buf[0]) 
	{
		case '0': // control via sysfs
			ret = cpu_down(cpu->dev.id);
			if (!ret)
				kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
			online_control_mode[cpu->dev.id] = ONL_CONT_MODE_SYSFS;
			break;
			
		case '1': // forced online
			ret = cpu_up(cpu->dev.id);
			if (!ret)
				kobject_uevent(&dev->kobj, KOBJ_ONLINE);
			online_control_mode[cpu->dev.id] = ONL_CONT_MODE_ONLINE;
			break;
			
		case '2': // forced offline
			ret = cpu_down(cpu->dev.id);
			if (!ret)
				kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
			online_control_mode[cpu->dev.id] = ONL_CONT_MODE_OFFLINE;
			break;
			
		case '3': // only allowed for CPU core 4 - force offline but lock it to core 3
			if (cpu->dev.id == ID_CPU_CORE_4)
			{
				ret = cpu_down(cpu->dev.id);
				if (!ret)
					kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
				online_control_mode[cpu->dev.id] = ONL_CONT_MODE_LOCK4_3;
			}
			else
				ret = -EINVAL;
			break;
			
		default:
			ret = -EINVAL;
	}
	cpu_hotplug_driver_unlock();

	if (ret >= 0)
		ret = count;
	return ret;
}
static DEVICE_ATTR(online_control, 0644, show_online_control, store_online_control);

static void __cpuinit register_cpu_control(struct cpu *cpu)
{
	device_create_file(&cpu->dev, &dev_attr_online);
}
void unregister_cpu(struct cpu *cpu)
{
	int logical_cpu = cpu->dev.id;

	unregister_cpu_under_node(logical_cpu, cpu_to_node(logical_cpu));

	device_remove_file(&cpu->dev, &dev_attr_online);

	device_unregister(&cpu->dev);
	per_cpu(cpu_sys_devices, logical_cpu) = NULL;
	return;
}

#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE
static ssize_t cpu_probe_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	return arch_cpu_probe(buf, count);
}

static ssize_t cpu_release_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	return arch_cpu_release(buf, count);
}

static DEVICE_ATTR(probe, S_IWUSR, NULL, cpu_probe_store);
static DEVICE_ATTR(release, S_IWUSR, NULL, cpu_release_store);
#endif /* CONFIG_ARCH_CPU_PROBE_RELEASE */

#else /* ... !CONFIG_HOTPLUG_CPU */
static inline void register_cpu_control(struct cpu *cpu)
{
}
#endif /* CONFIG_HOTPLUG_CPU */

#ifdef CONFIG_KEXEC
#include <linux/kexec.h>

static ssize_t show_crash_notes(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);
	ssize_t rc;
	unsigned long long addr;
	int cpunum;

	cpunum = cpu->dev.id;

	/*
	 * Might be reading other cpu's data based on which cpu read thread
	 * has been scheduled. But cpu data (memory) is allocated once during
	 * boot up and this data does not change there after. Hence this
	 * operation should be safe. No locking required.
	 */
	addr = per_cpu_ptr_to_phys(per_cpu_ptr(crash_notes, cpunum));
	rc = sprintf(buf, "%Lx\n", addr);
	return rc;
}
static DEVICE_ATTR(crash_notes, 0400, show_crash_notes, NULL);
#endif

/*
 * Print cpu online, possible, present, and system maps
 */

struct cpu_attr {
	struct device_attribute attr;
	const struct cpumask *const * const map;
};

static ssize_t show_cpus_attr(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct cpu_attr *ca = container_of(attr, struct cpu_attr, attr);
	int n = cpulist_scnprintf(buf, PAGE_SIZE-2, *(ca->map));

	buf[n++] = '\n';
	buf[n] = '\0';
	return n;
}

#define _CPU_ATTR(name, map) \
	{ __ATTR(name, 0444, show_cpus_attr, NULL), map }

/* Keep in sync with cpu_subsys_attrs */
static struct cpu_attr cpu_attrs[] = {
	_CPU_ATTR(online, &cpu_online_mask),
	_CPU_ATTR(possible, &cpu_possible_mask),
	_CPU_ATTR(present, &cpu_present_mask),
};

/*
 * Print values for NR_CPUS and offlined cpus
 */
static ssize_t print_cpus_kernel_max(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int n = snprintf(buf, PAGE_SIZE-2, "%d\n", NR_CPUS - 1);
	return n;
}
static DEVICE_ATTR(kernel_max, 0444, print_cpus_kernel_max, NULL);

/* arch-optional setting to enable display of offline cpus >= nr_cpu_ids */
unsigned int total_cpus;

static ssize_t print_cpus_offline(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int n = 0, len = PAGE_SIZE-2;
	cpumask_var_t offline;

	/* display offline cpus < nr_cpu_ids */
	if (!alloc_cpumask_var(&offline, GFP_KERNEL))
		return -ENOMEM;
	cpumask_andnot(offline, cpu_possible_mask, cpu_online_mask);
	n = cpulist_scnprintf(buf, len, offline);
	free_cpumask_var(offline);

	/* display offline cpus >= nr_cpu_ids */
	if (total_cpus && nr_cpu_ids < total_cpus) {
		if (n && n < len)
			buf[n++] = ',';

		if (nr_cpu_ids == total_cpus-1)
			n += snprintf(&buf[n], len - n, "%d", nr_cpu_ids);
		else
			n += snprintf(&buf[n], len - n, "%d-%d",
						      nr_cpu_ids, total_cpus-1);
	}

	n += snprintf(&buf[n], len - n, "\n");
	return n;
}
static DEVICE_ATTR(offline, 0444, print_cpus_offline, NULL);

static void cpu_device_release(struct device *dev)
{
	/*
	 * This is an empty function to prevent the driver core from spitting a
	 * warning at us.  Yes, I know this is directly opposite of what the
	 * documentation for the driver core and kobjects say, and the author
	 * of this code has already been publically ridiculed for doing
	 * something as foolish as this.  However, at this point in time, it is
	 * the only way to handle the issue of statically allocated cpu
	 * devices.  The different architectures will have their cpu device
	 * code reworked to properly handle this in the near future, so this
	 * function will then be changed to correctly free up the memory held
	 * by the cpu device.
	 *
	 * Never copy this way of doing things, or you too will be made fun of
	 * on the linux-kerenl list, you have been warned.
	 */
}

/*
 * register_cpu - Setup a sysfs device for a CPU.
 * @cpu - cpu->hotpluggable field set to 1 will generate a control file in
 *	  sysfs for this CPU.
 * @num - CPU number to use when creating the device.
 *
 * Initialize and register the CPU device.
 */
int __cpuinit register_cpu(struct cpu *cpu, int num)
{
	int error;

	cpu->node_id = cpu_to_node(num);
	memset(&cpu->dev, 0x00, sizeof(struct device));
	cpu->dev.id = num;
	cpu->dev.bus = &cpu_subsys;
	cpu->dev.release = cpu_device_release;
#ifdef CONFIG_ARCH_HAS_CPU_AUTOPROBE
	cpu->dev.bus->uevent = arch_cpu_uevent;
#endif
	error = device_register(&cpu->dev);
	if (!error && cpu->hotpluggable)
		register_cpu_control(cpu);
	if (!error)
		per_cpu(cpu_sys_devices, num) = &cpu->dev;
	if (!error)
		register_cpu_under_node(num, cpu_to_node(num));

#ifdef CONFIG_KEXEC
	if (!error)
		error = device_create_file(&cpu->dev, &dev_attr_crash_notes);
#endif
	return error;
}

struct device *get_cpu_device(unsigned cpu)
{
	if (cpu < nr_cpu_ids && cpu_possible(cpu))
		return per_cpu(cpu_sys_devices, cpu);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(get_cpu_device);

#ifdef CONFIG_ARCH_HAS_CPU_AUTOPROBE
static DEVICE_ATTR(modalias, 0444, arch_print_cpu_modalias, NULL);
#endif

static struct attribute *cpu_root_attrs[] = {
#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE
	&dev_attr_probe.attr,
	&dev_attr_release.attr,
#endif
	&cpu_attrs[0].attr.attr,
	&cpu_attrs[1].attr.attr,
	&cpu_attrs[2].attr.attr,
	&dev_attr_kernel_max.attr,
	&dev_attr_offline.attr,
#ifdef CONFIG_ARCH_HAS_CPU_AUTOPROBE
	&dev_attr_modalias.attr,
#endif
	NULL
};

static struct attribute_group cpu_root_attr_group = {
	.attrs = cpu_root_attrs,
};

static const struct attribute_group *cpu_root_attr_groups[] = {
	&cpu_root_attr_group,
	NULL,
};

bool cpu_is_hotpluggable(unsigned cpu)
{
	struct device *dev = get_cpu_device(cpu);
	return dev && container_of(dev, struct cpu, dev)->hotpluggable;
}
EXPORT_SYMBOL_GPL(cpu_is_hotpluggable);

#ifdef CONFIG_GENERIC_CPU_DEVICES
static DEFINE_PER_CPU(struct cpu, cpu_devices);
#endif

static void __init cpu_dev_register_generic(void)
{
#ifdef CONFIG_GENERIC_CPU_DEVICES
	int i;

	for_each_possible_cpu(i) {
		if (register_cpu(&per_cpu(cpu_devices, i), i))
			panic("Failed to register CPU device");
	}
#endif
}

void __init cpu_dev_init(void)
{
	if (subsys_system_register(&cpu_subsys, cpu_root_attr_groups))
		panic("Failed to register CPU subsystem");

	cpu_dev_register_generic();

#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
	sched_create_sysfs_power_savings_entries(cpu_subsys.dev_root);
#endif
}
