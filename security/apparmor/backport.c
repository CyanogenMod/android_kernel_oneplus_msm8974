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
 * This is a file of helper fns backported from newer kernels to support
 * backporting of apparmor to older kernels. Fns prefixed with code they
 * are copied of modified from
 */

#include "include/backport.h"

/* copied from commit c3c073f808b22dfae15ef8412b6f7b998644139a */
int iterate_fd_apparmor(struct files_struct *files, unsigned n,
               int (*f)(const void *, struct file *, unsigned),
               const void *p)
{
	struct fdtable *fdt;
	struct file *file;
	int res = 0;
	if (!files)
		return 0;
	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	while (!res && n < fdt->max_fds) {
		file = rcu_dereference_check_fdtable(files, fdt->fd[n++]);
		if (file)
			res = f(p, file, n);
	}
	spin_unlock(&files->file_lock);
	return res;
}

/* 3.6 backport support fns for replace_fd, copied from fs/file.c */
static void __put_unused_fd(struct files_struct *files, unsigned int fd)
{
	struct fdtable *fdt = files_fdtable(files);
	__clear_open_fd(fd, fdt);
	if (fd < files->next_fd)
		files->next_fd = fd;
}

static int __close_fd(struct files_struct *files, unsigned fd)
{
       struct file *file;
       struct fdtable *fdt;

       spin_lock(&files->file_lock);
       fdt = files_fdtable(files);
       if (fd >= fdt->max_fds)
               goto out_unlock;
       file = fdt->fd[fd];
       if (!file)
               goto out_unlock;
       rcu_assign_pointer(fdt->fd[fd], NULL);
       __clear_close_on_exec(fd, fdt);
       __put_unused_fd(files, fd);
       spin_unlock(&files->file_lock);
       return filp_close(file, files);

out_unlock:
       spin_unlock(&files->file_lock);
       return -EBADF;
}

static int do_dup2(struct files_struct *files,
	struct file *file, unsigned fd, unsigned flags)
{
	struct file *tofree;
	struct fdtable *fdt;

	/*
	 * We need to detect attempts to do dup2() over allocated but still
	 * not finished descriptor.  NB: OpenBSD avoids that at the price of
	 * extra work in their equivalent of fget() - they insert struct
	 * file immediately after grabbing descriptor, mark it larval if
	 * more work (e.g. actual opening) is needed and make sure that
	 * fget() treats larval files as absent.  Potentially interesting,
	 * but while extra work in fget() is trivial, locking implications
	 * and amount of surgery on open()-related paths in VFS are not.
	 * FreeBSD fails with -EBADF in the same situation, NetBSD "solution"
	 * deadlocks in rather amusing ways, AFAICS.  All of that is out of
	 * scope of POSIX or SUS, since neither considers shared descriptor
	 * tables and this condition does not arise without those.
	 */
	fdt = files_fdtable(files);
	tofree = fdt->fd[fd];
	if (!tofree && fd_is_open(fd, fdt))
		goto Ebusy;
	get_file(file);
	rcu_assign_pointer(fdt->fd[fd], file);
	__set_open_fd(fd, fdt);
	if (flags & O_CLOEXEC)
		__set_close_on_exec(fd, fdt);
	else
		__clear_close_on_exec(fd, fdt);
	spin_unlock(&files->file_lock);

	if (tofree)
		filp_close(tofree, files);

	return fd;

Ebusy:
	spin_unlock(&files->file_lock);
	return -EBUSY;
}

/* copied from commit 8280d16172243702ed43432f826ca6130edb4086 */
int replace_fd(unsigned fd, struct file *file, unsigned flags)
{
	int err;
	struct files_struct *files = current->files;

	if (!file)
		return __close_fd(files, fd);

	if (fd >= rlimit(RLIMIT_NOFILE))
		return -EMFILE;

	spin_lock(&files->file_lock);
	err = expand_files(files, fd);
	if (unlikely(err < 0))
		goto out_unlock;
	return do_dup2(files, file, fd, flags);

out_unlock:
	spin_unlock(&files->file_lock);
	return err;
}
