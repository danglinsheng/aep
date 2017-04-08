/*     Aep         */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include "ae.h"
#include "anet.h"

int m_c = 0;
int f_c = 0;


static int fd_listen;


#define WORKER_COUNT  64
#define AEP_NOTUSED(V) ((void) V)
#define aep_tolower(c)      (char) ((c >= 'A' && c <= 'Z') ? (c | 0x20) : c)

typedef struct _WORKER {
    pthread_t thread_id;
    int ind;
    int notified_rfd;
    int notifed_wfd;
    aeEventLoop *el;

}WORKER;

typedef struct _CONN {
    int fd;

    WORKER *owner;
    int cip;
    short cport;
    short err_type;
    void *user;
    struct _CONN *next;

}CONN;


typedef struct _aep_data {
    char readbuf[1024];
    int readlen;
    int totalread;

    char writebuf[1024];
    int writelen;
    int totalwrite;

}aepData;

char neterr[256];
aeEventLoop *el = NULL;


void aep_strlow(char *dst, char *src, size_t n)
{
    while (n) {
        *dst = aep_tolower(*src);
        dst++;
        src++;
        n--;
    }
}

void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask);


#if 0
HTTP/1.0 200 OK\r\nContent-type: text/plain\r\n\r\n%s","Hello world!
#endif

void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    aepData *data = privdata;
    int nwritten = 0;

    AEP_NOTUSED(el);
    AEP_NOTUSED(mask);

    aeDeleteFileEvent(el, fd, AE_READABLE | AE_WRITABLE);

    aep_strlow(data->writebuf, data->readbuf, data->totalread);
    data->writelen = data->totalread;

    nwritten = write(fd, data->writebuf + data->totalwrite, data->writelen - data->totalwrite);

    if (nwritten == -1) {
        if (errno == EAGAIN || errno == EINTR) {
            nwritten = 0;

            //fprintf(stderr, "write EAGAIN error \n");
        }
        else{
            free(data);
            f_c++;
            close(fd);
            //fprintf(stderr, "write real error \n");
            return;
        }
    }else if(nwritten == 0) {
        free(data);
        f_c++;
        close(fd);
        //fprintf(stderr, "write 0 byte error \n");
        return;
    }

    data->totalwrite += nwritten;

    if(data->totalwrite < data->writelen) {

        if (aeCreateFileEvent(el, fd, AE_WRITABLE, sendReplyToClient, data) == -1) {
            free(data);
            f_c++;
            close(fd);
            return;
        }

        //fprintf(stderr, "write not enough error \n");
        return;
    }

    memset(data, 0, sizeof(aepData));
    if (aeCreateFileEvent(el, fd, AE_READABLE, readQueryFromClient, data) == -1)
    {
        close(fd);
        free(data);
        f_c++;
        return;
    }



}



void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    aepData *data = (aepData*) privdata;

    //int need_read;
    int nread;
    int flag = 0;
    AEP_NOTUSED(el);
    AEP_NOTUSED(mask);

    aeDeleteFileEvent(el, fd, AE_READABLE | AE_WRITABLE);

    nread = read(fd, data->readbuf + data->totalread, 1024 - data->totalread);
    if (nread == -1) {
        if (errno == EAGAIN || errno == EINTR) {
            /* Not error */
            nread = 0;
            flag = 1;
            //fprintf(stderr, "read EAGAIN error \n");


            if (aeCreateFileEvent(el, fd, AE_READABLE, readQueryFromClient, data) == -1)
            {
                free(data);
                f_c++;
                close(fd);
                return;
            }

            //fprintf(stderr, "read not enough error \n");

        } else {
            /* real error */
            free(data);
            f_c++;
            close(fd);
            //fprintf(stderr, "read real error \n");
            return;
        }
    } else if (nread == 0) {
        /* client close connection */
        free(data);
        f_c++;
        close(fd);
        //   fprintf(stderr, "read 0 byte error \n");
        return;
    } else {
        /* nread>0 */
    }

    data->totalread += nread;


    /* data->readlen >= need_read  */

    if(strncasecmp(data->readbuf, "quit", 4)==0){
        free(data);
        f_c++;
        close(fd);
        return;
    }

    if (aeCreateFileEvent(el, fd, AE_WRITABLE, sendReplyToClient, data) == -1) {
        free(data);
        f_c++;
        close(fd);
        return;
    }
}


void acceptCommonHandler(aeEventLoop *wel, int fd) {
    if (fd != -1) {
        aepData *data = malloc(sizeof(aepData));
        m_c++;
        memset(data, 0, sizeof(aepData));

        anetNonBlock(NULL,fd);
        anetEnableTcpNoDelay(NULL,fd);

        if (aeCreateFileEvent(wel, fd, AE_READABLE, readQueryFromClient, data) == -1)
        {
            close(fd);
            free(data);
            f_c++;
            return;
        }
    }

}

void acceptTcpHandler(aeEventLoop *wel, int fd, void *privdata, int mask) {
    int cport, cfd;
    char cip[256];
    AEP_NOTUSED(el);
    AEP_NOTUSED(mask);
    AEP_NOTUSED(privdata);

    cfd = anetTcpAccept(neterr, fd, cip, &cport);
    if (cfd == -1) {
        return;
    }

    fprintf(stderr, "Begin to accept from %s:%d\n", cip, cport);

    acceptCommonHandler(wel, cfd);

}

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    fprintf(stderr, "------%ld\n", time(NULL));
    return 1000;
}



void child_main(int i)
{

    printf("child process %d------------------\n",i);

    aeEventLoop *wel = aeCreateEventLoop(65000);
    if (NULL == wel) {
        return;
    }

    if(aeCreateFileEvent(wel, fd_listen, AE_READABLE, acceptTcpHandler, NULL) == -1){
        return;
    }

    fprintf(stdout, "--------------start server ok--------------\n");
    fflush(stdout);
    aeMain(wel);
    aeDeleteEventLoop(wel);
    
}



pid_t child_make(int i)
{
    pid_t pid;

    if( (pid=fork()) > 0)
        return pid;

    child_main(i);

    return pid;
}


int main(int argc, char **argv) {

    fd_listen = anetTcpServer(neterr, 7878, NULL);
    if (fd_listen != -1) {
        anetNonBlock(NULL,fd_listen);
    }

    int i;

    for(i=0; i < WORKER_COUNT; i++)
        child_make(i);

#if 0
    if(aeCreateTimeEvent(el, 1000, serverCron, NULL, NULL) == -1) {
        return -1;
    }
#endif

    
    for(;;) {
        pause();
    }
    

    return 0;
}





