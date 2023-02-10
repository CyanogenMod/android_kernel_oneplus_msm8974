#ifndef _LINUX_USER_NAMESPACE_H
#define _LINUX_USER_NAMESPACE_H

#include <linux/kref.h>
#include <linux/nsproxy.h>
#include <linux/sched.h>
#include <linux/err.h>

#define UID_GID_MAP_MAX_EXTENTS 5

struct uid_gid_map {	/* 64 bytes -- 1 cache line */
	u32 nr_extents;
	struct uid_gid_extent {
		u32 first;
		u32 lower_first;
		u32 count;
	} extent[UID_GID_MAP_MAX_EXTENTS];
};

struct user_namespace {
	struct uid_gid_map	uid_map;
	struct uid_gid_map	gid_map;
	struct kref		kref;
	struct user_namespace	*parent;
	kuid_t			owner;
	kgid_t			group;
	unsigned int		proc_inum;
};

extern struct user_namespace init_user_ns;

#ifdef CONFIG_USER_NS

static inline struct user_namespace *get_user_ns(struct user_namespace *ns)
{
	if (ns)
		kref_get(&ns->kref);
	return ns;
}

extern int create_user_ns(struct cred *new);
extern void free_user_ns(struct kref *kref);

static inline void put_user_ns(struct user_namespace *ns)
{
	if (ns)
		kref_put(&ns->kref, free_user_ns);
}

struct seq_operations;
extern struct seq_operations proc_uid_seq_operations;
extern struct seq_operations proc_gid_seq_operations;
extern ssize_t proc_uid_map_write(struct file *, const char __user *, size_t, loff_t *);
extern ssize_t proc_gid_map_write(struct file *, const char __user *, size_t, loff_t *);
#else

static inline struct user_namespace *get_user_ns(struct user_namespace *ns)
{
	return &init_user_ns;
}

static inline int create_user_ns(struct cred *new)
{
	return -EINVAL;
}

static inline void put_user_ns(struct user_namespace *ns)
{
}

#endif

static inline uid_t user_ns_map_uid(struct user_namespace *to,
	const struct cred *cred, uid_t uid)
{
	return from_kuid_munged(to, make_kuid(cred->user_ns, uid));
}

static inline gid_t user_ns_map_gid(struct user_namespace *to,
	const struct cred *cred, gid_t gid)
{
	return from_kgid_munged(to, make_kgid(cred->user_ns, gid));
}

#endif /* _LINUX_USER_H */
