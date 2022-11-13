#include <errno.h>
#include <limits.h>
#include <linux/limits.h>
#include <solution.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define _STRUCT_TIMESPEC 1
#define __timespec_defined
#include <ntfs-3g/attrib.h>
#include <ntfs-3g/dir.h>
#include <ntfs-3g/types.h>
#include <ntfs-3g/volume.h>

int fd_to_path(int fd, char *out);
int unmount(ntfs_volume *p_ntfs_volume);
int ntfs_read(ntfs_attr *p_ntfs_attr, int out, u32 blk_sz);
int ntfs_read_mst(ntfs_attr *p_ntfs_attr, int out, u32 blk_sz);
ntfs_inode *ntfs_pathname_to_inode_enotdir(ntfs_volume *vol, ntfs_inode *parent,
                                           const char *pathname);

int dump_file(int img, const char *path, int out) {
  char img_file[PATH_MAX];
  ntfs_volume *p_ntfs_volume = NULL;
  ntfs_inode *p_ntfs_inode = NULL;
  ntfs_attr *p_ntfs_attr = NULL;
  u32 blk_sz = 1 << 12;
  int res = 0;
  int err = 0;
  if ((res = fd_to_path(img, img_file)) < 0) {
    return res;
  }
  if (!(p_ntfs_volume = ntfs_mount(img_file, NTFS_MNT_RDONLY))) {
    return -errno;
  }
  if (!(p_ntfs_inode = ntfs_pathname_to_inode(p_ntfs_volume, NULL, path))) {
    err = -errno;
    unmount(p_ntfs_volume);
    return err;
  }
  if (!(p_ntfs_attr = ntfs_attr_open(p_ntfs_inode, AT_DATA, AT_UNNAMED, 0))) {
    err = -errno;
    ntfs_inode_close(p_ntfs_inode);
    unmount(p_ntfs_volume);
    return err;
  }

  if (p_ntfs_inode->mft_no < 2 &&
      (res = ntfs_read_mst(p_ntfs_attr, out, p_ntfs_volume->mft_record_size)) <
          0) {
    ntfs_attr_close(p_ntfs_attr);
    ntfs_inode_close(p_ntfs_inode);
    unmount(p_ntfs_volume);
    return res;
  }
  if ((res = ntfs_read(p_ntfs_attr, out, blk_sz)) < 0) {
    ntfs_attr_close(p_ntfs_attr);
    ntfs_inode_close(p_ntfs_inode);
    unmount(p_ntfs_volume);
    return res;
  }
  ntfs_attr_close(p_ntfs_attr);
  ntfs_inode_close(p_ntfs_inode);
  return unmount(p_ntfs_volume);
}

int fd_to_path(int fd, char *out) {
  char fd_path[PATH_MAX];
  sprintf(fd_path, "/proc/self/fd/%d", fd);
  ssize_t res = readlink(fd_path, out, PATH_MAX - 1);
  if (res < 0) {
    return -errno;
  }
  out[res] = '\0';
  return 0;
}

int unmount(ntfs_volume *p_ntfs_volume) {
  while (ntfs_umount(p_ntfs_volume, FALSE) == -1) {
    if (errno != EAGAIN) {
      return -errno;
    }
  }
  return 0;
}

int ntfs_read(ntfs_attr *p_ntfs_attr, int out, u32 blk_sz) {
  char *buf = malloc(blk_sz);
  s64 read = 0;
  s64 offset = 0;
  while (1) {
    read = ntfs_attr_pread(p_ntfs_attr, offset, blk_sz, buf);
    if (read < 0) {
      free(buf);
      return -errno;
    }
    if (!read) {
      free(buf);
      return 0;
    }
    if (write(out, buf, read) < read) {
      free(buf);
      return -errno;
    }
    offset += read;
  }
}

int ntfs_read_mst(ntfs_attr *p_ntfs_attr, int out, u32 blk_sz) {
  char *buf = malloc(blk_sz);
  s64 read_blks = 0;
  s64 read_bytes = 0;
  s64 offset = 0;
  while (1) {
    read_blks = ntfs_attr_mst_pread(p_ntfs_attr, offset, 1, blk_sz, buf);
    if (read_blks < 0) {
      free(buf);
      return -errno;
    }
    if (!read_blks) {
      free(buf);
      return 0;
    }
    read_bytes = read_blks * blk_sz;
    if (write(out, buf, read_bytes) < read_bytes) {
      free(buf);
      return -errno;
    }
    offset += read_bytes;
  }
}

ntfs_inode *ntfs_pathname_to_inode_enotdir(ntfs_volume *vol, ntfs_inode *parent,
                                           const char *pathname) {
  u64 inum;
  int len, err = 0;
  char *p, *q;
  ntfs_inode *ni;
  ntfs_inode *result = NULL;
  ntfschar *unicode = NULL;
  char *ascii = NULL;

  if (!vol || !pathname) {
    errno = EINVAL;
    return NULL;
  }

  ntfs_log_trace("path: '%s'\n", pathname);

  ascii = strdup(pathname);
  if (!ascii) {
    ntfs_log_error("Out of memory.\n");
    err = ENOMEM;
    goto out;
  }

  p = ascii;
  /* Remove leading /'s. */
  while (p && *p && *p == PATH_SEP) p++;
  if (parent) {
    ni = parent;
  } else {
    ni = ntfs_inode_open(vol, FILE_root);
    if (!ni) {
      ntfs_log_debug(
          "Couldn't open the inode of the root "
          "directory.\n");
      err = EIO;
      result = (ntfs_inode *)NULL;
      goto out;
    }
  }

  while (p && *p) {
    /* Find the end of the first token. */
    q = strchr(p, PATH_SEP);
    if (q != NULL) {
      *q = '\0';
    }
    len = ntfs_mbstoucs(p, &unicode);
    if (len < 0) {
      ntfs_log_perror(
          "Could not convert filename to Unicode:"
          " '%s'",
          p);
      err = errno;
      goto close;
    } else if (len > NTFS_MAX_NAME_LEN) {
      err = ENAMETOOLONG;
      goto close;
    }
    inum = ntfs_inode_lookup_by_name(ni, unicode, len);
    if (inum == (u64)-1) {
      ntfs_log_debug(
          "Couldn't find name '%s' in pathname "
          "'%s'.\n",
          p, pathname);
      err = ENOENT;
      goto close;
    }

    if (ni != parent)
      if (ntfs_inode_close(ni)) {
        err = errno;
        goto out;
      }

    inum = MREF(inum);
    ni = ntfs_inode_open(vol, inum);
    if (!ni) {
      ntfs_log_debug("Cannot open inode %llu: %s.\n", (unsigned long long)inum,
                     p);
      err = EIO;
      goto close;
    }

    free(unicode);
    unicode = NULL;

    if (q) {
      *q++ = PATH_SEP; /* JPA */
    } else if (!(ni->mrec->flags & MFT_RECORD_IS_DIRECTORY)) {
      err = ENOTDIR;
      goto close;
    }
    p = q;
    while (p && *p && *p == PATH_SEP) p++;
  }

  result = ni;
  ni = NULL;
close:
  if (ni && (ni != parent))
    if (ntfs_inode_close(ni) && !err) err = errno;
out:
  free(ascii);
  free(unicode);
  if (err) errno = err;
  return result;
}
