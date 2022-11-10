#include <assert.h>
#include <ext2fs/ext2fs.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <solution.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SBOFF 1024

int read_sb(struct ext2_super_block* sb, int img) {
  if (pread(img, sb, sizeof(struct ext2_super_block), SBOFF) == -1) {
    return -errno;
  }
  return 0;
}

int read_gd(int img, struct ext2_super_block* p_sb,
            struct ext2_group_desc* p_gd, uint32_t inode_idx, int64_t blk_sz) {
  uint32_t gd_idx = (inode_idx - 1) / p_sb->s_inodes_per_group;
  size_t gd_offset = (p_sb->s_first_data_block + 1) * blk_sz +
                     gd_idx * sizeof(struct ext2_group_desc);
  if (pread(img, p_gd, sizeof(struct ext2_group_desc), gd_offset) == -1) {
    return -errno;
  }
  return 0;
}

int read_inode(int img, struct ext2_super_block* p_sb,
               struct ext2_group_desc* p_gd, struct ext2_inode* p_inode,
               size_t inode_idx, int64_t blk_sz) {
  size_t inode_offset =
      p_gd->bg_inode_table * blk_sz + (inode_idx - 1) * p_sb->s_inode_size;
  if (pread(img, p_inode, sizeof(struct ext2_inode), inode_offset) == -1) {
    return -errno;
  }
  return 0;
}

int64_t send_blk(int img, int out, size_t blk_idx, int64_t blk_sz,
                 int64_t to_send) {
  int64_t cur_len = to_send > blk_sz ? blk_sz : to_send;
  char* buf = malloc(cur_len);
  if (pread(img, buf, cur_len, blk_idx * blk_sz) == -1) {
    free(buf);
    return -errno;
  }
  if (write(out, buf, cur_len) < 0) {
    free(buf);
    return -errno;
  }
  free(buf);
  return cur_len;
}

int64_t search_inode_in_blk(int img, const char* filename, int64_t* p_left_read,
                            size_t blk_idx, int64_t blk_sz) {
  char* buf = malloc(blk_sz);
  const char const* cur = buf;
  const char const* buf_end = buf + blk_sz;
  size_t filename_len = strlen(filename);
  if (pread(img, buf, blk_sz, blk_idx * blk_sz) == -1) {
    free(buf);
    return -errno;
  }
  while (buf_end - cur > 0) {
    struct ext2_dir_entry_2* p_dir_entry = (struct ext2_dir_entry_2*)buf;
    if (p_dir_entry->inode <= 0) {
      break;
    }
    if (filename_len == p_dir_entry->name_len &&
        !strncmp(filename, p_dir_entry->name, p_dir_entry->name_len)) {
      int64_t inode_nr = p_dir_entry->inode;
      free(buf);
      return inode_nr;
    }
    cur += p_dir_entry->rec_len;
  }
  free(buf);
  *p_left_read -= blk_sz;
  return -ENOENT;
}

int64_t search_inode_in_ind_blk(int img, const char* filename,
                                int64_t* p_left_read, size_t blk_idx,
                                int64_t blk_sz) {
  int64_t res = 0;
  uint32_t* blk_idxs = malloc(blk_sz);
  size_t ub = blk_sz / sizeof(uint32_t);
  if (*p_left_read <= 0) {
    return -ENOENT;
  }
  if (pread(img, blk_idxs, blk_sz, blk_idx * blk_sz) == -1) {
    return -errno;
  }
  for (size_t i = 0; i < ub && *p_left_read; ++i) {
    if ((res = search_inode_in_blk(img, filename, p_left_read, blk_idxs[i],
                                   blk_sz)) != -1) {
      free(blk_idxs);
      return res;
    }
    *p_left_read -= blk_sz;
  }
  free(blk_idxs);
  return -ENOENT;
}

int64_t search_inode_in_dind_blk(int img, const char* filename,
                                 int64_t* p_left_read, size_t blk_idx,
                                 int64_t blk_sz) {
  int64_t res = 0;
  uint32_t* blk_idxs = malloc(blk_sz);
  size_t ub = blk_sz / sizeof(uint32_t);
  if (*p_left_read <= 0) {
    return -ENOENT;
  }
  if (pread(img, blk_idxs, blk_sz, blk_sz * blk_idx) == -1) {
    return -errno;
  }
  for (size_t i = 0; i < ub && *p_left_read; ++i) {
    if ((res = search_inode_in_ind_blk(img, filename, p_left_read, blk_idxs[i],
                                       blk_sz)) != -ENOENT) {
      free(blk_idxs);
      return res;
    }
    *p_left_read -= blk_sz;
  }
  free(blk_idxs);
  return -ENOENT;
}

char* next_node(const char* path, char* filename) {
  if (path[0] != '/') {
    return NULL;
  }
  ++path;
  memset(filename, 0, EXT2_NAME_LEN + 1);
  while (*path != '\0' && *path != '/') {
    *filename++ = *path++;
  }
  return path;
}

int64_t search_inode(int img, const char* path, struct ext2_super_block* p_sb,
                     struct ext2_group_desc* p_gd, int64_t inode_idx,
                     int64_t blk_sz) {
  int64_t res = 0;
  struct ext2_inode inode;
  char filename[EXT2_NAME_LEN + 1];
  while ((path = next_node(path, filename))) {
    if ((res = read_inode(img, p_sb, p_gd, &inode, inode_idx, blk_sz)) < 0) {
      return res;
    }
    if (!S_ISDIR(inode.i_mode)) {
      return -ENOTDIR;
    }
    uint32_t left_read = inode.i_size;
    int found = 0;
    for (size_t i = 0; i < EXT2_NDIR_BLOCKS && left_read > 0; ++i) {
      if ((res = search_inode_in_blk(img, filename, &left_read,
                                     inode.i_block[i], blk_sz)) >= 0) {
        inode_idx = res;
        found = 1;
        break;
      }
      if (res != -ENOENT) {
        return res;
      }
    }
    if (found) {
      continue;
    }
    if ((res = search_inode_in_ind_blk(img, filename, &left_read,
                                       inode.i_block[EXT2_IND_BLOCK],
                                       blk_sz)) >= 0) {
      inode_idx = res;
      continue;
    }
    if (res != -ENOENT) {
      return res;
    }
    if ((res = search_inode_in_dind_blk(img, filename, &left_read,
                                        inode.i_block[EXT2_DIND_BLOCK],
                                        blk_sz)) >= 0) {
      inode_idx = res;
      continue;
    }
    if (res != -ENOENT) {
      return res;
    }

    return -ENOENT;
  }
  return inode_idx;
}

int64_t send_dir_blks(int img, int out, struct ext2_super_block* p_sb,
                      struct ext2_inode* p_inode, int64_t blk_sz,
                      int64_t left_read) {
  int64_t ret = 0;
  int64_t read = 0;
  for (size_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
    if ((ret = send_blk(img, out, p_inode->i_block[i], blk_sz, left_read)) <
        0) {
      return ret;
    }
    read += ret;
    if ((left_read -= ret) <= 0) {
      return read;
    }
  }
  return read;
}

int64_t send_ind_blk(int img, int out, struct ext2_super_block* p_sb,
                     struct ext2_inode* p_inode, int64_t blk_sz,
                     int64_t left_read) {
  int64_t ret = 0;
  int64_t read = 0;
  uint32_t* blk_idxs = malloc(blk_sz);
  size_t ub = blk_sz / sizeof(uint32_t);
  if (pread(img, blk_idxs, blk_sz, blk_sz * p_inode->i_block[EXT2_IND_BLOCK]) ==
      -1) {
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if ((ret = send_blk(img, out, blk_idxs[i], blk_sz, left_read)) < 0) {
      free(blk_idxs);
      return ret;
    }
    read += ret;
    if ((left_read -= ret) <= 0) {
      free(blk_idxs);
      return read;
    }
  }
  return read;
}

int64_t send_dind_blk(int img, int out, struct ext2_super_block* p_sb,
                      struct ext2_inode* p_inode, int64_t blk_sz,
                      int64_t left_read) {
  int64_t ret = 0;
  int64_t read = 0;
  uint32_t* blk_idxs = malloc(blk_sz);
  uint32_t* blk_didxs = malloc(blk_sz);
  size_t ub = blk_sz / sizeof(uint32_t);
  if (pread(img, blk_didxs, blk_sz,
            p_inode->i_block[EXT2_DIND_BLOCK] * blk_sz) == -1) {
    free(blk_idxs);
    free(blk_didxs);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if ((ret = pread(img, blk_idxs, blk_sz, blk_didxs[i] * blk_sz)) == -1) {
      free(blk_idxs);
      free(blk_didxs);
      return -errno;
    }
    for (size_t j = 0; j < ub; ++j) {
      if ((ret = send_blk(img, out, blk_idxs[j], blk_sz, left_read)) < 0) {
        free(blk_idxs);
        free(blk_didxs);
        return ret;
      }
      read += ret;
      if ((left_read -= ret) <= 0) {
        free(blk_idxs);
        free(blk_didxs);
        return read;
      }
    }
  }
  free(blk_idxs);
  free(blk_didxs);
  return read;
}

int64_t send_file(int img, int out, struct ext2_super_block* p_sb,
                  int64_t inode_idx, int64_t blk_sz) {
  struct ext2_group_desc gd;
  struct ext2_inode inode;
  int64_t ret = 0;
  if ((ret = read_gd(img, p_sb, &gd, inode_idx, blk_sz))) {
    return ret;
  }
  if ((ret = read_inode(img, p_sb, &gd, &inode, inode_idx, blk_sz))) {
    return ret;
  }
  int64_t left_read = inode.i_size;
  if (left_read < 0) {
    return 0;
  }
  if ((ret = send_dir_blks(img, out, p_sb, &inode, blk_sz, left_read)) < 0) {
    return ret;
  }
  if ((left_read -= ret) <= 0) {
    return 0;
  }
  if ((ret = send_ind_blk(img, out, p_sb, &inode, blk_sz, left_read)) < 0) {
    return ret;
  }
  if ((left_read -= ret) <= 0) {
    return 0;
  }
  if ((ret = send_dind_blk(img, out, p_sb, &inode, blk_sz, left_read)) < 0) {
    return ret;
  }
  return 0;
}

int dump_file(int img, const char* path, int out) {
  struct ext2_super_block sb;
  struct ext2_group_desc gd;
  int64_t start_inode_idx = 2;
  int ret = 0;
  int64_t inode_idx = 0;
  if ((ret = read_sb(&sb, img))) {
    return ret;
  }
  int64_t blk_sz = EXT2_BLOCK_SIZE(&sb);
  if ((ret = read_gd(img, &sb, &gd, start_inode_idx, blk_sz))) {
    return ret;
  }
  if ((inode_idx = search_inode(img, path, &sb, &gd, start_inode_idx, blk_sz)) <
      0) {
    return (int)inode_idx;
  }
  if ((ret = (int)send_file(img, out, &sb, inode_idx, blk_sz)) < 0) {
    return ret;
  }
  return 0;
}
