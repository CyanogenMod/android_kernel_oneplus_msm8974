#ifndef _COMPAT_NET_NET_NAMESPACE_H
#define _COMPAT_NET_NET_NAMESPACE_H 1

#include_next <net/net_namespace.h>

#if IS_ENABLED(CONFIG_BACKPORT_IEEE802154_6LOWPAN)
#include <linux/version.h>
#include <net/netns/ieee802154_6lowpan.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
/*
 * we provide backport for 6lowpan as per the dependencies file
 * down to 3.5 only.
 */
extern struct netns_ieee802154_lowpan ieee802154_lowpan;
struct netns_ieee802154_lowpan *net_ieee802154_lowpan(struct net *net);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
/* This can be removed once and if this gets upstream */
static inline struct netns_ieee802154_lowpan *
net_ieee802154_lowpan(struct net *net)
{
	return &net->ieee802154_lowpan;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0) */
#endif /* CONFIG_BACKPORT_IEEE802154_6LOWPAN */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,20,0)
/*
 * In older kernels we simply fail this function.
 */
#define get_net_ns_by_fd	LINUX_BACKPORT(get_net_ns_by_fd)
static inline struct net *get_net_ns_by_fd(int fd)
{
	return ERR_PTR(-EINVAL);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0)
typedef struct {
#ifdef CONFIG_NET_NS
	struct net *net;
#endif
} possible_net_t;

static inline void possible_write_pnet(possible_net_t *pnet, struct net *net)
{
#ifdef CONFIG_NET_NS
	pnet->net = net;
#endif
}

static inline struct net *possible_read_pnet(const possible_net_t *pnet)
{
#ifdef CONFIG_NET_NS
	return pnet->net;
#else
	return &init_net;
#endif
}
#else
#define possible_write_pnet(pnet, net) write_pnet(pnet, net)
#define possible_read_pnet(pnet) read_pnet(pnet)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0) */

#endif	/* _COMPAT_NET_NET_NAMESPACE_H */
