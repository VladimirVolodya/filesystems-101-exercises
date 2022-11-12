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
int search_inode_dir(int img, struct ext2_super_block* p_sb, uint32_t blk_idx,
                     const char* path);
int search_inode_ind(int img, struct ext2_super_block* p_sb, uint32_t blk_idx,
                     const char* path);
int search_inode_dind(int img, struct ext2_super_block* p_sb, uint32_t blk_idx,
                      const char* path);
int64_t search_inode(int img, struct ext2_super_block* p_sb, long inode_nr,
                     const char* path);

int dump_file(int img, const char* path, int out) {
  long res;
  struct ext2_super_block sb;
  if ((res = read_sb(img, &sb)) < 0) {
    return res;
  }
  if ((res = search_inode(img, &sb, 2, path)) < 0) {
    return res;
  }
  if ((res = send_file(img, out, &sb, res)) < 0) {
    return res;
  }
  return 0;
}

int read_sb(int img, struct ext2_super_block* sb) {
  if (pread(img, sb, sizeof(struct ext2_super_block), SUPERBLOCK_OFFSET) < 0) {
    return -errno;
  }
  return 0;
}

int read_gd(int img, struct ext2_super_block* sb, struct ext2_group_desc* bg,
            size_t inode_nr) {
  size_t bg_number = (inode_nr - 1) / sb->s_inodes_per_group;
  size_t block_size = EXT2_BLOCK_SIZE(sb);
  if (pread(img, bg, sizeof(struct ext2_group_desc),
            (sb->s_first_data_block + 1) * block_size +
                sizeof(struct ext2_group_desc) * bg_number) < 0) {
    return -errno;
  }
  return 0;
}

int read_inode(int img, struct ext2_super_block* p_sb,
               struct ext2_inode* p_inode, long inode_nr) {
  struct ext2_group_desc gd;
  int res = 0;
  if ((res = read_gd(img, p_sb, &gd, inode_nr)) < 0) {
    return res;
  }
  long inode_idx = (inode_nr - 1) % p_sb->s_inodes_per_group;
  off_t inode_offset = gd.bg_inode_table * EXT2_BLOCK_SIZE(p_sb) +
                       inode_idx * p_sb->s_inode_size;
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
  uint32_t* ind_blk = dind_blk + blk_sz;
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

int search_inode_dir(int img, struct ext2_super_block* p_sb, uint32_t blk_idx,
                     const char* path) {
  if (blk_idx == 0) {
    return -ENOENT;
  }
  const int blk_sz = EXT2_BLOCK_SIZE(p_sb);
  char* buf = malloc(blk_sz);
  if (pread(img, buf, blk_sz, blk_idx * blk_sz) < 0) {
    free(buf);
    return -errno;
  }
  struct ext2_dir_entry_2* p_de = (struct ext2_dir_entry_2*)buf;
  while ((char*)p_de - buf < blk_sz) {
    if (p_de->inode == 0) {
      return -ENOENT;
    }
    const char* next_dir = strchr(path, '/');
    if (next_dir == NULL) {
      next_dir = path + strlen(path);
    }
    if (next_dir - path == p_de->name_len &&
        !strncmp(path, p_de->name, p_de->name_len)) {
      long inode_nr = p_de->inode;
      if (next_dir[0] != '/') {
        free(buf);
        return inode_nr;
      }
      if (p_de->file_type == EXT2_FT_DIR) {
        free(buf);
        return search_inode(img, p_sb, inode_nr, next_dir);
      }
      free(buf);
      return -ENOTDIR;
    }
    p_de = (struct ext2_dir_entry_2*)((char*)p_de + p_de->rec_len);
  }
  free(buf);
  return 0;
}

int search_inode_ind(int img, struct ext2_super_block* p_sb, uint32_t blk_idx,
                     const char* path) {
  const int blk_sz = EXT2_BLOCK_SIZE(p_sb);
  size_t ub = blk_sz / sizeof(uint32_t);
  uint32_t* redir = malloc(blk_sz);
  if (blk_idx == 0) {
    free(redir);
    return -ENOENT;
  }
  int res = pread(img, redir, blk_sz, blk_sz * blk_idx);
  if (res < 0) {
    free(redir);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if ((res = search_inode_dir(img, p_sb, redir[i], path))) {
      free(redir);
      return res;
    }
  }
  free(redir);
  return 0;
}

int search_inode_dind(int img, struct ext2_super_block* p_sb, uint32_t blk_idx,
                      const char* path) {
  int res = 0;
  const int block_size = EXT2_BLOCK_SIZE(p_sb);
  uint32_t* dind_blk = malloc(block_size);
  size_t ub = block_size / sizeof(uint32_t);
  if (blk_idx == 0) {
    return -ENOENT;
  }
  if ((res = pread(img, dind_blk, block_size, block_size * blk_idx)) < 0) {
    free(dind_blk);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if ((res = search_inode_ind(img, p_sb, dind_blk[i], path)) != 0) {
      free(dind_blk);
      return res;
    }
  }
  free(dind_blk);
  return 0;
}

int64_t search_inode(int img, struct ext2_super_block* p_sb, long inode_nr,
                     const char* path) {
  int64_t res = 0;
  struct ext2_inode inode;
  if (inode_nr == 0) {
    return -ENOENT;
  }
  if (path[0] != '/') {
    return inode_nr;
  }
  ++path;
  if ((res = read_inode(img, p_sb, &inode, inode_nr)) < 0) {
    return res;
  }
  for (size_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
    if ((res = search_inode_dir(img, p_sb, inode.i_block[i], path))) {
      return res;
    }
  }
  if ((res =
           search_inode_ind(img, p_sb, inode.i_block[EXT2_IND_BLOCK], path))) {
    return res;
  }
  if ((res = search_inode_dind(img, p_sb, inode.i_block[EXT2_DIND_BLOCK],
                               path))) {
    return res;
  }
  return -ENOENT;
}
