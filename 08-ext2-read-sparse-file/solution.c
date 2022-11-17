#include <assert.h>
#include <ext2fs/ext2fs.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <solution.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int read_sb(int img, struct ext2_super_block* sb);
int read_gd(int img, struct ext2_super_block* sb, struct ext2_group_desc* bg,
            size_t inode_nr);
int read_inode(int img, struct ext2_super_block* p_sb,
               struct ext2_inode* p_inode, long inode_nr);
int send_blk(int img, int out, uint32_t blk_idx, size_t blk_sz, size_t to_send);
int send_dir_blks(int img, int out, struct ext2_inode* p_inode,
                  size_t* p_left_read, size_t blk_sz);
int send_idir_blk(int img, int out, struct ext2_inode* p_inode,
                  size_t* p_left_read, size_t blk_sz);
int send_didir_blk(int img, int out, struct ext2_inode* p_inode,
                   size_t* p_left_read, size_t blk_sz);
int send_file(int img, int out, struct ext2_super_block* sb, int inode_nr);

int dump_file(int img, int inode_nr, int out) {
  struct ext2_super_block sb;
  struct ext2_inode inode;
  int res = 0;
  if ((res = read_sb(img, &sb)) < 0) {
    return res;
  }
  if ((res = read_inode(img, &sb, &inode, inode_nr)) < 0) {
    return res;
  }

  return send_file(img, out, &sb, inode_nr);
}

int read_sb(int img, struct ext2_super_block* sb) {
  if (pread(img, sb, sizeof(struct ext2_super_block), SUPERBLOCK_OFFSET) < 0) {
    return -errno;
  }
  return 0;
}

int read_gd(int img, struct ext2_super_block* sb, struct ext2_group_desc* bg,
            size_t inode_nr) {
  size_t gd_idx = (inode_nr - 1) / sb->s_inodes_per_group;
  size_t blk_sz = EXT2_BLOCK_SIZE(sb);
  size_t gd_offset = (sb->s_first_data_block + 1) * blk_sz +
                     sizeof(struct ext2_group_desc) * gd_idx;
  if (pread(img, bg, sizeof(struct ext2_group_desc), gd_offset) < 0) {
    return -errno;
  }
  return 0;
}

int read_inode(int img, struct ext2_super_block* p_sb,
               struct ext2_inode* p_inode, long inode_nr) {
  struct ext2_group_desc gd;
  size_t blk_sz = EXT2_BLOCK_SIZE(p_sb);
  int res = 0;
  size_t inode_idx = (inode_nr - 1) % p_sb->s_inodes_per_group;
  if ((res = read_gd(img, p_sb, &gd, inode_nr)) < 0) {
    return res;
  }
  size_t inode_offset =
      gd.bg_inode_table * blk_sz + inode_idx * p_sb->s_inode_size;
  if (pread(img, p_inode, sizeof(struct ext2_inode), inode_offset)) {
    return -errno;
  }
  return 0;
}

int send_blk(int img, int out, uint32_t blk_idx, size_t blk_sz,
             size_t to_send) {
  int res = 0;
  char* buf = malloc(to_send);
  if ((res = pread(img, buf, to_send, blk_idx * blk_sz)) < 0) {
    free(buf);
    return -errno;
  }
  if (write(out, buf, to_send) < 0) {
    free(buf);
    return -errno;
  }
  free(buf);
  return 0;
}

int send_dir_blks(int img, int out, struct ext2_inode* p_inode,
                  size_t* p_left_read, size_t blk_sz) {
  int res = 0;
  size_t to_send = 0;
  for (size_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
    to_send = *p_left_read > blk_sz ? blk_sz : *p_left_read;
    if ((res = send_blk(img, out, p_inode->i_block[i], blk_sz, to_send)) < 0) {
      return res;
    }
    if (!(*p_left_read -= to_send)) {
      return 0;
    }
  }
  return 1;
}

int send_idir_blk(int img, int out, struct ext2_inode* p_inode,
                  size_t* p_left_read, size_t blk_sz) {
  int res = 0;
  size_t to_send = 0;
  size_t ub = blk_sz / sizeof(uint32_t);
  uint32_t* ind_blk = malloc(blk_sz);
  if ((res = pread(img, ind_blk, blk_sz,
                   blk_sz * p_inode->i_block[EXT2_IND_BLOCK])) < 0) {
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    to_send = *p_left_read > blk_sz ? blk_sz : *p_left_read;
    if ((res = send_blk(img, out, ind_blk[i], blk_sz, to_send)) < 0) {
      free(ind_blk);
      return res;
    }
    if (!(*p_left_read -= to_send)) {
      free(ind_blk);
      return 0;
    }
  }
  free(ind_blk);
  return 1;
}

int send_didir_blk(int img, int out, struct ext2_inode* p_inode,
                   size_t* p_left_read, size_t blk_sz) {
  int res = 0;
  size_t to_send = 0;
  size_t ub = blk_sz / sizeof(uint32_t);
  uint32_t* dind_blk = malloc(2 * blk_sz);
  uint32_t* ind_blk = dind_blk + ub;
  if ((res = pread(img, dind_blk, blk_sz,
                   p_inode->i_block[EXT2_DIND_BLOCK] * blk_sz)) < 0) {
    free(dind_blk);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if ((res = pread(img, ind_blk, blk_sz, dind_blk[i] * blk_sz)) < 0) {
      free(dind_blk);
      return -errno;
    }
    for (size_t j = 0; j < ub; ++j) {
      to_send = *p_left_read > blk_sz ? blk_sz : *p_left_read;
      if ((res = send_blk(img, out, ind_blk[j], blk_sz, to_send)) < 0) {
        free(dind_blk);
        return res;
      }
      if (!(*p_left_read -= to_send)) {
        free(dind_blk);
        return 0;
      }
    }
  }
  free(dind_blk);
  return 1;
}

int send_file(int img, int out, struct ext2_super_block* p_sb, int inode_nr) {
  size_t blk_sz = EXT2_BLOCK_SIZE(p_sb);
  int res = 0;
  struct ext2_inode inode;
  if ((res = read_inode(img, p_sb, &inode, inode_nr)) < 0) {
    return res;
  }
  size_t left_read = inode.i_size;
  if ((res = send_dir_blks(img, out, &inode, &left_read, blk_sz)) <= 0) {
    return res;
  }
  if ((res = send_idir_blk(img, out, &inode, &left_read, blk_sz)) <= 0) {
    return res;
  }
  if ((res = send_didir_blk(img, out, &inode, &left_read, blk_sz)) <= 0) {
    return res;
  }
  return 0;
}
