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
#include "rfsCommon.h"
#include <nng/nng.h>
#include <nng/protocol/reqrep0/req.h>

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


#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }

static const struct fuse_opt option_spec[] = {
        OPTION("--name=%s", filename),
        OPTION("--contents=%s", contents),
        OPTION("-h", show_help),
        OPTION("--help", show_help),
        FUSE_OPT_END
};

#define QSIZE 64
typedef struct {
    int index;
    int size;
    pthread_mutex_t mutex;
    pthread_cond_t cond_full;
    pthread_cond_t cond_empty;
    op_t *queue[QSIZE];
} queue_t; // = {, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER};

static pthread_mutex_t slave_mut = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t slave_cond = PTHREAD_COND_INITIALIZER;

int q_empty(queue_t *queue) { return queue->size == 0; }

int q_full(queue_t *queue) { return queue->size == QSIZE; }

void enqueue(queue_t *queue, op_t *val) {
    pthread_mutex_lock(&queue->mutex);
    int was_empty = q_empty(queue);
    while (q_full(queue)) {
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

_Noreturn void *dispatcher_local(void *q);
_Noreturn void *dispatcher_remote();

typedef struct {
//    queue_t *q;   UNNECESSARY IF WE SPECIFY "PERSONALITY".........
    char personality;  // """const"""          FUCK C
} dispatcher_param_t;

nng_socket globSock;

static void *hello_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    (void) conn;
    // Disable as many caches as possible, just to be on the safe side.
    cfg->kernel_cache = 0;
    cfg->direct_io = 1;

    pthread_t bogus;
    dispatcher_param_t *dispatcherParam;

    // Global 'A'-side init
      // ... To be implemented.
    // 'A'-side dispatcher(s) spawn.
    dispatcherParam = malloc(sizeof(dispatcher_param_t));
    dispatcherParam->personality = 'A';
    pthread_create(&bogus, NULL, dispatcher_local, dispatcherParam);
    pthread_detach(bogus);

    // Global 'B'-side init
    int rv;
    rv = nng_req0_open(&globSock); //TODO errcheck
    rv = nng_dial(globSock, "tcp://127.0.0.1:1420", NULL, 0); //TODO errcheck

      // ... To be implemented.
    // 'B'-side dispatcher(s) spawn.
    pthread_create(&bogus, NULL, dispatcher_remote, NULL);
    pthread_detach(bogus);

    return NULL;
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

static int hello_mkdir(const char *path, mode_t mode) {
    op_t op_A = {.op = OP_MKDIR, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_mkdir = {.mode = mode, .tgt_path = path}};
    op_t op_B = {.op = OP_MKDIR, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_mkdir = {.mode = mode, .tgt_path = path}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}

static int hello_mknod(const char *path, mode_t mode, dev_t dev) {
    op_t op_A = {.op = OP_MKNOD, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_mknod = {.mode = mode, .tgt_path = path}};
    op_t op_B = {.op = OP_MKNOD, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_mknod = {.mode = mode, .tgt_path = path}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}

static int hello_unlink(const char *path) {
    op_t op_A = {.op = OP_UNLINK, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_unlink = {.tgt_path = path}};
    op_t op_B = {.op = OP_UNLINK, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_unlink = {.tgt_path = path}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}


static int hello_rmdir(const char *path) {
    op_t op_A = {.op = OP_RMDIR, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_rmdir = {.tgt_path = path}};
    op_t op_B = {.op = OP_RMDIR, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_rmdir = {.tgt_path = path}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}


static int hello_rename(const char *src_path, const char *dst_path, unsigned int flags) {
//    assert(!flags);  // neither RENAME_EXCHANGE nor RENAME_NOREPLACE implemented in our code.
//  above assert disabled because I can't for the life of me find where the constants are defined, and *flags* isn't
//  0 by default.
    op_t op_A = {.op = OP_RENAME, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_rename = {.src_path = src_path, .dst_path = dst_path}};
    op_t op_B = {.op = OP_RENAME, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_rename = {.src_path = src_path, .dst_path = dst_path}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}


static int hello_open(const char *path, struct fuse_file_info *fi) {
    op_t op_A = {.op = OP_OPEN, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_open = {.path = path, fi = fi}};
    op_t op_B = {.op = OP_OPEN, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_open = {.path = path, fi = fi}};
    int retval = generic_enqueue_and_wait(&op_A, &op_B);
    myhack_t *my = ((myhack_t *) &fi->fh);
    printf("DEBUG: fh.a=%d, fh.b=%d\n", my->inner.a, my->inner.b);
    return retval;
}

static int hello_release(const char *path, struct fuse_file_info *fi) {
    (void) path;
    op_t op_A = {.op = OP_RELEASE, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_release = {fi = fi}};
    op_t op_B = {.op = OP_RELEASE, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_release = {fi = fi}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}

static int hello_write(const char *path, const char *buf, size_t size, off_t offset, ffi *fi) {
    (void) path;
    op_t op_A = {.op = OP_WRITE, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_write = {fi = fi, .buf = buf, .size = size, .offset = offset}};
    op_t op_B = {.op = OP_WRITE, .mut = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .data.op_write = {fi = fi, .buf = buf, .size = size, .offset = offset}};
    return generic_enqueue_and_wait(&op_A, &op_B);
}

int *obtain_dehacked_fh(struct fuse_file_info *fi, char pers) {
    assert(pers == 'A' || pers == 'B');
    return pers == 'A' ?
           &((myhack_t *) &fi->fh)->inner.a :
           &((myhack_t *) &fi->fh)->inner.b;
}

_Noreturn void *dispatcher_local(void *arg) {
    /* In this iteration of development, dispatchers and worker functions do essentially the same for backends
     A and B, i.e. write to a directory in the local filesystem. Hence they are reused, with a "personality
     argument to denote which backend they're writing to.
     */
    dispatcher_param_t *dispatcherParam = arg;
    char pers = dispatcherParam->personality;
    queue_t *q;
    const char *prepath;
    printf("Local-spec dispatcher w/ ThreadID %lu, pers: %c reporting.\n", pthread_self(), dispatcherParam->personality);
    switch (dispatcherParam->personality) {
        case 'A':
            q = &wr_queue_A;
            prepath = "/home/daniel/CLionProjects/elmeutfg/ladoA";
            break;
        case 'B':
            q = &wr_queue_B;
            prepath = "/home/daniel/CLionProjects/elmeutfg/ladoB";
            break;
        default:
            assert(0);
    }
    op_t *nextop;
    int retval;
    for (;;) {
        nextop = dequeue(q);
        switch (nextop->op) {
            case OP_MKDIR:
                retval = hello_mkdir_AB(nextop->data.op_mkdir.tgt_path, nextop->data.op_mkdir.mode, prepath);
                break;
            case OP_MKNOD:
                retval = hello_mknod_AB(nextop->data.op_mknod.tgt_path, nextop->data.op_mknod.mode, prepath);
                break;
            case OP_UNLINK:
                retval = hello_unlink_AB(nextop->data.op_unlink.tgt_path, prepath);
                break;
            case OP_RMDIR:
                retval = hello_rmdir_AB(nextop->data.op_rmdir.tgt_path, prepath);
                break;
            case OP_RENAME:
                retval = hello_rename_AB(nextop->data.op_rename.src_path, nextop->data.op_rename.dst_path, prepath);
                break;
            case OP_OPEN: {
                // WARNING. The code here has the "lower" function set (as side-effect) the 'fh' field with the returned file descriptor.
                //  In the "remote" version, it will have to be returned instead, and (myhack_t) op->fi->fh set in the body of this dispatcher.
                op_open_t *o = &(nextop->data.op_open);
                int *fh = obtain_dehacked_fh(o->fi, pers);
                int flags = o->fi->flags;
                retval = hello_open_AB(nextop->data.op_open.path, flags, fh, prepath);
                break;
            }
            case OP_RELEASE: {
                op_release_t *o = &(nextop->data.op_release);
                int *fh = obtain_dehacked_fh(o->fi, pers);
                retval = hello_release_AB(fh);
                break;
            }
            case OP_WRITE: {
                op_write_t *o = &(nextop->data.op_write);
                int *fh = obtain_dehacked_fh(o->fi, pers);
                retval = hello_write_AB(o->buf, o->size, o->offset, fh);
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

void *dispatcher_remote() {
    //Dequeues operations and sends them to the slave node.
    // #? of these functions concurrently running spawned by ME I GUESS.
    // BEGIN init section
    char pers = 'B'; //TODO: Hardcoded
    queue_t *q = &wr_queue_B; //TODO: Hardcoded
    nng_socket *sock = &globSock;
    //TODO set socket context (?)

    char msg_buf[2*1024*1024];  //big chonker
    int retval;
    size_t buf_size;
    op_t *nextop;
    // END init section
    for (;;) {
        char *msg_buf_cpy = msg_buf;
        buf_size = -1;
        nextop = dequeue(q);
        switch (nextop->op) {
            case OP_MKDIR: {
                //set ser_*_t fields
                ser_t *tmp = (ser_t *) msg_buf;
                tmp->op = OP_MKDIR;
                ser_mkdir_t *tmp2 = &tmp->data.ser_mkdir;
                tmp2->tgt_path_L = strlen(nextop->data.op_mkdir.tgt_path) + 1;
                tmp2->mode = nextop->data.op_mkdir.mode;
                //set 1 data field
                msg_buf_cpy += sizeof(*tmp);
                memcpy(msg_buf_cpy, nextop->data.op_mkdir.tgt_path, tmp2->tgt_path_L);
                //set total buf_size
                buf_size = sizeof(ser_t) + tmp2->tgt_path_L;
                break;
            }
            case OP_MKNOD: {
                ser_t *tmp = (ser_t *) msg_buf;
                tmp->op = OP_MKNOD;
                ser_mknod_t *tmp2 = &tmp->data.ser_mknod;
                tmp2->tgt_path_L = strlen(nextop->data.op_mknod.tgt_path) +1;
                tmp2->mode = nextop->data.op_mknod.mode;
                //set 1 data field
                msg_buf_cpy += sizeof(*tmp);
                memcpy(msg_buf_cpy, nextop->data.op_mknod.tgt_path, tmp2->tgt_path_L);
                //set total buf_size
                buf_size = sizeof(ser_t) + tmp2->tgt_path_L;
                break;
            }
            case OP_UNLINK: {
                //set ser_*_t fields
                ser_t *tmp = (ser_t *) msg_buf;
                tmp->op = OP_UNLINK;
                ser_unlink_t *tmp2 = &tmp->data.ser_unlink;
                tmp2->tgt_path_L = strlen(nextop->data.op_unlink.tgt_path) + 1;
                //set 1 data field
                msg_buf_cpy += sizeof(*tmp);
                memcpy(msg_buf_cpy, nextop->data.op_unlink.tgt_path, tmp2->tgt_path_L);
                //set total buf_size
                buf_size = sizeof(ser_t) + tmp2->tgt_path_L;
                break;
            }
            case OP_RMDIR: {
                //set ser_*_t fields
                ser_t *tmp = (ser_t *) msg_buf;
                tmp->op = OP_RMDIR;
                ser_rmdir_t *tmp2 = &tmp->data.ser_rmdir;
                tmp2->tgt_path_L = strlen(nextop->data.op_rmdir.tgt_path) + 1;
                //set 1 data field
                msg_buf_cpy += sizeof(*tmp);
                memcpy(msg_buf_cpy, nextop->data.op_rmdir.tgt_path, tmp2->tgt_path_L);
                //set total buf_size
                buf_size = sizeof(ser_t) + tmp2->tgt_path_L;
                break;
            }
            case OP_RENAME: {
                //set ser_*_t fields
                ser_t *tmp = (ser_t *) msg_buf;
                tmp->op = OP_RENAME;
                ser_rename_t *tmp2 = &tmp->data.ser_rename;
                tmp2->src_path_L = strlen(nextop->data.op_rename.src_path) + 1;
                tmp2->dst_path_L = strlen(nextop->data.op_rename.dst_path) + 1;
                //set 1 data field
                msg_buf_cpy += sizeof(*tmp);
                memcpy(msg_buf_cpy, nextop->data.op_rename.src_path, tmp2->src_path_L);
                //set 2 data field
                msg_buf_cpy += tmp2->src_path_L;
                memcpy(msg_buf_cpy, nextop->data.op_rename.dst_path, tmp2->dst_path_L);
                //set total buf_size
                buf_size = sizeof(ser_t) + tmp2->src_path_L + tmp2->dst_path_L;
                break;
            }
            case OP_OPEN: {
                // WARNING. The code here has the "lower" function set (as side-effect) the 'fh' field with the returned file descriptor.
                //  In the "remote" version, it will have to be returned instead, and (myhack_t) op->fi->fh set in the body of this dispatcher.
                //set ser_*_t fields
                ser_t *tmp = (ser_t *) msg_buf;
                tmp->op = OP_OPEN;
                ser_open_t *tmp2 = &tmp->data.ser_open;
                tmp2->path_L = strlen(nextop->data.op_open.path) + 1;
                tmp2->flags = nextop->data.op_open.fi->flags;
//                tmp2->fh = *obtain_dehacked_fh(nextop->data.op_open.fi, 'B'); NOT NEEDED, IT IS SET BELOW, NOT USED HERE.
                //set 1 data field
                msg_buf_cpy += sizeof(*tmp);
                memcpy(msg_buf_cpy, nextop->data.op_open.path, tmp2->path_L);
                //set total buf_size
                buf_size = sizeof(ser_t) + tmp2->path_L;
                break;
            }
            case OP_RELEASE: {
                ser_t *tmp = (ser_t *) msg_buf;
                tmp->op = OP_RELEASE;
                ser_release_t *tmp2 = &tmp->data.ser_release;
                tmp2->fh = *obtain_dehacked_fh(nextop->data.op_release.fi, 'B');
                //set total buf_size
                buf_size = sizeof(ser_t);
                break;
            }
            case OP_WRITE: {
                ser_t *tmp = (ser_t *) msg_buf;
                tmp->op = OP_WRITE;
                ser_write_t *tmp2 = &tmp->data.ser_write;
                tmp2->offset = nextop->data.op_write.offset;
                tmp2->size = nextop->data.op_write.size;
                tmp2->fh = *obtain_dehacked_fh(nextop->data.op_write.fi, 'B');
                //set 1 data field
                msg_buf_cpy += sizeof(*tmp);
                memcpy(msg_buf_cpy, nextop->data.op_write.buf, tmp2->size);
                //set total buf_size
                buf_size = sizeof(ser_t) + tmp2->size;
                break;
            }
            default:
                assert(0);
        }
        printf("TID %lu sending %zu\n", pthread_self(), buf_size);
        cnc(nng_send(*sock, msg_buf, buf_size, 0));
        buf_size = 2 * 1024 * 1024;
        cnc(nng_recv(*sock, msg_buf, &buf_size, 0));
        assert(buf_size == sizeof(ser_res_t));
        ser_res_t* result = (ser_res_t*) msg_buf;
        retval = result->retval;
        if (nextop->op == OP_OPEN) {
            op_open_t *o = &(nextop->data.op_open);
            int *fh = obtain_dehacked_fh(o->fi, pers);
            printf("Snake, we just hit OP_OPEN! Let's check the value of fh: %d\n", result->fh);
            *fh = result->fh;
        }

        pthread_mutex_lock(&nextop->mut);
        nextop->result = retval;
        nextop->done = 1;
        pthread_cond_signal(&nextop->cond);
        pthread_mutex_unlock(&nextop->mut);
    }
}

int null_truncate(const char *path, off_t offset, ffi *fi) {
    return 0;
}

static const struct fuse_operations hello_oper = {
        .init           = hello_init,
        .getattr        = hello_getattr,
        .readlink       = hello_readlink,
        .mknod          = hello_mknod,
        .mkdir          = hello_mkdir,
        .unlink         = hello_unlink,
        .rmdir          = hello_rmdir,
        .symlink        = NULL,
        .rename         = hello_rename,
        .link           = NULL,
        .chmod          = NULL,
        .chown          = NULL,
        .truncate       = null_truncate,
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