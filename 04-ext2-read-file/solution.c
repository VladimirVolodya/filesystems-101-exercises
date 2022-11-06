#include <errno.h>
#include <ext2fs/ext2fs.h>
#include <solution.h>
#include <sys/types.h>
#include <unistd.h>

#define SBOFF 1024
#define DEFBLKSZ 1024

int read_blk(int img, int out, int blk_idx, int blk_sz, int *left_read,
             int level) {
  char *buf = malloc(blk_sz);
  if (level) {
    int *blk_idxs = (int *)buf;
    if (pread(img, buf, blk_sz, blk_idx * blk_sz) < 0) {
      free(buf);
      return -errno;
    }
    unsigned ub = blk_sz / sizeof(int);
    for (unsigned i = 0; i < ub && *left_read < 0; ++i) {
      if (read_blk(img, out, blk_idxs[i], blk_sz, left_read, level - 1) < 0) {
        free(buf);
        return -errno;
      }
    }
  } else {
    int cur_len = *left_read > blk_sz ? blk_sz : *left_read;
    if (pread(img, buf, blk_sz, blk_idx * blk_sz) < 0) {
      free(buf);
      return -errno;
    }
    if (write(out, buf, cur_len) < cur_len) {
      free(buf);
      return -errno;
    }
    *left_read -= cur_len;
  }
  free(buf);
  return 0;
}

int dump_file(int img, int inode_nr, int out) {
  struct ext2_super_block sb;
  struct ext2_group_desc gd;
  struct ext2_inode inode;

  if (pread(img, &sb, sizeof(sb), SBOFF) < 0) {
    return -errno;
  }
  int blk_sz = DEFBLKSZ << sb.s_log_block_size;

  off_t gd_idx = (inode_nr - 1) / sb.s_inodes_per_group;
  off_t gd_offset = blk_sz * (sb.s_first_data_block + 1) +
                    gd_idx * sizeof(struct ext2_group_desc);
  off_t inode_idx = (inode_nr - 1) % sb.s_inodes_per_group;
  off_t inode_offset =
      gd.bg_inode_table * blk_sz + (inode_idx * sb.s_inode_size);

  if (pread(img, &gd, sizeof(struct ext2_group_desc), gd_offset) < 0) {
    return -errno;
  }
  if (pread(img, &inode, sizeof(inode), inode_offset) < 0) {
    return -errno;
  }

  int left_read = inode.i_size;
  for (int i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
    if (read_blk(img, out, inode.i_block[i], blk_sz, &left_read, 0) < 0) {
      return -errno;
    }
  }
  if (read_blk(img, out, inode.i_block[EXT2_IND_BLOCK], blk_sz, &left_read,
               1)) {
    return -errno;
  }
  if (read_blk(img, out, inode.i_block[EXT2_DIND_BLOCK], blk_sz, &left_read,
               2)) {
    return -errno;
  }
  return 0;
}
