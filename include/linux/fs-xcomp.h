/*
 * Copyright (C) 2015 Cyanogen, Inc.
 */

typedef int (*readpage_t)(struct page *);

struct xcomp_cluster {
	loff_t			off;
	size_t			len;
	struct mutex		lock;
	unsigned int		flags;
};

struct xcomp_inode_info {
	readpage_t              i_lower_readpage;
	struct address_space	i_mapping;
	int			i_param1;	/* Compression method and flags */
	size_t			i_cluster_size;
	struct cluster		*i_clusters;	/* Cluster info */
};


#ifdef CONFIG_FS_TRANSPARENT_COMPRESSION

extern int xcomp_enabled(void);
extern int xcomp_inode_info_init(struct inode *inode,
		struct xcomp_inode_info *info, readpage_t lower_readpage);
extern int xcomp_inode_info_free(struct xcomp_inode_info *info);
extern int xcomp_readpage(struct xcomp_inode_info *info, struct page *page);
extern int xcomp_readpages(struct xcomp_inode_info *info, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages);

#else

static inline int xcomp_enabled(void) { return 0; }
static inline int xcomp_inode_info_init(struct inode *inode,
		struct xcomp_inode_info *info, readpage_t lower_readpage) { return 0; }
static inline int xcomp_inode_info_free(struct xcomp_inode_info *info) { return 0; }
static inline int xcomp_readpage(struct xcomp_inode_info *info, struct page *page) { return -EINVAL; }
static inline int xcomp_readpages(struct xcomp_inode_info *info, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages) { return -EINVAL; }

#endif
