/*
 * Copyright (c) 2015  Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Backport functionality introduced in Linux 4.0.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/export.h>

static __always_inline long __get_user_pages_locked(struct task_struct *tsk,
						struct mm_struct *mm,
						unsigned long start,
						unsigned long nr_pages,
						int write, int force,
						struct page **pages,
						struct vm_area_struct **vmas,
						int *locked, bool notify_drop,
						unsigned int flags)
{
	long ret, pages_done;
	bool lock_dropped;

	if (locked) {
		/* if VM_FAULT_RETRY can be returned, vmas become invalid */
		BUG_ON(vmas);
		/* check caller initialized locked */
		BUG_ON(*locked != 1);
	}

	if (pages)
		flags |= FOLL_GET;
	if (write)
		flags |= FOLL_WRITE;
	if (force)
		flags |= FOLL_FORCE;

	pages_done = 0;
	lock_dropped = false;
	for (;;) {
		ret = __get_user_pages(tsk, mm, start, nr_pages, flags, pages,
				       vmas, locked);
		if (!locked)
			/* VM_FAULT_RETRY couldn't trigger, bypass */
			return ret;

		/* VM_FAULT_RETRY cannot return errors */
		if (!*locked) {
			BUG_ON(ret < 0);
			BUG_ON(ret >= nr_pages);
		}

		if (!pages)
			/* If it's a prefault don't insist harder */
			return ret;

		if (ret > 0) {
			nr_pages -= ret;
			pages_done += ret;
			if (!nr_pages)
				break;
		}
		if (*locked) {
			/* VM_FAULT_RETRY didn't trigger */
			if (!pages_done)
				pages_done = ret;
			break;
		}
		/* VM_FAULT_RETRY triggered, so seek to the faulting offset */
		pages += ret;
		start += ret << PAGE_SHIFT;

		/*
		 * Repeat on the address that fired VM_FAULT_RETRY
		 * without FAULT_FLAG_ALLOW_RETRY but with
		 * FAULT_FLAG_TRIED.
		 */
		*locked = 1;
		lock_dropped = true;
		down_read(&mm->mmap_sem);
		ret = __get_user_pages(tsk, mm, start, 1, flags | FOLL_TRIED,
				       pages, NULL, NULL);
		if (ret != 1) {
			BUG_ON(ret > 1);
			if (!pages_done)
				pages_done = ret;
			break;
		}
		nr_pages--;
		pages_done++;
		if (!nr_pages)
			break;
		pages++;
		start += PAGE_SIZE;
	}
	if (notify_drop && lock_dropped && *locked) {
		/*
		 * We must let the caller know we temporarily dropped the lock
		 * and so the critical section protected by it was lost.
		 */
		up_read(&mm->mmap_sem);
		*locked = 0;
	}
	return pages_done;
}

/*
 * We can leverage the VM_FAULT_RETRY functionality in the page fault
 * paths better by using either get_user_pages_locked() or
 * get_user_pages_unlocked().
 *
 * get_user_pages_locked() is suitable to replace the form:
 *
 *      down_read(&mm->mmap_sem);
 *      do_something()
 *      get_user_pages(tsk, mm, ..., pages, NULL);
 *      up_read(&mm->mmap_sem);
 *
 *  to:
 *
 *      int locked = 1;
 *      down_read(&mm->mmap_sem);
 *      do_something()
 *      get_user_pages_locked(tsk, mm, ..., pages, &locked);
 *      if (locked)
 *          up_read(&mm->mmap_sem);
 */
long get_user_pages_locked(struct task_struct *tsk, struct mm_struct *mm,
			   unsigned long start, unsigned long nr_pages,
			   int write, int force, struct page **pages,
			   int *locked)
{
	return __get_user_pages_locked(tsk, mm, start, nr_pages, write, force,
				       pages, NULL, locked, true, FOLL_TOUCH);
}
EXPORT_SYMBOL_GPL(get_user_pages_locked);

/*
 * Same as get_user_pages_unlocked(...., FOLL_TOUCH) but it allows to
 * pass additional gup_flags as last parameter (like FOLL_HWPOISON).
 *
 * NOTE: here FOLL_TOUCH is not set implicitly and must be set by the
 * caller if required (just like with __get_user_pages). "FOLL_GET",
 * "FOLL_WRITE" and "FOLL_FORCE" are set implicitly as needed
 * according to the parameters "pages", "write", "force"
 * respectively.
 */
__always_inline long __get_user_pages_unlocked(struct task_struct *tsk, struct mm_struct *mm,
					       unsigned long start, unsigned long nr_pages,
					       int write, int force, struct page **pages,
					       unsigned int gup_flags)
{
	long ret;
	int locked = 1;
	down_read(&mm->mmap_sem);
	ret = __get_user_pages_locked(tsk, mm, start, nr_pages, write, force,
				      pages, NULL, &locked, false, gup_flags);
	if (locked)
		up_read(&mm->mmap_sem);
	return ret;
}
EXPORT_SYMBOL_GPL(__get_user_pages_unlocked);

/*
 * get_user_pages_unlocked() is suitable to replace the form:
 *
 *      down_read(&mm->mmap_sem);
 *      get_user_pages(tsk, mm, ..., pages, NULL);
 *      up_read(&mm->mmap_sem);
 *
 *  with:
 *
 *      get_user_pages_unlocked(tsk, mm, ..., pages);
 *
 * It is functionally equivalent to get_user_pages_fast so
 * get_user_pages_fast should be used instead, if the two parameters
 * "tsk" and "mm" are respectively equal to current and current->mm,
 * or if "force" shall be set to 1 (get_user_pages_fast misses the
 * "force" parameter).
 */
long get_user_pages_unlocked(struct task_struct *tsk, struct mm_struct *mm,
			     unsigned long start, unsigned long nr_pages,
			     int write, int force, struct page **pages)
{
	return __get_user_pages_unlocked(tsk, mm, start, nr_pages, write,
					 force, pages, FOLL_TOUCH);
}
EXPORT_SYMBOL_GPL(get_user_pages_unlocked);
