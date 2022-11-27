#include "solution.h"

#include <assert.h>
#include <ext2fs/ext2fs.h>
#include <fuse.h>
#include <linux/limits.h>

int ext2_img;
struct ext2_super_block ext2_sb;

static int ext2_fuse_write(const char *req, const char *buf, size_t size,
                           off_t off, struct fuse_file_info *fi) {
  (void)req;
  (void)buf;
  (void)size;
  (void)off;
  (void)fi;
  return -EROFS;
}

static int ext2_fuse_create(const char *path, mode_t mode,
                            struct fuse_file_info *fi) {
  (void)path;
  (void)mode;
  (void)fi;
  return -EROFS;
}

static int ext2_fuse_write_buf(const char *path, struct fuse_bufvec *buf,
                               off_t offset, struct fuse_file_info *fi) {
  (void)path;
  (void)buf;
  (void)offset;
  (void)fi;
  return -EROFS;
}

static int ext2_fuse_mkdir(const char *path, mode_t mode) {
  (void)path;
  (void)mode;
  return -EROFS;
}

static int ext2_fuse_mknod(const char *path, mode_t mode, dev_t rdev) {
  (void)path;
  (void)mode;
  (void)rdev;
  return -EROFS;
}

int read_sb(int img, struct ext2_super_block *sb) {
  if (pread(img, sb, sizeof(struct ext2_super_block), SUPERBLOCK_OFFSET) < 0) {
    return -errno;
  }
  return 0;
}

int read_gd(int img, struct ext2_super_block *sb, struct ext2_group_desc *bg,
            size_t inode_nr) {
  size_t gd_idx = (inode_nr - 1) / sb->s_inodes_per_group;
  size_t block_size = EXT2_BLOCK_SIZE(sb);
  size_t gd_offset = (sb->s_first_data_block + 1) * block_size +
                     sizeof(struct ext2_group_desc) * gd_idx;
  if (pread(img, bg, sizeof(struct ext2_group_desc), gd_offset) < 0) {
    return -errno;
  }
  return 0;
}

int read_inode(int img, struct ext2_super_block *p_sb,
               struct ext2_inode *p_inode, long inode_nr) {
  struct ext2_group_desc gd;
  int res = 0;
  size_t inode_idx = (inode_nr - 1) % p_sb->s_inodes_per_group;
  if ((res = read_gd(img, p_sb, &gd, inode_nr)) < 0) {
    return res;
  }
  size_t inode_offset = gd.bg_inode_table * EXT2_BLOCK_SIZE(p_sb) +
                        inode_idx * p_sb->s_inode_size;
  if (pread(img, p_inode, sizeof(struct ext2_inode), inode_offset)) {
    return -errno;
  }
  return 0;
}

static void cp_inode_stat(int inode_nr, struct ext2_inode *p_inode,
                          struct stat *p_stat) {
  p_stat->st_ino = inode_nr;
  p_stat->st_mode = p_inode->i_mode;
  p_stat->st_nlink = p_inode->i_links_count;
  p_stat->st_uid = p_inode->i_uid;
  p_stat->st_gid = p_inode->i_gid;
  p_stat->st_size = p_inode->i_size;
  p_stat->st_blksize = EXT2_BLOCK_SIZE(&ext2_sb);
  p_stat->st_blocks = p_inode->i_blocks;
  p_stat->st_atime = p_inode->i_atime;
  p_stat->st_mtime = p_inode->i_mtime;
  p_stat->st_ctime = p_inode->i_ctime;
}

int64_t search_inode(int img, struct ext2_super_block *p_sb, long inode_nr,
                     const char *path);

int search_inode_dir(int img, struct ext2_super_block *p_sb, uint32_t blk_idx,
                     const char *path) {
  if (blk_idx == 0) {
    return -ENOENT;
  }
  int32_t blk_sz = EXT2_BLOCK_SIZE(p_sb);
  char *buf = malloc(blk_sz);
  char *cur = buf;
  if (pread(img, buf, blk_sz, blk_idx * blk_sz) < 0) {
    free(buf);
    return -errno;
  }
  while (cur - buf < blk_sz) {
    struct ext2_dir_entry_2 *p_de = (struct ext2_dir_entry_2 *)cur;
    if (p_de->inode == 0) {
      return -ENOENT;
    }
    const char *next_dir = path;
    while (*next_dir && *next_dir != '/') {
      ++next_dir;
    }
    if (next_dir - path == p_de->name_len &&
        !strncmp(path, p_de->name, p_de->name_len)) {
      long inode_nr = p_de->inode;
      if (next_dir[0] != '/') {
        free(buf);
        return (int)inode_nr;
      }
      if (p_de->file_type == EXT2_FT_DIR) {
        free(buf);
        return (int)search_inode(img, p_sb, inode_nr, next_dir);
      }
      free(buf);
      return -ENOTDIR;
    }
    cur += p_de->rec_len;
  }
  free(buf);
  return 0;
}

int search_inode_ind(int img, struct ext2_super_block *p_sb, uint32_t blk_idx,
                     const char *path) {
  const int blk_sz = EXT2_BLOCK_SIZE(p_sb);
  size_t ub = blk_sz / sizeof(uint32_t);
  uint32_t *redir = malloc(blk_sz);
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

int search_inode_dind(int img, struct ext2_super_block *p_sb, uint32_t blk_idx,
                      const char *path) {
  int res = 0;
  const int block_size = EXT2_BLOCK_SIZE(p_sb);
  uint32_t *dind_blk = malloc(block_size);
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

int64_t search_inode(int img, struct ext2_super_block *p_sb, long inode_nr,
                     const char *path) {
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

static int ext2_fuse_getattr(const char *path, struct stat *p_stat,
                             struct fuse_file_info *fi) {
  (void)fi;
  memset(p_stat, 0, sizeof(struct stat));
  int inode_nr;
  int res;
  if ((inode_nr = (int)search_inode(ext2_img, &ext2_sb, EXT2_ROOT_INO, path)) <
      0) {
    return -ENOENT;
  }
  struct ext2_inode inode;
  if ((res = read_inode(ext2_img, &ext2_sb, &inode, inode_nr)) < 0) {
    return res;
  }
  cp_inode_stat(inode_nr, &inode, p_stat);
  return 0;
}

static int ext2_fuse_open(const char *path, struct fuse_file_info *fi) {
  if ((fi->flags & O_ACCMODE) != O_RDONLY) {
    return -EROFS;
  }
  if (search_inode(ext2_img, &ext2_sb, EXT2_ROOT_INO, path) < 0) {
    return -ENOENT;
  }
  return 0;
}

static int read_dirents_from_blk(const char *blk_buf, ssize_t to_read,
                                 void *buf, fuse_fill_dir_t filler) {
  const char *cur = blk_buf;
  struct ext2_dir_entry_2 *p_de;
  struct ext2_inode inode;
  struct stat stat;
  char path[PATH_MAX];
  int res;
  while (to_read && cur - blk_buf < to_read) {
    p_de = (struct ext2_dir_entry_2 *)cur;
    if (!p_de->inode) {
      return 0;
    }
    memset(path, 0, PATH_MAX);
    strncpy(path, p_de->name, p_de->name_len);
    if ((res = read_inode(ext2_img, &ext2_sb, &inode, p_de->inode)) < 0) {
      return res;
    }
    cp_inode_stat(p_de->inode, &inode, &stat);
    filler(buf, path, &stat, 0, 0);
    cur += p_de->rec_len;
    to_read -= p_de->rec_len;
  }
  return 0;
}

static int read_blk(char *blk_buf, char *buf, off_t *offset, size_t *read,
                    size_t *left_read, uint32_t blk_sz) {
  if (*offset >= blk_sz) {
    *offset -= blk_sz;
    return 1;
  }
  size_t to_read;
  if (*offset > 0) {
    to_read = blk_sz - (size_t)*offset;
    if (*left_read < to_read) {
      to_read = *left_read;
    }
    memcpy(buf + *read, blk_buf + *offset, to_read);
    *read += to_read;
    *offset = 0;
    if (!(*left_read -= to_read)) {
      return 0;
    }
    return 1;
  }
  to_read = *left_read > blk_sz ? blk_sz : *left_read;
  memcpy(buf + *read, blk_buf, to_read);
  *read += to_read;
  if (!(*left_read -= to_read)) {
    return 1;
  }
  return 0;
}

static int ext2_fuse_readdir(const char *path, void *buf,
                             fuse_fill_dir_t filler, off_t offset,
                             struct fuse_file_info *fi,
                             enum fuse_readdir_flags flags) {
  (void)offset;
  (void)fi;
  (void)flags;
  int inode_nr;
  int res;
  uint32_t blk_sz = EXT2_BLOCK_SIZE(&ext2_sb);
  char *blk_buf = malloc(blk_sz);
  uint32_t *ind_blk_buf;
  uint32_t *dind_blk_buf;
  uint32_t left_read;
  uint32_t to_read;
  uint32_t ub = blk_sz / 4;
  struct ext2_inode inode;
  if ((inode_nr = (int)search_inode(ext2_img, &ext2_sb, EXT2_ROOT_INO, path)) <
      0) {
    free(blk_buf);
    return -ENOENT;
  }
  if ((res = read_inode(ext2_img, &ext2_sb, &inode, inode_nr)) < 0) {
    free(blk_buf);
    return res;
  }
  left_read = inode.i_size;
  for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
    if (!inode.i_block[i]) {
      free(blk_buf);
      return 0;
    }
    to_read = left_read > blk_sz ? blk_sz : left_read;
    if (pread(ext2_img, blk_buf, to_read, inode.i_block[i] * blk_sz) < 0) {
      free(blk_buf);
      return -errno;
    }
    // body
    if ((res = read_dirents_from_blk(blk_buf, to_read, buf, filler)) < 0) {
      free(blk_buf);
      return res;
    }
    //
    left_read -= to_read;
    if (!left_read) {
      free(blk_buf);
      return 0;
    }
  }

  ind_blk_buf = malloc(blk_sz);
  if (pread(ext2_img, ind_blk_buf, blk_sz,
            inode.i_block[EXT2_IND_BLOCK] * blk_sz) < 0) {
    free(ind_blk_buf);
    free(blk_buf);
    return -errno;
  }
  for (uint32_t i = 0; i < ub; ++i) {
    to_read = left_read > blk_sz ? blk_sz : left_read;
    if (!ind_blk_buf[i]) {
      free(ind_blk_buf);
      free(blk_buf);
      return 0;
    }
    if (pread(ext2_img, blk_buf, to_read, ind_blk_buf[i] * blk_sz) < 0) {
      free(ind_blk_buf);
      free(blk_buf);
      return -errno;
    }
    // body
    if ((res = read_dirents_from_blk(blk_buf, to_read, buf, filler)) < 0) {
      free(ind_blk_buf);
      free(blk_buf);
      return res;
    }
    //
    left_read -= to_read;
    if (!left_read) {
      free(ind_blk_buf);
      free(blk_buf);
      return 0;
    }
  }

  dind_blk_buf = malloc(blk_sz);
  if (pread(ext2_img, dind_blk_buf, blk_sz,
            inode.i_block[EXT2_DIND_BLOCK] * blk_sz) < 0) {
    free(dind_blk_buf);
    free(ind_blk_buf);
    free(blk_buf);
    return -errno;
  }
  for (uint32_t i = 0; i < ub; ++i) {
    if (!dind_blk_buf[i]) {
      free(dind_blk_buf);
      free(ind_blk_buf);
      free(blk_buf);
      return 0;
    }
    if (pread(ext2_img, ind_blk_buf, blk_sz, dind_blk_buf[i] * blk_sz) < 0) {
      free(dind_blk_buf);
      free(ind_blk_buf);
      free(blk_buf);
      return -errno;
    }
    for (uint32_t j = 0; j < ub; ++j) {
      to_read = left_read > blk_sz ? blk_sz : left_read;
      if (!ind_blk_buf[j]) {
        free(dind_blk_buf);
        free(ind_blk_buf);
        free(blk_buf);
        return 0;
      }
      if (pread(ext2_img, blk_buf, to_read, ind_blk_buf[j] * blk_sz) < 0) {
        free(dind_blk_buf);
        free(ind_blk_buf);
        free(blk_buf);
        return -errno;
      }
      // body
      if ((res = read_dirents_from_blk(blk_buf, to_read, buf, filler)) < 0) {
        free(dind_blk_buf);
        free(ind_blk_buf);
        free(blk_buf);
        return res;
      }
      //
      left_read -= to_read;
      if (!left_read) {
        free(dind_blk_buf);
        free(ind_blk_buf);
        free(blk_buf);
        return 0;
      }
    }
  }

  free(dind_blk_buf);
  free(ind_blk_buf);
  free(blk_buf);
  return 0;
}

static int ext2_fuse_read(const char *path, char *buf, size_t size,
                          off_t offset, struct fuse_file_info *fi) {
  (void)fi;
  int inode_nr;
  int res;
  size_t blk_sz = EXT2_BLOCK_SIZE(&ext2_sb);
  char *blk_buf = malloc(blk_sz);
  uint32_t *ind_blk_buf;
  uint32_t *dind_blk_buf;
  size_t left_read;
  size_t to_read;
  size_t ub = blk_sz / 4;
  struct ext2_inode inode;
  size_t read = 0;
  if ((inode_nr = (int)search_inode(ext2_img, &ext2_sb, EXT2_ROOT_INO, path)) <
      0) {
    free(blk_buf);
    return -ENOENT;
  }
  if ((res = read_inode(ext2_img, &ext2_sb, &inode, inode_nr)) < 0) {
    free(blk_buf);
    return res;
  }
  left_read = inode.i_size;
  for (size_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
    to_read = left_read > blk_sz ? blk_sz : left_read;
    if (pread(ext2_img, blk_buf, to_read, inode.i_block[i] * blk_sz) < 0) {
      free(blk_buf);
      return -errno;
    }
    //
    if ((res = read_blk(blk_buf, buf, &offset, &read, &size, blk_sz)) <= 0) {
      free(blk_buf);
      return res;
    }
    //
    left_read -= to_read;
    if (!left_read) {
      free(blk_buf);
      return 0;
    }
  }

  ind_blk_buf = malloc(blk_sz);
  if (pread(ext2_img, ind_blk_buf, blk_sz,
            inode.i_block[EXT2_IND_BLOCK] * blk_sz) < 0) {
    free(ind_blk_buf);
    free(blk_buf);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    to_read = left_read > blk_sz ? blk_sz : left_read;
    if (pread(ext2_img, blk_buf, to_read, ind_blk_buf[i] * blk_sz) < 0) {
      free(ind_blk_buf);
      free(blk_buf);
      return -errno;
    }
    //
    if ((res = read_blk(blk_buf, buf, &offset, &read, &size, blk_sz)) <= 0) {
      free(ind_blk_buf);
      free(blk_buf);
      return res;
    }
    //
    left_read -= to_read;
    if (!left_read) {
      free(ind_blk_buf);
      free(blk_buf);
      return 0;
    }
  }

  dind_blk_buf = malloc(blk_sz);
  if (pread(ext2_img, dind_blk_buf, blk_sz,
            inode.i_block[EXT2_DIND_BLOCK] * blk_sz) < 0) {
    free(dind_blk_buf);
    free(ind_blk_buf);
    free(blk_buf);
    return -errno;
  }
  for (size_t i = 0; i < ub; ++i) {
    if (pread(ext2_img, ind_blk_buf, blk_sz, dind_blk_buf[i] * blk_sz) < 0) {
      free(dind_blk_buf);
      free(ind_blk_buf);
      free(blk_buf);
      return -errno;
    }
    for (size_t j = 0; j < ub; ++j) {
      to_read = left_read > blk_sz ? blk_sz : left_read;
      if (pread(ext2_img, blk_buf, to_read, ind_blk_buf[j] * blk_sz) < 0) {
        free(dind_blk_buf);
        free(ind_blk_buf);
        free(blk_buf);
        return -errno;
      }
      //
      if ((res = read_blk(blk_buf, buf, &offset, &read, &size, blk_sz)) <= 0) {
        free(dind_blk_buf);
        free(ind_blk_buf);
        free(blk_buf);
        return res;
      }
      //
      left_read -= to_read;
      if (!left_read) {
        free(dind_blk_buf);
        free(ind_blk_buf);
        free(blk_buf);
        return 0;
      }
    }
  }

  free(dind_blk_buf);
  free(ind_blk_buf);
  free(blk_buf);
  return 0;
}

static const struct fuse_operations ext2_ops = {
    .write = ext2_fuse_write,
    .create = ext2_fuse_create,
    .write_buf = ext2_fuse_write_buf,
    .mkdir = ext2_fuse_mkdir,
    .mknod = ext2_fuse_mknod,
    .getattr = ext2_fuse_getattr,
    .readdir = ext2_fuse_readdir,
    .open = ext2_fuse_open,
    .read = ext2_fuse_read,
};

int ext2fuse(int img, const char *mntp) {
  //   assert(0);
  ext2_img = img;
  if (read_sb(img, &ext2_sb) < 0) {
    return -1;
  }

  char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
  return fuse_main(3, argv, &ext2_ops, NULL);
}
