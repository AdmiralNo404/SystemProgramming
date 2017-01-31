#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>

#define PORT "6789"  // the port users will be connecting to
#define BACKLOG 10     // how many pending connections queue will hold
#define NAMESIZE 256
#define BUFSIZE 1024

FILE* logfp;
pthread_mutex_t logprotect;

typedef struct {int fd; char* IP_addr; char* filename;} courier;

void sigchld_handler(int s) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// get current time
void get_time(char* buffer) {
	time_t  clocktime;
	struct tm  *timeinfo;
	time (&clocktime);
	timeinfo = localtime( &clocktime);
	strftime(buffer, BUFSIZE, "%b-%d-%Y-%H-%M-%S", timeinfo);
}

// get number of lines in a file
int file_line_number(char* filename) {
    FILE* tempfp = fopen(filename, "r");
    int lines = 0;
    char c;
    while ((c = fgetc(tempfp)) != EOF) {
        if (c == '\n')
            lines++;
    }
    fclose(tempfp);
    return lines;
}

// open all files indicated in config and make an index
void open_config(char* filename, FILE* streams[], char* index[], int lines) {
    FILE *cfgfp = fopen(filename, "r");
    int i = 0, total = 0;
    while (!feof(cfgfp) && i < lines) {
        char temp[NAMESIZE];
        fscanf(cfgfp, "%s %s", index[i], temp);
        // eliminate the quote symbol
        int j = strlen(index[i])-1;
        index[i][j] = '\0';
        // done
        streams[i] = fopen(temp, "r");
        i++;
    }
    fclose(cfgfp);
}

// conbine the names in index to string
void combine_string(char* message, char* index[], int lines) {
    strcpy(message, index[0]);
    strcat(message, "\n");
    int i;
    for (i = 1; i < lines; i++) {
        strcat(message, index[i]);
        strcat(message, "\n");
    }
}

// get quote from the required file with index i
void get_quote(char* quote, FILE* streams[], int i) {
    char* name = (char*) malloc (sizeof(char) * NAMESIZE);
    if (fgets(quote, BUFSIZE, streams[i]) == NULL) {
        rewind(streams[i]);
        fgets(quote, BUFSIZE, streams[i]);
    }
    if (fgets(name, NAMESIZE, streams[i]) == NULL) {
        rewind(streams[i]);
        printf("%s", quote);
        fgets(quote, BUFSIZE, streams[i]);
        fgets(name, NAMESIZE, streams[i]);
    }
    strcat(quote, name);
}

void *server_thread(void *input) {
    courier* bird = (courier*) input;
    char addr[BUFSIZE], config[NAMESIZE];
    strcpy(addr, bird->IP_addr);
    strcpy(config, bird->filename);
    int new_fd = bird->fd;
    char buffer[BUFSIZE];
    
    // write LOGFILE
    get_time(buffer);        
    pthread_mutex_lock(&logprotect);
    logfp = fopen("LOGFILE", "a");
    fprintf(logfp, "Connection Opened: %s: %s\n", buffer, addr);
    fclose(logfp);
    pthread_mutex_unlock(&logprotect);
    // done
    
    // get the number of lines in config file
    int lines = file_line_number(config);
    FILE* streams[lines];
    char* index[lines];
    // open files in config.txt
    int i;
    for (i = 0; i < lines; i++)
        index[i] = (char*) malloc (sizeof(char) * NAMESIZE);
    open_config(config, streams, index, lines);
    // done
        
    while (1) {
        // read request from client
        int rval;
        char option[BUFSIZE];
        if ((rval = read(new_fd, option, BUFSIZE)) < 0) {
            perror("while reading from socket");
        }
        // block ends
        
        char* quote = (char*) malloc (sizeof(char) * BUFSIZE);

        if (strcmp(option, "BYE\n") == 0) {
            break;
        } else if (strcmp(option, "GET: LIST\n") == 0) {
            char message[BUFSIZE];
            combine_string(message, index, lines);
            if (send(new_fd, message, strlen(message), 0) == -1)
                perror("send");
        } else if (strcmp(option, "GET: QUOTE CAT: ANY\n") == 0) {
            //a random number between 0 to lines-1
            i = rand() % lines;
            get_quote(quote, streams, i);
            if (send(new_fd, quote, strlen(quote), 0) == -1)
                perror("send");
        } else {
            char* name = (char*) malloc (sizeof(char) * NAMESIZE);
            sscanf(option, "GET: QUOTE CAT: %s\n", name);
            // search for coresponding name
            int found = 0;
            for (i = 0; i < lines; i++) {
                if (strcmp(name, index[i]) == 0 && option[0] == 'G') {
                    get_quote(quote, streams, i);
                    if (send(new_fd, quote, strlen(quote), 0) == -1)
                        perror("send");
                    found = 1;
                    break;
                }
            }
            // if not found
            if (found == 0 && option[0] == 'G') {
                if (send(new_fd, "Sorry, no quote for this category.", BUFSIZE, 0) == -1)
                    perror("send");
            }
        }
    }
    
    // write LOGFILE
    get_time(buffer);
    pthread_mutex_lock(&logprotect);
    logfp = fopen("LOGFILE", "a");
    fprintf(logfp, "Connection Closed: %s: %s\n", buffer, addr);
    fclose(logfp);
    pthread_mutex_unlock(&logprotect);
    // done
    
    // close all other file streams
    for (i = 0; i < lines; i++) {
        fclose(streams[i]);
    }
    // block ends
    
    printf("server: disconnected from %s\n", addr);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    srand(time(NULL)); //initialize rand
    
    if (argc != 2) {
        printf("Usage: %s filename\n", argv[0]);
        exit(0);
    }
    
    // clean and open LOGFILE
    logfp = fopen("LOGFILE", "w");
    
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    
    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
        
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        
        break;
    }
    
    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }
    
    freeaddrinfo(servinfo); // all done with this structure
    
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
    
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    printf("server: waiting for connections...\n");
    
    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
        
        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  s, sizeof s);
        printf("server: got connection from %s\n", s);
        
        pthread_t tid;
        courier bird;
        bird.fd = new_fd;
        bird.IP_addr = s;
        bird.filename = argv[1];
        int rc;
        
        pthread_mutex_init(&logprotect, NULL);
        
        if (rc = pthread_create(&tid, NULL, server_thread, (void*)&bird)) {
            printf ("ERROR; return code from pthread_create() is %d\n", rc);
            exit (-1);
        }
        
    }
    
    return 0;
}