#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h> // for getnameinfo()
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Socket headers
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <semaphore.h>

#include "request.h"
#include "server.h"

// For enabling strncasecmp(), getnameinfo(), etc.
#define _POSIX_C_SOURCE 200809L

#define VERSION 23
#define DEFAULT_PORT 8080

HTTP_Server http_server;

typedef struct {
    int buffer[1024];
    int in;                  // insertion index
    int out;                 // removal index
    int size;                // current number of items
    int capacity;            // maximum capacity
    pthread_mutex_t mutex;   // protects buffer access
    sem_t empty;             // counts empty slots
    sem_t full;              // counts full slots
} conn_buffer_t;

// Global variables for thread pool and buffer
conn_buffer_t connection_buffer;
pthread_t *worker_threads;
int thread_pool_size;

void *worker_thread(void *arg);

// Buffer functions
void buffer_init(conn_buffer_t *cb, int capacity) {
    cb->in = 0;
    cb->out = 0;
    cb->size = 0;
    cb->capacity = capacity;
    pthread_mutex_init(&cb->mutex, NULL);
    sem_init(&cb->empty, 0, capacity);
    sem_init(&cb->full, 0, 0);
}

void buffer_put(conn_buffer_t *cb, int connfd) {
    sem_wait(&cb->empty);
    pthread_mutex_lock(&cb->mutex);
    
    cb->buffer[cb->in] = connfd;
    cb->in = (cb->in + 1) % cb->capacity;
    cb->size++;
    
    pthread_mutex_unlock(&cb->mutex);
    sem_post(&cb->full);
}

int buffer_get(conn_buffer_t *cb) {
    int connfd;
    
    sem_wait(&cb->full);
    pthread_mutex_lock(&cb->mutex);
    
    connfd = cb->buffer[cb->out];
    cb->out = (cb->out + 1) % cb->capacity;
    cb->size--;
    
    pthread_mutex_unlock(&cb->mutex);
    sem_post(&cb->empty);
    
    return connfd;
}

void buffer_destroy(conn_buffer_t *cb) {
    pthread_mutex_destroy(&cb->mutex);
    sem_destroy(&cb->empty);
    sem_destroy(&cb->full);
}

// Worker thread function
void *worker_thread(void *arg) {
    while (1) {
        int connfd = buffer_get(&connection_buffer);
        handle_request((void *)&connfd);
        close(connfd);
    }
    return NULL;
}

void logMessage(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    char buf[1024];

    vsnprintf(buf, sizeof(buf), format, arg);
    va_end(arg);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "[%H:%M:%S]", tm);

    printf("%s %s\n", time_str, buf);
}

void cleanup(int sig) {
    printf("Cleaning up connections and exiting.\n");

    // try to close the listening socket
    if (close(http_server.socket) < 0) {
        fprintf(stderr, "Error calling close()\n");
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}

// Custom report function
void report(struct sockaddr_in *serverAddress) {
    char hostBuffer[INET6_ADDRSTRLEN];
    char serviceBuffer[NI_MAXSERV]; // defined in <netdb.h>
    socklen_t addr_len = sizeof(*serverAddress);
    int err =
        getnameinfo((struct sockaddr *)serverAddress, addr_len, hostBuffer,
                    sizeof(hostBuffer), serviceBuffer, sizeof(serviceBuffer),
                    NI_NUMERICHOST | NI_NUMERICSERV);

    if (err != 0) {
        fprintf(stderr, "getnameinfo: %s\n", gai_strerror(err));
    }

    logMessage("\n\tServer listening on http://%s:%s\n", hostBuffer,
               serviceBuffer);
}

// Main function
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int threads = 2;
    char *docroot = "docroot";
    int c;

    while ((c = getopt(argc, argv, "d:p:t:")) != -1)
        switch (c) {
        case 'd':
            docroot = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 't':
            threads = atoi(optarg);
            break;
        default:
            printf("Usage: %s [-d docroot] [-p port] [-t threads]\n", argv[0]);
            printf("  port: Port number (default: 8080)\n");
            printf("  docroot: Document root directory (default: docroot)\n");
            printf(
                "  threads: Number of threads in thread pool (default: 10)\n");
            exit(EXIT_FAILURE);
        }

    // Get current working directory
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd() error");
        exit(EXIT_FAILURE);
    }

    // Build absolute path to docroot
    char abs_docroot[1024];
    if (docroot[0] == '/') {
        strncpy(abs_docroot, docroot, sizeof(abs_docroot) - 1);
    } else {
        snprintf(abs_docroot, sizeof(abs_docroot), "%s/%s", cwd, docroot);
    }
    printf("Document root: %s\n", abs_docroot);

    // Verify docroot exists
    struct stat st;
    if (stat(abs_docroot, &st) == -1 || !S_ISDIR(st.st_mode)) {
        printf("Error: Document root %s is not a directory\n", abs_docroot);
        exit(EXIT_FAILURE);
    }

    if (chdir(abs_docroot) == -1) {
        printf("Error: Can't change to directory %s\n", abs_docroot);
        exit(EXIT_FAILURE);
    }

    if (port <= 0 || port > 49151) {
        printf("Invalid port number: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    // set up signal handler for ctrl-C
    (void)signal(SIGINT, cleanup);

    struct sockaddr_in client_address;

    // initiate server
    init_server(&http_server, port);

    // report
    report(http_server.address);

    // Ignore SIGPIPE signal, so if browser cancels the request, it
    // won't kill the whole process.
    signal(SIGPIPE, SIG_IGN);

    // Initialize thread pool and connection buffer
    buffer_init(&connection_buffer, threads*10);
    thread_pool_size = threads;
    worker_threads = malloc(threads * sizeof(pthread_t));
    
    // Create worker threads
    for (int i = 0; i < threads; i++) {
        if (pthread_create(&worker_threads[i], NULL, worker_thread, NULL) != 0) {
            perror("Failed to create worker thread");
            exit(EXIT_FAILURE);
        }
    }

    while (1) {
        int client_fd = accept(http_server.socket, (struct sockaddr *)&client_address,&http_server.address_len);
        
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        
        logMessage("Connection accepted from %s:%d", 
                  inet_ntoa(client_address.sin_addr),
                  ntohs(client_address.sin_port));

        // Add connection to buffer
        buffer_put(&connection_buffer, client_fd);
    }

    return EXIT_SUCCESS;
}
