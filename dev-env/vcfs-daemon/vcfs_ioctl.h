#ifndef VCFS_IOCTL_H
#define VCFS_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#ifndef __user
#define __user
#endif

#define VCFS_IOCTL_MAGIC 'v'

/* IOCTL to get the number of versions for a given file */
#define VCFS_IOC_GET_VERSION_COUNT _IOR(VCFS_IOCTL_MAGIC, 1, __u32)

/* IOCTL to get version history (returns array of version IDs/timestamps) */
struct vcfs_ioctl_version_info {
    __u32 version_id;
    __u32 timestamp;
    __u32 size;            /* Added for size information */
    __u32 is_compressed;
};
#define VCFS_IOC_GET_VERSIONS _IOWR(VCFS_IOCTL_MAGIC, 2, struct vcfs_ioctl_version_info)

/* IOCTL to trigger background compression of a specific version */
struct vcfs_ioctl_compress_args {
    __u32 version_id;
};
#define VCFS_IOC_COMPRESS_VERSION _IOW(VCFS_IOCTL_MAGIC, 3, struct vcfs_ioctl_compress_args)

/* IOCTL to checkout/restore a specific version */
#define VCFS_IOC_CHECKOUT_VERSION _IOW(VCFS_IOCTL_MAGIC, 4, __u32)

/* IOCTL structures for Trash Management */
struct vcfs_ioctl_trash_info {
    __u32 inode_no;
    __u32 delete_timestamp;
    __u32 size;
    char filename[32];
};

struct vcfs_ioctl_trash_list_args {
    __u32 count;                                /* In/Out: Number of items */
    struct vcfs_ioctl_trash_info __user *items; /* User buffer */
};

#define VCFS_IOC_GET_TRASH_COUNT _IOR(VCFS_IOCTL_MAGIC, 5, __u32)
#define VCFS_IOC_GET_TRASH_LIST  _IOWR(VCFS_IOCTL_MAGIC, 6, struct vcfs_ioctl_trash_list_args)
#define VCFS_IOC_RESTORE_TRASH   _IOW(VCFS_IOCTL_MAGIC, 7, __u32) /* Takes inode number */
#define VCFS_IOC_CLEAN_TRASH     _IO(VCFS_IOCTL_MAGIC, 8)

struct vcfs_ioctl_read_args {
    __u32 version_id;
    char __user *buf;
    __u32 count; /* size to read */
};

#define VCFS_IOC_READ_VERSION _IOWR(VCFS_IOCTL_MAGIC, 9, struct vcfs_ioctl_read_args)

#endif /* VCFS_IOCTL_H */
