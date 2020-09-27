/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/
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

/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
static struct options {
    const char *filename;
    const char *contents;
    int show_help;
} options;

const char srcpnt[] = "/home/daniel/CLionProjects/elmeutfg/srcpnt";
const char altpnt[] = "/home/daniel/CLionProjects/elmeutfg/altpnt";

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }

static const struct fuse_opt option_spec[] = {
        OPTION("--name=%s", filename),
        OPTION("--contents=%s", contents),
        OPTION("-h", show_help),
        OPTION("--help", show_help),
        FUSE_OPT_END
};

static void fullpath_A(char *fpath, const char *path) {
    strcpy(fpath, srcpnt);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will break here
}

static void fullpath_B(char *fpath, const char *path) {
    strcpy(fpath, altpnt);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will break here
}

typedef struct fuse_file_info ffi;

enum op_enum {
    OP_MKDIR,
    OP_UNLINK,
    OP_RMDIR,
    OP_RENAME,
    OP_OPEN,
    OP_RELEASE,
    OP_WRITE,
};

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

#define QSIZE 64
typedef struct {
    int index;
    int size;
    pthread_mutex_t mutex;
    pthread_cond_t cond_full;
    pthread_cond_t cond_empty;
    op_t *queue[QSIZE];
} queue_t; // = {, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER};

int q_empty(queue_t *queue) { return queue->size == 0; }

int q_full(queue_t *queue) { return queue->size == QSIZE; }

void enqueue(queue_t *queue, op_t *val) {
    pthread_mutex_lock(&queue->mutex);
    int was_empty = q_empty(queue);
    while (q_full(queue)) {
        printf("Enqueue: full. Schleeping.\n");
        pthread_cond_wait(&queue->cond_full, &queue->mutex);
    }
    queue->queue[(queue->index + queue->size) % QSIZE] = val;
    queue->size++;
    if (was_empty) pthread_cond_signal(&queue->cond_empty);
    pthread_mutex_unlock(&queue->mutex);
}

op_t *dequeue(queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);
    int was_full = q_full(queue);
    while (q_empty(queue)) {
        printf("Dequeue: empty. Schleeping.\n");
        pthread_cond_wait(&queue->cond_empty, &queue->mutex);
    }
    op_t *retval = queue->queue[queue->index];
    queue->size--;
    queue->index++;
    queue->index %= QSIZE;
    if (was_full) pthread_cond_signal(&queue->cond_full);
    pthread_mutex_unlock(&queue->mutex);
    return retval;
}

queue_t wr_queue_A = {0, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER};
queue_t wr_queue_B = {0, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER};

_Noreturn void *dispatcher_AB(void *q);

typedef struct {
//    queue_t *q;   UNNECESSARY IF WE SPECIFY "PERSONALITY".........
    char personality;  // """const"""          FUCK C
} dispatcher_param_t;

static void *hello_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    (void) conn;
//    cfg->kernel_cache = 0;
    /* In this case both "queues" use the same kind of dispatcher: it writes to a local directory on a disk.
     */
    pthread_t bogus;
    dispatcher_param_t *dispatcherParam;

    dispatcherParam = malloc(sizeof(dispatcher_param_t));
    dispatcherParam->personality = 'A';
//    dispatcherParam->q = &wr_queue_A;
    pthread_create(&bogus, NULL, dispatcher_AB, dispatcherParam);
    pthread_detach(bogus);

    dispatcherParam = malloc(sizeof(dispatcher_param_t));
    dispatcherParam->personality = 'B';
//    dispatcherParam->q = &wr_queue_B;
    pthread_create(&bogus, NULL, dispatcher_AB, dispatcherParam);
    pthread_detach(bogus);

    return NULL;
}

static int hello_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi) {
    printf("%ld\n", pthread_self());
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));
    char fullpath[PATH_MAX];
    fullpath_A(fullpath, path);
    errno = 0;
    lstat(fullpath, stbuf);
    return -errno;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi,
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

static int hello_readlink(const char *path, char *buf, size_t bufsize) {
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

static int generic_enqueue_and_wait(op_t *op_A, op_t *op_B) {
    enqueue(&wr_queue_A, op_A);
    enqueue(&wr_queue_B, op_B);

    pthread_mutex_lock(&op_A->mut);
    while (!op_A->done) pthread_cond_wait(&op_A->cond, &op_A->mut);  // TODO add timeouts on the same condition

    pthread_mutex_lock(&op_B->mut);
    while (!op_B->done) pthread_cond_wait(&op_B->cond, &op_B->mut);  // TODO add timeouts on the same condition

    assert(op_A->result == op_B->result);  // TODO not necessarily the same, this is also indication of backend failure.
    return op_A->result;
}

static int hello_mkdir_AB(const char *path, mode_t mode, const char pers) {
    mode = mode | S_IFDIR;
    char fullpath[PATH_MAX];
    if (pers == 'A') fullpath_A(fullpath, path);
    if (pers == 'B') fullpath_B(fullpath, path);
    errno = 0;
    int ret = mkdir(fullpath, mode);
    int errsv = errno;
    if (ret < 0) {
        return -errsv;
    }
    return 0;
}

static int hello_mkdir(const char *path, mode_t mode) {
    op_t op_A = {.op = OP_MKDIR, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_mkdir = {.mode = mode, .tgt_path = path}};
    op_t op_B = {.op = OP_MKDIR, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_mkdir = {.mode = mode, .tgt_path = path}};
    return generic_enqueue_and_wait(&op_A, &op_B);
//    enqueue(&wr_queue_A, &op_A);
//    enqueue(&wr_queue_B, &op_B);
//
//    pthread_mutex_lock(&op_A.mut);
//    while (!op_A.done) pthread_cond_wait(&op_A.cond, &op_A.mut);  // TODO add timeouts on the same condition
//
//    pthread_mutex_lock(&op_B.mut);
//    while (!op_B.done) pthread_cond_wait(&op_B.cond, &op_B.mut);  // TODO add timeouts on the same condition
//
//    assert(op_A.result == op_B.result);  // TODO not necessarily the same, this is also indication of backend failure.
//    return op_A.result;
}

static int hello_unlink_AB(const char *path, char pers) {
    char fullpath[PATH_MAX];
    if (pers == 'A') fullpath_A(fullpath, path);
    if (pers == 'B') fullpath_B(fullpath, path);
    errno = 0;
    int ret = unlink(fullpath);
    int errsv = errno;
    if (ret < 0) {
        return -errsv;
    }
    return 0;
}

static int hello_unlink(const char *path) {
    op_t op_A = {.op = OP_UNLINK, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_unlink = {.tgt_path = path}};
    op_t op_B = {.op = OP_UNLINK, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_unlink = {.tgt_path = path}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}

static int hello_rmdir_AB(const char *path, char pers) {
    char fullpath[PATH_MAX];
    if (pers == 'A') fullpath_A(fullpath, path);
    if (pers == 'B') fullpath_B(fullpath, path);
    errno = 0;
    int ret = rmdir(fullpath);
    int errsv = errno;
    if (ret < 0) {
        return -errsv;
    }
    return 0;
}

static int hello_rmdir(const char *path) {
    op_t op_A = {.op = OP_RMDIR, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_rmdir = {.tgt_path = path}};
    op_t op_B = {.op = OP_RMDIR, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_rmdir = {.tgt_path = path}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}

static int hello_rename_AB(const char *src_path, const char *dst_path, char pers) {
    char src_fullpath[PATH_MAX];
    char dst_fullpath[PATH_MAX];
    if (pers == 'A') {
        fullpath_A(src_fullpath, src_path);
        fullpath_A(dst_fullpath, dst_path);
    }
    if (pers == 'B') {
        fullpath_B(src_fullpath, src_path);
        fullpath_B(dst_fullpath, dst_path);
    }
    errno = 0;
    int ret = rename(src_fullpath, dst_fullpath);
    int errsv = errno;
    if (ret < 0) {
        return -errsv;
    }
    return 0;
}

static int hello_rename(const char *src_path, const char *dst_path, unsigned int flags) {
//    assert(!flags);  // neither RENAME_EXCHANGE nor RENAME_NOREPLACE implemented in our code.
//  above assert disabled because I can't for the life of me find where the constants are defined, and *flags* isn't
//  0 by default.
    op_t op_A = {.op = OP_RENAME, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_rename = {.src_path = src_path, .dst_path = dst_path}};
    op_t op_B = {.op = OP_RENAME, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_rename = {.src_path = src_path, .dst_path = dst_path}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}

typedef union {
    uint64_t real;
    struct {
        int a;
        int b;
    } inner;
} myhack_t;

static int hello_open_AB(const char *path, struct fuse_file_info *fi, char pers) {
    char fullpath[PATH_MAX];
    if (pers == 'A') fullpath_A(fullpath, path);
    if (pers == 'B') fullpath_B(fullpath, path);
//    myhack_t *myhack = (myhack_t *) &fi->fh;
//    int *my_fh = &myhack->inner.a;
    int *my_fh;
    if (pers == 'A') my_fh = &((myhack_t *) &fi->fh)->inner.a;
    if (pers == 'B') my_fh = &((myhack_t *) &fi->fh)->inner.b;
    errno = 0;
    int ret = open(fullpath, fi->flags);
    int errsv = errno;
    if (ret < 0) {
        *my_fh = -1;
        return -errsv;
    }
    *my_fh = ret;
    return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi) {
    op_t op_A = {.op = OP_OPEN, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_open = {.path = path, fi = fi}};
    op_t op_B = {.op = OP_OPEN, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_open = {.path = path, fi = fi}};
    int retval = generic_enqueue_and_wait(&op_A, &op_B);
    myhack_t *my = ((myhack_t *) &fi->fh);
    printf("DEBUG: fh.a=%d, fh.b=%d\n", my->inner.a, my->inner.b);
    return retval;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    int ret = pread(((myhack_t) fi->fh).inner.a, buf, size, offset);
    if (ret < 0) {
        return -errno;
    }
    if (ret != size) {
        printf("WARNING: pread gave us %d out of requested %d bytes.\n", ret, (int) size);
    }
    return ret;
}

static int hello_release_AB(struct fuse_file_info *fi, char pers) {
    int *my_fh;
    if (pers == 'A') my_fh = &((myhack_t *) &fi->fh)->inner.a;
    if (pers == 'B') my_fh = &((myhack_t *) &fi->fh)->inner.b;
    int ret = close(*my_fh);
    if (ret < 0) {
        return -errno;
    }
    return ret;
}

static int hello_release(const char *path, struct fuse_file_info *fi) {
    (void) path;
    op_t op_A = {.op = OP_RELEASE, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_release = {fi = fi}};
    op_t op_B = {.op = OP_RELEASE, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_release = {fi = fi}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}

static int hello_write_AB(const char *buf, size_t size, off_t offset, ffi *fi, char pers) {
    int *my_fh;
    size_t orig_size = size;
    if (pers == 'A') my_fh = &((myhack_t *) &fi->fh)->inner.a;
    if (pers == 'B') my_fh = &((myhack_t *) &fi->fh)->inner.b;
    int retval;
    while (size) {
        retval = pwrite(*my_fh, buf, size, offset);
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

static int hello_write(const char *path, const char *buf, size_t size, off_t offset, ffi *fi) {
    (void) path;
    printf("Been called lmao\n");
    op_t op_A = {.op = OP_WRITE, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_write = {fi = fi, .buf = buf, .size = size, .offset = offset}};
    op_t op_B = {.op = OP_WRITE, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_write = {fi = fi, .buf = buf, .size = size, .offset = offset}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}

_Noreturn void *dispatcher_AB(void *arg) {
    /* In this iteration of development, dispatchers and worker functions do essentially the same for backends
     A and B, i.e. write to a directory in the local filesystem. Hence they are reused, with a "personality
     argument to denote which backend they're writing to.
     */
    dispatcher_param_t *dispatcherParam = arg;
    char pers = dispatcherParam->personality;
    queue_t *q;
    printf("ptr: %p\t, pers: %c\n", arg, pers);
    switch (pers) {
        case 'A':
            q = &wr_queue_A;
            break;
        case 'B':
            q = &wr_queue_B;
            break;
        default:
            assert(0);
    }
    op_t *nextop;
    int retval;
    while (1) {
        nextop = dequeue(q);
        switch (nextop->op) {
            case OP_MKDIR:
                retval = hello_mkdir_AB(nextop->data.op_mkdir.tgt_path, nextop->data.op_mkdir.mode, pers);
                break;
            case OP_UNLINK:
                retval = hello_unlink_AB(nextop->data.op_unlink.tgt_path, pers);
                break;
            case OP_RMDIR:
                retval = hello_rmdir_AB(nextop->data.op_rmdir.tgt_path, pers);
                break;
            case OP_RENAME:
                retval = hello_rename_AB(nextop->data.op_rename.src_path, nextop->data.op_rename.dst_path, pers);
                break;
            case OP_OPEN:
                retval = hello_open_AB(nextop->data.op_open.path, nextop->data.op_open.fi, pers);
                break;
            case OP_RELEASE:
                retval = hello_release_AB(nextop->data.op_release.fi, pers);
                break;
            case OP_WRITE: {
                op_write_t *o = &(nextop->data.op_write);
                retval = hello_write_AB(o->buf, o->size, o->offset, o->fi, pers);
                break;
            }
            default:
                assert(0);
        }
        pthread_mutex_lock(&nextop->mut);
        nextop->result = retval;
        nextop->done = 1;
        pthread_cond_signal(&nextop->cond);
        pthread_mutex_unlock(&nextop->mut);
    }
    //heh
    free(arg);
}

static const struct fuse_operations hello_oper = {
        .init           = hello_init,
        .getattr        = hello_getattr,
        .readlink       = hello_readlink,
        .mknod          = NULL,
        .mkdir          = hello_mkdir,
        .unlink         = hello_unlink,
        .rmdir          = hello_rmdir,
        .symlink        = NULL,
        .rename         = hello_rename,
        .link           = NULL,
        .chmod          = NULL,
        .chown          = NULL,
        .truncate       = NULL,
        .open           = hello_open,
        .read           = hello_read,
        .write          = hello_write,
        .release        = hello_release,

        .readdir        = hello_readdir,

};

static void show_help(const char *progname) {
    printf("usage: %s [options] <mountpoint>\n\n", progname);
    printf("File-system specific options:\n"
           "    --name=<s>          Name of the \"hello\" file\n"
           "                        (default: \"hello\")\n"
           "    --contents=<s>      Contents \"hello\" file\n"
           "                        (default \"Hello, World!\\n\")\n"
           "\n");
}

int main(int argc, char *argv[]) {
    int ret;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    /* Set defaults -- we have to use strdup so that
       fuse_opt_parse can free the defaults if other
       values are specified */
    options.filename = strdup("hello");
    options.contents = strdup("Hello World!\n");
    /* Parse options */
    if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
        return 1;
    /* When --help is specified, first print our own file-system
       specific help text, then signal fuse_main to show
       additional help (by adding `--help` to the options again)
       without usage: line (by setting argv[0] to the empty
       string) */
    if (options.show_help) {
        show_help(argv[0]);
        assert(fuse_opt_add_arg(&args, "--help") == 0);
        args.argv[0][0] = '\0';
    }
    ret = fuse_main(args.argc, args.argv, &hello_oper, NULL);
    fuse_opt_free_args(&args);
    return ret;
}