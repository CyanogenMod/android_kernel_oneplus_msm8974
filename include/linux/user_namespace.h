#ifndef _LINUX_USER_NAMESPACE_H
#define _LINUX_USER_NAMESPACE_H

#include <linux/kref.h>
#include <linux/nsproxy.h>
#include <linux/sched.h>
#include <linux/err.h>

struct user_namespace {
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

uid_t user_ns_map_uid(struct user_namespace *to, const struct cred *cred, uid_t uid);
gid_t user_ns_map_gid(struct user_namespace *to, const struct cred *cred, gid_t gid);

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

static inline uid_t user_ns_map_uid(struct user_namespace *to,
	const struct cred *cred, uid_t uid)
{
	return uid;
}
static inline gid_t user_ns_map_gid(struct user_namespace *to,
	const struct cred *cred, gid_t gid)
{
	return gid;
}

#endif

#endif /* _LINUX_USER_H */
