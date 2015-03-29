/*
 * Copyright (C) 2015 Cyanogen, Inc.
 */

#ifdef CONFIG_FS_TRANSPARENT_COMPRESSION

extern int zfile_is_compressed(struct file *file);
extern struct file *zfile_open(struct file *file, const char *filename);

extern int zpath_realsize(struct dentry *dentry, loff_t *off);

#else

static inline int zfile_is_compressed(struct file *file) { return 0; }
static inline struct file *zfile_open(struct file *file, const char *filename) { return ERR_PTR(-EINVAL); }

static inline int zpath_realsize(struct dentry *dentry, loff_t *off) { return -EINVAL; }

#endif
