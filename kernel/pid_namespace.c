/*
 * Pid namespaces
 *
 * Authors:
 *    (C) 2007 Pavel Emelyanov <xemul@openvz.org>, OpenVZ, SWsoft Inc.
 *    (C) 2007 Sukadev Bhattiprolu <sukadev@us.ibm.com>, IBM
 *     Many thanks to Oleg Nesterov for comments and help
 *
 */

#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/syscalls.h>
#include <linux/err.h>
#include <linux/acct.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/reboot.h>

#define BITS_PER_PAGE		(PAGE_SIZE*8)

struct pid_cache {
	int nr_ids;
	char name[16];
	struct kmem_cache *cachep;
	struct list_head list;
};

static LIST_HEAD(pid_caches_lh);
static DEFINE_MUTEX(pid_caches_mutex);
static struct kmem_cache *pid_ns_cachep;

/*
 * creates the kmem cache to allocate pids from.
 * @nr_ids: the number of numerical ids this pid will have to carry
 */

static struct kmem_cache *create_pid_cachep(int nr_ids)
{
	struct pid_cache *pcache;
	struct kmem_cache *cachep;

	mutex_lock(&pid_caches_mutex);
	list_for_each_entry(pcache, &pid_caches_lh, list)
		if (pcache->nr_ids == nr_ids)
			goto out;

	pcache = kmalloc(sizeof(struct pid_cache), GFP_KERNEL);
	if (pcache == NULL)
		goto err_alloc;

	snprintf(pcache->name, sizeof(pcache->name), "pid_%d", nr_ids);
	cachep = kmem_cache_create(pcache->name,
			sizeof(struct pid) + (nr_ids - 1) * sizeof(struct upid),
			0, SLAB_HWCACHE_ALIGN, NULL);
	if (cachep == NULL)
		goto err_cachep;

	pcache->nr_ids = nr_ids;
	pcache->cachep = cachep;
	list_add(&pcache->list, &pid_caches_lh);
out:
	mutex_unlock(&pid_caches_mutex);
	return pcache->cachep;

err_cachep:
	kfree(pcache);
err_alloc:
	mutex_unlock(&pid_caches_mutex);
	return NULL;
}

static struct pid_namespace *create_pid_namespace(struct pid_namespace *parent_pid_ns)
{
	struct pid_namespace *ns;
	unsigned int level = parent_pid_ns->level + 1;
	int i, err = -ENOMEM;

	ns = kmem_cache_zalloc(pid_ns_cachep, GFP_KERNEL);
	if (ns == NULL)
		goto out;

	ns->pidmap[0].page = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!ns->pidmap[0].page)
		goto out_free;

	ns->pid_cachep = create_pid_cachep(level + 1);
	if (ns->pid_cachep == NULL)
		goto out_free_map;

	err = proc_alloc_inum(&ns->proc_inum);
	if (err)
		goto out_free_map;

	kref_init(&ns->kref);
	ns->level = level;
	ns->parent = get_pid_ns(parent_pid_ns);

	set_bit(0, ns->pidmap[0].page);
	atomic_set(&ns->pidmap[0].nr_free, BITS_PER_PAGE - 1);

	for (i = 1; i < PIDMAP_ENTRIES; i++)
		atomic_set(&ns->pidmap[i].nr_free, BITS_PER_PAGE);

	err = pid_ns_prepare_proc(ns);
	if (err)
		goto out_put_parent_pid_ns;

	return ns;

out_put_parent_pid_ns:
	put_pid_ns(parent_pid_ns);
out_free_map:
	kfree(ns->pidmap[0].page);
out_free:
	kmem_cache_free(pid_ns_cachep, ns);
out:
	return ERR_PTR(err);
}

static void destroy_pid_namespace(struct pid_namespace *ns)
{
	int i;

	proc_free_inum(ns->proc_inum);
	for (i = 0; i < PIDMAP_ENTRIES; i++)
		kfree(ns->pidmap[i].page);
	kmem_cache_free(pid_ns_cachep, ns);
}

struct pid_namespace *copy_pid_ns(unsigned long flags, struct pid_namespace *old_ns)
{
	if (!(flags & CLONE_NEWPID))
		return get_pid_ns(old_ns);
	if (flags & (CLONE_THREAD|CLONE_PARENT))
		return ERR_PTR(-EINVAL);
	return create_pid_namespace(old_ns);
}

void free_pid_ns(struct kref *kref)
{
	struct pid_namespace *ns, *parent;

	ns = container_of(kref, struct pid_namespace, kref);

	parent = ns->parent;
	destroy_pid_namespace(ns);

	if (parent != NULL)
		put_pid_ns(parent);
}

void zap_pid_ns_processes(struct pid_namespace *pid_ns)
{
	int nr;
	int rc;
	struct task_struct *task;

	/*
	 * The last thread in the cgroup-init thread group is terminating.
	 * Find remaining pid_ts in the namespace, signal and wait for them
	 * to exit.
	 *
	 * Note:  This signals each threads in the namespace - even those that
	 * 	  belong to the same thread group, To avoid this, we would have
	 * 	  to walk the entire tasklist looking a processes in this
	 * 	  namespace, but that could be unnecessarily expensive if the
	 * 	  pid namespace has just a few processes. Or we need to
	 * 	  maintain a tasklist for each pid namespace.
	 *
	 */
	read_lock(&tasklist_lock);
	nr = next_pidmap(pid_ns, 1);
	while (nr > 0) {
		rcu_read_lock();

		task = pid_task(find_vpid(nr), PIDTYPE_PID);
		if (task && !__fatal_signal_pending(task))
			send_sig_info(SIGKILL, SEND_SIG_FORCED, task);

		rcu_read_unlock();

		nr = next_pidmap(pid_ns, nr);
	}
	read_unlock(&tasklist_lock);

	do {
		clear_thread_flag(TIF_SIGPENDING);
		rc = sys_wait4(-1, NULL, __WALL, NULL);
	} while (rc != -ECHILD);

	if (pid_ns->reboot)
		current->signal->group_exit_code = pid_ns->reboot;

	acct_exit_ns(pid_ns);
	return;
}

static int pid_ns_ctl_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table tmp = *table;

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * Writing directly to ns' last_pid field is OK, since this field
	 * is volatile in a living namespace anyway and a code writing to
	 * it should synchronize its usage with external means.
	 */

	tmp.data = &current->nsproxy->pid_ns->last_pid;
	return proc_dointvec(&tmp, write, buffer, lenp, ppos);
}

static struct ctl_table pid_ns_ctl_table[] = {
	{
		.procname = "ns_last_pid",
		.maxlen = sizeof(int),
		.mode = 0666, /* permissions are checked in the handler */
		.proc_handler = pid_ns_ctl_handler,
	},
	{ }
};

static struct ctl_path kern_path[] = { { .procname = "kernel", }, { } };

int reboot_pid_ns(struct pid_namespace *pid_ns, int cmd)
{
	if (pid_ns == &init_pid_ns)
		return 0;

	switch (cmd) {
	case LINUX_REBOOT_CMD_RESTART2:
	case LINUX_REBOOT_CMD_RESTART:
		pid_ns->reboot = SIGHUP;
		break;

	case LINUX_REBOOT_CMD_POWER_OFF:
	case LINUX_REBOOT_CMD_HALT:
		pid_ns->reboot = SIGINT;
		break;
	default:
		return -EINVAL;
	}

	read_lock(&tasklist_lock);
	force_sig(SIGKILL, pid_ns->child_reaper);
	read_unlock(&tasklist_lock);

	do_exit(0);

	/* Not reached */
	return 0;
}

static __init int pid_namespaces_init(void)
{
	pid_ns_cachep = KMEM_CACHE(pid_namespace, SLAB_PANIC);
	register_sysctl_paths(kern_path, pid_ns_ctl_table);
	return 0;
}

__initcall(pid_namespaces_init);
