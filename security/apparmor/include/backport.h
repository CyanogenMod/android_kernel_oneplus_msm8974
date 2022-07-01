/*
 * AppArmor security module
 *
 * This file contains AppArmor file mediation function definitions.
 *
 * Copyright 2014 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 *
 * This is a file of helper macros, defines for backporting newer versions
 * of apparmor to older kernels
 */
#ifndef __AA_BACKPORT_H
#define __AA_BACKPORT_H

#include <linux/stddef.h>
#include <linux/fdtable.h>

/* 3.10 backport, commit 496ad9aa8ef448058e36ca7a787c61f2e63f0f54 */
#define file_inode(FILE) ((FILE)->f_path.dentry->d_inode)

/* 3.6 backport, commit c3c073f808b22dfae15ef8412b6f7b998644139a */
int iterate_fd_apparmor(struct files_struct *, unsigned,
               int (*)(const void *, struct file *, unsigned),
               const void *);

/* 3.6 backport commit 8280d16172243702ed43432f826ca6130edb4086 */
int replace_fd(unsigned fd, struct file *file, unsigned flags);

/* 3.6 backport, commit d2b31ca644fdc8704de3367a6a56a5c958c77f53 */
#define kuid_t uid_t
#define kgid_t gid_t

/* 3.6 backport, commit 2db81452931eb51cc739d6e495cf1bd4860c3c99 */
#define GLOBAL_ROOT_UID_APPARMOR 0
//#define from_kuid(X, UID) UID
//#define uid_eq(X, Y) ((X) == (Y))

/* 3.5 backport commit 765927b2d508712d320c8934db963bbe14c3fcec */
#define dentry_open(P, F, C) (dentry_open)((P)->dentry, (P)->mnt, (F), (C))

/* 3.4 backport, commit d007794a182bc072a7b7479909dbd0d67ba341be */
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/security.h>
static inline int cap_mmap_addr(unsigned long addr)
{
	int ret = 0;

	if (addr < dac_mmap_min_addr) {
		ret = cap_capable(current_cred(), &init_user_ns, CAP_SYS_RAWIO,
				  SECURITY_CAP_AUDIT);
		/* set PF_SUPERPRIV if it turns out we allow the low mmap */
		if (ret == 0)
			current->flags |= PF_SUPERPRIV;
	}
	return ret;
}

/* 3.4 backport, commits: 259e5e6c75a910f3b5e656151dc602f53f9d7548
 *                        c29bceb3967398cf2ac8bf8edf9634fdb722df7d
 */
/* turn no_new_privs into an always false expr so the compiler throws it away */
#define no_new_privs did_exec < 0

/* 3.4 backport, commit: 83d498569e9a7a4b92c4c5d3566f2d6a604f28c9 */
#define file_open dentry_open
#define apparmor_file_open apparmor_dentry_open

#endif /* __AA_BACKPORT_H */
