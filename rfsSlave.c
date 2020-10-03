#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include "rfsCommon.h"
#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>

nng_socket globSock;
_Noreturn void rfsSlave();

int main(void)
{
    altpnt = "/home/daniel/CLionProjects/elmeutfg/altpnt";

    int rv;
    rv = nng_rep0_open(&globSock);
    rv = nng_listen(globSock, "tcp://127.0.0.1:1420", NULL, 0);
    rfsSlave();
}


_Noreturn void rfsSlave()
{
    nng_socket *sock = &globSock;
    char msg_buf[2 * 1024 * 1024];  //big chonker
    for (;;)
    {
        size_t buf_size = 2 * 1024 * 1024;
        int rv;
        printf("Slave thread #%lu ready for more.\n", pthread_self());
        rv = nng_recv(*sock, msg_buf, &buf_size, 0);
        printf("received message size %zu (thread #%lu)\n", buf_size, pthread_self());
        ser_t *received = (ser_t*) msg_buf;
        ser_res_t result;
        switch (received->op) {
            case OP_MKDIR: {
                //unmarshall stuff
                ser_mkdir_t *tmp = (ser_mkdir_t *) &received->data.ser_mkdir;
                int mode = tmp->mode;
                char path[PATH_MAX];
                char *dataBegin = msg_buf + sizeof(*received);
                strcpy(path, dataBegin);
                //do actual operation
                printf("mkdirab\t%s\t%d\t%s\n",path,mode,altpnt);
                result.retval = hello_mkdir_AB(path, mode, altpnt);
                if (result.retval)
                    printf("WARNING: op write returned %d\n", result.retval);
                break;
            }
            case OP_UNLINK: {
                ser_unlink_t *tmp = (ser_unlink_t*) &received->data.ser_unlink;
                char path[PATH_MAX];
                char *dataBegin = msg_buf + sizeof(*received);
                strcpy(path, dataBegin);
                printf("unlinkab\t%s\t%s\n",path,altpnt);
                //do actual operation
                result.retval = hello_unlink_AB(path,altpnt);
                if (result.retval)
                    printf("WARNING: op unlink returned %d\n", result.retval);
                break;
            }
            case OP_RMDIR: {
                ser_rmdir_t *tmp = (ser_rmdir_t *) &received->data.ser_rmdir;
                char path[PATH_MAX];
                char *dataBegin = msg_buf + sizeof(*received);
                strcpy(path, dataBegin);
                printf("rmdirab\t%s\t%s\n",path,altpnt);
                //do actual operation
                result.retval = hello_rmdir_AB(path,altpnt);
                if (result.retval)
                    printf("WARNING: op rmdirab returned %d\n", result.retval);
                break;
            }
            case OP_RENAME: {
                ser_rename_t *tmp = (ser_rename_t *) &received->data.ser_rename;
                char path1[PATH_MAX];
                char path2[PATH_MAX];
                char *dataBegin = msg_buf + sizeof(*received);
                strcpy(path1, dataBegin);
                dataBegin += strlen(path1)+1;
                strcpy(path2, dataBegin);
                printf("renameab\t%s  --> %s\t%s\n",path1,path2,altpnt);
                //do actual operation
                result.retval = hello_rename_AB(path1,path2,altpnt);
                if (result.retval)
                    printf("WARNING: op renameab returned %d\n", result.retval);
                break;
            }
            case OP_OPEN: {
                ser_open_t *tmp = (ser_open_t *) &received->data.ser_open;
                int flags = tmp->flags;
                int fh;
                char path[PATH_MAX];
                char *dataBegin = msg_buf + sizeof(*received);
                strcpy(path, dataBegin);
                printf("openab\t%s\t%s\n",path,altpnt);
                //do actual operation
                result.retval = hello_open_AB(path, flags, &fh, altpnt);
                if (result.retval)
                    printf("WARNING: op openab returned %d\n", result.retval);
                result.fh = fh;
                break;
            }
            case OP_RELEASE: {
                ser_release_t *tmp = (ser_release_t*) &received->data.ser_release;
                int fh = tmp->fh;
                printf("releaseab\t%d\n",fh);
                result.retval = hello_release_AB(&fh);
                if (result.retval)
                    printf("WARNING: op releaseab returned %d\n", result.retval);
                break;
            }
            case OP_WRITE: {
                ser_write_t *tmp = (ser_write_t *) &received->data.ser_write;
                size_t size = tmp->size;
                int fh = tmp->fh;
                off_t offset = tmp->offset;
                char *dataBegin = msg_buf + sizeof(*received);
                printf("writeab\tfh: %d\tsize: %zu\n",fh, size);
                //do actual operation
                result.retval = hello_write_AB(dataBegin, size, offset, &fh);
                if (result.retval < 0)
                    printf("WARNING: op writeab returned %d\n", result.retval);
                break;
            }
            default:
                assert(0);
        }
        nng_send(*sock, &result, sizeof(result), 0);
        printf("ok, replying (thread #%lu)\n\n", pthread_self());
    }
}