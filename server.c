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
#include <sys/stat.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>
#define ERR(source) (perror(source),                                 \
                     fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
                     exit(EXIT_FAILURE))

#define QUESTION_MAXLENGTH 2001

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
    fprintf(stderr, "USAGE: %s address port max_users file_path\n", name);
    exit(EXIT_FAILURE);
}

typedef struct question_t {
    int16_t length;
    char *text;
} question_t;

typedef struct connection_t {
    int fd;
    question_t *question;
    int16_t writtenbytes;
} connect_t;


int randinrange(int a, int b) {
    return rand()%(b-a+1) + a;
}

// returns how many questions were read
int readQuestions(char* filePath, question_t** questions)
{
    int no_questions = 0;
    struct stat st;
    stat(filePath, &st);
    size_t fileSize = st.st_size;
    
    // read whole file to buffer
    char *buf = (char*)malloc(fileSize);
    if(!buf) ERR("malloc");

    int fd;
    if((fd = TEMP_FAILURE_RETRY(open(filePath, O_RDONLY))) < 0)
        ERR("open");
    if(bulk_read(fd, buf, fileSize) < fileSize)
        ERR("read");

    // count questions
    for(int i = 0; i < fileSize; i++)
        if(buf[i] == '\n')
        {
            buf[i] = '\0';
            no_questions++;
        }    

    *questions = (question_t*)malloc(no_questions * sizeof(question_t));
    if(!(*questions))
        ERR("malloc");

    ssize_t offset = 0;
    for(int i = 0; i < no_questions; i++)
    {
        (*questions)[i].length = strlen(buf + offset) + 1;

        (*questions)[i].text = (char*) malloc(sizeof(char) * (*questions)[i].length);
        if(!((*questions)[i].text))
            ERR("malloc question text");

        strncpy((*questions)[i].text, (buf + offset), (*questions)[i].length);

        offset += (*questions)[i].length;
    }

    free(buf);


    return no_questions;
}

int writeText(int fd, char *data, char *text, int textsize, int isLast) {
    size_t data_size = sizeof(int16_t)*2 + textsize*sizeof(char);

    int16_t b1 = htons(textsize);
    int16_t b2 = htons(isLast); //islast

    memcpy(&data[0], (void*)&b1, sizeof(int16_t));
    memcpy(&data[2], (void*)&b2, sizeof(int16_t)); // islast
    strncpy(&data[4], text, textsize);

    if(bulk_write(fd, data, data_size) < data_size) {
        if(errno == EPIPE) // klient sie odlaczyl
        {
            return -1;
        }
        ERR("write:");
    }
    return 0;
}

// 1 last
// 0 zostalo
// -1 zerwano polaczenie
int writeMessageTo(connect_t* con, int bytesToWriteLeft) {
    int16_t bytesToWrite = randinrange(1, bytesToWriteLeft);

    size_t data_size = sizeof(int16_t)*2 + bytesToWrite*sizeof(char);

    char* data = (char*) malloc(data_size);

    int writeStatus = writeText(con->fd, data, &con->question->text[con->writtenbytes], 
                        bytesToWrite, bytesToWrite == bytesToWriteLeft);

    free(data);

    if(writeStatus < 0)
        return writeStatus;

    con->writtenbytes += bytesToWrite;

    return bytesToWrite == bytesToWriteLeft;
}

void write_no(int cfd) {
    size_t data_size = sizeof(int16_t)*2 + sizeof("NIE");
    char *data = (char*) malloc(data_size);

    writeText(cfd, data, "NIE", sizeof("NIE"), -1);

    free(data);
}

void write_end(int cfd) {
    size_t data_size = sizeof(int16_t)*2 + sizeof("KONIEC");
    char *data = (char*) malloc(data_size);

    writeText(cfd, data, "KONIEC", sizeof("KONIEC"), -1);

    free(data);
}

int findFreeIndex(connect_t* connections, int max_users) {
    for(int i = 0; i < max_users; ++i)
    {
        if(connections[i].fd == 0)
            return i;
    }
    return -1;
}

void writeToAll(connect_t* cons, int max_users, fd_set *baserfds, int *open_connections) {
    for (int i = 0; i < max_users; ++i)
    {
        if(cons[i].fd > 0 && cons[i].writtenbytes != -1) {
            int bytesToWriteLeft = cons[i].question->length - cons[i].writtenbytes;

            if(bytesToWriteLeft > 0) {
                int writeStatus = writeMessageTo(&cons[i], bytesToWriteLeft);

                if(writeStatus == 1) { // last
                    *open_connections = *open_connections - 1;
                    cons[i].writtenbytes = -1; // waiting for answer
                }
                else if(writeStatus < 0) // disconnected
                {
                    FD_CLR(cons[i].fd, baserfds);
                    cons[i].fd = 0; // zwalnianie miejsca
                    if(cons[i].writtenbytes == -1)
                        *open_connections = *open_connections - 1;
                    cons[i].question = NULL;
                    cons[i].writtenbytes = 0;
                }
            }
        }
    }
}

void debug_print_tab(int* tab, int tab_size){
    for(int i = 0; i < tab_size; ++i)
        printf("%d. %d\n", i, tab[i]);
    printf("\n");
}

question_t* getRandomQuestion(question_t* questions, int no_questions) {
    return &questions[randinrange(0, no_questions - 1)];
}

void checkNewClient(int fd, connect_t* cons, int max_users, fd_set* base_rfds, int* fd_max,
    question_t* questions, int no_questions, int* open_connections) {

    int cfd;
    if ((cfd = add_new_client(fd)) >= 0)
    {
        int index = findFreeIndex(cons, max_users);
        
        if(index >= 0) {// zapisujemy do tabeli cons
            cons[index].fd = cfd;
            *open_connections = *open_connections + 1;
            cons[index].question = getRandomQuestion(questions, no_questions);
            cons[index].writtenbytes = 0;
            if(cfd > *fd_max)
                *fd_max = cfd;

            FD_SET(cfd, base_rfds);
        }
        else { // brak miejsca
            write_no(cfd);
            if(TEMP_FAILURE_RETRY(close(cfd)) < 0)
                ERR("close");
        }
    }
}

void readFromClient(connect_t* cons, int max_users, fd_set *rfds, fd_set *base_rfds, question_t* questions, 
    int no_questions, int* open_connections) {

    for(int i = 0; i < max_users; ++i) {
        if(FD_ISSET(cons[i].fd, rfds)) {
            // read byte
            char ans;
            int bytes_read = bulk_read(cons[i].fd, (char*)&ans, sizeof(char));
            if(bytes_read == 0) {
                // klient sie odlaczyl
                if(cons[i].writtenbytes == -1)
                        *open_connections = *open_connections - 1;
                FD_CLR(cons[i].fd, base_rfds);
                cons[i].question = NULL;
                cons[i].fd = 0;
                cons[i].writtenbytes = 0;
                continue;
            }
            else if (bytes_read < 0) {
                ERR("read");
            }

            // new question
            *open_connections = *open_connections + 1;
            cons[i].question = getRandomQuestion(questions, no_questions);
            cons[i].writtenbytes = 0;
        }
    }
}

void doServer(int fd, int max_users, question_t* questions, int no_questions) 
{
    connect_t* cons = (connect_t*)malloc(sizeof(connect_t) * max_users);
    if(!cons) 
        ERR("malloc");

    for(int i = 0; i < max_users; ++i)
    {
        cons[i].fd = 0;
        cons[i].question = NULL;
        cons[i].writtenbytes = 0;
    }

    int open_connections = 0;

    fd_set base_rfds, rfds;
    sigset_t mask, oldmask;
    FD_ZERO(&base_rfds);
    FD_SET(fd, &base_rfds);
    int fdmax = fd;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    struct timespec t330s; // 330 ms
    t330s.tv_nsec = 330000000l; 
    t330s.tv_sec = 0;
    
    while (do_work)
    {
        if(accept_users == 0) {
            fdmax = -1;
            if(fd && TEMP_FAILURE_RETRY(close(fd)) < 0) // zamykanie
                ERR("close");
            accept_users = 1; // don't repeat this if
        }
        
        rfds = base_rfds;

        struct timespec *tptr = NULL;
        if(open_connections < 0)
            open_connections = 0;
        printf("opened: %d\n", open_connections);
        if(open_connections) { // jesli otwarte
            tptr = &t330s;
        }

        int pselect_status = pselect(fdmax + 1, &rfds, NULL, NULL, tptr, &oldmask);

        if (pselect_status > 0) // selected
        {
            if(FD_ISSET(fd, &rfds)) {
                printf("new client\n");
                checkNewClient(fd, cons, max_users, &base_rfds, &fdmax, questions, no_questions, &open_connections);
            }
            else {
                printf("client\n");
                readFromClient(cons, max_users, &rfds, &base_rfds, questions, no_questions, &open_connections);
            }
        }
        else if (pselect_status == 0) // timeout
        {
            printf("write\n");
            writeToAll(cons, max_users, &base_rfds, &open_connections);
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
        if(cons[i].fd > 0) {
            write_end(cons[i].fd);
            if(TEMP_FAILURE_RETRY(close(cons[i].fd)) < 0)
                ERR("close");
        }
    }
    free(cons);

    sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

int main(int argc, char** argv)
{
    int fd;
    int new_flags;
    question_t* questions = NULL;

    if(argc != 5)
    {
        usage(argv[0]);
    }
    
    int question_count = readQuestions(argv[4], &questions);

    srand((uint)time(NULL));

    for(int i = 0; i < question_count; ++i) {
        printf("%s\n", questions[i].text);
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

    doServer(fd, atoi(argv[3]), questions, question_count);

    if(fd && TEMP_FAILURE_RETRY(close(fd)) < 0)
        ERR("close");


    // free questions
    for(int i = 0; i < question_count; ++i) {
        free(questions[i].text);
    }
    free(questions);

    printf("server closed\n");

    return EXIT_SUCCESS;
}