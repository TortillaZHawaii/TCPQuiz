#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <fcntl.h>
#define ERR(source) (perror(source),                                 \
                     fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
                     exit(EXIT_FAILURE))

#define BACKLOG 3
volatile sig_atomic_t do_work = 1;
volatile sig_atomic_t accept_users = 1;

void sigint_handler(int sig)
{
    do_work = 0;
}
void sigusr1_handler(int sig)
{
    accept_users = 0;
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
int make_socket(int domain, int type)
{
    int sock;
    sock = socket(domain, type, 0);
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

int bind_tcp_socket(char* address, char * port)
{
    struct sockaddr_in addr = make_address(address, port);
    int socketfd, t = 1;
    socketfd = make_socket(PF_INET, SOCK_STREAM);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (listen(socketfd, BACKLOG) < 0)
        ERR("listen");
    return socketfd;
}
int add_new_client(int sfd) // 
{
    int nfd;
    if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, NULL, NULL))) < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return nfd;
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
    fprintf(stderr, "USAGE: %s address port max_users\n", name);
    exit(EXIT_FAILURE);
}

int writeMessageTo(int cfd, char* message, size_t message_size) {
    if (bulk_write(cfd, message, message_size) < 0)
    {
        if(errno == EPIPE) // klient sie odlaczyl
            return -1;
        ERR("write:");
    }
    return 0; // ok
}

void write_no(int cfd) {
    if (bulk_write(cfd, "NIE", sizeof(char[4])) < 0)
        ERR("write:");
}

int findFreeIndex(int* connections, int max_users) {
    for(int i = 0; i < max_users; ++i)
    {
        if(connections[i] == 0)
            return i;
    }
    return -1;
}

void writeToAll(int* connections, int max_users, char* message, size_t message_size) {
    for (int i = 0; i < max_users; ++i)
    {
        if(connections[i] > 0) {
            if(writeMessageTo(connections[i], message, message_size) < 0) // try to write x
            {
                connections[i] = 0; // zwalnianie miejsca
            }
        }
    }
}

void debug_print_tab(int* tab, int tab_size){
    for(int i = 0; i < tab_size; ++i)
        printf("%d. %d\n", i, tab[i]);
    printf("\n");
}

void checkNewClient(int fd, int* cons, int max_users) {
    int cfd;
    if ((cfd = add_new_client(fd)) >= 0)
    {
        int index = findFreeIndex(cons, max_users);
        
        if(index >= 0) // zapisujemy do tabeli cons
            cons[index] = cfd;
        else { // brak miejsca
            write_no(cfd);
            if(TEMP_FAILURE_RETRY(close(cfd)) < 0)
                ERR("close");
        }
    }
}

void doServer(int fd, int max_users) 
{
    int* cons = (int*)malloc(sizeof(int) * max_users); 
    if(cons == NULL)
        ERR("malloc");

    for(int i = 0; i < max_users; ++i)
    {
        cons[i] = 0;
    }

    fd_set base_rfds, rfds;
    sigset_t mask, oldmask;
    FD_ZERO(&base_rfds);
    FD_SET(fd, &base_rfds);
    int fdmax = fd;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    
    while (do_work)
    {
        if(accept_users == 0) {
            fdmax = -1;
            if(fd && TEMP_FAILURE_RETRY(close(fd)) < 0) // zamykanie
                ERR("close");
            accept_users = 1; // don't repeat this if
        }
        
        rfds = base_rfds;
        struct timespec t330s; // 330 ms
        t330s.tv_nsec = 330000000l; 
        t330s.tv_sec = 0;

        //debug_print_tab(cons, max_users);

        int pselect_status = pselect(fdmax + 1, &rfds, NULL, NULL, &t330s, &oldmask);

        if (pselect_status > 0) // selected
        {
            checkNewClient(fd, cons, max_users);
        }
        else if (pselect_status == 0) // timeout
        {
            writeToAll(cons, max_users, "x", sizeof("x"));
        }
        else // blad
        {
            if (EINTR == errno)
            {
                errno = 0;
                continue;
            }
            ERR("pselect");
        }
    }

    // zwalnianie cons
    for(int i = 0; i < max_users; ++i)
    {
        if(cons[i] > 0 && TEMP_FAILURE_RETRY(close(cons[i])) < 0)
            ERR("close");
    }
    free(cons);

    sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

int main(int argc, char** argv)
{
    int fd;
    int new_flags;

    if(argc != 4)
    {
        usage(argv[0]);
    }

    
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
    if(sethandler(sigusr1_handler, SIGUSR1))
        ERR("Setting SIGUSR1");
    
    fd = bind_tcp_socket(argv[1], argv[2]);
    
    if((new_flags = fcntl(fd, F_GETFL) | O_NONBLOCK) == -1) ERR("fcntl");
    if(fcntl(fd, F_SETFL, new_flags) == -1) ERR("fcntl");

    doServer(fd, atoi(argv[3]));

    if(fd && TEMP_FAILURE_RETRY(close(fd)) < 0)
        ERR("close");

    printf("server closed");

    return EXIT_SUCCESS;
}