#include <solution.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <fuse.h>

static const char* filename = "hello";
static const char* content_format = "hello, %d\n";

static int hello_getattr(const char* path, struct stat* st,
                         struct fuse_file_info* fi) {
    (void) fi;
    memset(st, 0, sizeof(struct stat));
    int res = 0;
    if (!strcmp(path, "/")) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
    } else if (!strcmp(path + 1, filename)) {
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
        st->st_size = strlen(content_format) + 10;
    } else {
        res = -ENOENT;
    }
    return res;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags) {
        (void) offset;
        (void) fi;
        (void) flags;
 
        if (strcmp(path, "/")) {
            return -ENOENT;
        } 

        filler(buf, ".", NULL, 0, 0);
        filler(buf, "..", NULL, 0, 0);
        filler(buf, filename, NULL, 0, 0);
 
        return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi) {
        if (strcmp(path + 1, filename)) {
            return -EROFS;
        }

        if ((fi->flags & O_ACCMODE) != O_RDONLY) {
            return -EROFS;
        }
 
        return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
        long int len;
        (void) fi;
        if(strcmp(path + 1, filename)) {
            return -ENOENT;
        }

        struct fuse_context* context = fuse_get_context();
        char output[100];
        sprintf(output, content_format, context->pid);
        len = strlen(output);

        if (offset < len) {
            if (offset + (long) size > len) {
                size = len - offset;
            }
            memcpy(buf, output + offset, size);
        } else {
            size = 0;
        }
 
        return size;
}

static int hello_write(const char* req, const char *buf, size_t size,
                       off_t off, struct fuse_file_info *fi) {
    (void) req;
    (void) buf;
    (void) size;
    (void) off;
    (void) fi;
    return -EROFS;
}

static int hello_create(const char *path, mode_t mode,
                        struct fuse_file_info *fi) {
    (void) path;
    (void) mode;
    (void) fi;
    return -EROFS;
}

static int hello_write_buf(const char *path, struct fuse_bufvec *buf,
                           off_t offset, struct fuse_file_info *fi) {
    (void) path;
    (void) buf;
    (void) offset;
    (void) fi;
    return -EROFS;
}

static int hello_mkdir(const char *path, mode_t mode) {
    (void) path;
    (void) mode;
    return -EROFS;
}

static int hello_mknod(const char *path, mode_t mode, dev_t rdev) {
    (void) path;
    (void) mode;
    (void) rdev;
    return -EROFS;
}

static const struct fuse_operations hellofs_ops = {
	    .getattr = hello_getattr,
        .readdir = hello_readdir,
        .open = hello_open,
        .read = hello_read,
        .write = hello_write,
        .create = hello_create,
        .write_buf = hello_write_buf,
        .mkdir = hello_mkdir,
        .mknod = hello_mknod,
};

int helloworld(const char *mntp) {
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}

