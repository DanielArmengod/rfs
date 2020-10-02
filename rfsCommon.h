#ifndef ELMEUTFG_RFSCOMMON_H
#define ELMEUTFG_RFSCOMMON_H
#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct fuse_file_info ffi;

extern const char* srcpnt;
extern const char* altpnt;

enum op_enum {
    OP_MKDIR,
    OP_UNLINK,
    OP_RMDIR,
    OP_RENAME,
    OP_OPEN,
    OP_RELEASE,
    OP_WRITE,
};

typedef union {
    uint64_t real;
    struct {
        int a;
        int b;
    } inner;
} myhack_t;

typedef struct {
    const char *tgt_path;
    mode_t mode;
} op_mkdir_t;
typedef struct {
    const char *tgt_path;
} op_unlink_t;
typedef struct {
    const char *tgt_path;
} op_rmdir_t;
typedef struct {
    const char *src_path;
    const char *dst_path;
} op_rename_t;
typedef struct {
    const char *path;
    struct fuse_file_info *fi;
} op_open_t;
typedef struct {
    struct fuse_file_info *fi;
} op_release_t;
typedef struct {
    ffi *fi;
    const char *buf;
    size_t size;
    off_t offset;
} op_write_t;

typedef union {
    op_mkdir_t op_mkdir;
    op_unlink_t op_unlink;
    op_rmdir_t op_rmdir;
    op_rename_t op_rename;
    op_open_t op_open;
    op_release_t op_release;
    op_write_t op_write;
} op_union;

typedef struct {
    enum op_enum op;
    int result;
    int done;
    pthread_mutex_t mut;
    pthread_cond_t cond;
    op_union data;
} op_t;

int hello_getattr(const char *path, struct stat *stbuf,
                  struct fuse_file_info *fi);
int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi,
                  enum fuse_readdir_flags flags);
int hello_readlink(const char *path, char *buf, size_t bufsize);
int hello_mkdir_AB(const char *path, mode_t mode, const char *prepath);
int hello_unlink_AB(const char *path, const char *prepath);
int hello_rmdir_AB(const char *path, const char *prepath);
int hello_rename_AB(const char *src_path, const char *dst_path, const char *prepath);
int hello_open_AB(const char *path, const int flags, int *fh, const char *prepath);
int hello_read(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi);
int hello_release_AB(const int *fh);
int hello_write_AB(const char *buf, size_t size, off_t offset, const int *fh);


//------------------------------- BEGIN serialized operations --------------------------------------------------

/* In the following structures, _L denotes a field which the length of the binary contents that follow after the struct.
   If there is more than one _L, contents are immediately after the other. Example:
    start of ser_t union
        buf
    start of ser_*_t data structure:
        buf + ser_t->data.ser_*_t
    start of 1st of rest of data buffers:
        buf + sizeof(ser_t)
    start of Nth of rest of data buffers:
        buf + sizeof(ser_t) + SUM(sizeof(Nth_buffer), 0..N-1)
 */
typedef struct {
    int tgt_path_L;
    mode_t mode;
} ser_mkdir_t;
typedef struct {
    int tgt_path_L;
} ser_unlink_t;
typedef struct {
    int tgt_path_L;
} ser_rmdir_t;
typedef struct {
    int src_path_L;
    int dst_path_L;
} ser_rename_t;
typedef struct {
    int path_L;
    int flags;
} ser_open_t;
typedef struct {
    int fh;
} ser_release_t;
typedef struct {
    // A special case. _L is implicit in field size_t size.
    int fh;
    size_t size;
    off_t offset;
} ser_write_t;


typedef union {
    ser_mkdir_t ser_mkdir;
    ser_unlink_t ser_unlink;
    ser_rmdir_t ser_rmdir;
    ser_rename_t ser_rename;
    ser_open_t ser_open;
    ser_release_t ser_release;
    ser_write_t ser_write;
} ser_union;

typedef struct {
    enum op_enum op;
    ser_union data;
} ser_t;

typedef struct {
    int retval;
    int fh;
} ser_res_t;

//Ackchyually max is unsigned short
//1048576 max bytes (1MB)


#endif //ELMEUTFG_RFSCOMMON_H
