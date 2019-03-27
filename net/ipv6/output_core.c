#include <linux/export.h>
#include <linux/skbuff.h>
#include <net/ip.h>
#include <net/ipv6.h>

/* This function exists only for tap drivers that must support broken
 * clients requesting UFO without specifying an IPv6 fragment ID.
 *
 * This is similar to ipv6_select_ident() but we use an independent hash
 * seed to limit information leakage.
 */
void ipv6_proxy_select_ident(struct net *net, struct sk_buff *skb)
{
	struct in6_addr buf[2];
	struct in6_addr *addrs;
	u32 hash, id;

	addrs = skb_header_pointer(skb,
				   skb_network_offset(skb) +
				   offsetof(struct ipv6hdr, saddr),
				   sizeof(buf), buf);
	if (addrs)
	{
		const struct {
			struct in6_addr dst;
			struct in6_addr src;
		} __aligned(SIPHASH_ALIGNMENT) combined = {
			.dst = addrs[1],
			.src = addrs[0],
		};

		/* Note the following code is not safe, but this is okay. */
		if (unlikely(siphash_key_is_zero(&net->ipv4.ip_id_key)))
			get_random_bytes(&net->ipv4.ip_id_key,
					 sizeof(net->ipv4.ip_id_key));

		hash = siphash(&combined, sizeof(combined), &net->ipv4.ip_id_key);

		id = ip_idents_reserve(hash, 1);
		skb_shinfo(skb)->ip6_frag_id = htonl(id);
	}
}
EXPORT_SYMBOL_GPL(ipv6_proxy_select_ident);
