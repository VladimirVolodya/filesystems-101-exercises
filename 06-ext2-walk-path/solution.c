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
            struct ext2_group_desc* p_gd, uint32_t inode_nr, uint32_t blk_sz);
int read_inode(int img, struct ext2_super_block* p_sb,
               struct ext2_inode* p_inode, uint32_t inode_nr, uint32_t blk_sz);
const char* next_path(const char* path);
int send_blk(int img, int out, uint32_t blk_idx, uint32_t blk_sz,
             uint32_t to_send);
int64_t send_dir_blks(int img, int out, struct ext2_inode* p_inode,
                      int64_t left_read, uint32_t blk_sz);
int64_t send_idir_blks(int img, int out, struct ext2_inode* p_inode,
                       int64_t left_read, uint32_t blk_sz);
int64_t send_didir_blks(int img, int out, struct ext2_inode* p_inode,
                        int64_t left_read, uint32_t blk_sz);
int send_file(int img, int out, struct ext2_super_block* p_sb,
              uint32_t inode_nr, uint32_t blk_sz);
int64_t search_inode_in_dblk(int img, struct ext2_super_block* p_sb,
                             uint32_t blk_idx, const char* path,
                             uint32_t blk_sz);
int64_t search_inode_in_iblk(int img, struct ext2_super_block* p_sb,
                             uint32_t blk_idx, const char* path,
                             uint32_t blk_sz);
int64_t search_inode_in_diblk(int img, struct ext2_super_block* p_sb,
                              uint32_t blk_idx, const char* path,
                              uint32_t blk_sz);
int64_t search_inode(int img, struct ext2_super_block* p_sb, uint32_t inode_nr,
                     const char* path, uint32_t blk_sz);

int dump_file(int img, const char* path, int out) {
  int64_t res = 0;
  struct ext2_super_block sb;
  if ((res = read_sb(&sb, img)) < 0) {
    return (int)res;
  }
  uint32_t blk_sz = EXT2_BLOCK_SIZE(&sb);
  if ((res = search_inode(img, &sb, 2, path, blk_sz)) < 0) {
    return (int)res;
  }
  if ((res = send_file(img, out, &sb, (uint32_t)res, blk_sz)) < 0) {
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
            struct ext2_group_desc* p_gd, uint32_t inode_nr, uint32_t blk_sz) {
  size_t gd_idx = (inode_nr - 1) / p_sb->s_inodes_per_group;
  uint32_t gd_offset = (p_sb->s_first_data_block + 1) * blk_sz +
                       gd_idx * sizeof(struct ext2_group_desc);
  if (pread(img, p_gd, sizeof(struct ext2_group_desc), gd_offset)) {
    return -errno;
  }
  return 0;
}

int read_inode(int img, struct ext2_super_block* p_sb,
               struct ext2_inode* p_inode, uint32_t inode_nr, uint32_t blk_sz) {
  uint32_t inode_idx = (inode_nr - 1) % p_sb->s_inodes_per_group;
  int res = 0;
  struct ext2_group_desc gd;
  if ((res = read_gd(img, p_sb, &gd, inode_nr, blk_sz)) < 0) {
    return res;
  }
  off_t inode_offset =
      gd.bg_inode_table * blk_sz + inode_nr * p_sb->s_inode_size;
  if (pread(img, p_inode, sizeof(struct ext2_inode), inode_offset) < 0) {
    return -errno;
  }
  return 0;
}

int send_blk(int img, int out, uint32_t blk_idx, uint32_t blk_sz,
             uint32_t to_send) {
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

int64_t send_dir_blks(int img, int out, struct ext2_inode* p_inode,
                      int64_t left_read, uint32_t blk_sz) {
  int64_t to_send = 0;
  int64_t sent = 0;
  int res = 0;
  for (int32_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
    to_send = left_read > blk_sz ? blk_sz : left_read;
    if ((res = send_blk(img, out, p_inode->i_block[i], blk_sz, to_send)) < 0) {
      return res;
    }
    if ((sent += to_send) >= left_read) {
      return sent;
    }
  }
  return sent;
}

int64_t send_idir_blks(int img, int out, struct ext2_inode* p_inode,
                       int64_t left_read, uint32_t blk_sz) {
  int64_t to_send = 0;
  int64_t sent = 0;
  int res = 0;
  size_t ub = blk_sz / sizeof(uint32_t);
  uint32_t* blk_idxs = malloc(blk_sz);
  if (pread(img, blk_idxs, blk_sz, p_inode->i_block[EXT2_IND_BLOCK] * blk_sz) <
      0) {
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    to_send = left_read > blk_sz ? blk_sz : left_read;
    if ((res = send_blk(img, out, blk_idxs[i], blk_sz, to_send)) < 0) {
      free(blk_idxs);
      return res;
    }
    if ((sent += to_send) >= left_read) {
      return sent;
    }
  }
  return sent;
}

int64_t send_didir_blks(int img, int out, struct ext2_inode* p_inode,
                        int64_t left_read, uint32_t blk_sz) {
  int64_t to_send = 0;
  int64_t sent = 0;
  int res = 0;
  size_t ub = blk_sz / sizeof(uint32_t);
  uint32_t* blk_idxs = malloc(2 * blk_sz);
  uint32_t* blk_didxs = blk_idxs + ub;
  if (pread(img, blk_didxs, blk_sz,
            p_inode->i_block[EXT2_DIND_BLOCK] * blk_sz) < 0) {
    free(blk_idxs);
    free(blk_didxs);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if (pread(img, blk_idxs, blk_sz, blk_didxs[i] * blk_sz) < 0) {
      free(blk_idxs);
      free(blk_didxs);
      return -errno;
    }
    for (size_t j = 0; j < ub; ++j) {
      to_send = left_read > blk_sz ? blk_sz : left_read;
      if ((res = send_blk(img, out, blk_idxs[i], blk_sz, to_send)) < 0) {
        free(blk_idxs);
        free(blk_didxs);
        return res;
      }
      if ((sent += to_send) > left_read) {
        free(blk_idxs);
        free(blk_didxs);
        return sent;
      }
    }
  }
  free(blk_idxs);
  free(blk_didxs);
  return sent;
}

int send_file(int img, int out, struct ext2_super_block* p_sb,
              uint32_t inode_nr, uint32_t blk_sz) {
  struct ext2_inode inode;
  int64_t res = 0;
  if ((res = read_inode(img, p_sb, &inode, inode_nr, blk_sz)) < 0) {
    return (int)res;
  }
  int64_t left_read = inode.i_size;
  assert(left_read > 0);

  if ((res = send_dir_blks(img, out, &inode, left_read, blk_sz)) < 0) {
    return (int)res;
  }
  if ((left_read -= res) <= 0) {
    return 0;
  }

  if ((res = send_idir_blks(img, out, &inode, left_read, blk_sz)) < 0) {
    return (int)res;
  }
  if ((left_read -= res) <= 0) {
    return 0;
  }

  if ((res = send_didir_blks(img, out, &inode, left_read, blk_sz)) < 0) {
    return (int)res;
  }
  return 0;
}

int64_t search_inode_in_dblk(int img, struct ext2_super_block* p_sb,
                             uint32_t blk_idx, const char* path,
                             uint32_t blk_sz) {
  char* buf = malloc(blk_sz);
  char* cur = buf;
  uint32_t inode_nr = 0;

  if (pread(img, buf, blk_sz, blk_idx * blk_sz) < 0) {
    free(buf);
    return -errno;
  }

  while (cur - buf < blk_sz) {
    struct ext2_dir_entry_2* p_de = (struct ext2_dir_entry_2*)cur;
    if (p_de->inode == 0) {
      return -ENOENT;
    }
    const char* next_path = strchr(path, '/');
    if (!next_path) {
      next_path = path + strlen(path);
    }
    if (next_path - path == p_de->name_len &&
        !strncmp(path, p_de->name, p_de->name_len)) {
      inode_nr = p_de->inode;
      // TODO: move this into search_node
      if (!*next_path) {
        free(buf);
        return inode_nr;
      }
      if (p_de->file_type == EXT2_FT_DIR) {
        free(buf);
        return search_inode(img, p_sb, inode_nr, next_path, blk_sz);
      }
      free(buf);
      return -ENOTDIR;
    }
    cur += p_de->rec_len;
  }
  free(buf);
  return 0;
}

int64_t search_inode_in_iblk(int img, struct ext2_super_block* p_sb,
                             uint32_t blk_idx, const char* path,
                             uint32_t blk_sz) {
  uint32_t* blk_idxs = malloc(blk_sz);
  int64_t res = 0;
  size_t ub = blk_sz / sizeof(uint32_t);
  if ((res = pread(img, blk_idxs, blk_sz, blk_sz * blk_idx)) < 0) {
    free(blk_idx);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if (!blk_idxs[i]) {
      return -ENOENT;
    }
    if ((res = search_inode_in_dblk(img, blk_idxs[i], p_sb, path, blk_sz))) {
      free(blk_idxs);
      return res;
    }
  }
  free(blk_idxs);
  return 0;
}

int64_t search_inode_in_diblk(int img, struct ext2_super_block* p_sb,
                              uint32_t blk_idx, const char* path,
                              uint32_t blk_sz) {
  uint32_t* blk_didxs = malloc(blk_sz);
  size_t ub = blk_sz / sizeof(uint32_t);
  int64_t res = 0;
  if ((res = pread(img, blk_didxs, blk_sz, blk_sz * blk_idx)) < 0) {
    free(blk_didxs);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if (!blk_didxs[i]) {
      return -ENOENT;
    }
    if ((res = search_inode_in_idblk(img, p_sb, blk_didxs[i], path, blk_sz))) {
      free(blk_didxs);
      return res;
    }
  }
  free(blk_didxs);
  return 0;
}

int64_t search_inode(int img, struct ext2_super_block* p_sb, uint32_t inode_nr,
                     const char* path, uint32_t blk_sz) {
  int64_t res = 0;
  struct ext2_inode inode;
  if (!inode_nr) {
    return -ENOENT;
  }
  if (*path != '/') {
    return inode_nr;
  }
  ++path;
  if ((res = read_inode(img, p_sb, &inode, inode_nr, blk_sz)) < 0) {
    return res;
  }
  for (size_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
    if ((res =
             search_inode_in_dblk(img, p_sb, inode.i_block[i], path, blk_sz))) {
      return res;
    }
  }
  if ((res = search_inode_in_iblk(img, p_sb, inode.i_block[EXT2_IND_BLOCK],
                                  path, blk_sz))) {
    return res;
  }
  if ((res = search_inode_in_diblk(img, p_sb, inode.i_block[EXT2_DIND_BLOCK],
                                   path, blk_sz))) {
    return res;
  }
  return -ENOENT;
}
