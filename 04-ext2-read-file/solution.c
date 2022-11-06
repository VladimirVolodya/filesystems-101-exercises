#include <errno.h>
#include <ext2fs/ext2fs.h>
#include <solution.h>
#include <sys/sendfile.h>
#include <unistd.h>

#define DEFBLKSZ 1024
#define SBOFF 1024

int read_sb(int img, struct ext2_super_block* p_sb) {
  if (pread(img, p_sb, sizeof(struct ext2_super_block), SBOFF) == -1) {
    return -errno;
  }
  return 0;
}

int read_gd(int img, struct ext2_super_block* p_sb,
            struct ext2_group_desc* p_gd, int64_t blk_sz, int inode_nr) {
  int64_t gd_idx = (inode_nr - 1) / p_sb->s_inodes_per_group;
  off_t gd_offset = blk_sz * (p_sb->s_first_data_block + 1) +
                    gd_idx * sizeof(struct ext2_group_desc);
  if (pread(img, p_gd, sizeof(struct ext2_group_desc), gd_offset) == -1) {
    return -errno;
  }
  return 0;
}

int read_inode(int img, struct ext2_super_block* p_sb,
               struct ext2_group_desc* p_gd, struct ext2_inode* p_inode,
               int64_t blk_sz, int inode_nr) {
  int64_t inode_idx = (inode_nr - 1) % p_sb->s_inodes_per_group;
  off_t inode_offset =
      blk_sz * p_gd->bg_inode_table + p_sb->s_inode_size * inode_idx;
  if (pread(img, p_inode, sizeof(struct ext2_inode), inode_offset) == -1) {
    return -errno;
  }
  return 0;
}

int read_blk(int img, int out, int32_t blk_idx, int32_t blk_sz,
             int32_t* p_read_left, int level) {
  int ret;
  if (level) {
    int64_t ub = blk_sz / sizeof(int32_t);
    int32_t* blk_idxs = malloc(blk_sz);
    if (pread(img, blk_idxs, blk_sz, blk_sz * blk_idx) == -1) {
      free(blk_idxs);
      return -errno;
    }
    for (int64_t i = 0; i < ub; i++) {
      if (!blk_idxs[i]) {
        break;
      }
      if ((ret = read_blk(img, out, blk_idxs[i], blk_sz, p_read_left,
                          level - 1))) {
        return ret;
      }
    }
    free(blk_idxs);
  } else {
    int cur_len = *p_read_left > blk_sz ? blk_sz : *p_read_left;
    p_read_left -= cur_len;
    off_t offset = blk_sz * blk_idx;

    if (sendfile(out, img, &offset, cur_len) == -1) {
      return -errno;
    }
  }
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
  int32_t blk_sz = DEFBLKSZ << sb.s_log_block_size;

  if ((ret = read_gd(img, &sb, &gd, blk_sz, inode_nr))) {
    return ret;
  }

  if ((ret = read_inode(img, &sb, &gd, &inode, blk_sz, inode_nr))) {
    return ret;
  }

  int32_t read_left = inode.i_size;
  for (int i = 0; i < EXT2_NDIR_BLOCKS; i++) {
    if ((ret = read_blk(img, out, inode.i_block[i], blk_sz, &read_left, 0))) {
      return ret;
    }
  }
  if ((ret = read_blk(img, out, inode.i_block[EXT2_IND_BLOCK], blk_sz,
                      &read_left, 1))) {
    return ret;
  }
  if ((ret = read_blk(img, out, inode.i_block[EXT2_DIND_BLOCK], blk_sz,
                      &read_left, 2))) {
    return ret;
  }

  return 0;
}