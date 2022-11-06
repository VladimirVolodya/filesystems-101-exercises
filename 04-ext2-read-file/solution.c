#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <ext2fs/ext2fs.h>

#include "solution.h"

#define INDBLK 13
#define DINDBLK 14
#define DEFBLKSZ 1024
#define SBOFF 1024

int read_sb(int img, struct ext2_super_block* p_sb) {
    if (pread(img, p_sb, sizeof(struct ext2_super_block), SBOFF) == -1) {
        return -errno;
    }
    return 0;
}

int read_gd(int img, struct ext2_super_block* p_sb, struct ext2_group_desc* p_gd, int inode_nr) {
    int64_t blk_sz = DEFBLKSZ << p_sb->s_log_block_size;
    int32_t gd_idx = (inode_nr - 1) / p_sb->s_inodes_per_group;
    off_t gd_off = blk_sz * (p_sb->s_first_data_block + 1) + gd_idx * sizeof(struct ext2_group_desc);
    if (pread(img, p_gd, sizeof(struct ext2_group_desc), gd_off) == -1) {
        return -errno;
    }
    return 0;
}

int read_inode(int img, struct ext2_super_block* p_sb, struct ext2_group_desc* p_gd,
                struct ext2_inode* p_inode, int inode_nr) {
    int64_t blk_sz = DEFBLKSZ << p_sb->s_log_block_size;
    int32_t inode_idx = (inode_nr - 1) % p_sb->s_inodes_per_group;
    off_t inode_off = blk_sz * p_gd->bg_inode_table + inode_idx * p_sb->s_inode_size;
    if (pread(img, p_inode, sizeof(struct ext2_inode), inode_off) == -1) {
        return -errno;
    }
    return 0;
}

int read_blk(int img, int out, off_t blk_idx, int64_t blk_sz, int64_t* out_off,
             int64_t* left_read, int level) {
    if (!*left_read) {
        return 0;
    }
    char* buf = malloc(blk_sz * sizeof(char));
    int64_t cur_len = *left_read > blk_sz ? blk_sz : *left_read;
    if (pread(img, buf, cur_len, blk_idx * blk_sz) == -1) {
        free(buf);
        return -errno;
    }
    if (!level) {
        if (pwrite(out, buf, cur_len, *out_off) == -1) {
            free(buf);
            return -errno;
        }
        *left_read -= cur_len;
        *out_off += cur_len;
    } else {
        int32_t* blk_idxs = (int32_t*) buf;
        int64_t ub = blk_sz / sizeof(int32_t);
        int ret;
        for (uint32_t i = 0; i < ub; ++i) {
            if (!blk_idxs[i]) {
                break;
            }
            if ((ret = read_blk(img, out, blk_idxs[i], blk_sz, out_off, left_read, level - 1))) {
                free(buf);
                return ret;
            }
        }
    }
    free(buf);
    return 0;
}

int dump_file(int img, int inode_nr, int out) {
    struct ext2_super_block sb;
    struct ext2_group_desc gd;
    struct ext2_inode inode;
    int ret;

    if ((ret = read_sb(img, &sb))) {
        return ret;
    }
    if ((ret = read_gd(img, &sb, &gd, inode_nr))) {
        return ret;
    }
    if ((ret = read_inode(img, &sb, &gd, &inode, inode_nr))) {
        return ret;
    }

    int64_t left_read = inode.i_size;
    int64_t out_off = 0;
    int64_t blk_sz = DEFBLKSZ << sb.s_log_block_size;
    for (int i = 0; i < INDBLK; ++i) {
        if ((ret = read_blk(img, out, inode.i_block[i], blk_sz, &out_off, &left_read, 0))) {
            return ret;
        }
    }
    if ((ret = read_blk(img, out, inode.i_block[INDBLK], blk_sz, &out_off, &left_read, 1))) {
        return ret;
    }
    if ((ret = read_blk(img, out, inode.i_block[DINDBLK], blk_sz, &out_off, &left_read, 2))) {
        return ret;
    }

    return 0;
}

