#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "vcfs.h"

/* Mount a vcfs partition */
struct dentry *vcfs_mount(struct file_system_type *fs_type,
                              int flags,
                              const char *dev_name,
                              void *data)
{
    struct dentry *dentry =
        mount_bdev(fs_type, flags, dev_name, data, vcfs_fill_super);
    if (IS_ERR(dentry))
        pr_err("'%s' mount failure\n", dev_name);
    else
        pr_info("'%s' mount success\n", dev_name);

    return dentry;
}

/* Unmount a vcfs partition */
void vcfs_kill_sb(struct super_block *sb)
{
    struct vcfs_sb_info *sbi = vcfs_SB(sb);
#if vcfs_AT_LEAST(6, 9, 0)
    if (sbi->s_journal_bdev_file)
        fput(sbi->s_journal_bdev_file);
#elif vcfs_AT_LEAST(6, 7, 0)
    if (sbi->s_journal_bdev_handle)
        bdev_release(sbi->s_journal_bdev_handle);
#endif
    kill_block_super(sb);

    pr_info("unmounted disk\n");
}

static struct file_system_type vcfs_file_system_type = {
    .owner = THIS_MODULE,
    .name = "vcfs",
    .mount = vcfs_mount,
    .kill_sb = vcfs_kill_sb,
    .fs_flags = FS_REQUIRES_DEV,
    .next = NULL,
};

static int __init vcfs_init(void)
{
    int ret = vcfs_init_inode_cache();
    if (ret) {
        pr_err("Failed to create inode cache\n");
        goto err;
    }

    ret = register_filesystem(&vcfs_file_system_type);
    if (ret) {
        pr_err("Failed to register file system\n");
        goto err_inode;
    }

    pr_info("module loaded\n");
    return 0;

err_inode:
    vcfs_destroy_inode_cache();
    /* Only after rcu_barrier() is the memory guaranteed to be freed. */
    rcu_barrier();
err:
    return ret;
}

static void __exit vcfs_exit(void)
{
    int ret = unregister_filesystem(&vcfs_file_system_type);
    if (ret)
        pr_err("Failed to unregister file system\n");

    vcfs_destroy_inode_cache();
    /* Only after rcu_barrier() is the memory guaranteed to be freed. */
    rcu_barrier();

    pr_info("module unloaded\n");
}

module_init(vcfs_init);
module_exit(vcfs_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("a simple file system");
