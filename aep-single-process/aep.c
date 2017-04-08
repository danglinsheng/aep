/*     Aep         */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>


#include "ae.h"
#include "anet.h"

char neterr[256];
aeEventLoop *el = NULL;

int m_c = 0;
int f_c = 0;

typedef struct _aep_data {
    char readbuf[1024];
    int readlen;
    int totalread;
    
    char writebuf[1024];
    int writelen;
    int totalwrite;
}aepData;

#define AEP_NOTUSED(V) ((void) V)
#define aep_tolower(c)      (char) ((c >= 'A' && c <= 'Z') ? (c | 0x20) : c)

    void
aep_strlow(char *dst, char *src, size_t n)
{
    while (n) {
        *dst = aep_tolower(*src);
        dst++;
        src++;
        n--;
    }
}

void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask);

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

            fprintf(stderr, "write EAGAIN error \n");
        }
        else{
            free(data);
            f_c++;
            close(fd);
            fprintf(stderr, "write real error \n");
            return;
        }
    }else if(nwritten == 0) {
        free(data);
        f_c++;
        close(fd);
        fprintf(stderr, "write 0 byte error \n");
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

        fprintf(stderr, "write not enough error \n");
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


#if 0
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    aepData *data = (aepData*) privdata;

    int need_read;
    int nread;
    int flag = 0;
    AEP_NOTUSED(el);
    AEP_NOTUSED(mask);

    aeDeleteFileEvent(el, fd, AE_READABLE | AE_WRITABLE);

    if(ioctl(fd, FIONREAD, &need_read) < 0 || need_read == 0){
        free(data);
        f_c++;
        close(fd);
        return;
    }

    nread = read(fd, data->readbuf + data->totalread, need_read - data->totalread);
    if (nread == -1) {
        if (errno == EAGAIN || errno == EINTR) {
            /* Not error */
            nread = 0;
            flag = 1;
            fprintf(stderr, "read EAGAIN error \n");

        } else {
            /* real error */
            free(data);
            f_c++;
            close(fd);
            fprintf(stderr, "read real error \n");
            return;
        }
    } else if (nread == 0) {
        /* client close connection */
        free(data);
        f_c++;
        close(fd);
        fprintf(stderr, "read 0 byte error \n");
        return;
    } else {
        /* nread>0 */
    }

    data->totalread += nread;

    if(data->totalread < need_read) {
        
        if (aeCreateFileEvent(el, fd, AE_READABLE, readQueryFromClient, data) == -1)
        {
            free(data);
            f_c++;
            close(fd);
            return;
        }

        fprintf(stderr, "read not enough error \n");
        return;
    }

    /* data->readlen >= need_read  */

    if(strncasecmp(data->readbuf, "quit", 4)==0){
        free(data);
        f_c++;
        close(fd);
        return;
    }

    aep_strlow(data->writebuf, data->readbuf, data->totalread);
    data->writelen = data->totalread;

    if (aeCreateFileEvent(el, fd, AE_WRITABLE, sendReplyToClient, data) == -1) {
        free(data);
        f_c++;
        close(fd);
        return;
    }
}

#endif

void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    aepData *data = (aepData*) privdata;

    //int need_read;
    int nread;
    int flag = 0;
    AEP_NOTUSED(el);
    AEP_NOTUSED(mask);

    aeDeleteFileEvent(el, fd, AE_READABLE | AE_WRITABLE);

#if 0
    if(ioctl(fd, FIONREAD, &need_read) < 0 || need_read == 0){
        free(data);
        f_c++;
        close(fd);
        return;
    }
#endif    

    nread = read(fd, data->readbuf + data->totalread, 1024 - data->totalread);
    if (nread == -1) {
        if (errno == EAGAIN || errno == EINTR) {
            /* Not error */
            nread = 0;
            flag = 1;
            fprintf(stderr, "read EAGAIN error \n");


            if (aeCreateFileEvent(el, fd, AE_READABLE, readQueryFromClient, data) == -1)
            {
                free(data);
                f_c++;
                close(fd);
                return;
            }

            fprintf(stderr, "read not enough error \n");

        } else {
            /* real error */
            free(data);
            f_c++;
            close(fd);
            fprintf(stderr, "read real error \n");
            return;
        }
    } else if (nread == 0) {
        /* client close connection */
        free(data);
        f_c++;
        close(fd);
        fprintf(stderr, "read 0 byte error \n");
        return;
    } else {
        /* nread>0 */
    }

    data->totalread += nread;

#if 0
    if(data->totalread < 1024) {
        
        if (aeCreateFileEvent(el, fd, AE_READABLE, readQueryFromClient, data) == -1)
        {
            free(data);
            f_c++;
            close(fd);
            return;
        }

        fprintf(stderr, "read not enough error \n");
        return;
    }
#endif    

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


void acceptCommonHandler(int fd) {
    if (fd != -1) {
        aepData *data = malloc(sizeof(aepData));
        m_c++;
        memset(data, 0, sizeof(aepData));

        anetNonBlock(NULL,fd);
        anetEnableTcpNoDelay(NULL,fd);

        if (aeCreateFileEvent(el, fd, AE_READABLE, readQueryFromClient, data) == -1)
        {
            close(fd);
            free(data);
            f_c++;
            return;
        }
    }

}

void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cport, cfd;
    char cip[256];
    AEP_NOTUSED(el);
    AEP_NOTUSED(mask);
    AEP_NOTUSED(privdata);

    cfd = anetTcpAccept(neterr, fd, cip, &cport);
    if (cfd == -1) {
        return;
    }

    //fprintf(stderr, "Begin to accept from %s:%d\n", cip, cport);

    acceptCommonHandler(cfd);
}

#if 0
int showMeminfo(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    AEP_NOTUSED(eventLoop);
    AEP_NOTUSED(id);
    AEP_NOTUSED(clientData);


    fprintf(stderr, "malloc:%d  free:%d\n", m_c, f_c);

    return 0;
}

static void aep_user (int i) {
    fprintf(stderr, "Got signal\n");
}
#endif


int main(int argc, char **argv) {
    int fd;

    el = aeCreateEventLoop(65000);
    if (NULL == el) {
        return -1;
    }


    fd = anetTcpServer(neterr, 7878, NULL);
    if (fd != -1) {
        anetNonBlock(NULL,fd);
    }

#if 0
    long long id;
    if((id = aeCreateTimeEvent(el, 1000, showMeminfo, (void *)id, NULL)) == -1) {
        return -1;
    }

    
    signal(SIGUSR1, aep_user);
#endif    

    if(aeCreateFileEvent(el, fd, AE_READABLE, acceptTcpHandler, NULL) == -1){
        return -1;
    }


    aeMain(el);
    aeDeleteEventLoop(el);

    return 0;
}



