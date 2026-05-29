#ifndef VCFS_IOCTL_H
#define VCFS_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define VCFS_IOCTL_MAGIC 'v'

/* IOCTL to get the number of versions for a given file */
#define VCFS_IOC_GET_VERSION_COUNT _IOR(VCFS_IOCTL_MAGIC, 1, __u32)

/* IOCTL to get version history (returns array of version IDs/timestamps) */
struct vcfs_ioctl_version_info {
    __u32 version_id;
    __u32 timestamp;
    __u32 is_compressed;
};
#define VCFS_IOC_GET_VERSIONS _IOWR(VCFS_IOCTL_MAGIC, 2, struct vcfs_ioctl_version_info)

/* IOCTL to trigger background compression of a specific version */
struct vcfs_ioctl_compress_args {
    __u32 version_id;
    /* user-space can pass a file descriptor or buffer of compressed data,
       or the kernel can be instructed to read from user-space daemon */
};
#define VCFS_IOC_COMPRESS_VERSION _IOW(VCFS_IOCTL_MAGIC, 3, struct vcfs_ioctl_compress_args)

/* IOCTL to checkout/restore a specific version */
#define VCFS_IOC_CHECKOUT_VERSION _IOW(VCFS_IOCTL_MAGIC, 4, __u32)

#endif /* VCFS_IOCTL_H */
