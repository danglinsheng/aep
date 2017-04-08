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
WORKER *allworker;
pthread_mutex_t start_lock; 
pthread_cond_t start_cond;
int start_worker_num;
int curr_worker_index;

extern void acceptCommonHandler(WORKER *w, int fd);

void receive_notify_action(aeEventLoop *el, int fd, void *privdata, int mask){

    CONN *c = NULL;

    long lc = 0;

    if (read(fd, (char *)&lc, sizeof(CONN *)) != sizeof(CONN *)) {
        //fprintf(stderr, "notify_pipe read error, errno: %d %m", errno);
        return;
    }

    c = (CONN *)lc;

    int newfd = c->fd;

    if(c) {
        free(c);
    }

    WORKER *w = privdata;
    fprintf(stderr, "--------------%d-----------------\n", w->ind);


    acceptCommonHandler(w, newfd);

}


void init_worker(WORKER *allworker, int worker_count) {
    int i;
    for (i = 0; i < worker_count; i++) {
        int fds[2];
        if (pipe(fds)) {
            //fprintf(stderr, "init_workers pipe error, errno: %d %m\n", errno);
            return;
        }
        WORKER *w = &allworker[i];
        w->notified_rfd = fds[0];
        w->notifed_wfd = fds[1];
        w->ind = i;
        w->el = aeCreateEventLoop(6500);

        if(aeCreateFileEvent(w->el, w->notified_rfd, AE_READABLE, receive_notify_action, w) == -1){
            return;
        }
    }
}


void *start_worker(void *arg) {

    WORKER *w = arg;

    pthread_mutex_lock(&start_lock);
    ++start_worker_num;
    pthread_cond_signal(&start_cond);
    pthread_mutex_unlock(&start_lock);

    aeMain(w->el);
    return NULL;
}



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

        //fprintf(stderr, "read not enough error \n");
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


void acceptCommonHandler(WORKER *w, int fd) {
    if (fd != -1) {
        aepData *data = malloc(sizeof(aepData));
        m_c++;
        memset(data, 0, sizeof(aepData));

        anetNonBlock(NULL,fd);
        anetEnableTcpNoDelay(NULL,fd);

        if (aeCreateFileEvent(w->el, fd, AE_READABLE, readQueryFromClient, data) == -1)
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

    int worker_index = 0;

    pthread_mutex_lock(&start_lock);
    worker_index = (curr_worker_index++ % WORKER_COUNT);
    pthread_mutex_unlock(&start_lock);

    WORKER *w = &allworker[worker_index];

    CONN *c = malloc(sizeof(CONN));
    if (NULL == c) {
        return;
    }
    memset(c, 0, sizeof(CONN));

    c->fd = cfd;
    c->owner = w;

    long lc = (long) c;

    write(w->notifed_wfd, (char *)&lc, sizeof(CONN *));

}

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    fprintf(stderr, "------%ld\n", time(NULL));
    return 1000;
}


int main(int argc, char **argv) {
    int fd;

    el = aeCreateEventLoop(1024);
    if (NULL == el) {
        return -1;
    }

    allworker = malloc(WORKER_COUNT * sizeof(WORKER));
    if (NULL == allworker) {
        return -1;
    }


    pthread_mutex_init(&start_lock, NULL);
    pthread_cond_init(&start_cond, NULL);

    init_worker(allworker, WORKER_COUNT);

    int i;
    for (i = 0; i < WORKER_COUNT; i++) {
        WORKER *w = &allworker[i];
        if (pthread_create(&w->thread_id, NULL, start_worker, w) != 0) {
            //fprintf(stderr, "start_workers create thread error, errno: %d %m\n", errno);
            return -1;
        }
    }

    while (start_worker_num < WORKER_COUNT) {
        pthread_mutex_lock(&start_lock);
        pthread_cond_wait(&start_cond, &start_lock);
        pthread_mutex_unlock(&start_lock);
    }

    fd = anetTcpServer(neterr, 7878, NULL);
    if (fd != -1) {
        anetNonBlock(NULL,fd);
    }


    if(aeCreateFileEvent(el, fd, AE_READABLE, acceptTcpHandler, NULL) == -1){
        return -1;
    }

#if 0
    if(aeCreateTimeEvent(el, 1000, serverCron, NULL, NULL) == -1) {
        return -1;
    }
#endif

    aeMain(el);
    aeDeleteEventLoop(el);

    return 0;
}





