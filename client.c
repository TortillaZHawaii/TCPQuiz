#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#define ERR(source) (perror(source),                                 \
                     fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
                     exit(EXIT_FAILURE))

#define QUESTION_MAXLENGTH 2001

volatile sig_atomic_t do_work = 1;
void sigint_handler(int sig)
{
    do_work = 0;
}

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}
int make_socket(void)
{
    int sock;
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}
struct sockaddr_in make_address(char *address, char *port)
{
    int ret;
    struct sockaddr_in addr;
    struct addrinfo *result;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    if ((ret = getaddrinfo(address, port, &hints, &result)))
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }
    addr = *(struct sockaddr_in *)(result->ai_addr);
    freeaddrinfo(result);
    return addr;
}
int connect_socket(char *name, char *port)
{
    struct sockaddr_in addr;
    int socketfd;
    socketfd = make_socket();
    addr = make_address(name, port);
    if (connect(socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
    {
        if (errno != EINTR)
            ERR("connect");
        else
        {
            fd_set wfds;
            int status;
            socklen_t size = sizeof(int);
            FD_ZERO(&wfds);
            FD_SET(socketfd, &wfds);
            if (TEMP_FAILURE_RETRY(select(socketfd + 1, NULL, &wfds, NULL, NULL)) < 0)
                ERR("select");
            if (getsockopt(socketfd, SOL_SOCKET, SO_ERROR, &status, &size) < 0)
                ERR("getsockopt");
            if (0 != status)
                ERR("connect");
        }
    }
    return socketfd;
}
ssize_t bulk_read(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (0 == c)
            return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}
ssize_t bulk_write(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s (domain port)\n", name);
}

typedef struct connection_t {
    int fd;
    char question[QUESTION_MAXLENGTH];
    int16_t readbytes;
} connect_t;

int readQuestion(connect_t *con) { // 0 if not fully read, 1 - fully read
    int16_t data[2];
    if(bulk_read(con->fd, (char*)data, sizeof(int16_t[2])) < 0)
        ERR("READ data:");
    int16_t bytesToRead = ntohs(data[0]);
    int16_t isLast = ntohs(data[1]);

    // printf("%d %d\n", bytesToRead, isLast);
    // printf("%d\n", con->readbytes);
    if(isLast < 0) {
        // nie | koniec
        char *buf = (char*) malloc(sizeof(char) * bytesToRead);
        if(!buf)
            ERR("malloc");
        int bytesRead = bulk_read(con->fd, buf, sizeof(char) * bytesToRead);
        if(bytesToRead < 0)
            ERR("read last");
        if(bytesToRead == bytesRead) {
            printf("%s\n", buf);
        }
        free(buf);
        return isLast;
    }

    if(bulk_read(con->fd, (char*)&con->question[con->readbytes], sizeof(char) * bytesToRead) < (sizeof(char) * bytesToRead))
        ERR("READ question:");
    
    con->readbytes += bytesToRead;
    return isLast;
}

void doClient(connect_t* cons, int no_cons) {
    int ready_question = -1;
    int fdmax = 0;

    int connected_servers = no_cons;

    fd_set base_rfds, rfds;
    sigset_t mask, oldmask;
    FD_ZERO(&base_rfds);
    for(int i = 0; i < no_cons; ++i) {
        FD_SET(cons[i].fd, &base_rfds);
        if(fdmax < cons[i].fd)
            fdmax = cons[i].fd;
    }
    FD_SET(STDIN_FILENO, &base_rfds); // zawsze 0 < fd
    
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    while(do_work) {
        if(connected_servers <= 0)
        {
            do_work = 0;
            continue;
        }

        rfds = base_rfds;

        int pselect_status = pselect(fdmax + 1, &rfds, NULL, NULL, NULL, &oldmask);

        if (pselect_status > 0 && do_work)
        {
            if(FD_ISSET(STDIN_FILENO, &rfds)) {
                if(ready_question < 0)
                    printf("nie teraz!\n");
                else {
                    char c;
                    // czytanie odpowiedzi
                    if(bulk_read(STDIN_FILENO, (char*)&c, sizeof(char)) < sizeof(char))
                        ERR("bulk_read");
                    
                    if(bulk_write(cons[ready_question].fd, (char*)&c, sizeof(char)) < sizeof(char))
                        ERR("bulk_write");
                    
                    ready_question = -1;
                }
                
                // clearing stdin
                int a;
                while((a = getchar()) != '\n' && a != EOF);
            }
            else {
                for(int i = 0; i < no_cons; ++i) {
                    if(FD_ISSET(cons[i].fd, &rfds)) {
                        int status = readQuestion(&cons[i]);
                        if(status > 0) {
                            printf("%s\n", cons[i].question);
                            cons[i].readbytes = 0;
                            if(ready_question >= 0) { // wysylamy 0 do serwera
                                uint8_t zero = 0;
                                if(bulk_write(cons[ready_question].fd, (char*)&zero, sizeof(uint8_t)) < 0)
                                    ERR("write");
                            }
                            ready_question = i;
                        } else if (status < 0) {
                            FD_CLR(cons[i].fd, &base_rfds);
                            if (cons[i].fd && TEMP_FAILURE_RETRY(close(cons[i].fd)) < 0)
                                ERR("close");
                            cons[i].fd = 0;
                            cons[i].readbytes = 0;
                            connected_servers--;
                        }
                    }
                }
            }
        }
        else {
            if (EINTR == errno)
            {
                errno = 0;
                continue;
            }
            ERR("pselect");
        }
    }

    sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

int main(int argc, char **argv)
{
    if (argc == 1 || argc % 2 != 1)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    int no_cons = argc/2;
    connect_t* cons = (connect_t*)malloc(sizeof(connect_t) * no_cons);
    if(!cons) 
        ERR("malloc");
    
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Setting SIGINT:");

    for(int i = 0; i < no_cons; ++i) {
        int con_number = 2*i + 1;
        printf("%d %s %s\n", i, argv[con_number], argv[con_number + 1]);
        cons[i].fd = connect_socket(argv[con_number], argv[con_number + 1]);
        cons[i].readbytes = 0;
    }
    
    doClient(cons, no_cons);

    for(int i = 0; i < no_cons; ++i) {
        if (cons[i].fd && TEMP_FAILURE_RETRY(close(cons[i].fd)) < 0)
            ERR("close");
    }

    free(cons);
    return EXIT_SUCCESS;
}