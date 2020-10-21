#include "rfsCommon.h"

static void fullpath_A(char *fpath, const char *path) {
    strcpy(fpath, ladoA);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will break here
}

static void make_fullpath(char *fpath, const char *prepath, const char *path) {
    strcpy(fpath, prepath);
    strncat(fpath, path, PATH_MAX); //TODO fix off-by-one error here, see strncat(3)
}


int hello_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));
    char fullpath[PATH_MAX];
    fullpath_A(fullpath, path);
    errno = 0;
    lstat(fullpath, stbuf);
    return -errno;
}

int hello_mknod_AB(const char *path, mode_t mode, const char *prepath) {
    assert(mode & S_IFREG);
    char fullpath[PATH_MAX];
    make_fullpath(fullpath, prepath, path);
    int ret = mknod(fullpath, mode, 0);
    int errsv = errno;
    if (ret < 0) {
        return -errsv;
    }
    return 0;
}

int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi,
                  enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;
    char fullpath[2048];
    fullpath_A(fullpath, path);
    DIR *mydir = opendir(fullpath);
    struct dirent *d;
    errno = 0;
    while (d = readdir(mydir)) {
        assert(!errno);
        filler(buf, d->d_name, NULL, 0, 0);
    }
    closedir(mydir);
    return 0;
}

int hello_readlink(const char *path, char *buf, size_t bufsize) {
    char fullpath[PATH_MAX];
    fullpath_A(fullpath, path);
    errno = 0;
    int res = readlink(fullpath, buf, bufsize);
    int errsv = errno;
    buf[res] = '\0';
    if (res < 0) {
        return -errsv;
    }
    return 0;
}

//hello_mkdir_local
int hello_mkdir_AB(const char *path, mode_t mode, const char *prepath) {
    mode = mode | S_IFDIR;
    char fullpath[PATH_MAX];
    make_fullpath(fullpath, prepath, path);
    errno = 0;
    int ret = mkdir(fullpath, mode);
    int errsv = errno;
    if (ret < 0) {
        return -errsv;
    }
    return 0;
}


int hello_unlink_AB(const char *path, const char *prepath) {
    char fullpath[PATH_MAX];
    make_fullpath(fullpath, prepath, path);
    errno = 0;
    int ret = unlink(fullpath);
    int errsv = errno;
    if (ret < 0) {
        return -errsv;
    }
    return 0;
}


int hello_rmdir_AB(const char *path, const char *prepath) {
    char fullpath[PATH_MAX];
    make_fullpath(fullpath, prepath, path);
    errno = 0;
    int ret = rmdir(fullpath);
    int errsv = errno;
    if (ret < 0) {
        return -errsv;
    }
    return 0;
}


int hello_rename_AB(const char *src_path, const char *dst_path, const char *prepath) {
    char src_fullpath[PATH_MAX];
    char dst_fullpath[PATH_MAX];
    make_fullpath(src_fullpath, prepath, src_path);
    make_fullpath(dst_fullpath, prepath, dst_path);
    errno = 0;
    int ret = rename(src_fullpath, dst_fullpath);
    int errsv = errno;
    if (ret < 0) {
        return -errsv;
    }
    return 0;
}

int hello_open_AB(const char *path, const int flags, int *fh, const char *prepath) {
    char fullpath[PATH_MAX];
    make_fullpath(fullpath, prepath, path);
    errno = 0;
    int ret = open(fullpath, flags);
    printf("DEBUG: Open returned %d \n", ret);
    int errsv = errno;
    if (ret < 0) {
        *fh = -1;
        return -errsv;
    }
    *fh = ret;
    return 0;
}


int hello_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int ret = pread(((myhack_t) fi->fh).inner.a, buf, size, offset);
    if (ret < 0) {
        return -errno;
    }
    if (ret != size) {
        printf("WARNING: pread gave us %d out of requested %d bytes.\n", ret, (int) size);
    }
    return ret;
}

int hello_release_AB(const int *fh) {
    int ret = close(*fh);
    if (ret < 0) {
        return -errno;
    }
    return ret;
}

int hello_write_AB(const char *buf, size_t size, off_t offset, const int *fh) {
    size_t orig_size = size;
    printf("DEBUG: WRITE op with %zu byte buffer\n", orig_size);
    int retval;
    while (size) {
        retval = pwrite(*fh, buf, size, offset);
        assert(retval);  // it'd be weird if pwrite transfers 0 bytes without error (?)
        if (retval < 0) {
            int errsv = errno;
            return -errsv;
        }
        offset += retval;
        size -= retval;
    }
    return orig_size;
}


void cnc(int val) {
    if (val == -1) {
        printf("***************************Error num %d\n", errno);
    }
}