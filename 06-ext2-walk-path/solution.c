#include <assert.h>
#include <ext2fs/ext2fs.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <solution.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int read_sb(int img, struct ext2_super_block* p_sb);
int read_gd(int img, struct ext2_super_block* p_sb,
            struct ext2_group_desc* p_gd, int32_t inode_nr, int32_t blk_sz);
int read_inode(int img, struct ext2_super_block* p_sb, struct ext2_inode* inode,
               int32_t inode_nr, int32_t blk_sz);
int send_blk(int img, int out, uint32_t blk_idx, int32_t to_send,
             int32_t blk_sz);
int32_t send_file(int img, int out, struct ext2_super_block* p_sb,
                  int32_t inode_nr, int32_t blk_sz);
int64_t search_inode_in_dir(int img, struct ext2_super_block* p_sb,
                            uint32_t blk_idx, const char* path, int32_t blk_sz);
int64_t search_inode_in_idir(int img, struct ext2_super_block* p_sb,
                             uint32_t block_nr, const char* path,
                             int32_t blk_sz);
int64_t search_inode_in_didir(int img, struct ext2_super_block* p_sb,
                              uint32_t block_nr, const char* path,
                              int32_t blk_sz);
int64_t search_inode(int img, struct ext2_super_block* p_sb, int32_t inode_nr,
                     const char* path, int32_t blk_sz);
int dump_file(int img, const char* path, int out);

int dump_file(int img, const char* path, int out) {
  int64_t res = 0;
  struct ext2_super_block sb;
  if ((res = read_sb(img, &sb)) < 0) {
    return (int)res;
  }
  int32_t blk_sz = EXT2_BLOCK_SIZE(&sb);
  if ((res = search_inode(img, &sb, 2, path, blk_sz)) < 0) {
    return (int)res;
  }
  if ((res = send_file(img, out, &sb, (int32_t)res, blk_sz)) < 0) {
    return (int)res;
  }
  return 0;
}

int read_sb(int img, struct ext2_super_block* p_sb) {
  if (pread(img, p_sb, sizeof(struct ext2_super_block), SUPERBLOCK_OFFSET) <
      0) {
    return -errno;
  }
  return 0;
}

int read_gd(int img, struct ext2_super_block* p_sb,
            struct ext2_group_desc* p_gd, int32_t inode_nr, int32_t blk_sz) {
  uint32_t gd_idx = (inode_nr - 1) / p_sb->s_inodes_per_group;
  uint32_t gd_offset = (p_sb->s_first_data_block + 1) * blk_sz +
                       sizeof(struct ext2_group_desc) * gd_idx;
  if (pread(img, p_gd, sizeof(struct ext2_group_desc), gd_offset) < 0) {
    return -errno;
  }
  return 0;
}

int read_inode(int img, struct ext2_super_block* p_sb, struct ext2_inode* inode,
               int32_t inode_nr, int32_t blk_sz) {
  struct ext2_group_desc gd;
  int res = 0;
  if ((res = read_gd(img, p_sb, &gd, inode_nr, blk_sz)) < 0) {
    return res;
  }
  off_t inode_offset =
      gd.bg_inode_table * blk_sz + inode_nr * p_sb->s_inode_size;
  if ((res = pread(img, inode, sizeof(struct ext2_inode), inode_offset)) < 0) {
    return -errno;
  }
  return 0;
}

int send_blk(int img, int out, uint32_t blk_idx, int32_t to_send,
             int32_t blk_sz) {
  char* buf = malloc(to_send);
  if (pread(img, buf, to_send, blk_idx * blk_sz) < 0) {
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

int32_t send_file(int img, int out, struct ext2_super_block* p_sb,
                  int32_t inode_nr, int32_t blk_sz) {
  struct ext2_inode inode;
  int32_t to_send = 0;
  int res = 0;
  if ((res = read_inode(img, p_sb, &inode, inode_nr, blk_sz)) < 0) {
    return res;
  }
  int64_t left_read = inode.i_size;
  for (size_t i = 0; i < EXT2_NDIR_BLOCKS && left_read > 0; ++i) {
    to_send = left_read > blk_sz ? blk_sz : (int32_t)left_read;
    if ((res = send_blk(img, out, inode.i_block[i], to_send, blk_sz)) < 0) {
      return res;
    }
    left_read -= to_send;
  }
  if (left_read <= 0) {
    return 0;
  }

  uint32_t* blk_idxs = malloc(2 * blk_sz);
  uint32_t* blk_didxs = blk_idxs + blk_sz;
  res = pread(img, blk_idxs, blk_sz, blk_sz * inode.i_block[EXT2_IND_BLOCK]);
  if (res < 0) {
    return -errno;
  }
  size_t ub = blk_sz / sizeof(uint32_t);
  for (size_t i = 0; i < ub && left_read > 0; ++i) {
    to_send = left_read > blk_sz ? blk_sz : (int32_t)left_read;
    if ((res = send_blk(img, out, blk_idxs[i], to_send, blk_sz)) < 0) {
      free(blk_idxs);
      return res;
    }
    left_read -= to_send;
  }
  if (left_read <= 0) {
    free(blk_idxs);
    return 0;
  }
  if ((res = pread(img, blk_didxs, blk_sz,
                   inode.i_block[EXT2_DIND_BLOCK] * blk_sz)) < 0) {
    return -errno;
  }
  for (size_t i = 0; i < ub && left_read > 0; ++i) {
    if ((res = pread(img, blk_idxs, blk_sz, blk_sz * blk_didxs[i])) < 0) {
      free(blk_idxs);
      return -errno;
    }
    for (size_t j = 0; j < ub && left_read > 0; ++j) {
      res = send_file(img, out, p_sb, inode_nr, blk_sz);
      if (res < 0) {
        free(blk_idxs);
        return -errno;
      }
      left_read -= res;
    }
  }
  free(blk_idxs);
  return 0;
}

int64_t search_inode_in_dir(int img, struct ext2_super_block* p_sb,
                            uint32_t blk_idx, const char* path,
                            int32_t blk_sz) {
  if (blk_idx == 0) {
    return -ENOENT;
  }
  char* buf = malloc(blk_sz);
  char* cur = buf;
  if (pread(img, buf, blk_sz, blk_idx * blk_sz) < 0) {
    free(buf);
    return -errno;
  }
  while (cur - buf < blk_sz) {
    struct ext2_dir_entry_2* p_de = (struct ext2_dir_entry_2*)cur;
    if (p_de->inode == 0) {
      free(buf);
      return -ENOENT;
    }
    const char* next_dir = path;
    while (*next_dir != '/' && *next_dir != '\0') {
      ++next_dir;
    }
    if (next_dir - path == p_de->name_len &&
        !strncmp(path, p_de->name, p_de->name_len)) {
      int64_t inode_nr = p_de->inode;
      if (!*next_dir) {
        free(buf);
        return inode_nr;
      }
      if (p_de->file_type == EXT2_FT_DIR) {
        free(buf);
        return search_inode(img, p_sb, (int32_t)inode_nr, next_dir, blk_sz);
      }
      free(buf);
      return -ENOTDIR;
    }
    cur += p_de->rec_len;
  }
  free(buf);
  return 0;
}

int64_t search_inode_in_idir(int img, struct ext2_super_block* p_sb,
                             uint32_t block_nr, const char* path,
                             int32_t blk_sz) {
  uint32_t* blk_idxs = malloc(blk_sz);
  int64_t res = 0;
  size_t ub = blk_sz / sizeof(uint32_t);
  if (block_nr == 0) {
    free(blk_idxs);
    return -ENOENT;
  }
  if ((res = pread(img, blk_idxs, blk_sz, blk_sz * block_nr)) < 0) {
    free(blk_idxs);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if ((res = search_inode_in_dir(img, p_sb, blk_idxs[i], path, blk_sz))) {
      free(blk_idxs);
      return res;
    }
  }
  free(blk_idxs);
  return 0;
}

int64_t search_inode_in_didir(int img, struct ext2_super_block* p_sb,
                              uint32_t block_nr, const char* path,
                              int32_t blk_sz) {
  uint32_t* blk_didxs = malloc(blk_sz);
  int64_t res = 0;
  size_t ub = blk_sz / sizeof(uint32_t);
  if (block_nr == 0) {
    free(blk_didxs);
    return -ENOENT;
  }
  if ((res = pread(img, blk_didxs, blk_sz, blk_sz * block_nr)) < 0) {
    free(blk_didxs);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if ((res = search_inode_in_idir(img, p_sb, blk_didxs[i], path, blk_sz))) {
      free(blk_didxs);
      return res;
    }
  }
  free(blk_didxs);
  return 0;
}

int64_t search_inode(int img, struct ext2_super_block* p_sb, int32_t inode_nr,
                     const char* path, int32_t blk_sz) {
  int64_t res = 0;
  struct ext2_inode inode;
  if (inode_nr == 0) {
    return -ENOENT;
  }
  if (path[0] != '/') {
    return inode_nr;
  }
  ++path;
  if ((res = read_inode(img, p_sb, &inode, inode_nr, blk_sz)) < 0) {
    return res;
  }
  for (size_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
    if ((res =
             search_inode_in_dir(img, p_sb, inode.i_block[i], path, blk_sz))) {
      return res;
    }
  }
  if ((res = search_inode_in_idir(img, p_sb, inode.i_block[EXT2_IND_BLOCK],
                                  path, blk_sz))) {
    return res;
  }
  if ((res = search_inode_in_didir(img, p_sb, inode.i_block[EXT2_DIND_BLOCK],
                                   path, blk_sz))) {
    return res;
  }
  return -ENOENT;
}
