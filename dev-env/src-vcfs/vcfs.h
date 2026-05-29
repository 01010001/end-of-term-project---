#ifndef vcfs_H
#define vcfs_H

/* source: https://en.wikipedia.org/wiki/Hexspeak */
#define vcfs_MAGIC 0xDEADCELL

#define vcfs_SB_BLOCK_NR 0

#define vcfs_BLOCK_SIZE (1 << 12) /* 4 KiB */
#define vcfs_MAX_EXTENTS \
    ((vcfs_BLOCK_SIZE - sizeof(uint32_t)) / sizeof(struct vcfs_extent))
#define vcfs_MAX_BLOCKS_PER_EXTENT 8 /* It can be ~(uint32) 0 */
#define vcfs_MAX_SIZES_PER_EXTENT \
    (vcfs_MAX_BLOCKS_PER_EXTENT * vcfs_BLOCK_SIZE)
#define vcfs_MAX_FILESIZE                                          \
    ((uint64_t) vcfs_MAX_BLOCKS_PER_EXTENT * vcfs_BLOCK_SIZE * \
     vcfs_MAX_EXTENTS)

#define vcfs_FILENAME_LEN 255

#define vcfs_FILES_PER_BLOCK \
    (vcfs_BLOCK_SIZE / sizeof(struct vcfs_file))
#define vcfs_FILES_PER_EXT \
    (vcfs_FILES_PER_BLOCK * vcfs_MAX_BLOCKS_PER_EXTENT)

#define vcfs_MAX_SUBFILES (vcfs_FILES_PER_EXT * vcfs_MAX_EXTENTS)

/* vcfs partition layout
 * +---------------+
 * |  superblock   |  1 block
 * +---------------+
 * |  inode store  |  sb->nr_istore_blocks blocks
 * +---------------+
 * | ifree bitmap  |  sb->nr_ifree_blocks blocks
 * +---------------+
 * | bfree bitmap  |  sb->nr_bfree_blocks blocks
 * +---------------+
 * |    data       |
 * |      blocks   |  rest of the blocks
 * +---------------+
 */
#ifdef __KERNEL__
#include <linux/jbd2.h>
#endif

struct vcfs_inode {
    uint32_t i_mode;   /* File mode */
    uint32_t i_uid;    /* Owner id */
    uint32_t i_gid;    /* Group id */
    uint32_t i_size;   /* Size in bytes */
    uint32_t i_ctime;  /* Inode change time */
    uint32_t i_atime;  /* Access time */
    uint32_t i_mtime;  /* Modification time */
    uint32_t i_blocks; /* Block count */
    uint32_t i_nlink;  /* Hard links count */
    uint32_t ei_block; /* Block with list of extents for this file */

    /* VCFS Versioning metadata */
    uint32_t version_id;         /* Incremental version number */
    uint32_t version_timestamp;  /* Timestamp of this version */
    uint32_t prev_version_inode; /* Inode number of the previous version */
    uint32_t is_deleted;         /* Trash flag */

    char i_data[32];   /* store symlink content */
    char padding[40];  /* Pad to 128 bytes */
};

#define vcfs_INODES_PER_BLOCK \
    (vcfs_BLOCK_SIZE / sizeof(struct vcfs_inode))

#ifdef __KERNEL__
#include <linux/version.h>
/* compatibility macros */
#define vcfs_AT_LEAST(major, minor, rev) \
    LINUX_VERSION_CODE >= KERNEL_VERSION(major, minor, rev)
#define vcfs_LESS_EQUAL(major, minor, rev) \
    LINUX_VERSION_CODE <= KERNEL_VERSION(major, minor, rev)

/* A 'container' structure that keeps the VFS inode and additional on-disk
 * data.
 */
struct vcfs_inode_info {
    uint32_t ei_block; /* Block with list of extents for this file */
    
    /* VCFS Versioning metadata in-memory */
    uint32_t version_id;
    uint32_t version_timestamp;
    uint32_t prev_version_inode;
    uint32_t is_deleted;

    char i_data[32];
    struct inode vfs_inode;
};

struct vcfs_extent {
    uint32_t ee_block; /* first logical block extent covers */
    uint32_t ee_len;   /* number of blocks covered by extent */
    uint32_t ee_start; /* first physical block extent covers */
    union {
        uint32_t nr_files;   /* Number of files in this extent (directories) */
        uint32_t version_id; /* Version ID that allocated this extent (regular files) */
    };
};

struct vcfs_file_ei_block {
    uint32_t nr_files; /* Number of files in directory */
    struct vcfs_extent extents[vcfs_MAX_EXTENTS];
};

struct vcfs_file {
    uint32_t inode;
    uint32_t nr_blk;
    char filename[vcfs_FILENAME_LEN];
};

struct vcfs_dir_block {
    uint32_t nr_files;
    struct vcfs_file files[vcfs_FILES_PER_BLOCK];
};

/* superblock functions */
int vcfs_fill_super(struct super_block *sb, void *data, int silent);
void vcfs_kill_sb(struct super_block *sb);

/* inode functions */
int vcfs_init_inode_cache(void);
void vcfs_destroy_inode_cache(void);
struct inode *vcfs_iget(struct super_block *sb, unsigned long ino);

/* dentry function */
struct dentry *vcfs_mount(struct file_system_type *fs_type,
                              int flags,
                              const char *dev_name,
                              void *data);

/* file functions */
extern const struct file_operations vcfs_file_ops;
extern const struct file_operations vcfs_dir_ops;
extern const struct address_space_operations vcfs_aops;

/* extent functions */
extern uint32_t vcfs_ext_search(struct vcfs_file_ei_block *index,
                                    uint32_t iblock);

/* Getters for superblock and inode */
#define vcfs_SB(sb) (sb->s_fs_info)
/* Extract a vcfs_inode_info object from a VFS inode */
#define vcfs_INODE(inode) \
    (container_of(inode, struct vcfs_inode_info, vfs_inode))

#endif /* __KERNEL__ */

struct vcfs_sb_info {
    uint32_t magic; /* Magic number */

    uint32_t nr_blocks; /* Total number of blocks (incl sb & inodes) */
    uint32_t nr_inodes; /* Total number of inodes */

    uint32_t nr_istore_blocks; /* Number of inode store blocks */
    uint32_t nr_ifree_blocks;  /* Number of inode free bitmap blocks */
    uint32_t nr_bfree_blocks;  /* Number of block free bitmap blocks */

    uint32_t nr_free_inodes; /* Number of free inodes */
    uint32_t nr_free_blocks; /* Number of free blocks */

    unsigned long *ifree_bitmap; /* In-memory free inodes bitmap */
    unsigned long *bfree_bitmap; /* In-memory free blocks bitmap */
#ifdef __KERNEL__
    journal_t *journal;
    struct block_device *s_journal_bdev; /* v5.10+ external journal device */
#if vcfs_AT_LEAST(6, 9, 0)
    struct file *s_journal_bdev_file; /* v6.11 external journal device */
#elif vcfs_AT_LEAST(6, 7, 0)
    struct bdev_handle
        *s_journal_bdev_handle; /* v6.7+ external journal device */
#endif /* vcfs_AT_LEAST */
#endif /* __KERNEL__ */
};

#endif /* vcfs_H */
