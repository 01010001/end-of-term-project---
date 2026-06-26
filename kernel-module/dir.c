#define pr_fmt(fmt) "vcfs: " fmt

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/namei.h>

#include "vcfs.h"

/* Iterate over the files contained in dir and commit them to @ctx.
 * This function is called by the VFS as ctx->pos changes.
 * Returns 0 on success.
 */
static int vcfs_iterate(struct file *dir, struct dir_context *ctx)
{
    struct inode *inode = file_inode(dir);
    struct vcfs_inode_info *ci = vcfs_INODE(inode);
    struct super_block *sb = inode->i_sb;
    struct buffer_head *bh = NULL, *bh2 = NULL;
    struct vcfs_file_ei_block *eblock = NULL;
    struct vcfs_dir_block *dblock = NULL;
    int ei = 0, bi = 0, fi = 0;
    int ret = 0;

    /* Check that dir is a directory */
    if (!S_ISDIR(inode->i_mode))
        return -ENOTDIR;

    /* Check that ctx->pos is not bigger than what we can handle (including
     * . and ..)
     */
    if (ctx->pos > vcfs_MAX_SUBFILES + 2)
        return 0;

    /* Commit . and .. to ctx */
    if (!dir_emit_dots(dir, ctx))
        return 0;

    /* Read the directory index block on disk */
    bh = sb_bread(sb, ci->ei_block);
    if (!bh)
        return -EIO;
    eblock = (struct vcfs_file_ei_block *) bh->b_data;

    if (ctx->pos - 2 == eblock->nr_files)
        goto release_bh;

    int remained_nr_files = eblock->nr_files - (ctx->pos - 2);

    int offset = ctx->pos - 2;
    for (ei = 0; ei < vcfs_MAX_EXTENTS; ei++) {
        if (eblock->extents[ei].ee_start == 0)
            continue;
        if (offset > eblock->extents[ei].nr_files) {
            offset -= eblock->extents[ei].nr_files;
        } else {
            break;
        }
    }

    /* Iterate over the index block and commit subfiles */
    for (; remained_nr_files && ei < vcfs_MAX_EXTENTS; ei++) {
        if (eblock->extents[ei].ee_start == 0)
            continue;

        /* Iterate over blocks in one extent */
        for (bi = 0; bi < eblock->extents[ei].ee_len && remained_nr_files;
             bi++) {
            bh2 = sb_bread(sb, eblock->extents[ei].ee_start + bi);
            if (!bh2) {
                ret = -EIO;
                goto release_bh;
            }
            dblock = (struct vcfs_dir_block *) bh2->b_data;

            if (offset > dblock->nr_files) {
                offset -= dblock->nr_files;
                brelse(bh2);
                bh2 = NULL;
                continue;
            }

            for (fi = 0; fi < vcfs_FILES_PER_BLOCK; fi++) {
                if (dblock->files[fi].inode != 0) {
                    if (offset) {
                        offset--;
                    } else {
                        remained_nr_files--;
                        if (!dir_emit(ctx, dblock->files[fi].filename,
                                      strnlen(dblock->files[fi].filename, vcfs_FILENAME_LEN),
                                      dblock->files[fi].inode, DT_UNKNOWN)) {
                            brelse(bh2);
                            bh2 = NULL;
                            goto release_bh;
                        }

                        ctx->pos++;
                    }
                }
            }
            brelse(bh2);
            bh2 = NULL;
        }
    }

release_bh:
    brelse(bh);

    return ret;
}

#include "../daemon/vcfs_ioctl.h"

/* Forward declaration for functions defined in inode.c needed for trash restore */
extern int vcfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry);

static long vcfs_dir_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct inode *dir = file_inode(filp);
    struct super_block *sb = dir->i_sb;
    struct vcfs_sb_info *sbi = vcfs_SB(sb);
    uint32_t i;

    if (!S_ISDIR(dir->i_mode))
        return -ENOTTY;

    switch (cmd) {
    case VCFS_IOC_GET_TRASH_COUNT: {
        __u32 count = 0;
        /* Scan all inodes in the filesystem */
        for (i = 1; i < sbi->nr_inodes; i++) {
            struct inode *tmp = vcfs_iget(sb, i);
            if (!IS_ERR(tmp)) {
                if (vcfs_INODE(tmp)->is_deleted == 1)
                    count++;
                iput(tmp);
            }
        }
        if (copy_to_user((__u32 __user *)arg, &count, sizeof(__u32)))
            return -EFAULT;
        return 0;
    }
    case VCFS_IOC_GET_TRASH_LIST: {
        struct vcfs_ioctl_trash_list_args args;
        struct vcfs_ioctl_trash_info *list;
        __u32 count = 0;

        if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
            return -EFAULT;

        list = kmalloc_array(args.count, sizeof(struct vcfs_ioctl_trash_info), GFP_KERNEL);
        if (!list && args.count > 0) return -ENOMEM;

        for (i = 1; i < sbi->nr_inodes && count < args.count; i++) {
            struct inode *tmp = vcfs_iget(sb, i);
            if (!IS_ERR(tmp)) {
                if (vcfs_INODE(tmp)->is_deleted == 1) {
                    list[count].inode_no = tmp->i_ino;
                    list[count].size = tmp->i_size;
                    list[count].uid = i_uid_read(tmp);
                    list[count].delete_timestamp = vcfs_INODE(tmp)->version_timestamp;
                    strncpy(list[count].filename, vcfs_INODE(tmp)->i_data, 31);
                    list[count].filename[31] = '\0';
                    count++;
                }
                iput(tmp);
            }
        }

        if (count > 0 && copy_to_user(args.items, list, count * sizeof(struct vcfs_ioctl_trash_info))) {
            kfree(list);
            return -EFAULT;
        }
        kfree(list);

        args.count = count;
        if (copy_to_user((void __user *)arg, &args, sizeof(args)))
            return -EFAULT;
            
        return 0;
    }
    case VCFS_IOC_RESTORE_TRASH: {
        __u32 target_ino;
        struct inode *target_inode;
        struct dentry *dentry;
        int err = 0;

        if (copy_from_user(&target_ino, (__u32 __user *)arg, sizeof(__u32)))
            return -EFAULT;

        target_inode = vcfs_iget(sb, target_ino);
        if (IS_ERR(target_inode))
            return PTR_ERR(target_inode);

        if (vcfs_INODE(target_inode)->is_deleted != 1) {
            iput(target_inode);
            return -EINVAL; /* Not in trash */
        }

        inode_lock(dir);
        
        /* Properly lookup the dentry in VFS cache (might return negative dentry from recent rm) */
        dentry = lookup_one_len(vcfs_INODE(target_inode)->i_data, filp->f_path.dentry, strlen(vcfs_INODE(target_inode)->i_data));
        if (!IS_ERR(dentry)) {
            /* Create link in current directory */
            extern int vcfs_link_inode(struct inode *old_inode, struct inode *dir, struct dentry *dentry);
            err = vcfs_link_inode(target_inode, dir, dentry);
            if (!err) {
                vcfs_INODE(target_inode)->is_deleted = 0;
                mark_inode_dirty(target_inode);
            }
            dput(dentry);
        } else {
            err = PTR_ERR(dentry);
        }

        inode_unlock(dir);
        iput(target_inode);
        return err;
    }
    case VCFS_IOC_CLEAN_TRASH: {
        /* Iterate and permanently delete items. For POC, we just reset their metadata block references */
        for (i = 1; i < sbi->nr_inodes; i++) {
            struct inode *tmp = vcfs_iget(sb, i);
            if (!IS_ERR(tmp)) {
                if (vcfs_INODE(tmp)->is_deleted == 1) {
                    /* Permanent deletion logic goes here */
                    vcfs_INODE(tmp)->is_deleted = 2; /* Mark as permanently purged */
                    mark_inode_dirty(tmp);
                }
                iput(tmp);
            }
        }
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

const struct file_operations vcfs_dir_ops = {
    .owner = THIS_MODULE,
    .llseek = generic_file_llseek,
    .iterate_shared = vcfs_iterate,
    .unlocked_ioctl = vcfs_dir_ioctl,
};
