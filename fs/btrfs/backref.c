/*
 * Copyright (C) 2011 STRATO.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include "ctree.h"
#include "disk-io.h"
#include "backref.h"
#include "ulist.h"
#include "transaction.h"
#include "delayed-ref.h"
#include "locking.h"

/*
 * this structure records all encountered refs on the way up to the root
 */
struct __prelim_ref {
	struct list_head list;
	u64 root_id;
	struct btrfs_key key;
	int level;
	int count;
	u64 parent;
	u64 wanted_disk_byte;
};

static int __add_prelim_ref(struct list_head *head, u64 root_id,
			    struct btrfs_key *key, int level, u64 parent,
			    u64 wanted_disk_byte, int count)
{
	struct __prelim_ref *ref;

	/* in case we're adding delayed refs, we're holding the refs spinlock */
	ref = kmalloc(sizeof(*ref), GFP_ATOMIC);
	if (!ref)
		return -ENOMEM;

	ref->root_id = root_id;
	if (key)
		ref->key = *key;
	else
		memset(&ref->key, 0, sizeof(ref->key));

	ref->level = level;
	ref->count = count;
	ref->parent = parent;
	ref->wanted_disk_byte = wanted_disk_byte;
	list_add_tail(&ref->list, head);

	return 0;
}

static int add_all_parents(struct btrfs_root *root, struct btrfs_path *path,
				struct ulist *parents,
				struct extent_buffer *eb, int level,
				u64 wanted_objectid, u64 wanted_disk_byte)
{
	int ret;
	int slot;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	u64 disk_byte;

add_parent:
	ret = ulist_add(parents, eb->start, 0, GFP_NOFS);
	if (ret < 0)
		return ret;

	if (level != 0)
		return 0;

	/*
	 * if the current leaf is full with EXTENT_DATA items, we must
	 * check the next one if that holds a reference as well.
	 * ref->count cannot be used to skip this check.
	 * repeat this until we don't find any additional EXTENT_DATA items.
	 */
	while (1) {
		ret = btrfs_next_leaf(root, path);
		if (ret < 0)
			return ret;
		if (ret)
			return 0;

		eb = path->nodes[0];
		for (slot = 0; slot < btrfs_header_nritems(eb); ++slot) {
			btrfs_item_key_to_cpu(eb, &key, slot);
			if (key.objectid != wanted_objectid ||
			    key.type != BTRFS_EXTENT_DATA_KEY)
				return 0;
			fi = btrfs_item_ptr(eb, slot,
						struct btrfs_file_extent_item);
			disk_byte = btrfs_file_extent_disk_bytenr(eb, fi);
			if (disk_byte == wanted_disk_byte)
				goto add_parent;
		}
	}

	return 0;
}

/*
 * resolve an indirect backref in the form (root_id, key, level)
 * to a logical address
 */
static int __resolve_indirect_ref(struct btrfs_fs_info *fs_info,
					int search_commit_root,
					struct __prelim_ref *ref,
					struct ulist *parents)
{
	struct btrfs_path *path;
	struct btrfs_root *root;
	struct btrfs_key root_key;
	struct btrfs_key key = {0};
	struct extent_buffer *eb;
	int ret = 0;
	int root_level;
	int level = ref->level;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->search_commit_root = !!search_commit_root;

	root_key.objectid = ref->root_id;
	root_key.type = BTRFS_ROOT_ITEM_KEY;
	root_key.offset = (u64)-1;
	root = btrfs_read_fs_root_no_name(fs_info, &root_key);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out;
	}

	rcu_read_lock();
	root_level = btrfs_header_level(root->node);
	rcu_read_unlock();

	if (root_level + 1 == level)
		goto out;

	path->lowest_level = level;
	ret = btrfs_search_slot(NULL, root, &ref->key, path, 0, 0);
	pr_debug("search slot in root %llu (level %d, ref count %d) returned "
		 "%d for key (%llu %u %llu)\n",
		 (unsigned long long)ref->root_id, level, ref->count, ret,
		 (unsigned long long)ref->key.objectid, ref->key.type,
		 (unsigned long long)ref->key.offset);
	if (ret < 0)
		goto out;

	eb = path->nodes[level];
	if (!eb) {
		WARN_ON(1);
		ret = 1;
		goto out;
	}

	if (level == 0) {
		if (ret == 1 && path->slots[0] >= btrfs_header_nritems(eb)) {
			ret = btrfs_next_leaf(root, path);
			if (ret)
				goto out;
			eb = path->nodes[0];
		}

		btrfs_item_key_to_cpu(eb, &key, path->slots[0]);
	}

	/* the last two parameters will only be used for level == 0 */
	ret = add_all_parents(root, path, parents, eb, level, key.objectid,
				ref->wanted_disk_byte);
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * resolve all indirect backrefs from the list
 */
static int __resolve_indirect_refs(struct btrfs_fs_info *fs_info,
				   int search_commit_root,
				   struct list_head *head)
{
	int err;
	int ret = 0;
	struct __prelim_ref *ref;
	struct __prelim_ref *ref_safe;
	struct __prelim_ref *new_ref;
	struct ulist *parents;
	struct ulist_node *node;

	parents = ulist_alloc(GFP_NOFS);
	if (!parents)
		return -ENOMEM;

	/*
	 * _safe allows us to insert directly after the current item without
	 * iterating over the newly inserted items.
	 * we're also allowed to re-assign ref during iteration.
	 */
	list_for_each_entry_safe(ref, ref_safe, head, list) {
		if (ref->parent)	/* already direct */
			continue;
		if (ref->count == 0)
			continue;
		err = __resolve_indirect_ref(fs_info, search_commit_root,
					     ref, parents);
		if (err) {
			if (ret == 0)
				ret = err;
			continue;
		}

		/* we put the first parent into the ref at hand */
		node = ulist_next(parents, NULL);
		ref->parent = node ? node->val : 0;

		/* additional parents require new refs being added here */
		while ((node = ulist_next(parents, node))) {
			new_ref = kmalloc(sizeof(*new_ref), GFP_NOFS);
			if (!new_ref) {
				ret = -ENOMEM;
				break;
			}
			memcpy(new_ref, ref, sizeof(*ref));
			new_ref->parent = node->val;
			list_add(&new_ref->list, &ref->list);
		}
		ulist_reinit(parents);
	}

	ulist_free(parents);
	return ret;
}

/*
 * merge two lists of backrefs and adjust counts accordingly
 *
 * mode = 1: merge identical keys, if key is set
 * mode = 2: merge identical parents
 */
static int __merge_refs(struct list_head *head, int mode)
{
	struct list_head *pos1;

	list_for_each(pos1, head) {
		struct list_head *n2;
		struct list_head *pos2;
		struct __prelim_ref *ref1;

		ref1 = list_entry(pos1, struct __prelim_ref, list);

		if (mode == 1 && ref1->key.type == 0)
			continue;
		for (pos2 = pos1->next, n2 = pos2->next; pos2 != head;
		     pos2 = n2, n2 = pos2->next) {
			struct __prelim_ref *ref2;

			ref2 = list_entry(pos2, struct __prelim_ref, list);

			if (mode == 1) {
				if (memcmp(&ref1->key, &ref2->key,
					   sizeof(ref1->key)) ||
				    ref1->level != ref2->level ||
				    ref1->root_id != ref2->root_id)
					continue;
				ref1->count += ref2->count;
			} else {
				if (ref1->parent != ref2->parent)
					continue;
				ref1->count += ref2->count;
			}
			list_del(&ref2->list);
			kfree(ref2);
		}

	}
	return 0;
}

/*
 * add all currently queued delayed refs from this head whose seq nr is
 * smaller or equal that seq to the list
 */
static int __add_delayed_refs(struct btrfs_delayed_ref_head *head, u64 seq,
			      struct btrfs_key *info_key,
			      struct list_head *prefs)
{
	struct btrfs_delayed_extent_op *extent_op = head->extent_op;
	struct rb_node *n = &head->node.rb_node;
	int sgn;
	int ret = 0;

	if (extent_op && extent_op->update_key)
		btrfs_disk_key_to_cpu(info_key, &extent_op->key);

	while ((n = rb_prev(n))) {
		struct btrfs_delayed_ref_node *node;
		node = rb_entry(n, struct btrfs_delayed_ref_node,
				rb_node);
		if (node->bytenr != head->node.bytenr)
			break;
		WARN_ON(node->is_head);

		if (node->seq > seq)
			continue;

		switch (node->action) {
		case BTRFS_ADD_DELAYED_EXTENT:
		case BTRFS_UPDATE_DELAYED_HEAD:
			WARN_ON(1);
			continue;
		case BTRFS_ADD_DELAYED_REF:
			sgn = 1;
			break;
		case BTRFS_DROP_DELAYED_REF:
			sgn = -1;
			break;
		default:
			BUG_ON(1);
		}
		switch (node->type) {
		case BTRFS_TREE_BLOCK_REF_KEY: {
			struct btrfs_delayed_tree_ref *ref;

			ref = btrfs_delayed_node_to_tree_ref(node);
			ret = __add_prelim_ref(prefs, ref->root, info_key,
					       ref->level + 1, 0, node->bytenr,
					       node->ref_mod * sgn);
			break;
		}
		case BTRFS_SHARED_BLOCK_REF_KEY: {
			struct btrfs_delayed_tree_ref *ref;

			ref = btrfs_delayed_node_to_tree_ref(node);
			ret = __add_prelim_ref(prefs, ref->root, info_key,
					       ref->level + 1, ref->parent,
					       node->bytenr,
					       node->ref_mod * sgn);
			break;
		}
		case BTRFS_EXTENT_DATA_REF_KEY: {
			struct btrfs_delayed_data_ref *ref;
			struct btrfs_key key;

			ref = btrfs_delayed_node_to_data_ref(node);

			key.objectid = ref->objectid;
			key.type = BTRFS_EXTENT_DATA_KEY;
			key.offset = ref->offset;
			ret = __add_prelim_ref(prefs, ref->root, &key, 0, 0,
					       node->bytenr,
					       node->ref_mod * sgn);
			break;
		}
		case BTRFS_SHARED_DATA_REF_KEY: {
			struct btrfs_delayed_data_ref *ref;
			struct btrfs_key key;

			ref = btrfs_delayed_node_to_data_ref(node);

			key.objectid = ref->objectid;
			key.type = BTRFS_EXTENT_DATA_KEY;
			key.offset = ref->offset;
			ret = __add_prelim_ref(prefs, ref->root, &key, 0,
					       ref->parent, node->bytenr,
					       node->ref_mod * sgn);
			break;
		}
		default:
			WARN_ON(1);
		}
		BUG_ON(ret);
	}

	return 0;
}

/*
 * add all inline backrefs for bytenr to the list
 */
static int __add_inline_refs(struct btrfs_fs_info *fs_info,
			     struct btrfs_path *path, u64 bytenr,
			     struct btrfs_key *info_key, int *info_level,
			     struct list_head *prefs)
{
	int ret = 0;
	int slot;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	unsigned long ptr;
	unsigned long end;
	struct btrfs_extent_item *ei;
	u64 flags;
	u64 item_size;

	/*
	 * enumerate all inline refs
	 */
	leaf = path->nodes[0];
	slot = path->slots[0] - 1;

	item_size = btrfs_item_size_nr(leaf, slot);
	BUG_ON(item_size < sizeof(*ei));

	ei = btrfs_item_ptr(leaf, slot, struct btrfs_extent_item);
	flags = btrfs_extent_flags(leaf, ei);

	ptr = (unsigned long)(ei + 1);
	end = (unsigned long)ei + item_size;

	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		struct btrfs_tree_block_info *info;
		struct btrfs_disk_key disk_key;

		info = (struct btrfs_tree_block_info *)ptr;
		*info_level = btrfs_tree_block_level(leaf, info);
		btrfs_tree_block_key(leaf, info, &disk_key);
		btrfs_disk_key_to_cpu(info_key, &disk_key);
		ptr += sizeof(struct btrfs_tree_block_info);
		BUG_ON(ptr > end);
	} else {
		BUG_ON(!(flags & BTRFS_EXTENT_FLAG_DATA));
	}

	while (ptr < end) {
		struct btrfs_extent_inline_ref *iref;
		u64 offset;
		int type;

		iref = (struct btrfs_extent_inline_ref *)ptr;
		type = btrfs_extent_inline_ref_type(leaf, iref);
		offset = btrfs_extent_inline_ref_offset(leaf, iref);

		switch (type) {
		case BTRFS_SHARED_BLOCK_REF_KEY:
			ret = __add_prelim_ref(prefs, 0, info_key,
						*info_level + 1, offset,
						bytenr, 1);
			break;
		case BTRFS_SHARED_DATA_REF_KEY: {
			struct btrfs_shared_data_ref *sdref;
			int count;

			sdref = (struct btrfs_shared_data_ref *)(iref + 1);
			count = btrfs_shared_data_ref_count(leaf, sdref);
			ret = __add_prelim_ref(prefs, 0, NULL, 0, offset,
					       bytenr, count);
			break;
		}
		case BTRFS_TREE_BLOCK_REF_KEY:
			ret = __add_prelim_ref(prefs, offset, info_key,
					       *info_level + 1, 0, bytenr, 1);
			break;
		case BTRFS_EXTENT_DATA_REF_KEY: {
			struct btrfs_extent_data_ref *dref;
			int count;
			u64 root;

			dref = (struct btrfs_extent_data_ref *)(&iref->offset);
			count = btrfs_extent_data_ref_count(leaf, dref);
			key.objectid = btrfs_extent_data_ref_objectid(leaf,
								      dref);
			key.type = BTRFS_EXTENT_DATA_KEY;
			key.offset = btrfs_extent_data_ref_offset(leaf, dref);
			root = btrfs_extent_data_ref_root(leaf, dref);
			ret = __add_prelim_ref(prefs, root, &key, 0, 0, bytenr,
						count);
			break;
		}
		default:
			WARN_ON(1);
		}
		BUG_ON(ret);
		ptr += btrfs_extent_inline_ref_size(type);
	}

	return 0;
}

/*
 * add all non-inline backrefs for bytenr to the list
 */
static int __add_keyed_refs(struct btrfs_fs_info *fs_info,
			    struct btrfs_path *path, u64 bytenr,
			    struct btrfs_key *info_key, int info_level,
			    struct list_head *prefs)
{
	struct btrfs_root *extent_root = fs_info->extent_root;
	int ret;
	int slot;
	struct extent_buffer *leaf;
	struct btrfs_key key;

	while (1) {
		ret = btrfs_next_item(extent_root, path);
		if (ret < 0)
			break;
		if (ret) {
			ret = 0;
			break;
		}

		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, slot);

		if (key.objectid != bytenr)
			break;
		if (key.type < BTRFS_TREE_BLOCK_REF_KEY)
			continue;
		if (key.type > BTRFS_SHARED_DATA_REF_KEY)
			break;

		switch (key.type) {
		case BTRFS_SHARED_BLOCK_REF_KEY:
			ret = __add_prelim_ref(prefs, 0, info_key,
						info_level + 1, key.offset,
						bytenr, 1);
			break;
		case BTRFS_SHARED_DATA_REF_KEY: {
			struct btrfs_shared_data_ref *sdref;
			int count;

			sdref = btrfs_item_ptr(leaf, slot,
					      struct btrfs_shared_data_ref);
			count = btrfs_shared_data_ref_count(leaf, sdref);
			ret = __add_prelim_ref(prefs, 0, NULL, 0, key.offset,
						bytenr, count);
			break;
		}
		case BTRFS_TREE_BLOCK_REF_KEY:
			ret = __add_prelim_ref(prefs, key.offset, info_key,
						info_level + 1, 0, bytenr, 1);
			break;
		case BTRFS_EXTENT_DATA_REF_KEY: {
			struct btrfs_extent_data_ref *dref;
			int count;
			u64 root;

			dref = btrfs_item_ptr(leaf, slot,
					      struct btrfs_extent_data_ref);
			count = btrfs_extent_data_ref_count(leaf, dref);
			key.objectid = btrfs_extent_data_ref_objectid(leaf,
								      dref);
			key.type = BTRFS_EXTENT_DATA_KEY;
			key.offset = btrfs_extent_data_ref_offset(leaf, dref);
			root = btrfs_extent_data_ref_root(leaf, dref);
			ret = __add_prelim_ref(prefs, root, &key, 0, 0,
						bytenr, count);
			break;
		}
		default:
			WARN_ON(1);
		}
		BUG_ON(ret);
	}

	return ret;
}

/*
 * this adds all existing backrefs (inline backrefs, backrefs and delayed
 * refs) for the given bytenr to the refs list, merges duplicates and resolves
 * indirect refs to their parent bytenr.
 * When roots are found, they're added to the roots list
 *
 * FIXME some caching might speed things up
 */
static int find_parent_nodes(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info, u64 bytenr,
			     u64 seq, struct ulist *refs, struct ulist *roots)
{
	struct btrfs_key key;
	struct btrfs_path *path;
	struct btrfs_key info_key = { 0 };
	struct btrfs_delayed_ref_root *delayed_refs = NULL;
	struct btrfs_delayed_ref_head *head;
	int info_level = 0;
	int ret;
	int search_commit_root = (trans == BTRFS_BACKREF_SEARCH_COMMIT_ROOT);
	struct list_head prefs_delayed;
	struct list_head prefs;
	struct __prelim_ref *ref;

	INIT_LIST_HEAD(&prefs);
	INIT_LIST_HEAD(&prefs_delayed);

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = (u64)-1;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->search_commit_root = !!search_commit_root;

	/*
	 * grab both a lock on the path and a lock on the delayed ref head.
	 * We need both to get a consistent picture of how the refs look
	 * at a specified point in time
	 */
again:
	head = NULL;

	ret = btrfs_search_slot(trans, fs_info->extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	BUG_ON(ret == 0);

	if (trans != BTRFS_BACKREF_SEARCH_COMMIT_ROOT) {
		/*
		 * look if there are updates for this ref queued and lock the
		 * head
		 */
		delayed_refs = &trans->transaction->delayed_refs;
		spin_lock(&delayed_refs->lock);
		head = btrfs_find_delayed_ref_head(trans, bytenr);
		if (head) {
			if (!mutex_trylock(&head->mutex)) {
				atomic_inc(&head->node.refs);
				spin_unlock(&delayed_refs->lock);

				btrfs_release_path(path);

				/*
				 * Mutex was contended, block until it's
				 * released and try again
				 */
				mutex_lock(&head->mutex);
				mutex_unlock(&head->mutex);
				btrfs_put_delayed_ref(&head->node);
				goto again;
			}
			ret = __add_delayed_refs(head, seq, &info_key,
						 &prefs_delayed);
			if (ret) {
				spin_unlock(&delayed_refs->lock);
				goto out;
			}
		}
		spin_unlock(&delayed_refs->lock);
	}

	if (path->slots[0]) {
		struct extent_buffer *leaf;
		int slot;

		leaf = path->nodes[0];
		slot = path->slots[0] - 1;
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid == bytenr &&
		    key.type == BTRFS_EXTENT_ITEM_KEY) {
			ret = __add_inline_refs(fs_info, path, bytenr,
						&info_key, &info_level, &prefs);
			if (ret)
				goto out;
			ret = __add_keyed_refs(fs_info, path, bytenr, &info_key,
					       info_level, &prefs);
			if (ret)
				goto out;
		}
	}
	btrfs_release_path(path);

	/*
	 * when adding the delayed refs above, the info_key might not have
	 * been known yet. Go over the list and replace the missing keys
	 */
	list_for_each_entry(ref, &prefs_delayed, list) {
		if ((ref->key.offset | ref->key.type | ref->key.objectid) == 0)
			memcpy(&ref->key, &info_key, sizeof(ref->key));
	}
	list_splice_init(&prefs_delayed, &prefs);

	ret = __merge_refs(&prefs, 1);
	if (ret)
		goto out;

	ret = __resolve_indirect_refs(fs_info, search_commit_root, &prefs);
	if (ret)
		goto out;

	ret = __merge_refs(&prefs, 2);
	if (ret)
		goto out;

	while (!list_empty(&prefs)) {
		ref = list_first_entry(&prefs, struct __prelim_ref, list);
		list_del(&ref->list);
		if (ref->count < 0)
			WARN_ON(1);
		if (ref->count && ref->root_id && ref->parent == 0) {
			/* no parent == root of tree */
			ret = ulist_add(roots, ref->root_id, 0, GFP_NOFS);
			BUG_ON(ret < 0);
		}
		if (ref->count && ref->parent) {
			ret = ulist_add(refs, ref->parent, 0, GFP_NOFS);
			BUG_ON(ret < 0);
		}
		kfree(ref);
	}

out:
	if (head)
		mutex_unlock(&head->mutex);
	btrfs_free_path(path);
	while (!list_empty(&prefs)) {
		ref = list_first_entry(&prefs, struct __prelim_ref, list);
		list_del(&ref->list);
		kfree(ref);
	}
	while (!list_empty(&prefs_delayed)) {
		ref = list_first_entry(&prefs_delayed, struct __prelim_ref,
				       list);
		list_del(&ref->list);
		kfree(ref);
	}

	return ret;
}

/*
 * Finds all leafs with a reference to the specified combination of bytenr and
 * offset. key_list_head will point to a list of corresponding keys (caller must
 * free each list element). The leafs will be stored in the leafs ulist, which
 * must be freed with ulist_free.
 *
 * returns 0 on success, <0 on error
 */
static int btrfs_find_all_leafs(struct btrfs_trans_handle *trans,
				struct btrfs_fs_info *fs_info, u64 bytenr,
				u64 num_bytes, u64 seq, struct ulist **leafs)
{
	struct ulist *tmp;
	int ret;

	tmp = ulist_alloc(GFP_NOFS);
	if (!tmp)
		return -ENOMEM;
	*leafs = ulist_alloc(GFP_NOFS);
	if (!*leafs) {
		ulist_free(tmp);
		return -ENOMEM;
	}

	ret = find_parent_nodes(trans, fs_info, bytenr, seq, *leafs, tmp);
	ulist_free(tmp);

	if (ret < 0 && ret != -ENOENT) {
		ulist_free(*leafs);
		return ret;
	}

	return 0;
}

/*
 * walk all backrefs for a given extent to find all roots that reference this
 * extent. Walking a backref means finding all extents that reference this
 * extent and in turn walk the backrefs of those, too. Naturally this is a
 * recursive process, but here it is implemented in an iterative fashion: We
 * find all referencing extents for the extent in question and put them on a
 * list. In turn, we find all referencing extents for those, further appending
 * to the list. The way we iterate the list allows adding more elements after
 * the current while iterating. The process stops when we reach the end of the
 * list. Found roots are added to the roots list.
 *
 * returns 0 on success, < 0 on error.
 */
int btrfs_find_all_roots(struct btrfs_trans_handle *trans,
				struct btrfs_fs_info *fs_info, u64 bytenr,
				u64 num_bytes, u64 seq, struct ulist **roots)
{
	struct ulist *tmp;
	struct ulist_node *node = NULL;
	int ret;

	tmp = ulist_alloc(GFP_NOFS);
	if (!tmp)
		return -ENOMEM;
	*roots = ulist_alloc(GFP_NOFS);
	if (!*roots) {
		ulist_free(tmp);
		return -ENOMEM;
	}

	while (1) {
		ret = find_parent_nodes(trans, fs_info, bytenr, seq,
					tmp, *roots);
		if (ret < 0 && ret != -ENOENT) {
			ulist_free(tmp);
			ulist_free(*roots);
			return ret;
		}
		node = ulist_next(tmp, node);
		if (!node)
			break;
		bytenr = node->val;
	}

	ulist_free(tmp);
	return 0;
}


static int __inode_info(u64 inum, u64 ioff, u8 key_type,
			struct btrfs_root *fs_root, struct btrfs_path *path,
			struct btrfs_key *found_key)
{
	int ret;
	struct btrfs_key key;
	struct extent_buffer *eb;

	key.type = key_type;
	key.objectid = inum;
	key.offset = ioff;

	ret = btrfs_search_slot(NULL, fs_root, &key, path, 0, 0);
	if (ret < 0)
		return ret;

	eb = path->nodes[0];
	if (ret && path->slots[0] >= btrfs_header_nritems(eb)) {
		ret = btrfs_next_leaf(fs_root, path);
		if (ret)
			return ret;
		eb = path->nodes[0];
	}

	btrfs_item_key_to_cpu(eb, found_key, path->slots[0]);
	if (found_key->type != key.type || found_key->objectid != key.objectid)
		return 1;

	return 0;
}

/*
 * this makes the path point to (inum INODE_ITEM ioff)
 */
int inode_item_info(u64 inum, u64 ioff, struct btrfs_root *fs_root,
			struct btrfs_path *path)
{
	struct btrfs_key key;
	return __inode_info(inum, ioff, BTRFS_INODE_ITEM_KEY, fs_root, path,
				&key);
}

static int inode_ref_info(u64 inum, u64 ioff, struct btrfs_root *fs_root,
				struct btrfs_path *path,
				struct btrfs_key *found_key)
{
	return __inode_info(inum, ioff, BTRFS_INODE_REF_KEY, fs_root, path,
				found_key);
}

/*
 * this iterates to turn a btrfs_inode_ref into a full filesystem path. elements
 * of the path are separated by '/' and the path is guaranteed to be
 * 0-terminated. the path is only given within the current file system.
 * Therefore, it never starts with a '/'. the caller is responsible to provide
 * "size" bytes in "dest". the dest buffer will be filled backwards. finally,
 * the start point of the resulting string is returned. this pointer is within
 * dest, normally.
 * in case the path buffer would overflow, the pointer is decremented further
 * as if output was written to the buffer, though no more output is actually
 * generated. that way, the caller can determine how much space would be
 * required for the path to fit into the buffer. in that case, the returned
 * value will be smaller than dest. callers must check this!
 */
static char *iref_to_path(struct btrfs_root *fs_root, struct btrfs_path *path,
				struct btrfs_inode_ref *iref,
				struct extent_buffer *eb_in, u64 parent,
				char *dest, u32 size)
{
	u32 len;
	int slot;
	u64 next_inum;
	int ret;
	s64 bytes_left = size - 1;
	struct extent_buffer *eb = eb_in;
	struct btrfs_key found_key;
	int leave_spinning = path->leave_spinning;

	if (bytes_left >= 0)
		dest[bytes_left] = '\0';

	path->leave_spinning = 1;
	while (1) {
		len = btrfs_inode_ref_name_len(eb, iref);
		bytes_left -= len;
		if (bytes_left >= 0)
			read_extent_buffer(eb, dest + bytes_left,
						(unsigned long)(iref + 1), len);
		if (eb != eb_in) {
			btrfs_tree_read_unlock_blocking(eb);
			free_extent_buffer(eb);
		}
		ret = inode_ref_info(parent, 0, fs_root, path, &found_key);
		if (ret > 0)
			ret = -ENOENT;
		if (ret)
			break;
		next_inum = found_key.offset;

		/* regular exit ahead */
		if (parent == next_inum)
			break;

		slot = path->slots[0];
		eb = path->nodes[0];
		/* make sure we can use eb after releasing the path */
		if (eb != eb_in) {
			atomic_inc(&eb->refs);
			btrfs_tree_read_lock(eb);
			btrfs_set_lock_blocking_rw(eb, BTRFS_READ_LOCK);
		}
		btrfs_release_path(path);

		iref = btrfs_item_ptr(eb, slot, struct btrfs_inode_ref);
		parent = next_inum;
		--bytes_left;
		if (bytes_left >= 0)
			dest[bytes_left] = '/';
	}

	btrfs_release_path(path);
	path->leave_spinning = leave_spinning;

	if (ret)
		return ERR_PTR(ret);

	return dest + bytes_left;
}

/*
 * this makes the path point to (logical EXTENT_ITEM *)
 * returns BTRFS_EXTENT_FLAG_DATA for data, BTRFS_EXTENT_FLAG_TREE_BLOCK for
 * tree blocks and <0 on error.
 */
int extent_from_logical(struct btrfs_fs_info *fs_info, u64 logical,
			struct btrfs_path *path, struct btrfs_key *found_key)
{
	int ret;
	u64 flags;
	u32 item_size;
	struct extent_buffer *eb;
	struct btrfs_extent_item *ei;
	struct btrfs_key key;

	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.objectid = logical;
	key.offset = (u64)-1;

	ret = btrfs_search_slot(NULL, fs_info->extent_root, &key, path, 0, 0);
	if (ret < 0)
		return ret;
	ret = btrfs_previous_item(fs_info->extent_root, path,
					0, BTRFS_EXTENT_ITEM_KEY);
	if (ret < 0)
		return ret;

	btrfs_item_key_to_cpu(path->nodes[0], found_key, path->slots[0]);
	if (found_key->type != BTRFS_EXTENT_ITEM_KEY ||
	    found_key->objectid > logical ||
	    found_key->objectid + found_key->offset <= logical) {
		pr_debug("logical %llu is not within any extent\n",
			 (unsigned long long)logical);
		return -ENOENT;
	}

	eb = path->nodes[0];
	item_size = btrfs_item_size_nr(eb, path->slots[0]);
	BUG_ON(item_size < sizeof(*ei));

	ei = btrfs_item_ptr(eb, path->slots[0], struct btrfs_extent_item);
	flags = btrfs_extent_flags(eb, ei);

	pr_debug("logical %llu is at position %llu within the extent (%llu "
		 "EXTENT_ITEM %llu) flags %#llx size %u\n",
		 (unsigned long long)logical,
		 (unsigned long long)(logical - found_key->objectid),
		 (unsigned long long)found_key->objectid,
		 (unsigned long long)found_key->offset,
		 (unsigned long long)flags, item_size);
	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK)
		return BTRFS_EXTENT_FLAG_TREE_BLOCK;
	if (flags & BTRFS_EXTENT_FLAG_DATA)
		return BTRFS_EXTENT_FLAG_DATA;

	return -EIO;
}

/*
 * helper function to iterate extent inline refs. ptr must point to a 0 value
 * for the first call and may be modified. it is used to track state.
 * if more refs exist, 0 is returned and the next call to
 * __get_extent_inline_ref must pass the modified ptr parameter to get the
 * next ref. after the last ref was processed, 1 is returned.
 * returns <0 on error
 */
static int __get_extent_inline_ref(unsigned long *ptr, struct extent_buffer *eb,
				struct btrfs_extent_item *ei, u32 item_size,
				struct btrfs_extent_inline_ref **out_eiref,
				int *out_type)
{
	unsigned long end;
	u64 flags;
	struct btrfs_tree_block_info *info;

	if (!*ptr) {
		/* first call */
		flags = btrfs_extent_flags(eb, ei);
		if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
			info = (struct btrfs_tree_block_info *)(ei + 1);
			*out_eiref =
				(struct btrfs_extent_inline_ref *)(info + 1);
		} else {
			*out_eiref = (struct btrfs_extent_inline_ref *)(ei + 1);
		}
		*ptr = (unsigned long)*out_eiref;
		if ((unsigned long)(*ptr) >= (unsigned long)ei + item_size)
			return -ENOENT;
	}

	end = (unsigned long)ei + item_size;
	*out_eiref = (struct btrfs_extent_inline_ref *)*ptr;
	*out_type = btrfs_extent_inline_ref_type(eb, *out_eiref);

	*ptr += btrfs_extent_inline_ref_size(*out_type);
	WARN_ON(*ptr > end);
	if (*ptr == end)
		return 1; /* last */

	return 0;
}

/*
 * reads the tree block backref for an extent. tree level and root are returned
 * through out_level and out_root. ptr must point to a 0 value for the first
 * call and may be modified (see __get_extent_inline_ref comment).
 * returns 0 if data was provided, 1 if there was no more data to provide or
 * <0 on error.
 */
int tree_backref_for_extent(unsigned long *ptr, struct extent_buffer *eb,
				struct btrfs_extent_item *ei, u32 item_size,
				u64 *out_root, u8 *out_level)
{
	int ret;
	int type;
	struct btrfs_tree_block_info *info;
	struct btrfs_extent_inline_ref *eiref;

	if (*ptr == (unsigned long)-1)
		return 1;

	while (1) {
		ret = __get_extent_inline_ref(ptr, eb, ei, item_size,
						&eiref, &type);
		if (ret < 0)
			return ret;

		if (type == BTRFS_TREE_BLOCK_REF_KEY ||
		    type == BTRFS_SHARED_BLOCK_REF_KEY)
			break;

		if (ret == 1)
			return 1;
	}

	/* we can treat both ref types equally here */
	info = (struct btrfs_tree_block_info *)(ei + 1);
	*out_root = btrfs_extent_inline_ref_offset(eb, eiref);
	*out_level = btrfs_tree_block_level(eb, info);

	if (ret == 1)
		*ptr = (unsigned long)-1;

	return 0;
}

static int iterate_leaf_refs(struct btrfs_fs_info *fs_info, u64 logical,
				u64 orig_extent_item_objectid,
				u64 extent_item_pos, u64 root,
				iterate_extent_inodes_t *iterate, void *ctx)
{
	u64 disk_byte;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	struct extent_buffer *eb;
	int slot;
	int nritems;
	int ret = 0;
	int extent_type;
	u64 data_offset;
	u64 data_len;

	eb = read_tree_block(fs_info->tree_root, logical,
				fs_info->tree_root->leafsize, 0);
	if (!eb)
		return -EIO;

	/*
	 * from the shared data ref, we only have the leaf but we need
	 * the key. thus, we must look into all items and see that we
	 * find one (some) with a reference to our extent item.
	 */
	nritems = btrfs_header_nritems(eb);
	for (slot = 0; slot < nritems; ++slot) {
		btrfs_item_key_to_cpu(eb, &key, slot);
		if (key.type != BTRFS_EXTENT_DATA_KEY)
			continue;
		fi = btrfs_item_ptr(eb, slot, struct btrfs_file_extent_item);
		extent_type = btrfs_file_extent_type(eb, fi);
		if (extent_type == BTRFS_FILE_EXTENT_INLINE)
			continue;
		/* don't skip BTRFS_FILE_EXTENT_PREALLOC, we can handle that */
		disk_byte = btrfs_file_extent_disk_bytenr(eb, fi);
		if (disk_byte != orig_extent_item_objectid)
			continue;

		data_offset = btrfs_file_extent_offset(eb, fi);
		data_len = btrfs_file_extent_num_bytes(eb, fi);

		if (extent_item_pos < data_offset ||
		    extent_item_pos >= data_offset + data_len)
			continue;

		pr_debug("ref for %llu resolved, key (%llu EXTEND_DATA %llu), "
				"root %llu\n", orig_extent_item_objectid,
				key.objectid, key.offset, root);
		ret = iterate(key.objectid,
				key.offset + (extent_item_pos - data_offset),
				root, ctx);
		if (ret) {
			pr_debug("stopping iteration because ret=%d\n", ret);
			break;
		}
	}

	free_extent_buffer(eb);

	return ret;
}

/*
 * calls iterate() for every inode that references the extent identified by
 * the given parameters.
 * when the iterator function returns a non-zero value, iteration stops.
 */
int iterate_extent_inodes(struct btrfs_fs_info *fs_info,
				u64 extent_item_objectid, u64 extent_item_pos,
				int search_commit_root,
				iterate_extent_inodes_t *iterate, void *ctx)
{
	int ret;
	struct list_head data_refs = LIST_HEAD_INIT(data_refs);
	struct list_head shared_refs = LIST_HEAD_INIT(shared_refs);
	struct btrfs_trans_handle *trans;
	struct ulist *refs = NULL;
	struct ulist *roots = NULL;
	struct ulist_node *ref_node = NULL;
	struct ulist_node *root_node = NULL;
	struct seq_list seq_elem;
	struct btrfs_delayed_ref_root *delayed_refs = NULL;

	pr_debug("resolving all inodes for extent %llu\n",
			extent_item_objectid);

	if (search_commit_root) {
		trans = BTRFS_BACKREF_SEARCH_COMMIT_ROOT;
	} else {
		trans = btrfs_join_transaction(fs_info->extent_root);
		if (IS_ERR(trans))
			return PTR_ERR(trans);

		delayed_refs = &trans->transaction->delayed_refs;
		spin_lock(&delayed_refs->lock);
		btrfs_get_delayed_seq(delayed_refs, &seq_elem);
		spin_unlock(&delayed_refs->lock);
	}

	ret = btrfs_find_all_leafs(trans, fs_info, extent_item_objectid,
				   extent_item_pos, seq_elem.seq,
				   &refs);

	if (ret)
		goto out;

	while (!ret && (ref_node = ulist_next(refs, ref_node))) {
		ret = btrfs_find_all_roots(trans, fs_info, ref_node->val, -1,
						seq_elem.seq, &roots);
		if (ret)
			break;
		while (!ret && (root_node = ulist_next(roots, root_node))) {
			pr_debug("root %llu references leaf %llu\n",
					root_node->val, ref_node->val);
			ret = iterate_leaf_refs(fs_info, ref_node->val,
						extent_item_objectid,
						extent_item_pos, root_node->val,
						iterate, ctx);
		}
	}

	ulist_free(refs);
	ulist_free(roots);
out:
	if (!search_commit_root) {
		btrfs_put_delayed_seq(delayed_refs, &seq_elem);
		btrfs_end_transaction(trans, fs_info->extent_root);
	}

	return ret;
}

int iterate_inodes_from_logical(u64 logical, struct btrfs_fs_info *fs_info,
				struct btrfs_path *path,
				iterate_extent_inodes_t *iterate, void *ctx)
{
	int ret;
	u64 extent_item_pos;
	struct btrfs_key found_key;
	int search_commit_root = path->search_commit_root;

	ret = extent_from_logical(fs_info, logical, path,
					&found_key);
	btrfs_release_path(path);
	if (ret & BTRFS_EXTENT_FLAG_TREE_BLOCK)
		ret = -EINVAL;
	if (ret < 0)
		return ret;

	extent_item_pos = logical - found_key.objectid;
	ret = iterate_extent_inodes(fs_info, found_key.objectid,
					extent_item_pos, search_commit_root,
					iterate, ctx);

	return ret;
}

static int iterate_irefs(u64 inum, struct btrfs_root *fs_root,
				struct btrfs_path *path,
				iterate_irefs_t *iterate, void *ctx)
{
	int ret = 0;
	int slot;
	u32 cur;
	u32 len;
	u32 name_len;
	u64 parent = 0;
	int found = 0;
	struct extent_buffer *eb;
	struct btrfs_item *item;
	struct btrfs_inode_ref *iref;
	struct btrfs_key found_key;

	while (!ret) {
		path->leave_spinning = 1;
		ret = inode_ref_info(inum, parent ? parent+1 : 0, fs_root, path,
					&found_key);
		if (ret < 0)
			break;
		if (ret) {
			ret = found ? 0 : -ENOENT;
			break;
		}
		++found;

		parent = found_key.offset;
		slot = path->slots[0];
		eb = path->nodes[0];
		/* make sure we can use eb after releasing the path */
		atomic_inc(&eb->refs);
		btrfs_tree_read_lock(eb);
		btrfs_set_lock_blocking_rw(eb, BTRFS_READ_LOCK);
		btrfs_release_path(path);

		item = btrfs_item_nr(eb, slot);
		iref = btrfs_item_ptr(eb, slot, struct btrfs_inode_ref);

		for (cur = 0; cur < btrfs_item_size(eb, item); cur += len) {
			name_len = btrfs_inode_ref_name_len(eb, iref);
			/* path must be released before calling iterate()! */
			pr_debug("following ref at offset %u for inode %llu in "
				 "tree %llu\n", cur,
				 (unsigned long long)found_key.objectid,
				 (unsigned long long)fs_root->objectid);
			ret = iterate(parent, iref, eb, ctx);
			if (ret)
				break;
			len = sizeof(*iref) + name_len;
			iref = (struct btrfs_inode_ref *)((char *)iref + len);
		}
		btrfs_tree_read_unlock_blocking(eb);
		free_extent_buffer(eb);
	}

	btrfs_release_path(path);

	return ret;
}

/*
 * returns 0 if the path could be dumped (probably truncated)
 * returns <0 in case of an error
 */
static int inode_to_path(u64 inum, struct btrfs_inode_ref *iref,
				struct extent_buffer *eb, void *ctx)
{
	struct inode_fs_paths *ipath = ctx;
	char *fspath;
	char *fspath_min;
	int i = ipath->fspath->elem_cnt;
	const int s_ptr = sizeof(char *);
	u32 bytes_left;

	bytes_left = ipath->fspath->bytes_left > s_ptr ?
					ipath->fspath->bytes_left - s_ptr : 0;

	fspath_min = (char *)ipath->fspath->val + (i + 1) * s_ptr;
	fspath = iref_to_path(ipath->fs_root, ipath->btrfs_path, iref, eb,
				inum, fspath_min, bytes_left);
	if (IS_ERR(fspath))
		return PTR_ERR(fspath);

	if (fspath > fspath_min) {
		pr_debug("path resolved: %s\n", fspath);
		ipath->fspath->val[i] = (u64)(unsigned long)fspath;
		++ipath->fspath->elem_cnt;
		ipath->fspath->bytes_left = fspath - fspath_min;
	} else {
		pr_debug("missed path, not enough space. missing bytes: %lu, "
			 "constructed so far: %s\n",
			 (unsigned long)(fspath_min - fspath), fspath_min);
		++ipath->fspath->elem_missed;
		ipath->fspath->bytes_missing += fspath_min - fspath;
		ipath->fspath->bytes_left = 0;
	}

	return 0;
}

/*
 * this dumps all file system paths to the inode into the ipath struct, provided
 * is has been created large enough. each path is zero-terminated and accessed
 * from ipath->fspath->val[i].
 * when it returns, there are ipath->fspath->elem_cnt number of paths available
 * in ipath->fspath->val[]. when the allocated space wasn't sufficient, the
 * number of missed paths in recored in ipath->fspath->elem_missed, otherwise,
 * it's zero. ipath->fspath->bytes_missing holds the number of bytes that would
 * have been needed to return all paths.
 */
int paths_from_inode(u64 inum, struct inode_fs_paths *ipath)
{
	return iterate_irefs(inum, ipath->fs_root, ipath->btrfs_path,
				inode_to_path, ipath);
}

struct btrfs_data_container *init_data_container(u32 total_bytes)
{
	struct btrfs_data_container *data;
	size_t alloc_bytes;

	alloc_bytes = max_t(size_t, total_bytes, sizeof(*data));
	data = kmalloc(alloc_bytes, GFP_NOFS);
	if (!data)
		return ERR_PTR(-ENOMEM);

	if (total_bytes >= sizeof(*data)) {
		data->bytes_left = total_bytes - sizeof(*data);
		data->bytes_missing = 0;
	} else {
		data->bytes_missing = sizeof(*data) - total_bytes;
		data->bytes_left = 0;
	}

	data->elem_cnt = 0;
	data->elem_missed = 0;

	return data;
}

/*
 * allocates space to return multiple file system paths for an inode.
 * total_bytes to allocate are passed, note that space usable for actual path
 * information will be total_bytes - sizeof(struct inode_fs_paths).
 * the returned pointer must be freed with free_ipath() in the end.
 */
struct inode_fs_paths *init_ipath(s32 total_bytes, struct btrfs_root *fs_root,
					struct btrfs_path *path)
{
	struct inode_fs_paths *ifp;
	struct btrfs_data_container *fspath;

	fspath = init_data_container(total_bytes);
	if (IS_ERR(fspath))
		return (void *)fspath;

	ifp = kmalloc(sizeof(*ifp), GFP_NOFS);
	if (!ifp) {
		kfree(fspath);
		return ERR_PTR(-ENOMEM);
	}

	ifp->btrfs_path = path;
	ifp->fspath = fspath;
	ifp->fs_root = fs_root;

	return ifp;
}

void free_ipath(struct inode_fs_paths *ipath)
{
	if (!ipath)
		return;
	kfree(ipath->fspath);
	kfree(ipath);
}
