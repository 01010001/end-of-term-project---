#define pr_fmt(fmt) "vcfs: " fmt

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mpage.h>

#include "bitmap.h"
#include "vcfs.h"
#include "../vcfs-daemon/vcfs_ioctl.h"

/* Associate the provided 'buffer_head' parameter with the iblock-th block of
 * the file denoted by inode. Should the specified block be unallocated and the
 * create flag is set to true, proceed to allocate a new block on the disk and
 * establish a mapping for it.
 */
static int vcfs_file_get_block(struct inode *inode,
                                   sector_t iblock,
                                   struct buffer_head *bh_result,
                                   int create)
{
    struct super_block *sb = inode->i_sb;
    struct vcfs_inode_info *ci = vcfs_INODE(inode);
    struct vcfs_file_ei_block *index;
    struct buffer_head *bh_index;
    int ret = 0, bno;
    uint32_t extent;

    /* If block number exceeds filesize, fail */
    if (iblock >= vcfs_MAX_BLOCKS_PER_EXTENT * vcfs_MAX_EXTENTS)
        return -EFBIG;

    /* Read directory block from disk */
    bh_index = sb_bread(sb, ci->ei_block);
    if (!bh_index)
        return -EIO;
    index = (struct vcfs_file_ei_block *) bh_index->b_data;

    extent = vcfs_ext_search(index, iblock);
    if (extent == -1) {
        ret = -EFBIG;
        goto brelse_index;
    }

    /* Determine whether the 'iblock' is currently allocated. If it is not and
     * the create parameter is set to true, then allocate the block. Otherwise,
     * retrieve the physical block number.
     */
    if (index->extents[extent].ee_start == 0) {
        if (!create) {
            ret = 0;
            goto brelse_index;
        }
        bno = get_free_blocks(sb, vcfs_MAX_BLOCKS_PER_EXTENT);
        if (!bno) {
            ret = -ENOSPC;
            goto brelse_index;
        }

        index->extents[extent].ee_start = bno;
        index->extents[extent].ee_len = vcfs_MAX_BLOCKS_PER_EXTENT;
        index->extents[extent].ee_block =
            extent ? index->extents[extent - 1].ee_block +
                         index->extents[extent - 1].ee_len
                   : 0;
        index->extents[extent].version_id = ci->version_id;
        mark_buffer_dirty(bh_index);
    } else if (create && index->extents[extent].version_id < ci->version_id) {
        /* Copy-on-Write: allocate new extent and copy data from old extent */
        int i;
        bno = get_free_blocks(sb, vcfs_MAX_BLOCKS_PER_EXTENT);
        if (!bno) {
            ret = -ENOSPC;
            goto brelse_index;
        }
        for (i = 0; i < index->extents[extent].ee_len; i++) {
            struct buffer_head *bh_old = sb_bread(sb, index->extents[extent].ee_start + i);
            struct buffer_head *bh_new = sb_getblk(sb, bno + i);
            if (bh_old && bh_new) {
                memcpy(bh_new->b_data, bh_old->b_data, sb->s_blocksize);
                mark_buffer_dirty(bh_new);
            }
            if (bh_old) brelse(bh_old);
            if (bh_new) brelse(bh_new);
        }
        index->extents[extent].ee_start = bno;
        index->extents[extent].version_id = ci->version_id;
        mark_buffer_dirty(bh_index);
        
        bno = bno + iblock - index->extents[extent].ee_block;
    } else {
        bno = index->extents[extent].ee_start + iblock -
              index->extents[extent].ee_block;
    }

    /* Map the physical block to the given 'buffer_head'. */
    map_bh(bh_result, sb, bno);

brelse_index:
    brelse(bh_index);

    return ret;
}

/* Called by the page cache to read a page from the physical disk and map it
 * into memory.
 */
#if vcfs_AT_LEAST(5, 19, 0)
static void vcfs_readahead(struct readahead_control *rac)
{
    mpage_readahead(rac, vcfs_file_get_block);
}
#else
static int vcfs_readpage(struct file *file, struct page *page)
{
    return mpage_readpage(page, vcfs_file_get_block);
}
#endif

/* Called by the page cache to write a dirty page to the physical disk (when
 * sync is called or when memory is needed).
 */
#if vcfs_AT_LEAST(6, 8, 0)
static int vcfs_writepage(struct page *page, struct writeback_control *wbc)
{
    struct folio *folio = page_folio(page);
    return __block_write_full_folio(page->mapping->host, folio,
                                    vcfs_file_get_block, wbc);
}
#else
static int vcfs_writepage(struct page *page, struct writeback_control *wbc)
{
    return block_write_full_page(page, vcfs_file_get_block, wbc);
}
#endif

/* Called by the VFS when a write() syscall is made on a file, before writing
 * the data into the page cache. This function checks if the write operation
 * can complete and allocates the necessary blocks through block_write_begin().
 */
#if vcfs_AT_LEAST(6, 15, 0)
static int vcfs_write_begin(const struct kiocb *iocb,
                                struct address_space *mapping,
                                loff_t pos,
                                unsigned int len,
                                struct folio **foliop,
                                void **fsdata)
{
    struct file *file = iocb->ki_filp;
    struct vcfs_sb_info *sbi = vcfs_SB(file->f_inode->i_sb);
    int err;
    uint32_t nr_allocs = 0;

    if (pos + len > vcfs_MAX_FILESIZE)
        return -ENOSPC;

    nr_allocs = max(pos + len, file->f_inode->i_size) / vcfs_BLOCK_SIZE;
    if (nr_allocs > file->f_inode->i_blocks - 1)
        nr_allocs -= file->f_inode->i_blocks - 1;
    else
        nr_allocs = 0;
    if (nr_allocs > sbi->nr_free_blocks)
        return -ENOSPC;

    err = block_write_begin(mapping, pos, len, foliop, vcfs_file_get_block);
    if (err < 0)
        pr_err("newly allocated blocks reclaim not implemented yet\n");
    return err;
}
#elif vcfs_AT_LEAST(6, 12, 0)
static int vcfs_write_begin(struct file *file,
                                struct address_space *mapping,
                                loff_t pos,
                                unsigned int len,
                                struct folio **foliop,
                                void **fsdata)
{
    struct vcfs_sb_info *sbi = vcfs_SB(file->f_inode->i_sb);
    int err;
    uint32_t nr_allocs = 0;

    if (pos + len > vcfs_MAX_FILESIZE)
        return -ENOSPC;

    nr_allocs = max(pos + len, file->f_inode->i_size) / vcfs_BLOCK_SIZE;
    if (nr_allocs > file->f_inode->i_blocks - 1)
        nr_allocs -= file->f_inode->i_blocks - 1;
    else
        nr_allocs = 0;
    if (nr_allocs > sbi->nr_free_blocks)
        return -ENOSPC;

    err = block_write_begin(mapping, pos, len, foliop, vcfs_file_get_block);
    if (err < 0)
        pr_err("newly allocated blocks reclaim not implemented yet\n");
    return err;
}
#elif vcfs_AT_LEAST(5, 19, 0)
static int vcfs_write_begin(struct file *file,
                                struct address_space *mapping,
                                loff_t pos,
                                unsigned int len,
                                struct page **pagep,
                                void **fsdata)
{
    struct vcfs_sb_info *sbi = vcfs_SB(file->f_inode->i_sb);
    int err;
    uint32_t nr_allocs = 0;

    if (pos + len > vcfs_MAX_FILESIZE)
        return -ENOSPC;

    nr_allocs = max(pos + len, file->f_inode->i_size) / vcfs_BLOCK_SIZE;
    if (nr_allocs > file->f_inode->i_blocks - 1)
        nr_allocs -= file->f_inode->i_blocks - 1;
    else
        nr_allocs = 0;
    if (nr_allocs > sbi->nr_free_blocks)
        return -ENOSPC;

    err = block_write_begin(mapping, pos, len, pagep, vcfs_file_get_block);
    if (err < 0)
        pr_err("newly allocated blocks reclaim not implemented yet\n");
    return err;
}
#else
static int vcfs_write_begin(struct file *file,
                                struct address_space *mapping,
                                loff_t pos,
                                unsigned int len,
                                unsigned int flags,
                                struct page **pagep,
                                void **fsdata)
{
    struct vcfs_sb_info *sbi = vcfs_SB(file->f_inode->i_sb);
    int err;
    uint32_t nr_allocs = 0;

    if (pos + len > vcfs_MAX_FILESIZE)
        return -ENOSPC;

    nr_allocs = max(pos + len, file->f_inode->i_size) / vcfs_BLOCK_SIZE;
    if (nr_allocs > file->f_inode->i_blocks - 1)
        nr_allocs -= file->f_inode->i_blocks - 1;
    else
        nr_allocs = 0;
    if (nr_allocs > sbi->nr_free_blocks)
        return -ENOSPC;

    err = block_write_begin(mapping, pos, len, flags, pagep,
                            vcfs_file_get_block);
    if (err < 0)
        pr_err("newly allocated blocks reclaim not implemented yet\n");
    return err;
}
#endif

/* Called by the VFS after writing data from a write() syscall to the page
 * cache. This function updates inode metadata and truncates the file if
 * necessary.
 */
#if vcfs_AT_LEAST(6, 15, 0)
static int vcfs_write_end(const struct kiocb *iocb,
                              struct address_space *mapping,
                              loff_t pos,
                              unsigned int len,
                              unsigned int copied,
                              struct folio *foliop,
                              void *fsdata)
{
    struct inode *inode = iocb->ki_filp->f_inode;
#elif vcfs_AT_LEAST(6, 12, 0)
static int vcfs_write_end(struct file *file,
                              struct address_space *mapping,
                              loff_t pos,
                              unsigned int len,
                              unsigned int copied,
                              struct folio *foliop,
                              void *fsdata)
{
    struct inode *inode = file->f_inode;
#else
static int vcfs_write_end(struct file *file,
                              struct address_space *mapping,
                              loff_t pos,
                              unsigned int len,
                              unsigned int copied,
                              struct page *page,
                              void *fsdata)
{
    struct inode *inode = file->f_inode;
#endif
    struct vcfs_inode_info *ci = vcfs_INODE(inode);
    struct super_block *sb = inode->i_sb;
#if vcfs_AT_LEAST(6, 6, 0)
    struct timespec64 cur_time;
#endif
    uint32_t nr_blocks_old;

    /* Complete the write() */
#if vcfs_AT_LEAST(6, 15, 0)
    int ret =
        generic_write_end(iocb, mapping, pos, len, copied, foliop, fsdata);
#elif vcfs_AT_LEAST(6, 12, 0)
    int ret =
        generic_write_end(file, mapping, pos, len, copied, foliop, fsdata);
#else
    int ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
#endif
    if (ret < len) {
        pr_err("wrote less than requested.");
        return ret;
    }

    nr_blocks_old = inode->i_blocks;

    /* Update inode metadata */
    inode->i_blocks = DIV_ROUND_UP(inode->i_size, vcfs_BLOCK_SIZE) + 1;

#if vcfs_AT_LEAST(6, 7, 0)
    cur_time = current_time(inode);
    inode_set_mtime_to_ts(inode, cur_time);
    inode_set_ctime_to_ts(inode, cur_time);
#elif vcfs_AT_LEAST(6, 6, 0)
    cur_time = current_time(inode);
    inode->i_mtime = cur_time;
    inode_set_ctime_to_ts(inode, cur_time);
#else
    inode->i_mtime = inode->i_ctime = current_time(inode);
#endif

    mark_inode_dirty(inode);

    /* Check if file is smaller than before, free unused blocks */
    if (nr_blocks_old > inode->i_blocks) {
        int i;
        struct buffer_head *bh_index;
        struct vcfs_file_ei_block *index;
        uint32_t first_ext;

        /* Free unused blocks from page cache */
        truncate_pagecache(inode, inode->i_size);

        /* Read ei_block to remove unused blocks */
        bh_index = sb_bread(sb, ci->ei_block);
        if (!bh_index) {
#if vcfs_AT_LEAST(6, 15, 0)
            pr_err("Failed to truncate '%s'. Lost %llu blocks\n",
                   iocb->ki_filp->f_path.dentry->d_name.name,
                   nr_blocks_old - inode->i_blocks);
#else
            pr_err("Failed to truncate '%s'. Lost %llu blocks\n",
                   file->f_path.dentry->d_name.name,
                   nr_blocks_old - inode->i_blocks);
#endif
            goto end;
        }
        index = (struct vcfs_file_ei_block *) bh_index->b_data;

        first_ext = vcfs_ext_search(index, inode->i_blocks - 1);

        /* Reserve unused block in last extent */
        if (inode->i_blocks - 1 != index->extents[first_ext].ee_block)
            first_ext++;

        for (i = first_ext; i < vcfs_MAX_EXTENTS; i++) {
            if (!index->extents[i].ee_start)
                break;
            put_blocks(vcfs_SB(sb), index->extents[i].ee_start,
                       index->extents[i].ee_len);
            memset(&index->extents[i], 0, sizeof(struct vcfs_extent));
        }
        mark_buffer_dirty(bh_index);
        brelse(bh_index);
    }
end:
    return ret;
}

static int vcfs_create_version(struct inode *inode)
{
    struct super_block *sb = inode->i_sb;
    struct vcfs_sb_info *sbi = vcfs_SB(sb);
    struct vcfs_inode_info *ci = vcfs_INODE(inode);
    struct inode *hist_inode;
    struct vcfs_inode_info *hist_ci;
    uint32_t hist_ino, new_ei_bno;
    struct buffer_head *bh_old_ei, *bh_new_ei;

    /* Only version regular files that have some data or metadata */
    if (!S_ISREG(inode->i_mode))
        return 0;

    hist_ino = get_free_inode(sbi);
    if (!hist_ino)
        return -ENOSPC;

    hist_inode = vcfs_iget(sb, hist_ino);
    if (IS_ERR(hist_inode)) {
        put_inode(sbi, hist_ino);
        return PTR_ERR(hist_inode);
    }
    hist_ci = vcfs_INODE(hist_inode);

    /* Allocate new ei_block for current inode */
    new_ei_bno = get_free_blocks(sb, 1);
    if (!new_ei_bno) {
        iput(hist_inode);
        put_inode(sbi, hist_ino);
        return -ENOSPC;
    }

    /* Copy metadata to hist_inode */
    hist_inode->i_mode = inode->i_mode;
    hist_inode->i_size = inode->i_size;
    hist_inode->i_blocks = inode->i_blocks;
    set_nlink(hist_inode, 1);
    i_uid_write(hist_inode, i_uid_read(inode));
    i_gid_write(hist_inode, i_gid_read(inode));

    hist_ci->ei_block = ci->ei_block;
    hist_ci->version_id = ci->version_id;
    hist_ci->version_timestamp = ci->version_timestamp;
    hist_ci->prev_version_inode = ci->prev_version_inode;
    hist_ci->is_deleted = 0;

#if vcfs_AT_LEAST(6, 7, 0)
    struct timespec64 cur_time = current_time(inode);
    inode_set_mtime_to_ts(hist_inode, cur_time);
    inode_set_atime_to_ts(hist_inode, cur_time);
    inode_set_ctime_to_ts(hist_inode, cur_time);
#elif vcfs_AT_LEAST(6, 6, 0)
    struct timespec64 cur_time = current_time(inode);
    hist_inode->i_mtime = hist_inode->i_atime = cur_time;
    inode_set_ctime_to_ts(hist_inode, cur_time);
#else
    hist_inode->i_mtime = hist_inode->i_atime = hist_inode->i_ctime = current_time(inode);
#endif

    /* Now copy old ei_block to new ei_block */
    bh_old_ei = sb_bread(sb, ci->ei_block);
    bh_new_ei = sb_getblk(sb, new_ei_bno);
    if (!bh_old_ei || !bh_new_ei) {
        if (bh_old_ei) brelse(bh_old_ei);
        if (bh_new_ei) brelse(bh_new_ei);
        iput(hist_inode);
        put_inode(sbi, hist_ino);
        put_blocks(sbi, new_ei_bno, 1);
        return -EIO;
    }
    memcpy(bh_new_ei->b_data, bh_old_ei->b_data, vcfs_BLOCK_SIZE);
    mark_buffer_dirty(bh_new_ei);
    brelse(bh_old_ei);
    brelse(bh_new_ei);

    /* Update current inode */
    ci->ei_block = new_ei_bno;
    ci->prev_version_inode = hist_ino;
    ci->version_id++;
#if vcfs_AT_LEAST(6, 6, 0)
    ci->version_timestamp = cur_time.tv_sec;
#else
    ci->version_timestamp = current_time(inode).tv_sec;
#endif

    mark_inode_dirty(hist_inode);
    mark_inode_dirty(inode);
    iput(hist_inode);

    return 0;
}

/*
 * Called when a file is opened in the vcfs.
 * It checks the flags associated with the file opening mode (O_WRONLY, O_RDWR,
 * O_TRUNC) and performs truncation if the file is being opened for write or
 * read/write and the O_TRUNC flag is set.
 *
 * Truncation is achieved by reading the file's index block from disk, iterating
 * over the data block pointers, releasing the associated data blocks, and
 * updating the inode metadata (size and block count).
 */
static int vcfs_open(struct inode *inode, struct file *filp)
{
    bool wronly = (filp->f_flags & O_WRONLY);
    bool rdwr = (filp->f_flags & O_RDWR);
    bool trunc = (filp->f_flags & O_TRUNC);

    if (wronly || rdwr) {
        inode_lock(inode);
        vcfs_create_version(inode);
        inode_unlock(inode);
    }

    if ((wronly || rdwr) && trunc && inode->i_size) {
        struct buffer_head *bh_index;
        struct vcfs_file_ei_block *ei_block;
        sector_t iblock;

        /* Fetch the file's extent block from disk */
        bh_index = sb_bread(inode->i_sb, vcfs_INODE(inode)->ei_block);
        if (!bh_index)
            return -EIO;

        ei_block = (struct vcfs_file_ei_block *) bh_index->b_data;

        for (iblock = 0; iblock <= vcfs_MAX_EXTENTS &&
                         ei_block->extents[iblock].ee_start;
             iblock++) {
            /* DO NOT free blocks here because the historical inode still references them! */
            memset(&ei_block->extents[iblock], 0,
                   sizeof(struct vcfs_extent));
        }
        /* Update inode metadata */
        inode->i_size = 0;
        inode->i_blocks = 1;

        mark_buffer_dirty(bh_index);
        brelse(bh_index);
        mark_inode_dirty(inode);
    }
    return 0;
}

static ssize_t vcfs_read(struct file *file,
                             char __user *buf,
                             size_t len,
                             loff_t *ppos)
{
    struct inode *inode = file_inode(file);
    struct super_block *sb = inode->i_sb;
    ssize_t bytes_read = 0;
    loff_t pos = *ppos;

    if (pos > inode->i_size)
        return 0;

    /* find extent block */
    struct buffer_head *bh = sb_bread(sb, vcfs_INODE(inode)->ei_block);
    struct vcfs_file_ei_block *ei_block =
        (struct vcfs_file_ei_block *) bh->b_data;

    if (pos + len > inode->i_size)
        len = inode->i_size - pos;

    /* count block position */
    sector_t block_index = pos / vcfs_BLOCK_SIZE;
    sector_t ei_index = block_index / vcfs_MAX_BLOCKS_PER_EXTENT;
    sector_t block_offset = ei_block->extents[ei_index].ee_start +
                            block_index % vcfs_MAX_BLOCKS_PER_EXTENT;

    while (len > 0) {
        struct buffer_head *bh_data = sb_bread(sb, block_offset);
        if (!bh_data) {
            pr_err("Failed to read data block %llu\n", block_offset);
            bytes_read = -EIO;
            break;
        }

        size_t offset = pos % vcfs_BLOCK_SIZE;
        size_t bytes_to_read =
            min_t(size_t, len, vcfs_BLOCK_SIZE - pos % vcfs_BLOCK_SIZE);
        if (copy_to_user(buf + bytes_read, bh_data->b_data + offset,
                         bytes_to_read)) {
            brelse(bh_data);
            bytes_read = -EFAULT;
            break;
        }
        brelse(bh_data);

        /* successfully read data */
        bytes_read += bytes_to_read;
        len -= bytes_to_read;
        pos += bytes_to_read;

        /* count extent block */
        block_index++;
        ei_index = block_index / vcfs_MAX_BLOCKS_PER_EXTENT;
        block_offset = ei_block->extents[ei_index].ee_start +
                       block_index % vcfs_MAX_BLOCKS_PER_EXTENT;
    }

    brelse(bh);
    *ppos = pos;

    return bytes_read;
}

static ssize_t vcfs_write(struct file *file,
                              const char __user *buf,
                              size_t len,
                              loff_t *ppos)
{
    struct inode *inode = file_inode(file);
    struct super_block *sb = inode->i_sb;
    ssize_t bytes_write = 0;
    loff_t pos = *ppos;

    if (pos > inode->i_size)
        return 0;
    len = min_t(size_t, len, vcfs_MAX_FILESIZE - pos);

    /* find extent block */
    struct buffer_head *bh = sb_bread(sb, vcfs_INODE(inode)->ei_block);
    if (!bh)
        return -EIO;
    struct vcfs_file_ei_block *ei_block =
        (struct vcfs_file_ei_block *) bh->b_data;

    /* count block position */
    sector_t block_index = pos / vcfs_BLOCK_SIZE;
    sector_t ei_index = block_index / vcfs_MAX_BLOCKS_PER_EXTENT;

    /* write data */
    while (len > 0) {
        /* check if block is allocated */
        if (ei_block->extents[ei_index].ee_start == 0) {
            int bno = get_free_blocks(sb, vcfs_MAX_BLOCKS_PER_EXTENT);
            if (!bno) {
                bytes_write = -ENOSPC;
                break;
            }
            ei_block->extents[ei_index].ee_start = bno;
            ei_block->extents[ei_index].ee_len = vcfs_MAX_BLOCKS_PER_EXTENT;
            ei_block->extents[ei_index].ee_block =
                ei_index ? ei_block->extents[ei_index - 1].ee_block +
                               ei_block->extents[ei_index - 1].ee_len
                         : 0;
            ei_block->extents[ei_index].version_id = vcfs_INODE(inode)->version_id;
        } else if (ei_block->extents[ei_index].version_id < vcfs_INODE(inode)->version_id) {
            /* Copy-on-Write: allocate new extent and copy old data */
            int bno = get_free_blocks(sb, vcfs_MAX_BLOCKS_PER_EXTENT);
            int i;
            if (!bno) {
                bytes_write = -ENOSPC;
                break;
            }
            for (i = 0; i < ei_block->extents[ei_index].ee_len; i++) {
                struct buffer_head *bh_old = sb_bread(sb, ei_block->extents[ei_index].ee_start + i);
                struct buffer_head *bh_new = sb_getblk(sb, bno + i);
                if (bh_old && bh_new) {
                    memcpy(bh_new->b_data, bh_old->b_data, sb->s_blocksize);
                    mark_buffer_dirty(bh_new);
                }
                if (bh_old) brelse(bh_old);
                if (bh_new) brelse(bh_new);
            }
            ei_block->extents[ei_index].ee_start = bno;
            ei_block->extents[ei_index].version_id = vcfs_INODE(inode)->version_id;
            mark_buffer_dirty(bh);
        }

        struct buffer_head *bh_data =
            sb_bread(sb, ei_block->extents[ei_index].ee_start +
                             block_index % vcfs_MAX_BLOCKS_PER_EXTENT);
        if (!bh_data) {
            pr_err("Failed to read data block %llu\n",
                   ei_block->extents[ei_index].ee_start +
                       block_index % vcfs_MAX_BLOCKS_PER_EXTENT);
            bytes_write = -EIO;
            break;
        }
        /* copy data from buffer */
        size_t bytes_to_write =
            min_t(size_t, len, vcfs_BLOCK_SIZE - pos % vcfs_BLOCK_SIZE);

        if (copy_from_user(bh_data->b_data + pos % vcfs_BLOCK_SIZE,
                           buf + bytes_write, bytes_to_write)) {
            brelse(bh_data);
            bytes_write = -EFAULT;
            break;
        }

        mark_buffer_dirty(bh_data);
        sync_dirty_buffer(bh_data);
        brelse(bh_data);

        /* successfully write data */
        len = len - bytes_to_write;
        bytes_write += bytes_to_write;
        pos += bytes_to_write;

        /* count extent block */
        block_index = pos / vcfs_BLOCK_SIZE;
        ei_index = block_index / vcfs_MAX_BLOCKS_PER_EXTENT;
    }
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    inode->i_size = max(pos, inode->i_size);
    inode->i_blocks = DIV_ROUND_UP(inode->i_size, vcfs_BLOCK_SIZE) + 1;
#if vcfs_AT_LEAST(6, 7, 0)
    struct timespec64 cur_time = current_time(inode);
    inode_set_mtime_to_ts(inode, cur_time);
    inode_set_ctime_to_ts(inode, cur_time);
#elif vcfs_AT_LEAST(6, 6, 0)
    struct timespec64 cur_time = current_time(inode);
    inode->i_mtime = cur_time;
    inode_set_ctime_to_ts(inode, cur_time);
#else
    inode->i_mtime = inode->i_ctime = current_time(inode);
#endif
    mark_inode_dirty(inode);
    *ppos = pos;

    return bytes_write;
}

const struct address_space_operations vcfs_aops = {
#if vcfs_AT_LEAST(5, 19, 0)
    .readahead = vcfs_readahead,
#else
    .readpage = vcfs_readpage,
#endif
#if !vcfs_AT_LEAST(6, 15, 0)
    .writepage = vcfs_writepage,
#endif
    .write_begin = vcfs_write_begin,
    .write_end = vcfs_write_end,
};

static long vcfs_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file_inode(filp);
    struct vcfs_inode_info *ci = vcfs_INODE(inode);

    switch (cmd) {
    case VCFS_IOC_GET_VERSION_COUNT: {
        __u32 count = ci->version_id;
        if (copy_to_user((__u32 __user *)arg, &count, sizeof(__u32)))
            return -EFAULT;
        return 0;
    }
    case VCFS_IOC_GET_VERSIONS: {
        __u32 count = ci->version_id;
        struct vcfs_ioctl_version_info *versions;
        struct super_block *sb = inode->i_sb;
        uint32_t curr_ino = ci->prev_version_inode;
        __u32 i = 0;

        if (count == 0) return 0;

        versions = kmalloc_array(count, sizeof(struct vcfs_ioctl_version_info), GFP_KERNEL);
        if (!versions) return -ENOMEM;

        while (curr_ino != 0 && i < count) {
            struct inode *hist_inode = vcfs_iget(sb, curr_ino);
            if (IS_ERR(hist_inode)) break;

            versions[i].version_id = vcfs_INODE(hist_inode)->version_id;
            versions[i].timestamp = vcfs_INODE(hist_inode)->version_timestamp;
            versions[i].is_compressed = 0; /* stub for daemon */

            curr_ino = vcfs_INODE(hist_inode)->prev_version_inode;
            iput(hist_inode);
            i++;
        }

        if (copy_to_user((struct vcfs_ioctl_version_info __user *)arg, versions, i * sizeof(struct vcfs_ioctl_version_info))) {
            kfree(versions);
            return -EFAULT;
        }
        kfree(versions);
        return 0;
    }
    case VCFS_IOC_CHECKOUT_VERSION: {
        __u32 target_version;
        struct super_block *sb = inode->i_sb;
        uint32_t curr_ino = ci->prev_version_inode;
        struct inode *hist_inode = NULL;

        if (copy_from_user(&target_version, (__u32 __user *)arg, sizeof(__u32)))
            return -EFAULT;

        /* Find historical inode */
        while (curr_ino != 0) {
            hist_inode = vcfs_iget(sb, curr_ino);
            if (IS_ERR(hist_inode)) {
                hist_inode = NULL;
                break;
            }
            if (vcfs_INODE(hist_inode)->version_id == target_version)
                break; /* Found it */
            
            curr_ino = vcfs_INODE(hist_inode)->prev_version_inode;
            iput(hist_inode);
            hist_inode = NULL;
        }

        if (!hist_inode) return -ENOENT;

        /* Swap metadata to restore file */
        inode_lock(inode);
        vcfs_create_version(inode); /* Backup current state before checkout */

        inode->i_size = hist_inode->i_size;
        inode->i_blocks = hist_inode->i_blocks;
        ci->ei_block = vcfs_INODE(hist_inode)->ei_block;
        
        mark_inode_dirty(inode);
        inode_unlock(inode);

        iput(hist_inode);
        return 0;
    }
    case VCFS_IOC_COMPRESS_VERSION: {
        struct vcfs_ioctl_compress_args args;
        if (copy_from_user(&args, (struct vcfs_ioctl_compress_args __user *)arg, sizeof(args)))
            return -EFAULT;
        
        pr_info("vcfs: Daemon requested compression for version %u of inode %lu\n", args.version_id, inode->i_ino);
        /* In a full implementation, we'd find the historical inode and mark it compressed */
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

const struct file_operations vcfs_file_ops = {
    .owner = THIS_MODULE,
    .open = vcfs_open,
    .read = vcfs_read,
    .write = vcfs_write,
    .llseek = generic_file_llseek,
    .fsync = generic_file_fsync,
    .unlocked_ioctl = vcfs_unlocked_ioctl,
};
