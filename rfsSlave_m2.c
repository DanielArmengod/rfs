#include "includes/config.h"

#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include "includes/Server.h" /* server_xxx functions */
#include "rfsCommon.h"

// -----------------------------------------------------------------------------

// global declarations

// convenience functions
static void printInfo(cchar*, ...);

// the thread code to be run
static threadfunc rfsSlave(void*);

// sets tracing for a connection
static void setTrace(void*);

// for identification purposes in the log file
static cchar sourceID[] = "SSS";

// -----------------------------------------------------------------------------









// ############################################3 aaylmao



// the main program

void main(void)
{
    altpnt = "/home/daniel/CLionProjects/elmeutfg/altpnt";
    server_setServicePort(1420);
    server_setLogLevel(LOG_LEVEL_DEBUG);
    //server_setCallback(0, setTrace);
    server_init();  // This is a TS2 framework function.
    server_addThreads(5, rfsSlave, NULL);

    // runs the server
    server_run();
}

// -----------------------------------------------------------------------------

// A typical thread to be run by the server framework.

_Noreturn static threadfunc rfsSlave(void* arg)
{
    Message *msg;
    (void) arg;

    for (;;)
    {
        printInfo("Slave thread #%d ready for more.\n", thread_selfSeqNo());
        msg = server_waitInputMessage();
        printInfo("received message with orgId=%d orgSeq=%d (thread #%d)",
                        server_messageOrgId(msg), server_messageOrgSeqNo(msg),
                        thread_selfSeqNo());

        char *msgbuf = message_buffer(msg);
        ser_t *received = (ser_t*) msgbuf;
        ser_res_t result;
        switch (received->op) {
            case OP_MKDIR: {
                //unmarshall stuff
                ser_mkdir_t *tmp = (ser_mkdir_t *) &received->data.ser_mkdir;
                int mode = tmp->mode;
                char path[PATH_MAX];
                char *dataBegin = msgbuf + sizeof(*received);
                strcpy(path, dataBegin);
                //do actual operation
                printf("mkdirab\t%s\t%d\t%s\n",path,mode,altpnt);
                result.retval = hello_mkdir_AB(path, mode, altpnt);
                if (result.retval)
                    printInfo("WARNING: op write returned %d\n", result.retval);
                break;
            }
            case OP_UNLINK: {
                ser_unlink_t *tmp = (ser_unlink_t*) &received->data.ser_unlink;
                char path[PATH_MAX];
                char *dataBegin = msgbuf + sizeof(*received);
                strcpy(path, dataBegin);
                printf("unlinkab\t%s\t%s\n",path,altpnt);
                //do actual operation
                result.retval = hello_unlink_AB(path,altpnt);
                if (result.retval)
                    printInfo("WARNING: op unlink returned %d\n", result.retval);
                break;
            }
            case OP_RMDIR: {
                ser_rmdir_t *tmp = (ser_rmdir_t *) &received->data.ser_rmdir;
                char path[PATH_MAX];
                char *dataBegin = msgbuf + sizeof(*received);
                strcpy(path, dataBegin);
                printf("rmdirab\t%s\t%s\n",path,altpnt);
                //do actual operation
                result.retval = hello_rmdir_AB(path,altpnt);
                if (result.retval)
                    printInfo("WARNING: op rmdirab returned %d\n", result.retval);
                break;
            }
            case OP_RENAME: {
                ser_rename_t *tmp = (ser_rename_t *) &received->data.ser_rename;
                char path1[PATH_MAX];
                char path2[PATH_MAX];
                char *dataBegin = msgbuf + sizeof(*received);
                strcpy(path1, dataBegin);
                dataBegin += strlen(path1)+1;
                strcpy(path2, dataBegin);
                printf("renameab\t%s  --> %s\t%s\n",path1,path2,altpnt);
                //do actual operation
                result.retval = hello_rename_AB(path1,path2,altpnt);
                if (result.retval)
                    printInfo("WARNING: op renameab returned %d\n", result.retval);
                break;
            }
            case OP_OPEN: {
                ser_open_t *tmp = (ser_open_t *) &received->data.ser_open;
                int flags = tmp->flags;
                int fh;
                char path[PATH_MAX];
                char *dataBegin = msgbuf + sizeof(*received);
                strcpy(path, dataBegin);
                printf("openab\t%s\t%s\n",path,altpnt);
                //do actual operation
                result.retval = hello_open_AB(path, flags, &fh, altpnt);
                if (result.retval)
                    printInfo("WARNING: op openab returned %d\n", result.retval);
                result.fh = fh;
                break;
            }
            case OP_RELEASE: {
                ser_release_t *tmp = (ser_release_t*) &received->data.ser_release;
                int fh = tmp->fh;
                printf("releaseab\t%d\n",fh);
                result.retval = hello_release_AB(&fh);
                if (result.retval)
                    printInfo("WARNING: op releaseab returned %d\n", result.retval);
                break;
            }
            case OP_WRITE: {
                ser_write_t *tmp = (ser_write_t *) &received->data.ser_write;
                size_t size = tmp->size;
                int fh = tmp->fh;
                off_t offset = tmp->offset;
                char *dataBegin = msgbuf + sizeof(*received);
                printf("writeab\tfh: %d\tsize: %d\n",fh, size);
                //do actual operation
                result.retval = hello_write_AB(dataBegin, size, offset, &fh);
                if (result.retval < 0)
                    printInfo("WARNING: op writeab returned %d\n", result.retval);
                break;
            }
            default:
                assert(0);
        }
        memcpy(message_buffer(msg), &result, sizeof(result));
        message_setSize(msg, sizeof(result));
        printInfo("ok, replying (thread #%d)", thread_selfSeqNo());
        server_dispatchOutputMessage(msg);
    }
}

// -----------------------------------------------------------------------------

// convenience printf-like function for formatting purposes

static void format(char * buf, ushort bufLen, cchar* fmt, va_list ap)
{
    buf[0] = '*';                    // (1 byte)  00
    buf[1] = ' ';                    // (1 byte)  01
    server_formatCurrentTime(buf+2); // (8 bytes) 02 03 04 05 06 07 08 09
    buf[10] = ' ';                   // (1 byte)  10

    vsnprintf(buf+11, bufLen-11, fmt, ap);
}

// -----------------------------------------------------------------------------

// printf-like function that writes on stdout an the log file

static void printInfo(cchar* fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    format(buf, 256, fmt, ap);
    va_end(ap);

    buf[0] = '*';                    // (1 byte)  00
    buf[1] = ' ';                    // (1 byte)  01
    server_formatCurrentTime(buf+2); // (8 bytes) 02 03 04 05 06 07 08 09
    buf[10] = ' ';                   // (1 byte)  10

    // uses "server_printf" (and not plain printf) because of contention issues
    // between threads
    server_printf("%s\n", buf);

    // writes the log file
    server_logInfo(buf+11);
}

// -----------------------------------------------------------------------------

// #if 0
static void setTrace(void* connection)
{
    // for tracing new connections, enable line below
    // (could be used to inspect new connections for selectively enable tracing)

    server_setTrace(connection, true);

    connection = 0; // just to avoid warnings...
}
// #endif

// -----------------------------------------------------------------------------
// the end
