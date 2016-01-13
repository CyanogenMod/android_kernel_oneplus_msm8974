/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * MMU support
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "irq.h"
#include "mmu.h"
#include "x86.h"
#include "kvm_cache_regs.h"

#include <linux/kvm_host.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/hugetlb.h>
#include <linux/compiler.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <asm/page.h>
#include <asm/cmpxchg.h>
#include <asm/io.h>
#include <asm/vmx.h>

/*
 * When setting this variable to true it enables Two-Dimensional-Paging
 * where the hardware walks 2 page tables:
 * 1. the guest-virtual to guest-physical
 * 2. while doing 1. it walks guest-physical to host-physical
 * If the hardware supports that we don't need to do shadow paging.
 */
bool tdp_enabled = false;

enum {
	AUDIT_PRE_PAGE_FAULT,
	AUDIT_POST_PAGE_FAULT,
	AUDIT_PRE_PTE_WRITE,
	AUDIT_POST_PTE_WRITE,
	AUDIT_PRE_SYNC,
	AUDIT_POST_SYNC
};

#undef MMU_DEBUG

#ifdef MMU_DEBUG

#define pgprintk(x...) do { if (dbg) printk(x); } while (0)
#define rmap_printk(x...) do { if (dbg) printk(x); } while (0)

#else

#define pgprintk(x...) do { } while (0)
#define rmap_printk(x...) do { } while (0)

#endif

#ifdef MMU_DEBUG
static bool dbg = 0;
module_param(dbg, bool, 0644);
#endif

#ifndef MMU_DEBUG
#define ASSERT(x) do { } while (0)
#else
#define ASSERT(x)							\
	if (!(x)) {							\
		printk(KERN_WARNING "assertion failed %s:%d: %s\n",	\
		       __FILE__, __LINE__, #x);				\
	}
#endif

#define PTE_PREFETCH_NUM		8

#define PT_FIRST_AVAIL_BITS_SHIFT 9
#define PT64_SECOND_AVAIL_BITS_SHIFT 52

#define PT64_LEVEL_BITS 9

#define PT64_LEVEL_SHIFT(level) \
		(PAGE_SHIFT + (level - 1) * PT64_LEVEL_BITS)

#define PT64_INDEX(address, level)\
	(((address) >> PT64_LEVEL_SHIFT(level)) & ((1 << PT64_LEVEL_BITS) - 1))


#define PT32_LEVEL_BITS 10

#define PT32_LEVEL_SHIFT(level) \
		(PAGE_SHIFT + (level - 1) * PT32_LEVEL_BITS)

#define PT32_LVL_OFFSET_MASK(level) \
	(PT32_BASE_ADDR_MASK & ((1ULL << (PAGE_SHIFT + (((level) - 1) \
						* PT32_LEVEL_BITS))) - 1))

#define PT32_INDEX(address, level)\
	(((address) >> PT32_LEVEL_SHIFT(level)) & ((1 << PT32_LEVEL_BITS) - 1))


#define PT64_BASE_ADDR_MASK (((1ULL << 52) - 1) & ~(u64)(PAGE_SIZE-1))
#define PT64_DIR_BASE_ADDR_MASK \
	(PT64_BASE_ADDR_MASK & ~((1ULL << (PAGE_SHIFT + PT64_LEVEL_BITS)) - 1))
#define PT64_LVL_ADDR_MASK(level) \
	(PT64_BASE_ADDR_MASK & ~((1ULL << (PAGE_SHIFT + (((level) - 1) \
						* PT64_LEVEL_BITS))) - 1))
#define PT64_LVL_OFFSET_MASK(level) \
	(PT64_BASE_ADDR_MASK & ((1ULL << (PAGE_SHIFT + (((level) - 1) \
						* PT64_LEVEL_BITS))) - 1))

#define PT32_BASE_ADDR_MASK PAGE_MASK
#define PT32_DIR_BASE_ADDR_MASK \
	(PAGE_MASK & ~((1ULL << (PAGE_SHIFT + PT32_LEVEL_BITS)) - 1))
#define PT32_LVL_ADDR_MASK(level) \
	(PAGE_MASK & ~((1ULL << (PAGE_SHIFT + (((level) - 1) \
					    * PT32_LEVEL_BITS))) - 1))

#define PT64_PERM_MASK (PT_PRESENT_MASK | PT_WRITABLE_MASK | PT_USER_MASK \
			| PT64_NX_MASK)

#define PTE_LIST_EXT 4

#define ACC_EXEC_MASK    1
#define ACC_WRITE_MASK   PT_WRITABLE_MASK
#define ACC_USER_MASK    PT_USER_MASK
#define ACC_ALL          (ACC_EXEC_MASK | ACC_WRITE_MASK | ACC_USER_MASK)

#include <trace/events/kvm.h>

#define CREATE_TRACE_POINTS
#include "mmutrace.h"

#define SPTE_HOST_WRITEABLE (1ULL << PT_FIRST_AVAIL_BITS_SHIFT)

#define SHADOW_PT_INDEX(addr, level) PT64_INDEX(addr, level)

struct pte_list_desc {
	u64 *sptes[PTE_LIST_EXT];
	struct pte_list_desc *more;
};

struct kvm_shadow_walk_iterator {
	u64 addr;
	hpa_t shadow_addr;
	u64 *sptep;
	int level;
	unsigned index;
};

#define for_each_shadow_entry(_vcpu, _addr, _walker)    \
	for (shadow_walk_init(&(_walker), _vcpu, _addr);	\
	     shadow_walk_okay(&(_walker));			\
	     shadow_walk_next(&(_walker)))

#define for_each_shadow_entry_lockless(_vcpu, _addr, _walker, spte)	\
	for (shadow_walk_init(&(_walker), _vcpu, _addr);		\
	     shadow_walk_okay(&(_walker)) &&				\
		({ spte = mmu_spte_get_lockless(_walker.sptep); 1; });	\
	     __shadow_walk_next(&(_walker), spte))

static struct kmem_cache *pte_list_desc_cache;
static struct kmem_cache *mmu_page_header_cache;
static struct percpu_counter kvm_total_used_mmu_pages;

static u64 __read_mostly shadow_nx_mask;
static u64 __read_mostly shadow_x_mask;	/* mutual exclusive with nx_mask */
static u64 __read_mostly shadow_user_mask;
static u64 __read_mostly shadow_accessed_mask;
static u64 __read_mostly shadow_dirty_mask;
static u64 __read_mostly shadow_mmio_mask;

static void mmu_spte_set(u64 *sptep, u64 spte);

void kvm_mmu_set_mmio_spte_mask(u64 mmio_mask)
{
	shadow_mmio_mask = mmio_mask;
}
EXPORT_SYMBOL_GPL(kvm_mmu_set_mmio_spte_mask);

static void mark_mmio_spte(u64 *sptep, u64 gfn, unsigned access)
{
	access &= ACC_WRITE_MASK | ACC_USER_MASK;

	trace_mark_mmio_spte(sptep, gfn, access);
	mmu_spte_set(sptep, shadow_mmio_mask | access | gfn << PAGE_SHIFT);
}

static bool is_mmio_spte(u64 spte)
{
	return (spte & shadow_mmio_mask) == shadow_mmio_mask;
}

static gfn_t get_mmio_spte_gfn(u64 spte)
{
	return (spte & ~shadow_mmio_mask) >> PAGE_SHIFT;
}

static unsigned get_mmio_spte_access(u64 spte)
{
	return (spte & ~shadow_mmio_mask) & ~PAGE_MASK;
}

static bool set_mmio_spte(u64 *sptep, gfn_t gfn, pfn_t pfn, unsigned access)
{
	if (unlikely(is_noslot_pfn(pfn))) {
		mark_mmio_spte(sptep, gfn, access);
		return true;
	}

	return false;
}

static inline u64 rsvd_bits(int s, int e)
{
	return ((1ULL << (e - s + 1)) - 1) << s;
}

void kvm_mmu_set_mask_ptes(u64 user_mask, u64 accessed_mask,
		u64 dirty_mask, u64 nx_mask, u64 x_mask)
{
	shadow_user_mask = user_mask;
	shadow_accessed_mask = accessed_mask;
	shadow_dirty_mask = dirty_mask;
	shadow_nx_mask = nx_mask;
	shadow_x_mask = x_mask;
}
EXPORT_SYMBOL_GPL(kvm_mmu_set_mask_ptes);

static int is_cpuid_PSE36(void)
{
	return 1;
}

static int is_nx(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.efer & EFER_NX;
}

static int is_shadow_present_pte(u64 pte)
{
	return pte & PT_PRESENT_MASK && !is_mmio_spte(pte);
}

static int is_large_pte(u64 pte)
{
	return pte & PT_PAGE_SIZE_MASK;
}

static int is_dirty_gpte(unsigned long pte)
{
	return pte & PT_DIRTY_MASK;
}

static int is_rmap_spte(u64 pte)
{
	return is_shadow_present_pte(pte);
}

static int is_last_spte(u64 pte, int level)
{
	if (level == PT_PAGE_TABLE_LEVEL)
		return 1;
	if (is_large_pte(pte))
		return 1;
	return 0;
}

static pfn_t spte_to_pfn(u64 pte)
{
	return (pte & PT64_BASE_ADDR_MASK) >> PAGE_SHIFT;
}

static gfn_t pse36_gfn_delta(u32 gpte)
{
	int shift = 32 - PT32_DIR_PSE36_SHIFT - PAGE_SHIFT;

	return (gpte & PT32_DIR_PSE36_MASK) << shift;
}

#ifdef CONFIG_X86_64
static void __set_spte(u64 *sptep, u64 spte)
{
	*sptep = spte;
}

static void __update_clear_spte_fast(u64 *sptep, u64 spte)
{
	*sptep = spte;
}

static u64 __update_clear_spte_slow(u64 *sptep, u64 spte)
{
	return xchg(sptep, spte);
}

static u64 __get_spte_lockless(u64 *sptep)
{
	return ACCESS_ONCE(*sptep);
}

static bool __check_direct_spte_mmio_pf(u64 spte)
{
	/* It is valid if the spte is zapped. */
	return spte == 0ull;
}
#else
union split_spte {
	struct {
		u32 spte_low;
		u32 spte_high;
	};
	u64 spte;
};

static void count_spte_clear(u64 *sptep, u64 spte)
{
	struct kvm_mmu_page *sp =  page_header(__pa(sptep));

	if (is_shadow_present_pte(spte))
		return;

	/* Ensure the spte is completely set before we increase the count */
	smp_wmb();
	sp->clear_spte_count++;
}

static void __set_spte(u64 *sptep, u64 spte)
{
	union split_spte *ssptep, sspte;

	ssptep = (union split_spte *)sptep;
	sspte = (union split_spte)spte;

	ssptep->spte_high = sspte.spte_high;

	/*
	 * If we map the spte from nonpresent to present, We should store
	 * the high bits firstly, then set present bit, so cpu can not
	 * fetch this spte while we are setting the spte.
	 */
	smp_wmb();

	ssptep->spte_low = sspte.spte_low;
}

static void __update_clear_spte_fast(u64 *sptep, u64 spte)
{
	union split_spte *ssptep, sspte;

	ssptep = (union split_spte *)sptep;
	sspte = (union split_spte)spte;

	ssptep->spte_low = sspte.spte_low;

	/*
	 * If we map the spte from present to nonpresent, we should clear
	 * present bit firstly to avoid vcpu fetch the old high bits.
	 */
	smp_wmb();

	ssptep->spte_high = sspte.spte_high;
	count_spte_clear(sptep, spte);
}

static u64 __update_clear_spte_slow(u64 *sptep, u64 spte)
{
	union split_spte *ssptep, sspte, orig;

	ssptep = (union split_spte *)sptep;
	sspte = (union split_spte)spte;

	/* xchg acts as a barrier before the setting of the high bits */
	orig.spte_low = xchg(&ssptep->spte_low, sspte.spte_low);
	orig.spte_high = ssptep->spte_high;
	ssptep->spte_high = sspte.spte_high;
	count_spte_clear(sptep, spte);

	return orig.spte;
}

/*
 * The idea using the light way get the spte on x86_32 guest is from
 * gup_get_pte(arch/x86/mm/gup.c).
 * The difference is we can not catch the spte tlb flush if we leave
 * guest mode, so we emulate it by increase clear_spte_count when spte
 * is cleared.
 */
static u64 __get_spte_lockless(u64 *sptep)
{
	struct kvm_mmu_page *sp =  page_header(__pa(sptep));
	union split_spte spte, *orig = (union split_spte *)sptep;
	int count;

retry:
	count = sp->clear_spte_count;
	smp_rmb();

	spte.spte_low = orig->spte_low;
	smp_rmb();

	spte.spte_high = orig->spte_high;
	smp_rmb();

	if (unlikely(spte.spte_low != orig->spte_low ||
	      count != sp->clear_spte_count))
		goto retry;

	return spte.spte;
}

static bool __check_direct_spte_mmio_pf(u64 spte)
{
	union split_spte sspte = (union split_spte)spte;
	u32 high_mmio_mask = shadow_mmio_mask >> 32;

	/* It is valid if the spte is zapped. */
	if (spte == 0ull)
		return true;

	/* It is valid if the spte is being zapped. */
	if (sspte.spte_low == 0ull &&
	    (sspte.spte_high & high_mmio_mask) == high_mmio_mask)
		return true;

	return false;
}
#endif

static bool spte_has_volatile_bits(u64 spte)
{
	if (!shadow_accessed_mask)
		return false;

	if (!is_shadow_present_pte(spte))
		return false;

	if ((spte & shadow_accessed_mask) &&
	      (!is_writable_pte(spte) || (spte & shadow_dirty_mask)))
		return false;

	return true;
}

static bool spte_is_bit_cleared(u64 old_spte, u64 new_spte, u64 bit_mask)
{
	return (old_spte & bit_mask) && !(new_spte & bit_mask);
}

/* Rules for using mmu_spte_set:
 * Set the sptep from nonpresent to present.
 * Note: the sptep being assigned *must* be either not present
 * or in a state where the hardware will not attempt to update
 * the spte.
 */
static void mmu_spte_set(u64 *sptep, u64 new_spte)
{
	WARN_ON(is_shadow_present_pte(*sptep));
	__set_spte(sptep, new_spte);
}

/* Rules for using mmu_spte_update:
 * Update the state bits, it means the mapped pfn is not changged.
 */
static void mmu_spte_update(u64 *sptep, u64 new_spte)
{
	u64 mask, old_spte = *sptep;

	WARN_ON(!is_rmap_spte(new_spte));

	if (!is_shadow_present_pte(old_spte))
		return mmu_spte_set(sptep, new_spte);

	new_spte |= old_spte & shadow_dirty_mask;

	mask = shadow_accessed_mask;
	if (is_writable_pte(old_spte))
		mask |= shadow_dirty_mask;

	if (!spte_has_volatile_bits(old_spte) || (new_spte & mask) == mask)
		__update_clear_spte_fast(sptep, new_spte);
	else
		old_spte = __update_clear_spte_slow(sptep, new_spte);

	if (!shadow_accessed_mask)
		return;

	if (spte_is_bit_cleared(old_spte, new_spte, shadow_accessed_mask))
		kvm_set_pfn_accessed(spte_to_pfn(old_spte));
	if (spte_is_bit_cleared(old_spte, new_spte, shadow_dirty_mask))
		kvm_set_pfn_dirty(spte_to_pfn(old_spte));
}

/*
 * Rules for using mmu_spte_clear_track_bits:
 * It sets the sptep from present to nonpresent, and track the
 * state bits, it is used to clear the last level sptep.
 */
static int mmu_spte_clear_track_bits(u64 *sptep)
{
	pfn_t pfn;
	u64 old_spte = *sptep;

	if (!spte_has_volatile_bits(old_spte))
		__update_clear_spte_fast(sptep, 0ull);
	else
		old_spte = __update_clear_spte_slow(sptep, 0ull);

	if (!is_rmap_spte(old_spte))
		return 0;

	pfn = spte_to_pfn(old_spte);
	if (!shadow_accessed_mask || old_spte & shadow_accessed_mask)
		kvm_set_pfn_accessed(pfn);
	if (!shadow_dirty_mask || (old_spte & shadow_dirty_mask))
		kvm_set_pfn_dirty(pfn);
	return 1;
}

/*
 * Rules for using mmu_spte_clear_no_track:
 * Directly clear spte without caring the state bits of sptep,
 * it is used to set the upper level spte.
 */
static void mmu_spte_clear_no_track(u64 *sptep)
{
	__update_clear_spte_fast(sptep, 0ull);
}

static u64 mmu_spte_get_lockless(u64 *sptep)
{
	return __get_spte_lockless(sptep);
}

static void walk_shadow_page_lockless_begin(struct kvm_vcpu *vcpu)
{
	rcu_read_lock();
	atomic_inc(&vcpu->kvm->arch.reader_counter);

	/* Increase the counter before walking shadow page table */
	smp_mb__after_atomic_inc();
}

static void walk_shadow_page_lockless_end(struct kvm_vcpu *vcpu)
{
	/* Decrease the counter after walking shadow page table finished */
	smp_mb__before_atomic_dec();
	atomic_dec(&vcpu->kvm->arch.reader_counter);
	rcu_read_unlock();
}

static int mmu_topup_memory_cache(struct kvm_mmu_memory_cache *cache,
				  struct kmem_cache *base_cache, int min)
{
	void *obj;

	if (cache->nobjs >= min)
		return 0;
	while (cache->nobjs < ARRAY_SIZE(cache->objects)) {
		obj = kmem_cache_zalloc(base_cache, GFP_KERNEL);
		if (!obj)
			return -ENOMEM;
		cache->objects[cache->nobjs++] = obj;
	}
	return 0;
}

static int mmu_memory_cache_free_objects(struct kvm_mmu_memory_cache *cache)
{
	return cache->nobjs;
}

static void mmu_free_memory_cache(struct kvm_mmu_memory_cache *mc,
				  struct kmem_cache *cache)
{
	while (mc->nobjs)
		kmem_cache_free(cache, mc->objects[--mc->nobjs]);
}

static int mmu_topup_memory_cache_page(struct kvm_mmu_memory_cache *cache,
				       int min)
{
	void *page;

	if (cache->nobjs >= min)
		return 0;
	while (cache->nobjs < ARRAY_SIZE(cache->objects)) {
		page = (void *)__get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		cache->objects[cache->nobjs++] = page;
	}
	return 0;
}

static void mmu_free_memory_cache_page(struct kvm_mmu_memory_cache *mc)
{
	while (mc->nobjs)
		free_page((unsigned long)mc->objects[--mc->nobjs]);
}

static int mmu_topup_memory_caches(struct kvm_vcpu *vcpu)
{
	int r;

	r = mmu_topup_memory_cache(&vcpu->arch.mmu_pte_list_desc_cache,
				   pte_list_desc_cache, 8 + PTE_PREFETCH_NUM);
	if (r)
		goto out;
	r = mmu_topup_memory_cache_page(&vcpu->arch.mmu_page_cache, 8);
	if (r)
		goto out;
	r = mmu_topup_memory_cache(&vcpu->arch.mmu_page_header_cache,
				   mmu_page_header_cache, 4);
out:
	return r;
}

static void mmu_free_memory_caches(struct kvm_vcpu *vcpu)
{
	mmu_free_memory_cache(&vcpu->arch.mmu_pte_list_desc_cache,
				pte_list_desc_cache);
	mmu_free_memory_cache_page(&vcpu->arch.mmu_page_cache);
	mmu_free_memory_cache(&vcpu->arch.mmu_page_header_cache,
				mmu_page_header_cache);
}

static void *mmu_memory_cache_alloc(struct kvm_mmu_memory_cache *mc,
				    size_t size)
{
	void *p;

	BUG_ON(!mc->nobjs);
	p = mc->objects[--mc->nobjs];
	return p;
}

static struct pte_list_desc *mmu_alloc_pte_list_desc(struct kvm_vcpu *vcpu)
{
	return mmu_memory_cache_alloc(&vcpu->arch.mmu_pte_list_desc_cache,
				      sizeof(struct pte_list_desc));
}

static void mmu_free_pte_list_desc(struct pte_list_desc *pte_list_desc)
{
	kmem_cache_free(pte_list_desc_cache, pte_list_desc);
}

static gfn_t kvm_mmu_page_get_gfn(struct kvm_mmu_page *sp, int index)
{
	if (!sp->role.direct)
		return sp->gfns[index];

	return sp->gfn + (index << ((sp->role.level - 1) * PT64_LEVEL_BITS));
}

static void kvm_mmu_page_set_gfn(struct kvm_mmu_page *sp, int index, gfn_t gfn)
{
	if (sp->role.direct)
		BUG_ON(gfn != kvm_mmu_page_get_gfn(sp, index));
	else
		sp->gfns[index] = gfn;
}

/*
 * Return the pointer to the large page information for a given gfn,
 * handling slots that are not large page aligned.
 */
static struct kvm_lpage_info *lpage_info_slot(gfn_t gfn,
					      struct kvm_memory_slot *slot,
					      int level)
{
	unsigned long idx;

	idx = gfn_to_index(gfn, slot->base_gfn, level);
	return &slot->arch.lpage_info[level - 2][idx];
}

static void account_shadowed(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *slot;
	struct kvm_lpage_info *linfo;
	int i;

	slot = gfn_to_memslot(kvm, gfn);
	for (i = PT_DIRECTORY_LEVEL;
	     i < PT_PAGE_TABLE_LEVEL + KVM_NR_PAGE_SIZES; ++i) {
		linfo = lpage_info_slot(gfn, slot, i);
		linfo->write_count += 1;
	}
	kvm->arch.indirect_shadow_pages++;
}

static void unaccount_shadowed(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *slot;
	struct kvm_lpage_info *linfo;
	int i;

	slot = gfn_to_memslot(kvm, gfn);
	for (i = PT_DIRECTORY_LEVEL;
	     i < PT_PAGE_TABLE_LEVEL + KVM_NR_PAGE_SIZES; ++i) {
		linfo = lpage_info_slot(gfn, slot, i);
		linfo->write_count -= 1;
		WARN_ON(linfo->write_count < 0);
	}
	kvm->arch.indirect_shadow_pages--;
}

static int has_wrprotected_page(struct kvm *kvm,
				gfn_t gfn,
				int level)
{
	struct kvm_memory_slot *slot;
	struct kvm_lpage_info *linfo;

	slot = gfn_to_memslot(kvm, gfn);
	if (slot) {
		linfo = lpage_info_slot(gfn, slot, level);
		return linfo->write_count;
	}

	return 1;
}

static int host_mapping_level(struct kvm *kvm, gfn_t gfn)
{
	unsigned long page_size;
	int i, ret = 0;

	page_size = kvm_host_page_size(kvm, gfn);

	for (i = PT_PAGE_TABLE_LEVEL;
	     i < (PT_PAGE_TABLE_LEVEL + KVM_NR_PAGE_SIZES); ++i) {
		if (page_size >= KVM_HPAGE_SIZE(i))
			ret = i;
		else
			break;
	}

	return ret;
}

static struct kvm_memory_slot *
gfn_to_memslot_dirty_bitmap(struct kvm_vcpu *vcpu, gfn_t gfn,
			    bool no_dirty_log)
{
	struct kvm_memory_slot *slot;

	slot = gfn_to_memslot(vcpu->kvm, gfn);
	if (!slot || slot->flags & KVM_MEMSLOT_INVALID ||
	      (no_dirty_log && slot->dirty_bitmap))
		slot = NULL;

	return slot;
}

static bool mapping_level_dirty_bitmap(struct kvm_vcpu *vcpu, gfn_t large_gfn)
{
	return !gfn_to_memslot_dirty_bitmap(vcpu, large_gfn, true);
}

static int mapping_level(struct kvm_vcpu *vcpu, gfn_t large_gfn)
{
	int host_level, level, max_level;

	host_level = host_mapping_level(vcpu->kvm, large_gfn);

	if (host_level == PT_PAGE_TABLE_LEVEL)
		return host_level;

	max_level = kvm_x86_ops->get_lpage_level() < host_level ?
		kvm_x86_ops->get_lpage_level() : host_level;

	for (level = PT_DIRECTORY_LEVEL; level <= max_level; ++level)
		if (has_wrprotected_page(vcpu->kvm, large_gfn, level))
			break;

	return level - 1;
}

/*
 * Pte mapping structures:
 *
 * If pte_list bit zero is zero, then pte_list point to the spte.
 *
 * If pte_list bit zero is one, (then pte_list & ~1) points to a struct
 * pte_list_desc containing more mappings.
 *
 * Returns the number of pte entries before the spte was added or zero if
 * the spte was not added.
 *
 */
static int pte_list_add(struct kvm_vcpu *vcpu, u64 *spte,
			unsigned long *pte_list)
{
	struct pte_list_desc *desc;
	int i, count = 0;

	if (!*pte_list) {
		rmap_printk("pte_list_add: %p %llx 0->1\n", spte, *spte);
		*pte_list = (unsigned long)spte;
	} else if (!(*pte_list & 1)) {
		rmap_printk("pte_list_add: %p %llx 1->many\n", spte, *spte);
		desc = mmu_alloc_pte_list_desc(vcpu);
		desc->sptes[0] = (u64 *)*pte_list;
		desc->sptes[1] = spte;
		*pte_list = (unsigned long)desc | 1;
		++count;
	} else {
		rmap_printk("pte_list_add: %p %llx many->many\n", spte, *spte);
		desc = (struct pte_list_desc *)(*pte_list & ~1ul);
		while (desc->sptes[PTE_LIST_EXT-1] && desc->more) {
			desc = desc->more;
			count += PTE_LIST_EXT;
		}
		if (desc->sptes[PTE_LIST_EXT-1]) {
			desc->more = mmu_alloc_pte_list_desc(vcpu);
			desc = desc->more;
		}
		for (i = 0; desc->sptes[i]; ++i)
			++count;
		desc->sptes[i] = spte;
	}
	return count;
}

static u64 *pte_list_next(unsigned long *pte_list, u64 *spte)
{
	struct pte_list_desc *desc;
	u64 *prev_spte;
	int i;

	if (!*pte_list)
		return NULL;
	else if (!(*pte_list & 1)) {
		if (!spte)
			return (u64 *)*pte_list;
		return NULL;
	}
	desc = (struct pte_list_desc *)(*pte_list & ~1ul);
	prev_spte = NULL;
	while (desc) {
		for (i = 0; i < PTE_LIST_EXT && desc->sptes[i]; ++i) {
			if (prev_spte == spte)
				return desc->sptes[i];
			prev_spte = desc->sptes[i];
		}
		desc = desc->more;
	}
	return NULL;
}

static void
pte_list_desc_remove_entry(unsigned long *pte_list, struct pte_list_desc *desc,
			   int i, struct pte_list_desc *prev_desc)
{
	int j;

	for (j = PTE_LIST_EXT - 1; !desc->sptes[j] && j > i; --j)
		;
	desc->sptes[i] = desc->sptes[j];
	desc->sptes[j] = NULL;
	if (j != 0)
		return;
	if (!prev_desc && !desc->more)
		*pte_list = (unsigned long)desc->sptes[0];
	else
		if (prev_desc)
			prev_desc->more = desc->more;
		else
			*pte_list = (unsigned long)desc->more | 1;
	mmu_free_pte_list_desc(desc);
}

static void pte_list_remove(u64 *spte, unsigned long *pte_list)
{
	struct pte_list_desc *desc;
	struct pte_list_desc *prev_desc;
	int i;

	if (!*pte_list) {
		printk(KERN_ERR "pte_list_remove: %p 0->BUG\n", spte);
		BUG();
	} else if (!(*pte_list & 1)) {
		rmap_printk("pte_list_remove:  %p 1->0\n", spte);
		if ((u64 *)*pte_list != spte) {
			printk(KERN_ERR "pte_list_remove:  %p 1->BUG\n", spte);
			BUG();
		}
		*pte_list = 0;
	} else {
		rmap_printk("pte_list_remove:  %p many->many\n", spte);
		desc = (struct pte_list_desc *)(*pte_list & ~1ul);
		prev_desc = NULL;
		while (desc) {
			for (i = 0; i < PTE_LIST_EXT && desc->sptes[i]; ++i)
				if (desc->sptes[i] == spte) {
					pte_list_desc_remove_entry(pte_list,
							       desc, i,
							       prev_desc);
					return;
				}
			prev_desc = desc;
			desc = desc->more;
		}
		pr_err("pte_list_remove: %p many->many\n", spte);
		BUG();
	}
}

typedef void (*pte_list_walk_fn) (u64 *spte);
static void pte_list_walk(unsigned long *pte_list, pte_list_walk_fn fn)
{
	struct pte_list_desc *desc;
	int i;

	if (!*pte_list)
		return;

	if (!(*pte_list & 1))
		return fn((u64 *)*pte_list);

	desc = (struct pte_list_desc *)(*pte_list & ~1ul);
	while (desc) {
		for (i = 0; i < PTE_LIST_EXT && desc->sptes[i]; ++i)
			fn(desc->sptes[i]);
		desc = desc->more;
	}
}

static unsigned long *__gfn_to_rmap(gfn_t gfn, int level,
				    struct kvm_memory_slot *slot)
{
	struct kvm_lpage_info *linfo;

	if (likely(level == PT_PAGE_TABLE_LEVEL))
		return &slot->rmap[gfn - slot->base_gfn];

	linfo = lpage_info_slot(gfn, slot, level);
	return &linfo->rmap_pde;
}

/*
 * Take gfn and return the reverse mapping to it.
 */
static unsigned long *gfn_to_rmap(struct kvm *kvm, gfn_t gfn, int level)
{
	struct kvm_memory_slot *slot;

	slot = gfn_to_memslot(kvm, gfn);
	return __gfn_to_rmap(gfn, level, slot);
}

static bool rmap_can_add(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_memory_cache *cache;

	cache = &vcpu->arch.mmu_pte_list_desc_cache;
	return mmu_memory_cache_free_objects(cache);
}

static int rmap_add(struct kvm_vcpu *vcpu, u64 *spte, gfn_t gfn)
{
	struct kvm_mmu_page *sp;
	unsigned long *rmapp;

	sp = page_header(__pa(spte));
	kvm_mmu_page_set_gfn(sp, spte - sp->spt, gfn);
	rmapp = gfn_to_rmap(vcpu->kvm, gfn, sp->role.level);
	return pte_list_add(vcpu, spte, rmapp);
}

static u64 *rmap_next(unsigned long *rmapp, u64 *spte)
{
	return pte_list_next(rmapp, spte);
}

static void rmap_remove(struct kvm *kvm, u64 *spte)
{
	struct kvm_mmu_page *sp;
	gfn_t gfn;
	unsigned long *rmapp;

	sp = page_header(__pa(spte));
	gfn = kvm_mmu_page_get_gfn(sp, spte - sp->spt);
	rmapp = gfn_to_rmap(kvm, gfn, sp->role.level);
	pte_list_remove(spte, rmapp);
}

static void drop_spte(struct kvm *kvm, u64 *sptep)
{
	if (mmu_spte_clear_track_bits(sptep))
		rmap_remove(kvm, sptep);
}

int kvm_mmu_rmap_write_protect(struct kvm *kvm, u64 gfn,
			       struct kvm_memory_slot *slot)
{
	unsigned long *rmapp;
	u64 *spte;
	int i, write_protected = 0;

	rmapp = __gfn_to_rmap(gfn, PT_PAGE_TABLE_LEVEL, slot);
	spte = rmap_next(rmapp, NULL);
	while (spte) {
		BUG_ON(!(*spte & PT_PRESENT_MASK));
		rmap_printk("rmap_write_protect: spte %p %llx\n", spte, *spte);
		if (is_writable_pte(*spte)) {
			mmu_spte_update(spte, *spte & ~PT_WRITABLE_MASK);
			write_protected = 1;
		}
		spte = rmap_next(rmapp, spte);
	}

	/* check for huge page mappings */
	for (i = PT_DIRECTORY_LEVEL;
	     i < PT_PAGE_TABLE_LEVEL + KVM_NR_PAGE_SIZES; ++i) {
		rmapp = __gfn_to_rmap(gfn, i, slot);
		spte = rmap_next(rmapp, NULL);
		while (spte) {
			BUG_ON(!(*spte & PT_PRESENT_MASK));
			BUG_ON(!is_large_pte(*spte));
			pgprintk("rmap_write_protect(large): spte %p %llx %lld\n", spte, *spte, gfn);
			if (is_writable_pte(*spte)) {
				drop_spte(kvm, spte);
				--kvm->stat.lpages;
				spte = NULL;
				write_protected = 1;
			}
			spte = rmap_next(rmapp, spte);
		}
	}

	return write_protected;
}

static int rmap_write_protect(struct kvm *kvm, u64 gfn)
{
	struct kvm_memory_slot *slot;

	slot = gfn_to_memslot(kvm, gfn);
	return kvm_mmu_rmap_write_protect(kvm, gfn, slot);
}

static int kvm_unmap_rmapp(struct kvm *kvm, unsigned long *rmapp,
			   unsigned long data)
{
	u64 *spte;
	int need_tlb_flush = 0;

	while ((spte = rmap_next(rmapp, NULL))) {
		BUG_ON(!(*spte & PT_PRESENT_MASK));
		rmap_printk("kvm_rmap_unmap_hva: spte %p %llx\n", spte, *spte);
		drop_spte(kvm, spte);
		need_tlb_flush = 1;
	}
	return need_tlb_flush;
}

static int kvm_set_pte_rmapp(struct kvm *kvm, unsigned long *rmapp,
			     unsigned long data)
{
	int need_flush = 0;
	u64 *spte, new_spte;
	pte_t *ptep = (pte_t *)data;
	pfn_t new_pfn;

	WARN_ON(pte_huge(*ptep));
	new_pfn = pte_pfn(*ptep);
	spte = rmap_next(rmapp, NULL);
	while (spte) {
		BUG_ON(!is_shadow_present_pte(*spte));
		rmap_printk("kvm_set_pte_rmapp: spte %p %llx\n", spte, *spte);
		need_flush = 1;
		if (pte_write(*ptep)) {
			drop_spte(kvm, spte);
			spte = rmap_next(rmapp, NULL);
		} else {
			new_spte = *spte &~ (PT64_BASE_ADDR_MASK);
			new_spte |= (u64)new_pfn << PAGE_SHIFT;

			new_spte &= ~PT_WRITABLE_MASK;
			new_spte &= ~SPTE_HOST_WRITEABLE;
			new_spte &= ~shadow_accessed_mask;
			mmu_spte_clear_track_bits(spte);
			mmu_spte_set(spte, new_spte);
			spte = rmap_next(rmapp, spte);
		}
	}
	if (need_flush)
		kvm_flush_remote_tlbs(kvm);

	return 0;
}

static int kvm_handle_hva(struct kvm *kvm, unsigned long hva,
			  unsigned long data,
			  int (*handler)(struct kvm *kvm, unsigned long *rmapp,
					 unsigned long data))
{
	int j;
	int ret;
	int retval = 0;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;

	slots = kvm_memslots(kvm);

	kvm_for_each_memslot(memslot, slots) {
		unsigned long start = memslot->userspace_addr;
		unsigned long end;

		end = start + (memslot->npages << PAGE_SHIFT);
		if (hva >= start && hva < end) {
			gfn_t gfn_offset = (hva - start) >> PAGE_SHIFT;
			gfn_t gfn = memslot->base_gfn + gfn_offset;

			ret = handler(kvm, &memslot->rmap[gfn_offset], data);

			for (j = 0; j < KVM_NR_PAGE_SIZES - 1; ++j) {
				struct kvm_lpage_info *linfo;

				linfo = lpage_info_slot(gfn, memslot,
							PT_DIRECTORY_LEVEL + j);
				ret |= handler(kvm, &linfo->rmap_pde, data);
			}
			trace_kvm_age_page(hva, memslot, ret);
			retval |= ret;
		}
	}

	return retval;
}

int kvm_unmap_hva(struct kvm *kvm, unsigned long hva)
{
	return kvm_handle_hva(kvm, hva, 0, kvm_unmap_rmapp);
}

void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	kvm_handle_hva(kvm, hva, (unsigned long)&pte, kvm_set_pte_rmapp);
}

static int kvm_age_rmapp(struct kvm *kvm, unsigned long *rmapp,
			 unsigned long data)
{
	u64 *spte;
	int young = 0;

	/*
	 * Emulate the accessed bit for EPT, by checking if this page has
	 * an EPT mapping, and clearing it if it does. On the next access,
	 * a new EPT mapping will be established.
	 * This has some overhead, but not as much as the cost of swapping
	 * out actively used pages or breaking up actively used hugepages.
	 */
	if (!shadow_accessed_mask)
		return kvm_unmap_rmapp(kvm, rmapp, data);

	spte = rmap_next(rmapp, NULL);
	while (spte) {
		int _young;
		u64 _spte = *spte;
		BUG_ON(!(_spte & PT_PRESENT_MASK));
		_young = _spte & PT_ACCESSED_MASK;
		if (_young) {
			young = 1;
			clear_bit(PT_ACCESSED_SHIFT, (unsigned long *)spte);
		}
		spte = rmap_next(rmapp, spte);
	}
	return young;
}

static int kvm_test_age_rmapp(struct kvm *kvm, unsigned long *rmapp,
			      unsigned long data)
{
	u64 *spte;
	int young = 0;

	/*
	 * If there's no access bit in the secondary pte set by the
	 * hardware it's up to gup-fast/gup to set the access bit in
	 * the primary pte or in the page structure.
	 */
	if (!shadow_accessed_mask)
		goto out;

	spte = rmap_next(rmapp, NULL);
	while (spte) {
		u64 _spte = *spte;
		BUG_ON(!(_spte & PT_PRESENT_MASK));
		young = _spte & PT_ACCESSED_MASK;
		if (young) {
			young = 1;
			break;
		}
		spte = rmap_next(rmapp, spte);
	}
out:
	return young;
}

#define RMAP_RECYCLE_THRESHOLD 1000

static void rmap_recycle(struct kvm_vcpu *vcpu, u64 *spte, gfn_t gfn)
{
	unsigned long *rmapp;
	struct kvm_mmu_page *sp;

	sp = page_header(__pa(spte));

	rmapp = gfn_to_rmap(vcpu->kvm, gfn, sp->role.level);

	kvm_unmap_rmapp(vcpu->kvm, rmapp, 0);
	kvm_flush_remote_tlbs(vcpu->kvm);
}

int kvm_age_hva(struct kvm *kvm, unsigned long hva)
{
	return kvm_handle_hva(kvm, hva, 0, kvm_age_rmapp);
}

int kvm_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	return kvm_handle_hva(kvm, hva, 0, kvm_test_age_rmapp);
}

#ifdef MMU_DEBUG
static int is_empty_shadow_page(u64 *spt)
{
	u64 *pos;
	u64 *end;

	for (pos = spt, end = pos + PAGE_SIZE / sizeof(u64); pos != end; pos++)
		if (is_shadow_present_pte(*pos)) {
			printk(KERN_ERR "%s: %p %llx\n", __func__,
			       pos, *pos);
			return 0;
		}
	return 1;
}
#endif

/*
 * This value is the sum of all of the kvm instances's
 * kvm->arch.n_used_mmu_pages values.  We need a global,
 * aggregate version in order to make the slab shrinker
 * faster
 */
static inline void kvm_mod_used_mmu_pages(struct kvm *kvm, int nr)
{
	kvm->arch.n_used_mmu_pages += nr;
	percpu_counter_add(&kvm_total_used_mmu_pages, nr);
}

/*
 * Remove the sp from shadow page cache, after call it,
 * we can not find this sp from the cache, and the shadow
 * page table is still valid.
 * It should be under the protection of mmu lock.
 */
static void kvm_mmu_isolate_page(struct kvm_mmu_page *sp)
{
	ASSERT(is_empty_shadow_page(sp->spt));
	hlist_del(&sp->hash_link);
	if (!sp->role.direct)
		free_page((unsigned long)sp->gfns);
}

/*
 * Free the shadow page table and the sp, we can do it
 * out of the protection of mmu lock.
 */
static void kvm_mmu_free_page(struct kvm_mmu_page *sp)
{
	list_del(&sp->link);
	free_page((unsigned long)sp->spt);
	kmem_cache_free(mmu_page_header_cache, sp);
}

static unsigned kvm_page_table_hashfn(gfn_t gfn)
{
	return gfn & ((1 << KVM_MMU_HASH_SHIFT) - 1);
}

static void mmu_page_add_parent_pte(struct kvm_vcpu *vcpu,
				    struct kvm_mmu_page *sp, u64 *parent_pte)
{
	if (!parent_pte)
		return;

	pte_list_add(vcpu, parent_pte, &sp->parent_ptes);
}

static void mmu_page_remove_parent_pte(struct kvm_mmu_page *sp,
				       u64 *parent_pte)
{
	pte_list_remove(parent_pte, &sp->parent_ptes);
}

static void drop_parent_pte(struct kvm_mmu_page *sp,
			    u64 *parent_pte)
{
	mmu_page_remove_parent_pte(sp, parent_pte);
	mmu_spte_clear_no_track(parent_pte);
}

static struct kvm_mmu_page *kvm_mmu_alloc_page(struct kvm_vcpu *vcpu,
					       u64 *parent_pte, int direct)
{
	struct kvm_mmu_page *sp;
	sp = mmu_memory_cache_alloc(&vcpu->arch.mmu_page_header_cache,
					sizeof *sp);
	sp->spt = mmu_memory_cache_alloc(&vcpu->arch.mmu_page_cache, PAGE_SIZE);
	if (!direct)
		sp->gfns = mmu_memory_cache_alloc(&vcpu->arch.mmu_page_cache,
						  PAGE_SIZE);
	set_page_private(virt_to_page(sp->spt), (unsigned long)sp);
	list_add(&sp->link, &vcpu->kvm->arch.active_mmu_pages);
	bitmap_zero(sp->slot_bitmap, KVM_MEM_SLOTS_NUM);
	sp->parent_ptes = 0;
	mmu_page_add_parent_pte(vcpu, sp, parent_pte);
	kvm_mod_used_mmu_pages(vcpu->kvm, +1);
	return sp;
}

static void mark_unsync(u64 *spte);
static void kvm_mmu_mark_parents_unsync(struct kvm_mmu_page *sp)
{
	pte_list_walk(&sp->parent_ptes, mark_unsync);
}

static void mark_unsync(u64 *spte)
{
	struct kvm_mmu_page *sp;
	unsigned int index;

	sp = page_header(__pa(spte));
	index = spte - sp->spt;
	if (__test_and_set_bit(index, sp->unsync_child_bitmap))
		return;
	if (sp->unsync_children++)
		return;
	kvm_mmu_mark_parents_unsync(sp);
}

static int nonpaging_sync_page(struct kvm_vcpu *vcpu,
			       struct kvm_mmu_page *sp)
{
	return 1;
}

static void nonpaging_invlpg(struct kvm_vcpu *vcpu, gva_t gva)
{
}

static void nonpaging_update_pte(struct kvm_vcpu *vcpu,
				 struct kvm_mmu_page *sp, u64 *spte,
				 const void *pte)
{
	WARN_ON(1);
}

#define KVM_PAGE_ARRAY_NR 16

struct kvm_mmu_pages {
	struct mmu_page_and_offset {
		struct kvm_mmu_page *sp;
		unsigned int idx;
	} page[KVM_PAGE_ARRAY_NR];
	unsigned int nr;
};

static int mmu_pages_add(struct kvm_mmu_pages *pvec, struct kvm_mmu_page *sp,
			 int idx)
{
	int i;

	if (sp->unsync)
		for (i=0; i < pvec->nr; i++)
			if (pvec->page[i].sp == sp)
				return 0;

	pvec->page[pvec->nr].sp = sp;
	pvec->page[pvec->nr].idx = idx;
	pvec->nr++;
	return (pvec->nr == KVM_PAGE_ARRAY_NR);
}

static int __mmu_unsync_walk(struct kvm_mmu_page *sp,
			   struct kvm_mmu_pages *pvec)
{
	int i, ret, nr_unsync_leaf = 0;

	for_each_set_bit(i, sp->unsync_child_bitmap, 512) {
		struct kvm_mmu_page *child;
		u64 ent = sp->spt[i];

		if (!is_shadow_present_pte(ent) || is_large_pte(ent))
			goto clear_child_bitmap;

		child = page_header(ent & PT64_BASE_ADDR_MASK);

		if (child->unsync_children) {
			if (mmu_pages_add(pvec, child, i))
				return -ENOSPC;

			ret = __mmu_unsync_walk(child, pvec);
			if (!ret)
				goto clear_child_bitmap;
			else if (ret > 0)
				nr_unsync_leaf += ret;
			else
				return ret;
		} else if (child->unsync) {
			nr_unsync_leaf++;
			if (mmu_pages_add(pvec, child, i))
				return -ENOSPC;
		} else
			 goto clear_child_bitmap;

		continue;

clear_child_bitmap:
		__clear_bit(i, sp->unsync_child_bitmap);
		sp->unsync_children--;
		WARN_ON((int)sp->unsync_children < 0);
	}


	return nr_unsync_leaf;
}

static int mmu_unsync_walk(struct kvm_mmu_page *sp,
			   struct kvm_mmu_pages *pvec)
{
	if (!sp->unsync_children)
		return 0;

	mmu_pages_add(pvec, sp, 0);
	return __mmu_unsync_walk(sp, pvec);
}

static void kvm_unlink_unsync_page(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	WARN_ON(!sp->unsync);
	trace_kvm_mmu_sync_page(sp);
	sp->unsync = 0;
	--kvm->stat.mmu_unsync;
}

static int kvm_mmu_prepare_zap_page(struct kvm *kvm, struct kvm_mmu_page *sp,
				    struct list_head *invalid_list);
static void kvm_mmu_commit_zap_page(struct kvm *kvm,
				    struct list_head *invalid_list);

#define for_each_gfn_sp(kvm, sp, gfn, pos)				\
  hlist_for_each_entry(sp, pos,						\
   &(kvm)->arch.mmu_page_hash[kvm_page_table_hashfn(gfn)], hash_link)	\
	if ((sp)->gfn != (gfn)) {} else

#define for_each_gfn_indirect_valid_sp(kvm, sp, gfn, pos)		\
  hlist_for_each_entry(sp, pos,						\
   &(kvm)->arch.mmu_page_hash[kvm_page_table_hashfn(gfn)], hash_link)	\
		if ((sp)->gfn != (gfn) || (sp)->role.direct ||		\
			(sp)->role.invalid) {} else

/* @sp->gfn should be write-protected at the call site */
static int __kvm_sync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			   struct list_head *invalid_list, bool clear_unsync)
{
	if (sp->role.cr4_pae != !!is_pae(vcpu)) {
		kvm_mmu_prepare_zap_page(vcpu->kvm, sp, invalid_list);
		return 1;
	}

	if (clear_unsync)
		kvm_unlink_unsync_page(vcpu->kvm, sp);

	if (vcpu->arch.mmu.sync_page(vcpu, sp)) {
		kvm_mmu_prepare_zap_page(vcpu->kvm, sp, invalid_list);
		return 1;
	}

	kvm_mmu_flush_tlb(vcpu);
	return 0;
}

static int kvm_sync_page_transient(struct kvm_vcpu *vcpu,
				   struct kvm_mmu_page *sp)
{
	LIST_HEAD(invalid_list);
	int ret;

	ret = __kvm_sync_page(vcpu, sp, &invalid_list, false);
	if (ret)
		kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);

	return ret;
}

#ifdef CONFIG_KVM_MMU_AUDIT
#include "mmu_audit.c"
#else
static void kvm_mmu_audit(struct kvm_vcpu *vcpu, int point) { }
static void mmu_audit_disable(void) { }
#endif

static int kvm_sync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			 struct list_head *invalid_list)
{
	return __kvm_sync_page(vcpu, sp, invalid_list, true);
}

/* @gfn should be write-protected at the call site */
static void kvm_sync_pages(struct kvm_vcpu *vcpu,  gfn_t gfn)
{
	struct kvm_mmu_page *s;
	struct hlist_node *node;
	LIST_HEAD(invalid_list);
	bool flush = false;

	for_each_gfn_indirect_valid_sp(vcpu->kvm, s, gfn, node) {
		if (!s->unsync)
			continue;

		WARN_ON(s->role.level != PT_PAGE_TABLE_LEVEL);
		kvm_unlink_unsync_page(vcpu->kvm, s);
		if ((s->role.cr4_pae != !!is_pae(vcpu)) ||
			(vcpu->arch.mmu.sync_page(vcpu, s))) {
			kvm_mmu_prepare_zap_page(vcpu->kvm, s, &invalid_list);
			continue;
		}
		flush = true;
	}

	kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
	if (flush)
		kvm_mmu_flush_tlb(vcpu);
}

struct mmu_page_path {
	struct kvm_mmu_page *parent[PT64_ROOT_LEVEL-1];
	unsigned int idx[PT64_ROOT_LEVEL-1];
};

#define for_each_sp(pvec, sp, parents, i)			\
		for (i = mmu_pages_next(&pvec, &parents, -1),	\
			sp = pvec.page[i].sp;			\
			i < pvec.nr && ({ sp = pvec.page[i].sp; 1;});	\
			i = mmu_pages_next(&pvec, &parents, i))

static int mmu_pages_next(struct kvm_mmu_pages *pvec,
			  struct mmu_page_path *parents,
			  int i)
{
	int n;

	for (n = i+1; n < pvec->nr; n++) {
		struct kvm_mmu_page *sp = pvec->page[n].sp;

		if (sp->role.level == PT_PAGE_TABLE_LEVEL) {
			parents->idx[0] = pvec->page[n].idx;
			return n;
		}

		parents->parent[sp->role.level-2] = sp;
		parents->idx[sp->role.level-1] = pvec->page[n].idx;
	}

	return n;
}

static void mmu_pages_clear_parents(struct mmu_page_path *parents)
{
	struct kvm_mmu_page *sp;
	unsigned int level = 0;

	do {
		unsigned int idx = parents->idx[level];

		sp = parents->parent[level];
		if (!sp)
			return;

		--sp->unsync_children;
		WARN_ON((int)sp->unsync_children < 0);
		__clear_bit(idx, sp->unsync_child_bitmap);
		level++;
	} while (level < PT64_ROOT_LEVEL-1 && !sp->unsync_children);
}

static void kvm_mmu_pages_init(struct kvm_mmu_page *parent,
			       struct mmu_page_path *parents,
			       struct kvm_mmu_pages *pvec)
{
	parents->parent[parent->role.level-1] = NULL;
	pvec->nr = 0;
}

static void mmu_sync_children(struct kvm_vcpu *vcpu,
			      struct kvm_mmu_page *parent)
{
	int i;
	struct kvm_mmu_page *sp;
	struct mmu_page_path parents;
	struct kvm_mmu_pages pages;
	LIST_HEAD(invalid_list);

	kvm_mmu_pages_init(parent, &parents, &pages);
	while (mmu_unsync_walk(parent, &pages)) {
		int protected = 0;

		for_each_sp(pages, sp, parents, i)
			protected |= rmap_write_protect(vcpu->kvm, sp->gfn);

		if (protected)
			kvm_flush_remote_tlbs(vcpu->kvm);

		for_each_sp(pages, sp, parents, i) {
			kvm_sync_page(vcpu, sp, &invalid_list);
			mmu_pages_clear_parents(&parents);
		}
		kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
		cond_resched_lock(&vcpu->kvm->mmu_lock);
		kvm_mmu_pages_init(parent, &parents, &pages);
	}
}

static void init_shadow_page_table(struct kvm_mmu_page *sp)
{
	int i;

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i)
		sp->spt[i] = 0ull;
}

static void __clear_sp_write_flooding_count(struct kvm_mmu_page *sp)
{
	sp->write_flooding_count = 0;
}

static void clear_sp_write_flooding_count(u64 *spte)
{
	struct kvm_mmu_page *sp =  page_header(__pa(spte));

	__clear_sp_write_flooding_count(sp);
}

static struct kvm_mmu_page *kvm_mmu_get_page(struct kvm_vcpu *vcpu,
					     gfn_t gfn,
					     gva_t gaddr,
					     unsigned level,
					     int direct,
					     unsigned access,
					     u64 *parent_pte)
{
	union kvm_mmu_page_role role;
	unsigned quadrant;
	struct kvm_mmu_page *sp;
	struct hlist_node *node;
	bool need_sync = false;

	role = vcpu->arch.mmu.base_role;
	role.level = level;
	role.direct = direct;
	if (role.direct)
		role.cr4_pae = 0;
	role.access = access;
	if (!vcpu->arch.mmu.direct_map
	    && vcpu->arch.mmu.root_level <= PT32_ROOT_LEVEL) {
		quadrant = gaddr >> (PAGE_SHIFT + (PT64_PT_BITS * level));
		quadrant &= (1 << ((PT32_PT_BITS - PT64_PT_BITS) * level)) - 1;
		role.quadrant = quadrant;
	}
	for_each_gfn_sp(vcpu->kvm, sp, gfn, node) {
		if (!need_sync && sp->unsync)
			need_sync = true;

		if (sp->role.word != role.word)
			continue;

		if (sp->unsync && kvm_sync_page_transient(vcpu, sp))
			break;

		mmu_page_add_parent_pte(vcpu, sp, parent_pte);
		if (sp->unsync_children) {
			kvm_make_request(KVM_REQ_MMU_SYNC, vcpu);
			kvm_mmu_mark_parents_unsync(sp);
		} else if (sp->unsync)
			kvm_mmu_mark_parents_unsync(sp);

		__clear_sp_write_flooding_count(sp);
		trace_kvm_mmu_get_page(sp, false);
		return sp;
	}
	++vcpu->kvm->stat.mmu_cache_miss;
	sp = kvm_mmu_alloc_page(vcpu, parent_pte, direct);
	if (!sp)
		return sp;
	sp->gfn = gfn;
	sp->role = role;
	hlist_add_head(&sp->hash_link,
		&vcpu->kvm->arch.mmu_page_hash[kvm_page_table_hashfn(gfn)]);
	if (!direct) {
		if (rmap_write_protect(vcpu->kvm, gfn))
			kvm_flush_remote_tlbs(vcpu->kvm);
		if (level > PT_PAGE_TABLE_LEVEL && need_sync)
			kvm_sync_pages(vcpu, gfn);

		account_shadowed(vcpu->kvm, gfn);
	}
	init_shadow_page_table(sp);
	trace_kvm_mmu_get_page(sp, true);
	return sp;
}

static void shadow_walk_init(struct kvm_shadow_walk_iterator *iterator,
			     struct kvm_vcpu *vcpu, u64 addr)
{
	iterator->addr = addr;
	iterator->shadow_addr = vcpu->arch.mmu.root_hpa;
	iterator->level = vcpu->arch.mmu.shadow_root_level;

	if (iterator->level == PT64_ROOT_LEVEL &&
	    vcpu->arch.mmu.root_level < PT64_ROOT_LEVEL &&
	    !vcpu->arch.mmu.direct_map)
		--iterator->level;

	if (iterator->level == PT32E_ROOT_LEVEL) {
		iterator->shadow_addr
			= vcpu->arch.mmu.pae_root[(addr >> 30) & 3];
		iterator->shadow_addr &= PT64_BASE_ADDR_MASK;
		--iterator->level;
		if (!iterator->shadow_addr)
			iterator->level = 0;
	}
}

static bool shadow_walk_okay(struct kvm_shadow_walk_iterator *iterator)
{
	if (iterator->level < PT_PAGE_TABLE_LEVEL)
		return false;

	iterator->index = SHADOW_PT_INDEX(iterator->addr, iterator->level);
	iterator->sptep	= ((u64 *)__va(iterator->shadow_addr)) + iterator->index;
	return true;
}

static void __shadow_walk_next(struct kvm_shadow_walk_iterator *iterator,
			       u64 spte)
{
	if (is_last_spte(spte, iterator->level)) {
		iterator->level = 0;
		return;
	}

	iterator->shadow_addr = spte & PT64_BASE_ADDR_MASK;
	--iterator->level;
}

static void shadow_walk_next(struct kvm_shadow_walk_iterator *iterator)
{
	return __shadow_walk_next(iterator, *iterator->sptep);
}

static void link_shadow_page(u64 *sptep, struct kvm_mmu_page *sp)
{
	u64 spte;

	spte = __pa(sp->spt)
		| PT_PRESENT_MASK | PT_ACCESSED_MASK
		| PT_WRITABLE_MASK | PT_USER_MASK;
	mmu_spte_set(sptep, spte);
}

static void drop_large_spte(struct kvm_vcpu *vcpu, u64 *sptep)
{
	if (is_large_pte(*sptep)) {
		drop_spte(vcpu->kvm, sptep);
		--vcpu->kvm->stat.lpages;
		kvm_flush_remote_tlbs(vcpu->kvm);
	}
}

static void validate_direct_spte(struct kvm_vcpu *vcpu, u64 *sptep,
				   unsigned direct_access)
{
	if (is_shadow_present_pte(*sptep) && !is_large_pte(*sptep)) {
		struct kvm_mmu_page *child;

		/*
		 * For the direct sp, if the guest pte's dirty bit
		 * changed form clean to dirty, it will corrupt the
		 * sp's access: allow writable in the read-only sp,
		 * so we should update the spte at this point to get
		 * a new sp with the correct access.
		 */
		child = page_header(*sptep & PT64_BASE_ADDR_MASK);
		if (child->role.access == direct_access)
			return;

		drop_parent_pte(child, sptep);
		kvm_flush_remote_tlbs(vcpu->kvm);
	}
}

static bool mmu_page_zap_pte(struct kvm *kvm, struct kvm_mmu_page *sp,
			     u64 *spte)
{
	u64 pte;
	struct kvm_mmu_page *child;

	pte = *spte;
	if (is_shadow_present_pte(pte)) {
		if (is_last_spte(pte, sp->role.level)) {
			drop_spte(kvm, spte);
			if (is_large_pte(pte))
				--kvm->stat.lpages;
		} else {
			child = page_header(pte & PT64_BASE_ADDR_MASK);
			drop_parent_pte(child, spte);
		}
		return true;
	}

	if (is_mmio_spte(pte))
		mmu_spte_clear_no_track(spte);

	return false;
}

static void kvm_mmu_page_unlink_children(struct kvm *kvm,
					 struct kvm_mmu_page *sp)
{
	unsigned i;

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i)
		mmu_page_zap_pte(kvm, sp, sp->spt + i);
}

static void kvm_mmu_put_page(struct kvm_mmu_page *sp, u64 *parent_pte)
{
	mmu_page_remove_parent_pte(sp, parent_pte);
}

static void kvm_mmu_unlink_parents(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	u64 *parent_pte;

	while ((parent_pte = pte_list_next(&sp->parent_ptes, NULL)))
		drop_parent_pte(sp, parent_pte);
}

static int mmu_zap_unsync_children(struct kvm *kvm,
				   struct kvm_mmu_page *parent,
				   struct list_head *invalid_list)
{
	int i, zapped = 0;
	struct mmu_page_path parents;
	struct kvm_mmu_pages pages;

	if (parent->role.level == PT_PAGE_TABLE_LEVEL)
		return 0;

	kvm_mmu_pages_init(parent, &parents, &pages);
	while (mmu_unsync_walk(parent, &pages)) {
		struct kvm_mmu_page *sp;

		for_each_sp(pages, sp, parents, i) {
			kvm_mmu_prepare_zap_page(kvm, sp, invalid_list);
			mmu_pages_clear_parents(&parents);
			zapped++;
		}
		kvm_mmu_pages_init(parent, &parents, &pages);
	}

	return zapped;
}

static int kvm_mmu_prepare_zap_page(struct kvm *kvm, struct kvm_mmu_page *sp,
				    struct list_head *invalid_list)
{
	int ret;

	trace_kvm_mmu_prepare_zap_page(sp);
	++kvm->stat.mmu_shadow_zapped;
	ret = mmu_zap_unsync_children(kvm, sp, invalid_list);
	kvm_mmu_page_unlink_children(kvm, sp);
	kvm_mmu_unlink_parents(kvm, sp);
	if (!sp->role.invalid && !sp->role.direct)
		unaccount_shadowed(kvm, sp->gfn);
	if (sp->unsync)
		kvm_unlink_unsync_page(kvm, sp);
	if (!sp->root_count) {
		/* Count self */
		ret++;
		list_move(&sp->link, invalid_list);
		kvm_mod_used_mmu_pages(kvm, -1);
	} else {
		list_move(&sp->link, &kvm->arch.active_mmu_pages);
		kvm_reload_remote_mmus(kvm);
	}

	sp->role.invalid = 1;
	return ret;
}

static void kvm_mmu_isolate_pages(struct list_head *invalid_list)
{
	struct kvm_mmu_page *sp;

	list_for_each_entry(sp, invalid_list, link)
		kvm_mmu_isolate_page(sp);
}

static void free_pages_rcu(struct rcu_head *head)
{
	struct kvm_mmu_page *next, *sp;

	sp = container_of(head, struct kvm_mmu_page, rcu);
	while (sp) {
		if (!list_empty(&sp->link))
			next = list_first_entry(&sp->link,
				      struct kvm_mmu_page, link);
		else
			next = NULL;
		kvm_mmu_free_page(sp);
		sp = next;
	}
}

static void kvm_mmu_commit_zap_page(struct kvm *kvm,
				    struct list_head *invalid_list)
{
	struct kvm_mmu_page *sp;

	if (list_empty(invalid_list))
		return;

	kvm_flush_remote_tlbs(kvm);

	if (atomic_read(&kvm->arch.reader_counter)) {
		kvm_mmu_isolate_pages(invalid_list);
		sp = list_first_entry(invalid_list, struct kvm_mmu_page, link);
		list_del_init(invalid_list);

		trace_kvm_mmu_delay_free_pages(sp);
		call_rcu(&sp->rcu, free_pages_rcu);
		return;
	}

	do {
		sp = list_first_entry(invalid_list, struct kvm_mmu_page, link);
		WARN_ON(!sp->role.invalid || sp->root_count);
		kvm_mmu_isolate_page(sp);
		kvm_mmu_free_page(sp);
	} while (!list_empty(invalid_list));

}

/*
 * Changing the number of mmu pages allocated to the vm
 * Note: if goal_nr_mmu_pages is too small, you will get dead lock
 */
void kvm_mmu_change_mmu_pages(struct kvm *kvm, unsigned int goal_nr_mmu_pages)
{
	LIST_HEAD(invalid_list);
	/*
	 * If we set the number of mmu pages to be smaller be than the
	 * number of actived pages , we must to free some mmu pages before we
	 * change the value
	 */

	if (kvm->arch.n_used_mmu_pages > goal_nr_mmu_pages) {
		while (kvm->arch.n_used_mmu_pages > goal_nr_mmu_pages &&
			!list_empty(&kvm->arch.active_mmu_pages)) {
			struct kvm_mmu_page *page;

			page = container_of(kvm->arch.active_mmu_pages.prev,
					    struct kvm_mmu_page, link);
			kvm_mmu_prepare_zap_page(kvm, page, &invalid_list);
		}
		kvm_mmu_commit_zap_page(kvm, &invalid_list);
		goal_nr_mmu_pages = kvm->arch.n_used_mmu_pages;
	}

	kvm->arch.n_max_mmu_pages = goal_nr_mmu_pages;
}

int kvm_mmu_unprotect_page(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_mmu_page *sp;
	struct hlist_node *node;
	LIST_HEAD(invalid_list);
	int r;

	pgprintk("%s: looking for gfn %llx\n", __func__, gfn);
	r = 0;
	spin_lock(&kvm->mmu_lock);
	for_each_gfn_indirect_valid_sp(kvm, sp, gfn, node) {
		pgprintk("%s: gfn %llx role %x\n", __func__, gfn,
			 sp->role.word);
		r = 1;
		kvm_mmu_prepare_zap_page(kvm, sp, &invalid_list);
	}
	kvm_mmu_commit_zap_page(kvm, &invalid_list);
	spin_unlock(&kvm->mmu_lock);

	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_unprotect_page);

static void page_header_update_slot(struct kvm *kvm, void *pte, gfn_t gfn)
{
	int slot = memslot_id(kvm, gfn);
	struct kvm_mmu_page *sp = page_header(__pa(pte));

	__set_bit(slot, sp->slot_bitmap);
}

/*
 * The function is based on mtrr_type_lookup() in
 * arch/x86/kernel/cpu/mtrr/generic.c
 */
static int get_mtrr_type(struct mtrr_state_type *mtrr_state,
			 u64 start, u64 end)
{
	int i;
	u64 base, mask;
	u8 prev_match, curr_match;
	int num_var_ranges = KVM_NR_VAR_MTRR;

	if (!mtrr_state->enabled)
		return 0xFF;

	/* Make end inclusive end, instead of exclusive */
	end--;

	/* Look in fixed ranges. Just return the type as per start */
	if (mtrr_state->have_fixed && (start < 0x100000)) {
		int idx;

		if (start < 0x80000) {
			idx = 0;
			idx += (start >> 16);
			return mtrr_state->fixed_ranges[idx];
		} else if (start < 0xC0000) {
			idx = 1 * 8;
			idx += ((start - 0x80000) >> 14);
			return mtrr_state->fixed_ranges[idx];
		} else if (start < 0x1000000) {
			idx = 3 * 8;
			idx += ((start - 0xC0000) >> 12);
			return mtrr_state->fixed_ranges[idx];
		}
	}

	/*
	 * Look in variable ranges
	 * Look of multiple ranges matching this address and pick type
	 * as per MTRR precedence
	 */
	if (!(mtrr_state->enabled & 2))
		return mtrr_state->def_type;

	prev_match = 0xFF;
	for (i = 0; i < num_var_ranges; ++i) {
		unsigned short start_state, end_state;

		if (!(mtrr_state->var_ranges[i].mask_lo & (1 << 11)))
			continue;

		base = (((u64)mtrr_state->var_ranges[i].base_hi) << 32) +
		       (mtrr_state->var_ranges[i].base_lo & PAGE_MASK);
		mask = (((u64)mtrr_state->var_ranges[i].mask_hi) << 32) +
		       (mtrr_state->var_ranges[i].mask_lo & PAGE_MASK);

		start_state = ((start & mask) == (base & mask));
		end_state = ((end & mask) == (base & mask));
		if (start_state != end_state)
			return 0xFE;

		if ((start & mask) != (base & mask))
			continue;

		curr_match = mtrr_state->var_ranges[i].base_lo & 0xff;
		if (prev_match == 0xFF) {
			prev_match = curr_match;
			continue;
		}

		if (prev_match == MTRR_TYPE_UNCACHABLE ||
		    curr_match == MTRR_TYPE_UNCACHABLE)
			return MTRR_TYPE_UNCACHABLE;

		if ((prev_match == MTRR_TYPE_WRBACK &&
		     curr_match == MTRR_TYPE_WRTHROUGH) ||
		    (prev_match == MTRR_TYPE_WRTHROUGH &&
		     curr_match == MTRR_TYPE_WRBACK)) {
			prev_match = MTRR_TYPE_WRTHROUGH;
			curr_match = MTRR_TYPE_WRTHROUGH;
		}

		if (prev_match != curr_match)
			return MTRR_TYPE_UNCACHABLE;
	}

	if (prev_match != 0xFF)
		return prev_match;

	return mtrr_state->def_type;
}

u8 kvm_get_guest_memory_type(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	u8 mtrr;

	mtrr = get_mtrr_type(&vcpu->arch.mtrr_state, gfn << PAGE_SHIFT,
			     (gfn << PAGE_SHIFT) + PAGE_SIZE);
	if (mtrr == 0xfe || mtrr == 0xff)
		mtrr = MTRR_TYPE_WRBACK;
	return mtrr;
}
EXPORT_SYMBOL_GPL(kvm_get_guest_memory_type);

static void __kvm_unsync_page(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp)
{
	trace_kvm_mmu_unsync_page(sp);
	++vcpu->kvm->stat.mmu_unsync;
	sp->unsync = 1;

	kvm_mmu_mark_parents_unsync(sp);
}

static void kvm_unsync_pages(struct kvm_vcpu *vcpu,  gfn_t gfn)
{
	struct kvm_mmu_page *s;
	struct hlist_node *node;

	for_each_gfn_indirect_valid_sp(vcpu->kvm, s, gfn, node) {
		if (s->unsync)
			continue;
		WARN_ON(s->role.level != PT_PAGE_TABLE_LEVEL);
		__kvm_unsync_page(vcpu, s);
	}
}

static int mmu_need_write_protect(struct kvm_vcpu *vcpu, gfn_t gfn,
				  bool can_unsync)
{
	struct kvm_mmu_page *s;
	struct hlist_node *node;
	bool need_unsync = false;

	for_each_gfn_indirect_valid_sp(vcpu->kvm, s, gfn, node) {
		if (!can_unsync)
			return 1;

		if (s->role.level != PT_PAGE_TABLE_LEVEL)
			return 1;

		if (!need_unsync && !s->unsync) {
			need_unsync = true;
		}
	}
	if (need_unsync)
		kvm_unsync_pages(vcpu, gfn);
	return 0;
}

static int set_spte(struct kvm_vcpu *vcpu, u64 *sptep,
		    unsigned pte_access, int user_fault,
		    int write_fault, int level,
		    gfn_t gfn, pfn_t pfn, bool speculative,
		    bool can_unsync, bool host_writable)
{
	u64 spte, entry = *sptep;
	int ret = 0;

	if (set_mmio_spte(sptep, gfn, pfn, pte_access))
		return 0;

	spte = PT_PRESENT_MASK;
	if (!speculative)
		spte |= shadow_accessed_mask;

	if (pte_access & ACC_EXEC_MASK)
		spte |= shadow_x_mask;
	else
		spte |= shadow_nx_mask;
	if (pte_access & ACC_USER_MASK)
		spte |= shadow_user_mask;
	if (level > PT_PAGE_TABLE_LEVEL)
		spte |= PT_PAGE_SIZE_MASK;
	if (tdp_enabled)
		spte |= kvm_x86_ops->get_mt_mask(vcpu, gfn,
			kvm_is_mmio_pfn(pfn));

	if (host_writable)
		spte |= SPTE_HOST_WRITEABLE;
	else
		pte_access &= ~ACC_WRITE_MASK;

	spte |= (u64)pfn << PAGE_SHIFT;

	if ((pte_access & ACC_WRITE_MASK)
	    || (!vcpu->arch.mmu.direct_map && write_fault
		&& !is_write_protection(vcpu) && !user_fault)) {

		if (level > PT_PAGE_TABLE_LEVEL &&
		    has_wrprotected_page(vcpu->kvm, gfn, level)) {
			ret = 1;
			drop_spte(vcpu->kvm, sptep);
			goto done;
		}

		spte |= PT_WRITABLE_MASK;

		if (!vcpu->arch.mmu.direct_map
		    && !(pte_access & ACC_WRITE_MASK)) {
			spte &= ~PT_USER_MASK;
			/*
			 * If we converted a user page to a kernel page,
			 * so that the kernel can write to it when cr0.wp=0,
			 * then we should prevent the kernel from executing it
			 * if SMEP is enabled.
			 */
			if (kvm_read_cr4_bits(vcpu, X86_CR4_SMEP))
				spte |= PT64_NX_MASK;
		}

		/*
		 * Optimization: for pte sync, if spte was writable the hash
		 * lookup is unnecessary (and expensive). Write protection
		 * is responsibility of mmu_get_page / kvm_sync_page.
		 * Same reasoning can be applied to dirty page accounting.
		 */
		if (!can_unsync && is_writable_pte(*sptep))
			goto set_pte;

		if (mmu_need_write_protect(vcpu, gfn, can_unsync)) {
			pgprintk("%s: found shadow page for %llx, marking ro\n",
				 __func__, gfn);
			ret = 1;
			pte_access &= ~ACC_WRITE_MASK;
			if (is_writable_pte(spte))
				spte &= ~PT_WRITABLE_MASK;
		}
	}

	if (pte_access & ACC_WRITE_MASK)
		mark_page_dirty(vcpu->kvm, gfn);

set_pte:
	mmu_spte_update(sptep, spte);
	/*
	 * If we overwrite a writable spte with a read-only one we
	 * should flush remote TLBs. Otherwise rmap_write_protect
	 * will find a read-only spte, even though the writable spte
	 * might be cached on a CPU's TLB.
	 */
	if (is_writable_pte(entry) && !is_writable_pte(*sptep))
		kvm_flush_remote_tlbs(vcpu->kvm);
done:
	return ret;
}

static void mmu_set_spte(struct kvm_vcpu *vcpu, u64 *sptep,
			 unsigned pt_access, unsigned pte_access,
			 int user_fault, int write_fault,
			 int *emulate, int level, gfn_t gfn,
			 pfn_t pfn, bool speculative,
			 bool host_writable)
{
	int was_rmapped = 0;
	int rmap_count;

	pgprintk("%s: spte %llx access %x write_fault %d"
		 " user_fault %d gfn %llx\n",
		 __func__, *sptep, pt_access,
		 write_fault, user_fault, gfn);

	if (is_rmap_spte(*sptep)) {
		/*
		 * If we overwrite a PTE page pointer with a 2MB PMD, unlink
		 * the parent of the now unreachable PTE.
		 */
		if (level > PT_PAGE_TABLE_LEVEL &&
		    !is_large_pte(*sptep)) {
			struct kvm_mmu_page *child;
			u64 pte = *sptep;

			child = page_header(pte & PT64_BASE_ADDR_MASK);
			drop_parent_pte(child, sptep);
			kvm_flush_remote_tlbs(vcpu->kvm);
		} else if (pfn != spte_to_pfn(*sptep)) {
			pgprintk("hfn old %llx new %llx\n",
				 spte_to_pfn(*sptep), pfn);
			drop_spte(vcpu->kvm, sptep);
			kvm_flush_remote_tlbs(vcpu->kvm);
		} else
			was_rmapped = 1;
	}

	if (set_spte(vcpu, sptep, pte_access, user_fault, write_fault,
		      level, gfn, pfn, speculative, true,
		      host_writable)) {
		if (write_fault)
			*emulate = 1;
		kvm_mmu_flush_tlb(vcpu);
	}

	if (unlikely(is_mmio_spte(*sptep) && emulate))
		*emulate = 1;

	pgprintk("%s: setting spte %llx\n", __func__, *sptep);
	pgprintk("instantiating %s PTE (%s) at %llx (%llx) addr %p\n",
		 is_large_pte(*sptep)? "2MB" : "4kB",
		 *sptep & PT_PRESENT_MASK ?"RW":"R", gfn,
		 *sptep, sptep);
	if (!was_rmapped && is_large_pte(*sptep))
		++vcpu->kvm->stat.lpages;

	if (is_shadow_present_pte(*sptep)) {
		page_header_update_slot(vcpu->kvm, sptep, gfn);
		if (!was_rmapped) {
			rmap_count = rmap_add(vcpu, sptep, gfn);
			if (rmap_count > RMAP_RECYCLE_THRESHOLD)
				rmap_recycle(vcpu, sptep, gfn);
		}
	}
	kvm_release_pfn_clean(pfn);
}

static void nonpaging_new_cr3(struct kvm_vcpu *vcpu)
{
}

static pfn_t pte_prefetch_gfn_to_pfn(struct kvm_vcpu *vcpu, gfn_t gfn,
				     bool no_dirty_log)
{
	struct kvm_memory_slot *slot;
	unsigned long hva;

	slot = gfn_to_memslot_dirty_bitmap(vcpu, gfn, no_dirty_log);
	if (!slot) {
		get_page(fault_page);
		return page_to_pfn(fault_page);
	}

	hva = gfn_to_hva_memslot(slot, gfn);

	return hva_to_pfn_atomic(vcpu->kvm, hva);
}

static int direct_pte_prefetch_many(struct kvm_vcpu *vcpu,
				    struct kvm_mmu_page *sp,
				    u64 *start, u64 *end)
{
	struct page *pages[PTE_PREFETCH_NUM];
	unsigned access = sp->role.access;
	int i, ret;
	gfn_t gfn;

	gfn = kvm_mmu_page_get_gfn(sp, start - sp->spt);
	if (!gfn_to_memslot_dirty_bitmap(vcpu, gfn, access & ACC_WRITE_MASK))
		return -1;

	ret = gfn_to_page_many_atomic(vcpu->kvm, gfn, pages, end - start);
	if (ret <= 0)
		return -1;

	for (i = 0; i < ret; i++, gfn++, start++)
		mmu_set_spte(vcpu, start, ACC_ALL,
			     access, 0, 0, NULL,
			     sp->role.level, gfn,
			     page_to_pfn(pages[i]), true, true);

	return 0;
}

static void __direct_pte_prefetch(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *sp, u64 *sptep)
{
	u64 *spte, *start = NULL;
	int i;

	WARN_ON(!sp->role.direct);

	i = (sptep - sp->spt) & ~(PTE_PREFETCH_NUM - 1);
	spte = sp->spt + i;

	for (i = 0; i < PTE_PREFETCH_NUM; i++, spte++) {
		if (is_shadow_present_pte(*spte) || spte == sptep) {
			if (!start)
				continue;
			if (direct_pte_prefetch_many(vcpu, sp, start, spte) < 0)
				break;
			start = NULL;
		} else if (!start)
			start = spte;
	}
}

static void direct_pte_prefetch(struct kvm_vcpu *vcpu, u64 *sptep)
{
	struct kvm_mmu_page *sp;

	/*
	 * Since it's no accessed bit on EPT, it's no way to
	 * distinguish between actually accessed translations
	 * and prefetched, so disable pte prefetch if EPT is
	 * enabled.
	 */
	if (!shadow_accessed_mask)
		return;

	sp = page_header(__pa(sptep));
	if (sp->role.level > PT_PAGE_TABLE_LEVEL)
		return;

	__direct_pte_prefetch(vcpu, sp, sptep);
}

static int __direct_map(struct kvm_vcpu *vcpu, gpa_t v, int write,
			int map_writable, int level, gfn_t gfn, pfn_t pfn,
			bool prefault)
{
	struct kvm_shadow_walk_iterator iterator;
	struct kvm_mmu_page *sp;
	int emulate = 0;
	gfn_t pseudo_gfn;

	if (!VALID_PAGE(vcpu->arch.mmu.root_hpa))
		return 0;

	for_each_shadow_entry(vcpu, (u64)gfn << PAGE_SHIFT, iterator) {
		if (iterator.level == level) {
			unsigned pte_access = ACC_ALL;

			mmu_set_spte(vcpu, iterator.sptep, ACC_ALL, pte_access,
				     0, write, &emulate,
				     level, gfn, pfn, prefault, map_writable);
			direct_pte_prefetch(vcpu, iterator.sptep);
			++vcpu->stat.pf_fixed;
			break;
		}

		if (!is_shadow_present_pte(*iterator.sptep)) {
			u64 base_addr = iterator.addr;

			base_addr &= PT64_LVL_ADDR_MASK(iterator.level);
			pseudo_gfn = base_addr >> PAGE_SHIFT;
			sp = kvm_mmu_get_page(vcpu, pseudo_gfn, iterator.addr,
					      iterator.level - 1,
					      1, ACC_ALL, iterator.sptep);
			if (!sp) {
				pgprintk("nonpaging_map: ENOMEM\n");
				kvm_release_pfn_clean(pfn);
				return -ENOMEM;
			}

			mmu_spte_set(iterator.sptep,
				     __pa(sp->spt)
				     | PT_PRESENT_MASK | PT_WRITABLE_MASK
				     | shadow_user_mask | shadow_x_mask
				     | shadow_accessed_mask);
		}
	}
	return emulate;
}

static void kvm_send_hwpoison_signal(unsigned long address, struct task_struct *tsk)
{
	siginfo_t info;

	info.si_signo	= SIGBUS;
	info.si_errno	= 0;
	info.si_code	= BUS_MCEERR_AR;
	info.si_addr	= (void __user *)address;
	info.si_addr_lsb = PAGE_SHIFT;

	send_sig_info(SIGBUS, &info, tsk);
}

static int kvm_handle_bad_page(struct kvm_vcpu *vcpu, gfn_t gfn, pfn_t pfn)
{
	kvm_release_pfn_clean(pfn);
	if (is_hwpoison_pfn(pfn)) {
		kvm_send_hwpoison_signal(gfn_to_hva(vcpu->kvm, gfn), current);
		return 0;
	}

	return -EFAULT;
}

static void transparent_hugepage_adjust(struct kvm_vcpu *vcpu,
					gfn_t *gfnp, pfn_t *pfnp, int *levelp)
{
	pfn_t pfn = *pfnp;
	gfn_t gfn = *gfnp;
	int level = *levelp;

	/*
	 * Check if it's a transparent hugepage. If this would be an
	 * hugetlbfs page, level wouldn't be set to
	 * PT_PAGE_TABLE_LEVEL and there would be no adjustment done
	 * here.
	 */
	if (!is_error_pfn(pfn) && !kvm_is_mmio_pfn(pfn) &&
	    level == PT_PAGE_TABLE_LEVEL &&
	    PageTransCompound(pfn_to_page(pfn)) &&
	    !has_wrprotected_page(vcpu->kvm, gfn, PT_DIRECTORY_LEVEL)) {
		unsigned long mask;
		/*
		 * mmu_notifier_retry was successful and we hold the
		 * mmu_lock here, so the pmd can't become splitting
		 * from under us, and in turn
		 * __split_huge_page_refcount() can't run from under
		 * us and we can safely transfer the refcount from
		 * PG_tail to PG_head as we switch the pfn to tail to
		 * head.
		 */
		*levelp = level = PT_DIRECTORY_LEVEL;
		mask = KVM_PAGES_PER_HPAGE(level) - 1;
		VM_BUG_ON((gfn & mask) != (pfn & mask));
		if (pfn & mask) {
			gfn &= ~mask;
			*gfnp = gfn;
			kvm_release_pfn_clean(pfn);
			pfn &= ~mask;
			if (!get_page_unless_zero(pfn_to_page(pfn)))
				BUG();
			*pfnp = pfn;
		}
	}
}

static bool mmu_invalid_pfn(pfn_t pfn)
{
	return unlikely(is_invalid_pfn(pfn));
}

static bool handle_abnormal_pfn(struct kvm_vcpu *vcpu, gva_t gva, gfn_t gfn,
				pfn_t pfn, unsigned access, int *ret_val)
{
	bool ret = true;

	/* The pfn is invalid, report the error! */
	if (unlikely(is_invalid_pfn(pfn))) {
		*ret_val = kvm_handle_bad_page(vcpu, gfn, pfn);
		goto exit;
	}

	if (unlikely(is_noslot_pfn(pfn)))
		vcpu_cache_mmio_info(vcpu, gva, gfn, access);

	ret = false;
exit:
	return ret;
}

static bool try_async_pf(struct kvm_vcpu *vcpu, bool prefault, gfn_t gfn,
			 gva_t gva, pfn_t *pfn, bool write, bool *writable);

static int nonpaging_map(struct kvm_vcpu *vcpu, gva_t v, int write, gfn_t gfn,
			 bool prefault)
{
	int r;
	int level;
	int force_pt_level;
	pfn_t pfn;
	unsigned long mmu_seq;
	bool map_writable;

	force_pt_level = mapping_level_dirty_bitmap(vcpu, gfn);
	if (likely(!force_pt_level)) {
		level = mapping_level(vcpu, gfn);
		/*
		 * This path builds a PAE pagetable - so we can map
		 * 2mb pages at maximum. Therefore check if the level
		 * is larger than that.
		 */
		if (level > PT_DIRECTORY_LEVEL)
			level = PT_DIRECTORY_LEVEL;

		gfn &= ~(KVM_PAGES_PER_HPAGE(level) - 1);
	} else
		level = PT_PAGE_TABLE_LEVEL;

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	smp_rmb();

	if (try_async_pf(vcpu, prefault, gfn, v, &pfn, write, &map_writable))
		return 0;

	if (handle_abnormal_pfn(vcpu, v, gfn, pfn, ACC_ALL, &r))
		return r;

	spin_lock(&vcpu->kvm->mmu_lock);
	if (mmu_notifier_retry(vcpu, mmu_seq))
		goto out_unlock;
	kvm_mmu_free_some_pages(vcpu);
	if (likely(!force_pt_level))
		transparent_hugepage_adjust(vcpu, &gfn, &pfn, &level);
	r = __direct_map(vcpu, v, write, map_writable, level, gfn, pfn,
			 prefault);
	spin_unlock(&vcpu->kvm->mmu_lock);


	return r;

out_unlock:
	spin_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	return 0;
}


static void mmu_free_roots(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_mmu_page *sp;
	LIST_HEAD(invalid_list);

	if (!VALID_PAGE(vcpu->arch.mmu.root_hpa))
		return;
	spin_lock(&vcpu->kvm->mmu_lock);
	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL &&
	    (vcpu->arch.mmu.root_level == PT64_ROOT_LEVEL ||
	     vcpu->arch.mmu.direct_map)) {
		hpa_t root = vcpu->arch.mmu.root_hpa;

		sp = page_header(root);
		--sp->root_count;
		if (!sp->root_count && sp->role.invalid) {
			kvm_mmu_prepare_zap_page(vcpu->kvm, sp, &invalid_list);
			kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
		}
		vcpu->arch.mmu.root_hpa = INVALID_PAGE;
		spin_unlock(&vcpu->kvm->mmu_lock);
		return;
	}
	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu.pae_root[i];

		if (root) {
			root &= PT64_BASE_ADDR_MASK;
			sp = page_header(root);
			--sp->root_count;
			if (!sp->root_count && sp->role.invalid)
				kvm_mmu_prepare_zap_page(vcpu->kvm, sp,
							 &invalid_list);
		}
		vcpu->arch.mmu.pae_root[i] = INVALID_PAGE;
	}
	kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
	spin_unlock(&vcpu->kvm->mmu_lock);
	vcpu->arch.mmu.root_hpa = INVALID_PAGE;
}

static int mmu_check_root(struct kvm_vcpu *vcpu, gfn_t root_gfn)
{
	int ret = 0;

	if (!kvm_is_visible_gfn(vcpu->kvm, root_gfn)) {
		kvm_make_request(KVM_REQ_TRIPLE_FAULT, vcpu);
		ret = 1;
	}

	return ret;
}

static int mmu_alloc_direct_roots(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *sp;
	unsigned i;

	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL) {
		spin_lock(&vcpu->kvm->mmu_lock);
		kvm_mmu_free_some_pages(vcpu);
		sp = kvm_mmu_get_page(vcpu, 0, 0, PT64_ROOT_LEVEL,
				      1, ACC_ALL, NULL);
		++sp->root_count;
		spin_unlock(&vcpu->kvm->mmu_lock);
		vcpu->arch.mmu.root_hpa = __pa(sp->spt);
	} else if (vcpu->arch.mmu.shadow_root_level == PT32E_ROOT_LEVEL) {
		for (i = 0; i < 4; ++i) {
			hpa_t root = vcpu->arch.mmu.pae_root[i];

			ASSERT(!VALID_PAGE(root));
			spin_lock(&vcpu->kvm->mmu_lock);
			kvm_mmu_free_some_pages(vcpu);
			sp = kvm_mmu_get_page(vcpu, i << (30 - PAGE_SHIFT),
					      i << 30,
					      PT32_ROOT_LEVEL, 1, ACC_ALL,
					      NULL);
			root = __pa(sp->spt);
			++sp->root_count;
			spin_unlock(&vcpu->kvm->mmu_lock);
			vcpu->arch.mmu.pae_root[i] = root | PT_PRESENT_MASK;
		}
		vcpu->arch.mmu.root_hpa = __pa(vcpu->arch.mmu.pae_root);
	} else
		BUG();

	return 0;
}

static int mmu_alloc_shadow_roots(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *sp;
	u64 pdptr, pm_mask;
	gfn_t root_gfn;
	int i;

	root_gfn = vcpu->arch.mmu.get_cr3(vcpu) >> PAGE_SHIFT;

	if (mmu_check_root(vcpu, root_gfn))
		return 1;

	/*
	 * Do we shadow a long mode page table? If so we need to
	 * write-protect the guests page table root.
	 */
	if (vcpu->arch.mmu.root_level == PT64_ROOT_LEVEL) {
		hpa_t root = vcpu->arch.mmu.root_hpa;

		ASSERT(!VALID_PAGE(root));

		spin_lock(&vcpu->kvm->mmu_lock);
		kvm_mmu_free_some_pages(vcpu);
		sp = kvm_mmu_get_page(vcpu, root_gfn, 0, PT64_ROOT_LEVEL,
				      0, ACC_ALL, NULL);
		root = __pa(sp->spt);
		++sp->root_count;
		spin_unlock(&vcpu->kvm->mmu_lock);
		vcpu->arch.mmu.root_hpa = root;
		return 0;
	}

	/*
	 * We shadow a 32 bit page table. This may be a legacy 2-level
	 * or a PAE 3-level page table. In either case we need to be aware that
	 * the shadow page table may be a PAE or a long mode page table.
	 */
	pm_mask = PT_PRESENT_MASK;
	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL)
		pm_mask |= PT_ACCESSED_MASK | PT_WRITABLE_MASK | PT_USER_MASK;

	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu.pae_root[i];

		ASSERT(!VALID_PAGE(root));
		if (vcpu->arch.mmu.root_level == PT32E_ROOT_LEVEL) {
			pdptr = vcpu->arch.mmu.get_pdptr(vcpu, i);
			if (!is_present_gpte(pdptr)) {
				vcpu->arch.mmu.pae_root[i] = 0;
				continue;
			}
			root_gfn = pdptr >> PAGE_SHIFT;
			if (mmu_check_root(vcpu, root_gfn))
				return 1;
		}
		spin_lock(&vcpu->kvm->mmu_lock);
		kvm_mmu_free_some_pages(vcpu);
		sp = kvm_mmu_get_page(vcpu, root_gfn, i << 30,
				      PT32_ROOT_LEVEL, 0,
				      ACC_ALL, NULL);
		root = __pa(sp->spt);
		++sp->root_count;
		spin_unlock(&vcpu->kvm->mmu_lock);

		vcpu->arch.mmu.pae_root[i] = root | pm_mask;
	}
	vcpu->arch.mmu.root_hpa = __pa(vcpu->arch.mmu.pae_root);

	/*
	 * If we shadow a 32 bit page table with a long mode page
	 * table we enter this path.
	 */
	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL) {
		if (vcpu->arch.mmu.lm_root == NULL) {
			/*
			 * The additional page necessary for this is only
			 * allocated on demand.
			 */

			u64 *lm_root;

			lm_root = (void*)get_zeroed_page(GFP_KERNEL);
			if (lm_root == NULL)
				return 1;

			lm_root[0] = __pa(vcpu->arch.mmu.pae_root) | pm_mask;

			vcpu->arch.mmu.lm_root = lm_root;
		}

		vcpu->arch.mmu.root_hpa = __pa(vcpu->arch.mmu.lm_root);
	}

	return 0;
}

static int mmu_alloc_roots(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.mmu.direct_map)
		return mmu_alloc_direct_roots(vcpu);
	else
		return mmu_alloc_shadow_roots(vcpu);
}

static void mmu_sync_roots(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_mmu_page *sp;

	if (vcpu->arch.mmu.direct_map)
		return;

	if (!VALID_PAGE(vcpu->arch.mmu.root_hpa))
		return;

	vcpu_clear_mmio_info(vcpu, MMIO_GVA_ANY);
	kvm_mmu_audit(vcpu, AUDIT_PRE_SYNC);
	if (vcpu->arch.mmu.root_level == PT64_ROOT_LEVEL) {
		hpa_t root = vcpu->arch.mmu.root_hpa;
		sp = page_header(root);
		mmu_sync_children(vcpu, sp);
		kvm_mmu_audit(vcpu, AUDIT_POST_SYNC);
		return;
	}
	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu.pae_root[i];

		if (root && VALID_PAGE(root)) {
			root &= PT64_BASE_ADDR_MASK;
			sp = page_header(root);
			mmu_sync_children(vcpu, sp);
		}
	}
	kvm_mmu_audit(vcpu, AUDIT_POST_SYNC);
}

void kvm_mmu_sync_roots(struct kvm_vcpu *vcpu)
{
	spin_lock(&vcpu->kvm->mmu_lock);
	mmu_sync_roots(vcpu);
	spin_unlock(&vcpu->kvm->mmu_lock);
}

static gpa_t nonpaging_gva_to_gpa(struct kvm_vcpu *vcpu, gva_t vaddr,
				  u32 access, struct x86_exception *exception)
{
	if (exception)
		exception->error_code = 0;
	return vaddr;
}

static gpa_t nonpaging_gva_to_gpa_nested(struct kvm_vcpu *vcpu, gva_t vaddr,
					 u32 access,
					 struct x86_exception *exception)
{
	if (exception)
		exception->error_code = 0;
	return vcpu->arch.nested_mmu.translate_gpa(vcpu, vaddr, access);
}

static bool quickly_check_mmio_pf(struct kvm_vcpu *vcpu, u64 addr, bool direct)
{
	if (direct)
		return vcpu_match_mmio_gpa(vcpu, addr);

	return vcpu_match_mmio_gva(vcpu, addr);
}


/*
 * On direct hosts, the last spte is only allows two states
 * for mmio page fault:
 *   - It is the mmio spte
 *   - It is zapped or it is being zapped.
 *
 * This function completely checks the spte when the last spte
 * is not the mmio spte.
 */
static bool check_direct_spte_mmio_pf(u64 spte)
{
	return __check_direct_spte_mmio_pf(spte);
}

static u64 walk_shadow_page_get_mmio_spte(struct kvm_vcpu *vcpu, u64 addr)
{
	struct kvm_shadow_walk_iterator iterator;
	u64 spte = 0ull;

	walk_shadow_page_lockless_begin(vcpu);
	for_each_shadow_entry_lockless(vcpu, addr, iterator, spte)
		if (!is_shadow_present_pte(spte))
			break;
	walk_shadow_page_lockless_end(vcpu);

	return spte;
}

/*
 * If it is a real mmio page fault, return 1 and emulat the instruction
 * directly, return 0 to let CPU fault again on the address, -1 is
 * returned if bug is detected.
 */
int handle_mmio_page_fault_common(struct kvm_vcpu *vcpu, u64 addr, bool direct)
{
	u64 spte;

	if (quickly_check_mmio_pf(vcpu, addr, direct))
		return 1;

	spte = walk_shadow_page_get_mmio_spte(vcpu, addr);

	if (is_mmio_spte(spte)) {
		gfn_t gfn = get_mmio_spte_gfn(spte);
		unsigned access = get_mmio_spte_access(spte);

		if (direct)
			addr = 0;

		trace_handle_mmio_page_fault(addr, gfn, access);
		vcpu_cache_mmio_info(vcpu, addr, gfn, access);
		return 1;
	}

	/*
	 * It's ok if the gva is remapped by other cpus on shadow guest,
	 * it's a BUG if the gfn is not a mmio page.
	 */
	if (direct && !check_direct_spte_mmio_pf(spte))
		return -1;

	/*
	 * If the page table is zapped by other cpus, let CPU fault again on
	 * the address.
	 */
	return 0;
}
EXPORT_SYMBOL_GPL(handle_mmio_page_fault_common);

static int handle_mmio_page_fault(struct kvm_vcpu *vcpu, u64 addr,
				  u32 error_code, bool direct)
{
	int ret;

	ret = handle_mmio_page_fault_common(vcpu, addr, direct);
	WARN_ON(ret < 0);
	return ret;
}

static int nonpaging_page_fault(struct kvm_vcpu *vcpu, gva_t gva,
				u32 error_code, bool prefault)
{
	gfn_t gfn;
	int r;

	pgprintk("%s: gva %lx error %x\n", __func__, gva, error_code);

	if (unlikely(error_code & PFERR_RSVD_MASK))
		return handle_mmio_page_fault(vcpu, gva, error_code, true);

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	ASSERT(vcpu);
	ASSERT(VALID_PAGE(vcpu->arch.mmu.root_hpa));

	gfn = gva >> PAGE_SHIFT;

	return nonpaging_map(vcpu, gva & PAGE_MASK,
			     error_code & PFERR_WRITE_MASK, gfn, prefault);
}

static int kvm_arch_setup_async_pf(struct kvm_vcpu *vcpu, gva_t gva, gfn_t gfn)
{
	struct kvm_arch_async_pf arch;

	arch.token = (vcpu->arch.apf.id++ << 12) | vcpu->vcpu_id;
	arch.gfn = gfn;
	arch.direct_map = vcpu->arch.mmu.direct_map;
	arch.cr3 = vcpu->arch.mmu.get_cr3(vcpu);

	return kvm_setup_async_pf(vcpu, gva, gfn, &arch);
}

static bool can_do_async_pf(struct kvm_vcpu *vcpu)
{
	if (unlikely(!irqchip_in_kernel(vcpu->kvm) ||
		     kvm_event_needs_reinjection(vcpu)))
		return false;

	return kvm_x86_ops->interrupt_allowed(vcpu);
}

static bool try_async_pf(struct kvm_vcpu *vcpu, bool prefault, gfn_t gfn,
			 gva_t gva, pfn_t *pfn, bool write, bool *writable)
{
	bool async;

	*pfn = gfn_to_pfn_async(vcpu->kvm, gfn, &async, write, writable);

	if (!async)
		return false; /* *pfn has correct page already */

	put_page(pfn_to_page(*pfn));

	if (!prefault && can_do_async_pf(vcpu)) {
		trace_kvm_try_async_get_page(gva, gfn);
		if (kvm_find_async_pf_gfn(vcpu, gfn)) {
			trace_kvm_async_pf_doublefault(gva, gfn);
			kvm_make_request(KVM_REQ_APF_HALT, vcpu);
			return true;
		} else if (kvm_arch_setup_async_pf(vcpu, gva, gfn))
			return true;
	}

	*pfn = gfn_to_pfn_prot(vcpu->kvm, gfn, write, writable);

	return false;
}

static int tdp_page_fault(struct kvm_vcpu *vcpu, gva_t gpa, u32 error_code,
			  bool prefault)
{
	pfn_t pfn;
	int r;
	int level;
	int force_pt_level;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	unsigned long mmu_seq;
	int write = error_code & PFERR_WRITE_MASK;
	bool map_writable;

	ASSERT(vcpu);
	ASSERT(VALID_PAGE(vcpu->arch.mmu.root_hpa));

	if (unlikely(error_code & PFERR_RSVD_MASK))
		return handle_mmio_page_fault(vcpu, gpa, error_code, true);

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	force_pt_level = mapping_level_dirty_bitmap(vcpu, gfn);
	if (likely(!force_pt_level)) {
		level = mapping_level(vcpu, gfn);
		gfn &= ~(KVM_PAGES_PER_HPAGE(level) - 1);
	} else
		level = PT_PAGE_TABLE_LEVEL;

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	smp_rmb();

	if (try_async_pf(vcpu, prefault, gfn, gpa, &pfn, write, &map_writable))
		return 0;

	if (handle_abnormal_pfn(vcpu, 0, gfn, pfn, ACC_ALL, &r))
		return r;

	spin_lock(&vcpu->kvm->mmu_lock);
	if (mmu_notifier_retry(vcpu, mmu_seq))
		goto out_unlock;
	kvm_mmu_free_some_pages(vcpu);
	if (likely(!force_pt_level))
		transparent_hugepage_adjust(vcpu, &gfn, &pfn, &level);
	r = __direct_map(vcpu, gpa, write, map_writable,
			 level, gfn, pfn, prefault);
	spin_unlock(&vcpu->kvm->mmu_lock);

	return r;

out_unlock:
	spin_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	return 0;
}

static void nonpaging_free(struct kvm_vcpu *vcpu)
{
	mmu_free_roots(vcpu);
}

static int nonpaging_init_context(struct kvm_vcpu *vcpu,
				  struct kvm_mmu *context)
{
	context->new_cr3 = nonpaging_new_cr3;
	context->page_fault = nonpaging_page_fault;
	context->gva_to_gpa = nonpaging_gva_to_gpa;
	context->free = nonpaging_free;
	context->sync_page = nonpaging_sync_page;
	context->invlpg = nonpaging_invlpg;
	context->update_pte = nonpaging_update_pte;
	context->root_level = 0;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->root_hpa = INVALID_PAGE;
	context->direct_map = true;
	context->nx = false;
	return 0;
}

void kvm_mmu_flush_tlb(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.tlb_flush;
	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
}

static void paging_new_cr3(struct kvm_vcpu *vcpu)
{
	pgprintk("%s: cr3 %lx\n", __func__, kvm_read_cr3(vcpu));
	mmu_free_roots(vcpu);
}

static unsigned long get_cr3(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr3(vcpu);
}

static void inject_page_fault(struct kvm_vcpu *vcpu,
			      struct x86_exception *fault)
{
	vcpu->arch.mmu.inject_page_fault(vcpu, fault);
}

static void paging_free(struct kvm_vcpu *vcpu)
{
	nonpaging_free(vcpu);
}

static bool is_rsvd_bits_set(struct kvm_mmu *mmu, u64 gpte, int level)
{
	int bit7;

	bit7 = (gpte >> 7) & 1;
	return (gpte & mmu->rsvd_bits_mask[bit7][level-1]) != 0;
}

static bool sync_mmio_spte(u64 *sptep, gfn_t gfn, unsigned access,
			   int *nr_present)
{
	if (unlikely(is_mmio_spte(*sptep))) {
		if (gfn != get_mmio_spte_gfn(*sptep)) {
			mmu_spte_clear_no_track(sptep);
			return true;
		}

		(*nr_present)++;
		mark_mmio_spte(sptep, gfn, access);
		return true;
	}

	return false;
}

#define PTTYPE 64
#include "paging_tmpl.h"
#undef PTTYPE

#define PTTYPE 32
#include "paging_tmpl.h"
#undef PTTYPE

static void reset_rsvds_bits_mask(struct kvm_vcpu *vcpu,
				  struct kvm_mmu *context)
{
	int maxphyaddr = cpuid_maxphyaddr(vcpu);
	u64 exb_bit_rsvd = 0;

	if (!context->nx)
		exb_bit_rsvd = rsvd_bits(63, 63);
	switch (context->root_level) {
	case PT32_ROOT_LEVEL:
		/* no rsvd bits for 2 level 4K page table entries */
		context->rsvd_bits_mask[0][1] = 0;
		context->rsvd_bits_mask[0][0] = 0;
		context->rsvd_bits_mask[1][0] = context->rsvd_bits_mask[0][0];

		if (!is_pse(vcpu)) {
			context->rsvd_bits_mask[1][1] = 0;
			break;
		}

		if (is_cpuid_PSE36())
			/* 36bits PSE 4MB page */
			context->rsvd_bits_mask[1][1] = rsvd_bits(17, 21);
		else
			/* 32 bits PSE 4MB page */
			context->rsvd_bits_mask[1][1] = rsvd_bits(13, 21);
		break;
	case PT32E_ROOT_LEVEL:
		context->rsvd_bits_mask[0][2] =
			rsvd_bits(maxphyaddr, 63) |
			rsvd_bits(7, 8) | rsvd_bits(1, 2);	/* PDPTE */
		context->rsvd_bits_mask[0][1] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 62);	/* PDE */
		context->rsvd_bits_mask[0][0] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 62); 	/* PTE */
		context->rsvd_bits_mask[1][1] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 62) |
			rsvd_bits(13, 20);		/* large page */
		context->rsvd_bits_mask[1][0] = context->rsvd_bits_mask[0][0];
		break;
	case PT64_ROOT_LEVEL:
		context->rsvd_bits_mask[0][3] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 51) | rsvd_bits(7, 8);
		context->rsvd_bits_mask[0][2] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 51) | rsvd_bits(7, 8);
		context->rsvd_bits_mask[0][1] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 51);
		context->rsvd_bits_mask[0][0] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 51);
		context->rsvd_bits_mask[1][3] = context->rsvd_bits_mask[0][3];
		context->rsvd_bits_mask[1][2] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 51) |
			rsvd_bits(13, 29);
		context->rsvd_bits_mask[1][1] = exb_bit_rsvd |
			rsvd_bits(maxphyaddr, 51) |
			rsvd_bits(13, 20);		/* large page */
		context->rsvd_bits_mask[1][0] = context->rsvd_bits_mask[0][0];
		break;
	}
}

static int paging64_init_context_common(struct kvm_vcpu *vcpu,
					struct kvm_mmu *context,
					int level)
{
	context->nx = is_nx(vcpu);
	context->root_level = level;

	reset_rsvds_bits_mask(vcpu, context);

	ASSERT(is_pae(vcpu));
	context->new_cr3 = paging_new_cr3;
	context->page_fault = paging64_page_fault;
	context->gva_to_gpa = paging64_gva_to_gpa;
	context->sync_page = paging64_sync_page;
	context->invlpg = paging64_invlpg;
	context->update_pte = paging64_update_pte;
	context->free = paging_free;
	context->shadow_root_level = level;
	context->root_hpa = INVALID_PAGE;
	context->direct_map = false;
	return 0;
}

static int paging64_init_context(struct kvm_vcpu *vcpu,
				 struct kvm_mmu *context)
{
	return paging64_init_context_common(vcpu, context, PT64_ROOT_LEVEL);
}

static int paging32_init_context(struct kvm_vcpu *vcpu,
				 struct kvm_mmu *context)
{
	context->nx = false;
	context->root_level = PT32_ROOT_LEVEL;

	reset_rsvds_bits_mask(vcpu, context);

	context->new_cr3 = paging_new_cr3;
	context->page_fault = paging32_page_fault;
	context->gva_to_gpa = paging32_gva_to_gpa;
	context->free = paging_free;
	context->sync_page = paging32_sync_page;
	context->invlpg = paging32_invlpg;
	context->update_pte = paging32_update_pte;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->root_hpa = INVALID_PAGE;
	context->direct_map = false;
	return 0;
}

static int paging32E_init_context(struct kvm_vcpu *vcpu,
				  struct kvm_mmu *context)
{
	return paging64_init_context_common(vcpu, context, PT32E_ROOT_LEVEL);
}

static int init_kvm_tdp_mmu(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = vcpu->arch.walk_mmu;

	context->base_role.word = 0;
	context->new_cr3 = nonpaging_new_cr3;
	context->page_fault = tdp_page_fault;
	context->free = nonpaging_free;
	context->sync_page = nonpaging_sync_page;
	context->invlpg = nonpaging_invlpg;
	context->update_pte = nonpaging_update_pte;
	context->shadow_root_level = kvm_x86_ops->get_tdp_level();
	context->root_hpa = INVALID_PAGE;
	context->direct_map = true;
	context->set_cr3 = kvm_x86_ops->set_tdp_cr3;
	context->get_cr3 = get_cr3;
	context->get_pdptr = kvm_pdptr_read;
	context->inject_page_fault = kvm_inject_page_fault;

	if (!is_paging(vcpu)) {
		context->nx = false;
		context->gva_to_gpa = nonpaging_gva_to_gpa;
		context->root_level = 0;
	} else if (is_long_mode(vcpu)) {
		context->nx = is_nx(vcpu);
		context->root_level = PT64_ROOT_LEVEL;
		reset_rsvds_bits_mask(vcpu, context);
		context->gva_to_gpa = paging64_gva_to_gpa;
	} else if (is_pae(vcpu)) {
		context->nx = is_nx(vcpu);
		context->root_level = PT32E_ROOT_LEVEL;
		reset_rsvds_bits_mask(vcpu, context);
		context->gva_to_gpa = paging64_gva_to_gpa;
	} else {
		context->nx = false;
		context->root_level = PT32_ROOT_LEVEL;
		reset_rsvds_bits_mask(vcpu, context);
		context->gva_to_gpa = paging32_gva_to_gpa;
	}

	return 0;
}

int kvm_init_shadow_mmu(struct kvm_vcpu *vcpu, struct kvm_mmu *context)
{
	int r;
	bool smep = kvm_read_cr4_bits(vcpu, X86_CR4_SMEP);
	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->arch.mmu.root_hpa));

	if (!is_paging(vcpu))
		r = nonpaging_init_context(vcpu, context);
	else if (is_long_mode(vcpu))
		r = paging64_init_context(vcpu, context);
	else if (is_pae(vcpu))
		r = paging32E_init_context(vcpu, context);
	else
		r = paging32_init_context(vcpu, context);

	vcpu->arch.mmu.base_role.cr4_pae = !!is_pae(vcpu);
	vcpu->arch.mmu.base_role.cr0_wp  = is_write_protection(vcpu);
	vcpu->arch.mmu.base_role.smep_andnot_wp
		= smep && !is_write_protection(vcpu);

	return r;
}
EXPORT_SYMBOL_GPL(kvm_init_shadow_mmu);

static int init_kvm_softmmu(struct kvm_vcpu *vcpu)
{
	int r = kvm_init_shadow_mmu(vcpu, vcpu->arch.walk_mmu);

	vcpu->arch.walk_mmu->set_cr3           = kvm_x86_ops->set_cr3;
	vcpu->arch.walk_mmu->get_cr3           = get_cr3;
	vcpu->arch.walk_mmu->get_pdptr         = kvm_pdptr_read;
	vcpu->arch.walk_mmu->inject_page_fault = kvm_inject_page_fault;

	return r;
}

static int init_kvm_nested_mmu(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *g_context = &vcpu->arch.nested_mmu;

	g_context->get_cr3           = get_cr3;
	g_context->get_pdptr         = kvm_pdptr_read;
	g_context->inject_page_fault = kvm_inject_page_fault;

	/*
	 * Note that arch.mmu.gva_to_gpa translates l2_gva to l1_gpa. The
	 * translation of l2_gpa to l1_gpa addresses is done using the
	 * arch.nested_mmu.gva_to_gpa function. Basically the gva_to_gpa
	 * functions between mmu and nested_mmu are swapped.
	 */
	if (!is_paging(vcpu)) {
		g_context->nx = false;
		g_context->root_level = 0;
		g_context->gva_to_gpa = nonpaging_gva_to_gpa_nested;
	} else if (is_long_mode(vcpu)) {
		g_context->nx = is_nx(vcpu);
		g_context->root_level = PT64_ROOT_LEVEL;
		reset_rsvds_bits_mask(vcpu, g_context);
		g_context->gva_to_gpa = paging64_gva_to_gpa_nested;
	} else if (is_pae(vcpu)) {
		g_context->nx = is_nx(vcpu);
		g_context->root_level = PT32E_ROOT_LEVEL;
		reset_rsvds_bits_mask(vcpu, g_context);
		g_context->gva_to_gpa = paging64_gva_to_gpa_nested;
	} else {
		g_context->nx = false;
		g_context->root_level = PT32_ROOT_LEVEL;
		reset_rsvds_bits_mask(vcpu, g_context);
		g_context->gva_to_gpa = paging32_gva_to_gpa_nested;
	}

	return 0;
}

static int init_kvm_mmu(struct kvm_vcpu *vcpu)
{
	if (mmu_is_nested(vcpu))
		return init_kvm_nested_mmu(vcpu);
	else if (tdp_enabled)
		return init_kvm_tdp_mmu(vcpu);
	else
		return init_kvm_softmmu(vcpu);
}

static void destroy_kvm_mmu(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	if (VALID_PAGE(vcpu->arch.mmu.root_hpa))
		/* mmu.free() should set root_hpa = INVALID_PAGE */
		vcpu->arch.mmu.free(vcpu);
}

int kvm_mmu_reset_context(struct kvm_vcpu *vcpu)
{
	destroy_kvm_mmu(vcpu);
	return init_kvm_mmu(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_mmu_reset_context);

int kvm_mmu_load(struct kvm_vcpu *vcpu)
{
	int r;

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		goto out;
	r = mmu_alloc_roots(vcpu);
	spin_lock(&vcpu->kvm->mmu_lock);
	mmu_sync_roots(vcpu);
	spin_unlock(&vcpu->kvm->mmu_lock);
	if (r)
		goto out;
	/* set_cr3() should ensure TLB has been flushed */
	vcpu->arch.mmu.set_cr3(vcpu, vcpu->arch.mmu.root_hpa);
out:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_load);

void kvm_mmu_unload(struct kvm_vcpu *vcpu)
{
	mmu_free_roots(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_mmu_unload);

static void mmu_pte_write_new_pte(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *sp, u64 *spte,
				  const void *new)
{
	if (sp->role.level != PT_PAGE_TABLE_LEVEL) {
		++vcpu->kvm->stat.mmu_pde_zapped;
		return;
        }

	++vcpu->kvm->stat.mmu_pte_updated;
	vcpu->arch.mmu.update_pte(vcpu, sp, spte, new);
}

static bool need_remote_flush(u64 old, u64 new)
{
	if (!is_shadow_present_pte(old))
		return false;
	if (!is_shadow_present_pte(new))
		return true;
	if ((old ^ new) & PT64_BASE_ADDR_MASK)
		return true;
	old ^= PT64_NX_MASK;
	new ^= PT64_NX_MASK;
	return (old & ~new & PT64_PERM_MASK) != 0;
}

static void mmu_pte_write_flush_tlb(struct kvm_vcpu *vcpu, bool zap_page,
				    bool remote_flush, bool local_flush)
{
	if (zap_page)
		return;

	if (remote_flush)
		kvm_flush_remote_tlbs(vcpu->kvm);
	else if (local_flush)
		kvm_mmu_flush_tlb(vcpu);
}

static u64 mmu_pte_write_fetch_gpte(struct kvm_vcpu *vcpu, gpa_t *gpa,
				    const u8 *new, int *bytes)
{
	u64 gentry;
	int r;

	/*
	 * Assume that the pte write on a page table of the same type
	 * as the current vcpu paging mode since we update the sptes only
	 * when they have the same mode.
	 */
	if (is_pae(vcpu) && *bytes == 4) {
		/* Handle a 32-bit guest writing two halves of a 64-bit gpte */
		*gpa &= ~(gpa_t)7;
		*bytes = 8;
		r = kvm_read_guest(vcpu->kvm, *gpa, &gentry, min(*bytes, 8));
		if (r)
			gentry = 0;
		new = (const u8 *)&gentry;
	}

	switch (*bytes) {
	case 4:
		gentry = *(const u32 *)new;
		break;
	case 8:
		gentry = *(const u64 *)new;
		break;
	default:
		gentry = 0;
		break;
	}

	return gentry;
}

/*
 * If we're seeing too many writes to a page, it may no longer be a page table,
 * or we may be forking, in which case it is better to unmap the page.
 */
static bool detect_write_flooding(struct kvm_mmu_page *sp)
{
	/*
	 * Skip write-flooding detected for the sp whose level is 1, because
	 * it can become unsync, then the guest page is not write-protected.
	 */
	if (sp->role.level == 1)
		return false;

	return ++sp->write_flooding_count >= 3;
}

/*
 * Misaligned accesses are too much trouble to fix up; also, they usually
 * indicate a page is not used as a page table.
 */
static bool detect_write_misaligned(struct kvm_mmu_page *sp, gpa_t gpa,
				    int bytes)
{
	unsigned offset, pte_size, misaligned;

	pgprintk("misaligned: gpa %llx bytes %d role %x\n",
		 gpa, bytes, sp->role.word);

	offset = offset_in_page(gpa);
	pte_size = sp->role.cr4_pae ? 8 : 4;

	/*
	 * Sometimes, the OS only writes the last one bytes to update status
	 * bits, for example, in linux, andb instruction is used in clear_bit().
	 */
	if (!(offset & (pte_size - 1)) && bytes == 1)
		return false;

	misaligned = (offset ^ (offset + bytes - 1)) & ~(pte_size - 1);
	misaligned |= bytes < 4;

	return misaligned;
}

static u64 *get_written_sptes(struct kvm_mmu_page *sp, gpa_t gpa, int *nspte)
{
	unsigned page_offset, quadrant;
	u64 *spte;
	int level;

	page_offset = offset_in_page(gpa);
	level = sp->role.level;
	*nspte = 1;
	if (!sp->role.cr4_pae) {
		page_offset <<= 1;	/* 32->64 */
		/*
		 * A 32-bit pde maps 4MB while the shadow pdes map
		 * only 2MB.  So we need to double the offset again
		 * and zap two pdes instead of one.
		 */
		if (level == PT32_ROOT_LEVEL) {
			page_offset &= ~7; /* kill rounding error */
			page_offset <<= 1;
			*nspte = 2;
		}
		quadrant = page_offset >> PAGE_SHIFT;
		page_offset &= ~PAGE_MASK;
		if (quadrant != sp->role.quadrant)
			return NULL;
	}

	spte = &sp->spt[page_offset / sizeof(*spte)];
	return spte;
}

void kvm_mmu_pte_write(struct kvm_vcpu *vcpu, gpa_t gpa,
		       const u8 *new, int bytes)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	union kvm_mmu_page_role mask = { .word = 0 };
	struct kvm_mmu_page *sp;
	struct hlist_node *node;
	LIST_HEAD(invalid_list);
	u64 entry, gentry, *spte;
	int npte;
	bool remote_flush, local_flush, zap_page;

	/*
	 * If we don't have indirect shadow pages, it means no page is
	 * write-protected, so we can exit simply.
	 */
	if (!ACCESS_ONCE(vcpu->kvm->arch.indirect_shadow_pages))
		return;

	zap_page = remote_flush = local_flush = false;

	pgprintk("%s: gpa %llx bytes %d\n", __func__, gpa, bytes);

	gentry = mmu_pte_write_fetch_gpte(vcpu, &gpa, new, &bytes);

	/*
	 * No need to care whether allocation memory is successful
	 * or not since pte prefetch is skiped if it does not have
	 * enough objects in the cache.
	 */
	mmu_topup_memory_caches(vcpu);

	spin_lock(&vcpu->kvm->mmu_lock);
	++vcpu->kvm->stat.mmu_pte_write;
	kvm_mmu_audit(vcpu, AUDIT_PRE_PTE_WRITE);

	mask.cr0_wp = mask.cr4_pae = mask.nxe = mask.smep_andnot_wp = 1;
	for_each_gfn_indirect_valid_sp(vcpu->kvm, sp, gfn, node) {
		if (detect_write_misaligned(sp, gpa, bytes) ||
		      detect_write_flooding(sp)) {
			zap_page |= !!kvm_mmu_prepare_zap_page(vcpu->kvm, sp,
						     &invalid_list);
			++vcpu->kvm->stat.mmu_flooded;
			continue;
		}

		spte = get_written_sptes(sp, gpa, &npte);
		if (!spte)
			continue;

		local_flush = true;
		while (npte--) {
			entry = *spte;
			mmu_page_zap_pte(vcpu->kvm, sp, spte);
			if (gentry &&
			      !((sp->role.word ^ vcpu->arch.mmu.base_role.word)
			      & mask.word) && rmap_can_add(vcpu))
				mmu_pte_write_new_pte(vcpu, sp, spte, &gentry);
			if (!remote_flush && need_remote_flush(entry, *spte))
				remote_flush = true;
			++spte;
		}
	}
	mmu_pte_write_flush_tlb(vcpu, zap_page, remote_flush, local_flush);
	kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
	kvm_mmu_audit(vcpu, AUDIT_POST_PTE_WRITE);
	spin_unlock(&vcpu->kvm->mmu_lock);
}

int kvm_mmu_unprotect_page_virt(struct kvm_vcpu *vcpu, gva_t gva)
{
	gpa_t gpa;
	int r;

	if (vcpu->arch.mmu.direct_map)
		return 0;

	gpa = kvm_mmu_gva_to_gpa_read(vcpu, gva, NULL);

	r = kvm_mmu_unprotect_page(vcpu->kvm, gpa >> PAGE_SHIFT);

	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_unprotect_page_virt);

void __kvm_mmu_free_some_pages(struct kvm_vcpu *vcpu)
{
	LIST_HEAD(invalid_list);

	while (kvm_mmu_available_pages(vcpu->kvm) < KVM_REFILL_PAGES &&
	       !list_empty(&vcpu->kvm->arch.active_mmu_pages)) {
		struct kvm_mmu_page *sp;

		sp = container_of(vcpu->kvm->arch.active_mmu_pages.prev,
				  struct kvm_mmu_page, link);
		kvm_mmu_prepare_zap_page(vcpu->kvm, sp, &invalid_list);
		++vcpu->kvm->stat.mmu_recycled;
	}
	kvm_mmu_commit_zap_page(vcpu->kvm, &invalid_list);
}

static bool is_mmio_page_fault(struct kvm_vcpu *vcpu, gva_t addr)
{
	if (vcpu->arch.mmu.direct_map || mmu_is_nested(vcpu))
		return vcpu_match_mmio_gpa(vcpu, addr);

	return vcpu_match_mmio_gva(vcpu, addr);
}

int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gva_t cr2, u32 error_code,
		       void *insn, int insn_len)
{
	int r, emulation_type = EMULTYPE_RETRY;
	enum emulation_result er;

	r = vcpu->arch.mmu.page_fault(vcpu, cr2, error_code, false);
	if (r < 0)
		goto out;

	if (!r) {
		r = 1;
		goto out;
	}

	if (is_mmio_page_fault(vcpu, cr2))
		emulation_type = 0;

	er = x86_emulate_instruction(vcpu, cr2, emulation_type, insn, insn_len);

	switch (er) {
	case EMULATE_DONE:
		return 1;
	case EMULATE_DO_MMIO:
		++vcpu->stat.mmio_exits;
		/* fall through */
	case EMULATE_FAIL:
		return 0;
	default:
		BUG();
	}
out:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_mmu_page_fault);

void kvm_mmu_invlpg(struct kvm_vcpu *vcpu, gva_t gva)
{
	vcpu->arch.mmu.invlpg(vcpu, gva);
	kvm_mmu_flush_tlb(vcpu);
	++vcpu->stat.invlpg;
}
EXPORT_SYMBOL_GPL(kvm_mmu_invlpg);

void kvm_enable_tdp(void)
{
	tdp_enabled = true;
}
EXPORT_SYMBOL_GPL(kvm_enable_tdp);

void kvm_disable_tdp(void)
{
	tdp_enabled = false;
}
EXPORT_SYMBOL_GPL(kvm_disable_tdp);

static void free_mmu_pages(struct kvm_vcpu *vcpu)
{
	free_page((unsigned long)vcpu->arch.mmu.pae_root);
	if (vcpu->arch.mmu.lm_root != NULL)
		free_page((unsigned long)vcpu->arch.mmu.lm_root);
}

static int alloc_mmu_pages(struct kvm_vcpu *vcpu)
{
	struct page *page;
	int i;

	ASSERT(vcpu);

	/*
	 * When emulating 32-bit mode, cr3 is only 32 bits even on x86_64.
	 * Therefore we need to allocate shadow page tables in the first
	 * 4GB of memory, which happens to fit the DMA32 zone.
	 */
	page = alloc_page(GFP_KERNEL | __GFP_DMA32);
	if (!page)
		return -ENOMEM;

	vcpu->arch.mmu.pae_root = page_address(page);
	for (i = 0; i < 4; ++i)
		vcpu->arch.mmu.pae_root[i] = INVALID_PAGE;

	return 0;
}

int kvm_mmu_create(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);

	vcpu->arch.walk_mmu = &vcpu->arch.mmu;
	vcpu->arch.mmu.root_hpa = INVALID_PAGE;
	vcpu->arch.mmu.translate_gpa = translate_gpa;
	vcpu->arch.nested_mmu.translate_gpa = translate_nested_gpa;

	return alloc_mmu_pages(vcpu);
}

int kvm_mmu_setup(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->arch.mmu.root_hpa));

	return init_kvm_mmu(vcpu);
}

void kvm_mmu_slot_remove_write_access(struct kvm *kvm, int slot)
{
	struct kvm_mmu_page *sp;

	list_for_each_entry(sp, &kvm->arch.active_mmu_pages, link) {
		int i;
		u64 *pt;

		if (!test_bit(slot, sp->slot_bitmap))
			continue;

		pt = sp->spt;
		for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
			if (!is_shadow_present_pte(pt[i]) ||
			      !is_last_spte(pt[i], sp->role.level))
				continue;

			if (is_large_pte(pt[i])) {
				drop_spte(kvm, &pt[i]);
				--kvm->stat.lpages;
				continue;
			}

			/* avoid RMW */
			if (is_writable_pte(pt[i]))
				mmu_spte_update(&pt[i],
						pt[i] & ~PT_WRITABLE_MASK);
		}
	}
	kvm_flush_remote_tlbs(kvm);
}

void kvm_mmu_zap_all(struct kvm *kvm)
{
	struct kvm_mmu_page *sp, *node;
	LIST_HEAD(invalid_list);

	spin_lock(&kvm->mmu_lock);
restart:
	list_for_each_entry_safe(sp, node, &kvm->arch.active_mmu_pages, link)
		if (kvm_mmu_prepare_zap_page(kvm, sp, &invalid_list))
			goto restart;

	kvm_mmu_commit_zap_page(kvm, &invalid_list);
	spin_unlock(&kvm->mmu_lock);
}

static void kvm_mmu_remove_some_alloc_mmu_pages(struct kvm *kvm,
						struct list_head *invalid_list)
{
	struct kvm_mmu_page *page;

	page = container_of(kvm->arch.active_mmu_pages.prev,
			    struct kvm_mmu_page, link);
	kvm_mmu_prepare_zap_page(kvm, page, invalid_list);
}

static int mmu_shrink(struct shrinker *shrink, struct shrink_control *sc)
{
	struct kvm *kvm;
	struct kvm *kvm_freed = NULL;
	int nr_to_scan = sc->nr_to_scan;

	if (nr_to_scan == 0)
		goto out;

	raw_spin_lock(&kvm_lock);

	list_for_each_entry(kvm, &vm_list, vm_list) {
		int idx;
		LIST_HEAD(invalid_list);

		idx = srcu_read_lock(&kvm->srcu);
		spin_lock(&kvm->mmu_lock);
		if (!kvm_freed && nr_to_scan > 0 &&
		    kvm->arch.n_used_mmu_pages > 0) {
			kvm_mmu_remove_some_alloc_mmu_pages(kvm,
							    &invalid_list);
			kvm_freed = kvm;
		}
		nr_to_scan--;

		kvm_mmu_commit_zap_page(kvm, &invalid_list);
		spin_unlock(&kvm->mmu_lock);
		srcu_read_unlock(&kvm->srcu, idx);
	}
	if (kvm_freed)
		list_move_tail(&kvm_freed->vm_list, &vm_list);

	raw_spin_unlock(&kvm_lock);

out:
	return percpu_counter_read_positive(&kvm_total_used_mmu_pages);
}

static struct shrinker mmu_shrinker = {
	.shrink = mmu_shrink,
	.seeks = DEFAULT_SEEKS * 10,
};

static void mmu_destroy_caches(void)
{
	if (pte_list_desc_cache)
		kmem_cache_destroy(pte_list_desc_cache);
	if (mmu_page_header_cache)
		kmem_cache_destroy(mmu_page_header_cache);
}

int kvm_mmu_module_init(void)
{
	pte_list_desc_cache = kmem_cache_create("pte_list_desc",
					    sizeof(struct pte_list_desc),
					    0, 0, NULL);
	if (!pte_list_desc_cache)
		goto nomem;

	mmu_page_header_cache = kmem_cache_create("kvm_mmu_page_header",
						  sizeof(struct kvm_mmu_page),
						  0, 0, NULL);
	if (!mmu_page_header_cache)
		goto nomem;

	if (percpu_counter_init(&kvm_total_used_mmu_pages, 0))
		goto nomem;

	register_shrinker(&mmu_shrinker);

	return 0;

nomem:
	mmu_destroy_caches();
	return -ENOMEM;
}

/*
 * Caculate mmu pages needed for kvm.
 */
unsigned int kvm_mmu_calculate_mmu_pages(struct kvm *kvm)
{
	unsigned int nr_mmu_pages;
	unsigned int  nr_pages = 0;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;

	slots = kvm_memslots(kvm);

	kvm_for_each_memslot(memslot, slots)
		nr_pages += memslot->npages;

	nr_mmu_pages = nr_pages * KVM_PERMILLE_MMU_PAGES / 1000;
	nr_mmu_pages = max(nr_mmu_pages,
			(unsigned int) KVM_MIN_ALLOC_MMU_PAGES);

	return nr_mmu_pages;
}

int kvm_mmu_get_spte_hierarchy(struct kvm_vcpu *vcpu, u64 addr, u64 sptes[4])
{
	struct kvm_shadow_walk_iterator iterator;
	u64 spte;
	int nr_sptes = 0;

	walk_shadow_page_lockless_begin(vcpu);
	for_each_shadow_entry_lockless(vcpu, addr, iterator, spte) {
		sptes[iterator.level-1] = spte;
		nr_sptes++;
		if (!is_shadow_present_pte(spte))
			break;
	}
	walk_shadow_page_lockless_end(vcpu);

	return nr_sptes;
}
EXPORT_SYMBOL_GPL(kvm_mmu_get_spte_hierarchy);

void kvm_mmu_destroy(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);

	destroy_kvm_mmu(vcpu);
	free_mmu_pages(vcpu);
	mmu_free_memory_caches(vcpu);
}

void kvm_mmu_module_exit(void)
{
	mmu_destroy_caches();
	percpu_counter_destroy(&kvm_total_used_mmu_pages);
	unregister_shrinker(&mmu_shrinker);
	mmu_audit_disable();
}
