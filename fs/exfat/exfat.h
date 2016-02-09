/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _EXFAT_H
#define _EXFAT_H

#include "exfat_config.h"
#include "exfat_global.h"
#include "exfat_data.h"
#include "exfat_oal.h"

#include "exfat_blkdev.h"
#include "exfat_cache.h"
#include "exfat_nls.h"
#include "exfat_api.h"
#include "exfat_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

#if EXFAT_CONFIG_KERNEL_DEBUG
#define EXFAT_IOC_GET_DEBUGFLAGS       _IOR('f', 100, long)
#define EXFAT_IOC_SET_DEBUGFLAGS       _IOW('f', 101, long)

#define EXFAT_DEBUGFLAGS_INVALID_UMOUNT        0x01
#define EXFAT_DEBUGFLAGS_ERROR_RW              0x02
#endif

#define MAX_VOLUME              4

#define DENTRY_SIZE             32
#define DENTRY_SIZE_BITS        5

#define PBR_SIGNATURE           0xAA55
#define EXT_SIGNATURE           0xAA550000
#define VOL_LABEL               "NO NAME    "
#define OEM_NAME                "MSWIN4.1"
#define STR_FAT12               "FAT12   "
#define STR_FAT16               "FAT16   "
#define STR_FAT32               "FAT32   "
#define STR_EXFAT               "EXFAT   "
#define VOL_CLEAN               0x0000
#define VOL_DIRTY               0x0002

#define FAT12_THRESHOLD         4087
#define FAT16_THRESHOLD         65527
#define FAT32_THRESHOLD         268435457
#define EXFAT_THRESHOLD         268435457

#define TYPE_UNUSED             0x0000
#define TYPE_DELETED            0x0001
#define TYPE_INVALID            0x0002
#define TYPE_CRITICAL_PRI       0x0100
#define TYPE_BITMAP             0x0101
#define TYPE_UPCASE             0x0102
#define TYPE_VOLUME             0x0103
#define TYPE_DIR                0x0104
#define TYPE_FILE               0x011F
#define TYPE_SYMLINK            0x015F
#define TYPE_CRITICAL_SEC       0x0200
#define TYPE_STREAM             0x0201
#define TYPE_EXTEND             0x0202
#define TYPE_ACL                0x0203
#define TYPE_BENIGN_PRI         0x0400
#define TYPE_GUID               0x0401
#define TYPE_PADDING            0x0402
#define TYPE_ACLTAB             0x0403
#define TYPE_BENIGN_SEC         0x0800
#define TYPE_ALL                0x0FFF

#define TM_CREATE               0
#define TM_MODIFY               1
#define TM_ACCESS               2

#define CS_DIR_ENTRY            0
#define CS_PBR_SECTOR           1
#define CS_DEFAULT              2

#define DIR_DELETED		0xFFFF0321

#define CLUSTER_16(x)           ((UINT16)(x))
#define CLUSTER_32(x)           ((UINT32)(x))

#define START_SECTOR(x) \
        ( (((x)-2) << p_fs->sectors_per_clu_bits) + p_fs->data_start_sector )

#define IS_LAST_SECTOR_IN_CLUSTER(sec) \
		( (((sec) -  p_fs->data_start_sector + 1) & ((1 <<  p_fs->sectors_per_clu_bits) -1)) == 0)

#define GET_CLUSTER_FROM_SECTOR(sec)			\
		((((sec) -  p_fs->data_start_sector) >> p_fs->sectors_per_clu_bits) +2)

#define GET16(p_src) \
        ( ((UINT16)(p_src)[0]) | (((UINT16)(p_src)[1]) << 8) )
#define GET32(p_src) \
        ( ((UINT32)(p_src)[0]) | (((UINT32)(p_src)[1]) << 8) | \
         (((UINT32)(p_src)[2]) << 16) | (((UINT32)(p_src)[3]) << 24) )
#define GET64(p_src) \
        ( ((UINT64)(p_src)[0]) | (((UINT64)(p_src)[1]) << 8) | \
         (((UINT64)(p_src)[2]) << 16) | (((UINT64)(p_src)[3]) << 24) | \
         (((UINT64)(p_src)[4]) << 32) | (((UINT64)(p_src)[5]) << 40) | \
         (((UINT64)(p_src)[6]) << 48) | (((UINT64)(p_src)[7]) << 56) )


#define SET16(p_dst,src)                                  \
        do {                                              \
            (p_dst)[0]=(UINT8)(src);                      \
            (p_dst)[1]=(UINT8)(((UINT16)(src)) >> 8);     \
        } while (0)
#define SET32(p_dst,src)                                  \
        do {                                              \
            (p_dst)[0]=(UINT8)(src);                      \
            (p_dst)[1]=(UINT8)(((UINT32)(src)) >> 8);     \
            (p_dst)[2]=(UINT8)(((UINT32)(src)) >> 16);    \
            (p_dst)[3]=(UINT8)(((UINT32)(src)) >> 24);    \
        } while (0)
#define SET64(p_dst,src)                                  \
        do {                                              \
            (p_dst)[0]=(UINT8)(src);                      \
            (p_dst)[1]=(UINT8)(((UINT64)(src)) >> 8);     \
            (p_dst)[2]=(UINT8)(((UINT64)(src)) >> 16);    \
            (p_dst)[3]=(UINT8)(((UINT64)(src)) >> 24);    \
            (p_dst)[4]=(UINT8)(((UINT64)(src)) >> 32);    \
            (p_dst)[5]=(UINT8)(((UINT64)(src)) >> 40);    \
            (p_dst)[6]=(UINT8)(((UINT64)(src)) >> 48);    \
            (p_dst)[7]=(UINT8)(((UINT64)(src)) >> 56);    \
        } while (0)

#if (FFS_CONFIG_LITTLE_ENDIAN == 1)
#define GET16_A(p_src)          (*((UINT16 *)(p_src)))
#define GET32_A(p_src)          (*((UINT32 *)(p_src)))
#define GET64_A(p_src)          (*((UINT64 *)(p_src)))
#define SET16_A(p_dst,src)      *((UINT16 *)(p_dst)) = (UINT16)(src)
#define SET32_A(p_dst,src)      *((UINT32 *)(p_dst)) = (UINT32)(src)
#define SET64_A(p_dst,src)      *((UINT64 *)(p_dst)) = (UINT64)(src)
#else
#define GET16_A(p_src)          GET16(p_src)
#define GET32_A(p_src)          GET32(p_src)
#define GET64_A(p_src)          GET64(p_src)
#define SET16_A(p_dst,src)      SET16(p_dst, src)
#define SET32_A(p_dst,src)      SET32(p_dst, src)
#define SET64_A(p_dst,src)      SET64(p_dst, src)
#endif

#define HIGH_INDEX_BIT (8)
#define HIGH_INDEX_MASK (0xFF00)
#define LOW_INDEX_BIT (16-HIGH_INDEX_BIT)
#define UTBL_ROW_COUNT (1<<LOW_INDEX_BIT)
#define UTBL_COL_COUNT (1<<HIGH_INDEX_BIT)

	static inline UINT16 get_col_index(UINT16 i)
	{
		return i >> LOW_INDEX_BIT;
	}
	static inline UINT16 get_row_index(UINT16 i)
	{
		return i & ~HIGH_INDEX_MASK;
	}
	
	typedef struct {
		UINT8       jmp_boot[3];
		UINT8       oem_name[8];
		UINT8       bpb[109];
		UINT8       boot_code[390];
		UINT8       signature[2];
	} PBR_SECTOR_T;

	typedef struct {
		UINT8       sector_size[2];
		UINT8       sectors_per_clu;
		UINT8       num_reserved[2];
		UINT8       num_fats;
		UINT8       num_root_entries[2];
		UINT8       num_sectors[2];
		UINT8       media_type;
		UINT8       num_fat_sectors[2];
		UINT8       sectors_in_track[2];
		UINT8       num_heads[2];
		UINT8       num_hid_sectors[4];
		UINT8       num_huge_sectors[4];

		UINT8       phy_drv_no;
		UINT8       reserved;
		UINT8       ext_signature;
		UINT8       vol_serial[4];
		UINT8       vol_label[11];
		UINT8       vol_type[8];
	} BPB16_T;

	typedef struct {
		UINT8       sector_size[2];
		UINT8       sectors_per_clu;
		UINT8       num_reserved[2];
		UINT8       num_fats;
		UINT8       num_root_entries[2];
		UINT8       num_sectors[2];
		UINT8       media_type;
		UINT8       num_fat_sectors[2];
		UINT8       sectors_in_track[2];
		UINT8       num_heads[2];
		UINT8       num_hid_sectors[4];
		UINT8       num_huge_sectors[4];
		UINT8       num_fat32_sectors[4];
		UINT8       ext_flags[2];
		UINT8       fs_version[2];
		UINT8       root_cluster[4];
		UINT8       fsinfo_sector[2];
		UINT8       backup_sector[2];
		UINT8       reserved[12];

		UINT8       phy_drv_no;
		UINT8       ext_reserved;
		UINT8       ext_signature;
		UINT8       vol_serial[4];
		UINT8       vol_label[11];
		UINT8       vol_type[8];
	} BPB32_T;

	typedef struct {
		UINT8       reserved1[53];
		UINT8       vol_offset[8];
		UINT8       vol_length[8];
		UINT8       fat_offset[4];
		UINT8       fat_length[4];
		UINT8       clu_offset[4];
		UINT8       clu_count[4];
		UINT8       root_cluster[4];
		UINT8       vol_serial[4];
		UINT8       fs_version[2];
		UINT8       vol_flags[2];
		UINT8       sector_size_bits;
		UINT8       sectors_per_clu_bits;
		UINT8       num_fats;
		UINT8       phy_drv_no;
		UINT8       perc_in_use;
		UINT8       reserved2[7];
	} BPBEX_T;

	typedef struct {
		UINT8       signature1[4];
		UINT8       reserved1[480];
		UINT8       signature2[4];
		UINT8       free_cluster[4];
		UINT8       next_cluster[4];
		UINT8       reserved2[14];
		UINT8       signature3[2];
	} FSI_SECTOR_T;

	typedef struct {
		UINT8       dummy[32];
	} DENTRY_T;

	typedef struct {
		UINT8       name[DOS_NAME_LENGTH];
		UINT8       attr;
		UINT8       lcase;
		UINT8       create_time_ms;
		UINT8       create_time[2];
		UINT8       create_date[2];
		UINT8       access_date[2];
		UINT8       start_clu_hi[2];
		UINT8       modify_time[2];
		UINT8       modify_date[2];
		UINT8       start_clu_lo[2];
		UINT8       size[4];
	} DOS_DENTRY_T;

	typedef struct {
		UINT8       order;
		UINT8       unicode_0_4[10];
		UINT8       attr;
		UINT8       sysid;
		UINT8       checksum;
		UINT8       unicode_5_10[12];
		UINT8       start_clu[2];
		UINT8       unicode_11_12[4];
	} EXT_DENTRY_T;

	typedef struct {
		UINT8       type;
		UINT8       num_ext;
		UINT8       checksum[2];
		UINT8       attr[2];
		UINT8       reserved1[2];
		UINT8       create_time[2];
		UINT8       create_date[2];
		UINT8       modify_time[2];
		UINT8       modify_date[2];
		UINT8       access_time[2];
		UINT8       access_date[2];
		UINT8       create_time_ms;
		UINT8       modify_time_ms;
		UINT8       access_time_ms;
		UINT8       reserved2[9];
	} FILE_DENTRY_T;

	typedef struct {
		UINT8       type;
		UINT8       flags;
		UINT8       reserved1;
		UINT8       name_len;
		UINT8       name_hash[2];
		UINT8       reserved2[2];
		UINT8       valid_size[8];
		UINT8       reserved3[4];
		UINT8       start_clu[4];
		UINT8       size[8];
	} STRM_DENTRY_T;

	typedef struct {
		UINT8       type;
		UINT8       flags;
		UINT8       unicode_0_14[30];
	} NAME_DENTRY_T;

	typedef struct {
		UINT8       type;
		UINT8       flags;
		UINT8       reserved[18];
		UINT8       start_clu[4];
		UINT8       size[8];
	} BMAP_DENTRY_T;

	typedef struct {
		UINT8       type;
		UINT8       reserved1[3];
		UINT8       checksum[4];
		UINT8       reserved2[12];
		UINT8       start_clu[4];
		UINT8       size[8];
	} CASE_DENTRY_T;

	typedef struct {
		UINT8       type;
		UINT8       label_len;
		UINT8       unicode_0_10[22];
		UINT8       reserved[8];
	} VOLM_DENTRY_T;

	typedef struct {
		UINT32      dir;
		INT32       entry;
		CHAIN_T     clu;
	} UENTRY_T;

	typedef struct __FS_STRUCT_T {
		UINT32      mounted;
		struct super_block *sb;
		struct semaphore v_sem;
	} FS_STRUCT_T;

	typedef struct {
		INT32       (*alloc_cluster)(struct super_block *sb, INT32 num_alloc, CHAIN_T *p_chain);
		void        (*free_cluster)(struct super_block *sb, CHAIN_T *p_chain, INT32 do_relse);
		INT32       (*count_used_clusters)(struct super_block *sb);

		INT32      (*init_dir_entry)(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 type,
									 UINT32 start_clu, UINT64 size);
		INT32      (*init_ext_entry)(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, INT32 num_entries,
									 UNI_NAME_T *p_uniname, DOS_NAME_T *p_dosname);
		INT32       (*find_dir_entry)(struct super_block *sb, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, INT32 num_entries, DOS_NAME_T *p_dosname, UINT32 type);
		void        (*delete_dir_entry)(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, INT32 offset, INT32 num_entries);
		void        (*get_uni_name_from_ext_entry)(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT16 *uniname);
		INT32       (*count_ext_entries)(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, DENTRY_T *p_entry);
		INT32       (*calc_num_entries)(UNI_NAME_T *p_uniname);

		UINT32      (*get_entry_type)(DENTRY_T *p_entry);
		void        (*set_entry_type)(DENTRY_T *p_entry, UINT32 type);
		UINT32      (*get_entry_attr)(DENTRY_T *p_entry);
		void        (*set_entry_attr)(DENTRY_T *p_entry, UINT32 attr);
		UINT8       (*get_entry_flag)(DENTRY_T *p_entry);
		void        (*set_entry_flag)(DENTRY_T *p_entry, UINT8 flag);
		UINT32      (*get_entry_clu0)(DENTRY_T *p_entry);
		void        (*set_entry_clu0)(DENTRY_T *p_entry, UINT32 clu0);
		UINT64      (*get_entry_size)(DENTRY_T *p_entry);
		void        (*set_entry_size)(DENTRY_T *p_entry, UINT64 size);
		void        (*get_entry_time)(DENTRY_T *p_entry, TIMESTAMP_T *tp, UINT8 mode);
		void        (*set_entry_time)(DENTRY_T *p_entry, TIMESTAMP_T *tp, UINT8 mode);
	} FS_FUNC_T;

	typedef struct __FS_INFO_T {
		UINT32      drv;                    
		UINT32      vol_type;               
		UINT32      vol_id;                 

		UINT32      num_sectors;            
		UINT32      num_clusters;           
		UINT32      cluster_size;           
		UINT32      cluster_size_bits;
		UINT32      sectors_per_clu;        
		UINT32      sectors_per_clu_bits;

		UINT32      PBR_sector;             
		UINT32      FAT1_start_sector;      
		UINT32      FAT2_start_sector;      
		UINT32      root_start_sector;      
		UINT32      data_start_sector;      
		UINT32      num_FAT_sectors;        

		UINT32      root_dir;               
		UINT32      dentries_in_root;       
		UINT32      dentries_per_clu;       

		UINT32      vol_flag;               
		struct buffer_head *pbr_bh;         

		UINT32      map_clu;                
		UINT32      map_sectors;            
		struct buffer_head **vol_amap;      

		UINT16      **vol_utbl;               

		UINT32      clu_srch_ptr;           
		UINT32      used_clusters;          
		UENTRY_T    hint_uentry;            

		UINT32      dev_ejected;            

		FS_FUNC_T	*fs_func;

		BUF_CACHE_T FAT_cache_array[FAT_CACHE_SIZE];
		BUF_CACHE_T FAT_cache_lru_list;
		BUF_CACHE_T FAT_cache_hash_list[FAT_CACHE_HASH_SIZE];

		BUF_CACHE_T buf_cache_array[BUF_CACHE_SIZE];
		BUF_CACHE_T buf_cache_lru_list;
		BUF_CACHE_T buf_cache_hash_list[BUF_CACHE_HASH_SIZE];
	} FS_INFO_T;

#define ES_2_ENTRIES		2
#define ES_3_ENTRIES		3
#define ES_ALL_ENTRIES	0

	typedef struct {
		UINT32	sector;		
		INT32	offset;		
		INT32	alloc_flag;	
		UINT32 num_entries;
		
		void *__buf;
	} ENTRY_SET_CACHE_T;

	INT32 ffsInit(void);
	INT32 ffsShutdown(void);

	INT32 ffsMountVol(struct super_block *sb, INT32 drv);
	INT32 ffsUmountVol(struct super_block *sb);
	INT32 ffsCheckVol(struct super_block *sb);
	INT32 ffsGetVolInfo(struct super_block *sb, VOL_INFO_T *info);
	INT32 ffsSyncVol(struct super_block *sb, INT32 do_sync);

	INT32 ffsLookupFile(struct inode *inode, UINT8 *path, FILE_ID_T *fid);
	INT32 ffsCreateFile(struct inode *inode, UINT8 *path, UINT8 mode, FILE_ID_T *fid);
	INT32 ffsReadFile(struct inode *inode, FILE_ID_T *fid, void *buffer, UINT64 count, UINT64 *rcount);
	INT32 ffsWriteFile(struct inode *inode, FILE_ID_T *fid, void *buffer, UINT64 count, UINT64 *wcount);
	INT32 ffsTruncateFile(struct inode *inode, UINT64 old_size, UINT64 new_size);
	INT32 ffsMoveFile(struct inode *old_parent_inode, FILE_ID_T *fid, struct inode *new_parent_inode, struct dentry *new_dentry);
	INT32 ffsRemoveFile(struct inode *inode, FILE_ID_T *fid);
	INT32 ffsSetAttr(struct inode *inode, UINT32 attr);
	INT32 ffsGetStat(struct inode *inode, DIR_ENTRY_T *info);
	INT32 ffsSetStat(struct inode *inode, DIR_ENTRY_T *info);
	INT32 ffsMapCluster(struct inode *inode, INT32 clu_offset, UINT32 *clu);

	INT32 ffsCreateDir(struct inode *inode, UINT8 *path, FILE_ID_T *fid);
	INT32 ffsReadDir(struct inode *inode, DIR_ENTRY_T *dir_ent);
	INT32 ffsRemoveDir(struct inode *inode, FILE_ID_T *fid);
	INT32 ffsRemoveEntry(struct inode *inode, FILE_ID_T *fid);

	INT32  fs_init(void);
	INT32  fs_shutdown(void);
	void   fs_set_vol_flags(struct super_block *sb, UINT32 new_flag);
	void   fs_sync(struct super_block *sb, INT32 do_sync);
	void   fs_error(struct super_block *sb);

	INT32   clear_cluster(struct super_block *sb, UINT32 clu);
	INT32  fat_alloc_cluster(struct super_block *sb, INT32 num_alloc, CHAIN_T *p_chain);
	INT32  exfat_alloc_cluster(struct super_block *sb, INT32 num_alloc, CHAIN_T *p_chain);
	void   fat_free_cluster(struct super_block *sb, CHAIN_T *p_chain, INT32 do_relse);
	void   exfat_free_cluster(struct super_block *sb, CHAIN_T *p_chain, INT32 do_relse);
	UINT32 find_last_cluster(struct super_block *sb, CHAIN_T *p_chain);
	INT32  count_num_clusters(struct super_block *sb, CHAIN_T *dir);
	INT32  fat_count_used_clusters(struct super_block *sb);
	INT32  exfat_count_used_clusters(struct super_block *sb);
	void   exfat_chain_cont_cluster(struct super_block *sb, UINT32 chain, INT32 len);

	INT32  load_alloc_bitmap(struct super_block *sb);
	void   free_alloc_bitmap(struct super_block *sb);
	INT32   set_alloc_bitmap(struct super_block *sb, UINT32 clu);
	INT32   clr_alloc_bitmap(struct super_block *sb, UINT32 clu);
	UINT32 test_alloc_bitmap(struct super_block *sb, UINT32 clu);
	void   sync_alloc_bitmap(struct super_block *sb);

	INT32  load_upcase_table(struct super_block *sb);
	void   free_upcase_table(struct super_block *sb);

	UINT32 fat_get_entry_type(DENTRY_T *p_entry);
	UINT32 exfat_get_entry_type(DENTRY_T *p_entry);
	void   fat_set_entry_type(DENTRY_T *p_entry, UINT32 type);
	void   exfat_set_entry_type(DENTRY_T *p_entry, UINT32 type);
	UINT32 fat_get_entry_attr(DENTRY_T *p_entry);
	UINT32 exfat_get_entry_attr(DENTRY_T *p_entry);
	void   fat_set_entry_attr(DENTRY_T *p_entry, UINT32 attr);
	void   exfat_set_entry_attr(DENTRY_T *p_entry, UINT32 attr);
	UINT8  fat_get_entry_flag(DENTRY_T *p_entry);
	UINT8  exfat_get_entry_flag(DENTRY_T *p_entry);
	void   fat_set_entry_flag(DENTRY_T *p_entry, UINT8 flag);
	void   exfat_set_entry_flag(DENTRY_T *p_entry, UINT8 flag);
	UINT32 fat_get_entry_clu0(DENTRY_T *p_entry);
	UINT32 exfat_get_entry_clu0(DENTRY_T *p_entry);
	void   fat_set_entry_clu0(DENTRY_T *p_entry, UINT32 start_clu);
	void   exfat_set_entry_clu0(DENTRY_T *p_entry, UINT32 start_clu);
	UINT64 fat_get_entry_size(DENTRY_T *p_entry);
	UINT64 exfat_get_entry_size(DENTRY_T *p_entry);
	void   fat_set_entry_size(DENTRY_T *p_entry, UINT64 size);
	void   exfat_set_entry_size(DENTRY_T *p_entry, UINT64 size);
	void   fat_get_entry_time(DENTRY_T *p_entry, TIMESTAMP_T *tp, UINT8 mode);
	void   exfat_get_entry_time(DENTRY_T *p_entry, TIMESTAMP_T *tp, UINT8 mode);
	void   fat_set_entry_time(DENTRY_T *p_entry, TIMESTAMP_T *tp, UINT8 mode);
	void   exfat_set_entry_time(DENTRY_T *p_entry, TIMESTAMP_T *tp, UINT8 mode);
	INT32   fat_init_dir_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 type, UINT32 start_clu, UINT64 size);
	INT32   exfat_init_dir_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 type, UINT32 start_clu, UINT64 size);
	INT32   fat_init_ext_dir_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, INT32 num_entries, UNI_NAME_T *p_uniname, DOS_NAME_T *p_dosname);
	INT32   exfat_init_ext_dir_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, INT32 num_entries, UNI_NAME_T *p_uniname, DOS_NAME_T *p_dosname);
	void   init_dos_entry(DOS_DENTRY_T *ep, UINT32 type, UINT32 start_clu, UINT8 tz_utc);
	void   init_ext_entry(EXT_DENTRY_T *ep, INT32 order, UINT8 chksum, UINT16 *uniname);
	void   init_file_entry(FILE_DENTRY_T *ep, UINT32 type, UINT8 tz_utc);
	void   init_strm_entry(STRM_DENTRY_T *ep, UINT8 flags, UINT32 start_clu, UINT64 size);
	void   init_name_entry(NAME_DENTRY_T *ep, UINT16 *uniname);
	void   fat_delete_dir_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, INT32 order, INT32 num_entries);
	void   exfat_delete_dir_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, INT32 order, INT32 num_entries);

	INT32   find_location(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 *sector, INT32 *offset);
	DENTRY_T *get_entry_with_sector(struct super_block *sb, UINT32 sector, INT32 offset);
	DENTRY_T *get_entry_in_dir(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 *sector);
	ENTRY_SET_CACHE_T *get_entry_set_in_dir (struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 type, DENTRY_T **file_ep);
	void release_entry_set (ENTRY_SET_CACHE_T *es);
	INT32 write_whole_entry_set (struct super_block *sb, ENTRY_SET_CACHE_T *es);
	INT32 write_partial_entries_in_entry_set (struct super_block *sb, ENTRY_SET_CACHE_T *es, DENTRY_T *ep, UINT32 count);
	INT32  search_deleted_or_unused_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 num_entries);
	INT32  find_empty_entry(struct inode *inode, CHAIN_T *p_dir, INT32 num_entries);
	INT32  fat_find_dir_entry(struct super_block *sb, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, INT32 num_entries, DOS_NAME_T *p_dosname, UINT32 type);
	INT32  exfat_find_dir_entry(struct super_block *sb, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, INT32 num_entries, DOS_NAME_T *p_dosname, UINT32 type);
	INT32  fat_count_ext_entries(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, DENTRY_T *p_entry);
	INT32  exfat_count_ext_entries(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, DENTRY_T *p_entry);
	INT32  count_dos_name_entries(struct super_block *sb, CHAIN_T *p_dir, UINT32 type);
	void   update_dir_checksum(struct super_block *sb, CHAIN_T *p_dir, INT32 entry);
	void update_dir_checksum_with_entry_set (struct super_block *sb, ENTRY_SET_CACHE_T *es);
	BOOL   is_dir_empty(struct super_block *sb, CHAIN_T *p_dir);

	INT32  get_num_entries_and_dos_name(struct super_block *sb, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, INT32 *entries, DOS_NAME_T *p_dosname);
	void   get_uni_name_from_dos_entry(struct super_block *sb, DOS_DENTRY_T *ep, UNI_NAME_T *p_uniname, UINT8 mode);
	void   fat_get_uni_name_from_ext_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT16 *uniname);
	void   exfat_get_uni_name_from_ext_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT16 *uniname);
	INT32  extract_uni_name_from_ext_entry(EXT_DENTRY_T *ep, UINT16 *uniname, INT32 order);
	INT32  extract_uni_name_from_name_entry(NAME_DENTRY_T *ep, UINT16 *uniname, INT32 order);
	INT32  fat_generate_dos_name(struct super_block *sb, CHAIN_T *p_dir, DOS_NAME_T *p_dosname);
	void   fat_attach_count_to_dos_name(UINT8 *dosname, INT32 count);
	INT32  fat_calc_num_entries(UNI_NAME_T *p_uniname);
	INT32  exfat_calc_num_entries(UNI_NAME_T *p_uniname);
	UINT8  calc_checksum_1byte(void *data, INT32 len, UINT8 chksum);
	UINT16 calc_checksum_2byte(void *data, INT32 len, UINT16 chksum, INT32 type);
	UINT32 calc_checksum_4byte(void *data, INT32 len, UINT32 chksum, INT32 type);

	INT32  resolve_path(struct inode *inode, UINT8 *path, CHAIN_T *p_dir, UNI_NAME_T *p_uniname);
	INT32  resolve_name(UINT8 *name, UINT8 **arg);

	INT32  fat16_mount(struct super_block *sb, PBR_SECTOR_T *p_pbr);
	INT32  fat32_mount(struct super_block *sb, PBR_SECTOR_T *p_pbr);
	INT32  exfat_mount(struct super_block *sb, PBR_SECTOR_T *p_pbr);
	INT32  create_dir(struct inode *inode, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, FILE_ID_T *fid);
	INT32  create_file(struct inode *inode, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, UINT8 mode, FILE_ID_T *fid);
	void   remove_file(struct inode *inode, CHAIN_T *p_dir, INT32 entry);
	INT32  rename_file(struct inode *inode, CHAIN_T *p_dir, INT32 old_entry, UNI_NAME_T *p_uniname, FILE_ID_T *fid);
	INT32  move_file(struct inode *inode, CHAIN_T *p_olddir, INT32 oldentry, CHAIN_T *p_newdir, UNI_NAME_T *p_uniname, FILE_ID_T *fid);

	INT32   sector_read(struct super_block *sb, UINT32 sec, struct buffer_head **bh, INT32 read);
	INT32   sector_write(struct super_block *sb, UINT32 sec, struct buffer_head *bh, INT32 sync);
	INT32   multi_sector_read(struct super_block *sb, UINT32 sec, struct buffer_head **bh, INT32 num_secs, INT32 read);
	INT32   multi_sector_write(struct super_block *sb, UINT32 sec, struct buffer_head *bh, INT32 num_secs, INT32 sync);

#ifdef __cplusplus
}
#endif 

#endif
