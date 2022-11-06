#include <ext2fs/ext2fs.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>

#include <solution.h>

#define DEFBLKSZ 1024
#define SBOFF 1024

int block_size(struct ext2_super_block* super) {
    return 1024u << super->s_log_block_size;
}

int read_sb(int img, struct ext2_super_block* p_sb) {
    if (pread(img, p_sb, sizeof(struct ext2_super_block), SBOFF) < 0) {
        return -errno;
    }
    return 0;
}

int read_gd(int img, struct ext2_super_block* p_sb, struct ext2_group_desc* p_gd, int64_t blk_sz, int inode_nr) {
    int64_t gd_idx = (inode_nr - 1)/ p_sb->s_inodes_per_group;
    off_t offset = blk_sz * (p_sb->s_first_data_block + 1) + gd_idx * sizeof(struct ext2_group_desc);
    if (pread(img, p_gd, sizeof(struct ext2_group_desc), offset) < 0) {
        return -errno;
    }
    return 0;
}

int read_inode(int img, struct ext2_super_block* p_sb, struct ext2_group_desc* p_gd, struct ext2_inode* p_inode, int64_t blk_sz, int inode_nr) {
    int64_t inode_idx = (inode_nr - 1) % p_sb->s_inodes_per_group;
    off_t offset = p_gd->bg_inode_table * blk_sz + inode_idx * p_sb->s_inode_size;
    if (pread(img, p_inode, sizeof(struct ext2_inode), offset) < 0) {
        return -errno;
    }
    return 0;
}

int read_blk(int img, int out, int64_t blk_idx, int64_t blk_sz, int64_t* left_read, int level) {
    if (!*left_read) {
        return 0;
    }
    char* buf = malloc(blk_sz);
    int64_t cur_len = *left_read > blk_sz ? blk_sz : *left_read;
    int ret;
    if (pread(img, buf, blk_sz, blk_idx * blk_sz) < 0) {
        free(buf);
        return -errno;
    }
    if (!level) {
        if (write(out, buf, cur_len) < cur_len) {
            free(buf);
            return -errno;
        }
        *left_read -= cur_len;
    } else {
        int32_t* blk_idxs = (int32_t*) buf;
        int64_t ub = blk_sz / sizeof(int32_t);
        for (int64_t i = 0; i < ub; ++i) {
            if (!blk_idxs[i]) {
                break;
            }
            if ((ret = read_blk(img, out, blk_idxs[i], blk_sz, left_read, level - 1)) || *left_read < 0) {
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
    int64_t blk_sz = 1024 << sb.s_log_block_size;
    if ((ret = read_gd(img, &sb, &gd, blk_sz, inode_nr))) {
        return ret;
    }
    if ((ret = read_inode(img, &sb, &gd, &inode, blk_sz, inode_nr))) {
        return ret;
    }
    
    int64_t left_read = inode.i_size;
    for (int i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
        if ((ret = read_blk(img, out, inode.i_block[i], blk_sz, &left_read, 0))) {
            return ret;
        }
    }
    if ((ret = read_blk(img, out, inode.i_block[EXT2_IND_BLOCK], blk_sz, &left_read, 0))) {
        return ret;
    }
    if ((ret = read_blk(img, out, inode.i_block[EXT2_DIND_BLOCK], blk_sz, &left_read, 0))) {
        return ret;
    }
    return 0;
}

