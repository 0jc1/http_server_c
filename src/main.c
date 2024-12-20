#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <netdb.h> // for getnameinfo()
#include <stdlib.h>
#include <string.h>

// Socket headers
#include <sys/stat.h>
#include <sys/types.h>

#include <arpa/inet.h>

#include "server.h"

#define VERSION 23
#define DEFAULT_PORT 8080
#define BUFFER_SIZE 8096

struct MimeType {
    char *extension;
    char *type;
} mimeTypes[] = {
    {"gif", "image/gif"},   {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
    {"png", "image/png"},   {"css", "text/css"},   {"ico", "image/x-icon"},
    {"zip", "application/zip"}, {"gz", "application/gzip"},
    {"tar", "application/x-tar"}, {"htm", "text/html"}, {"html", "text/html"},
    {"txt", "text/plain"}, {NULL, NULL} 
};

enum HttpStatusCode {
    OK = 200,
    CREATED = 201,
    ACCEPTED = 202,
    NO_CONTENT = 204,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    UNSUPPORTED_MEDIA_TYPE = 415,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    SERVICE_UNAVAILABLE = 503
};

HTTP_Server http_server;

// Function to get MIME type based on file extension
const char *getMimeType(const char *fileExtension) {
    if (fileExtension == NULL) {
        return "application/octet-stream";
    }

    for (int i = 0; mimeTypes[i].extension != NULL; i++) {
        if (strcasecmp(fileExtension, mimeTypes[i].extension) == 0) {
            return mimeTypes[i].type;
        }
    }
    return "application/octet-stream";  // Default MIME type
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

    // output to log file
    /*
    int fd = open("server.log", O_WRONLY | O_APPEND | O_CREAT | O_NONBLOCK,
    S_IRUSR | S_IWUSR);

    if (fd == -1)
    {
        perror("Error opening file");
        return;
    }
    char newline = '\n';
    write(fd, &newline, 1); // write new line

    ssize_t bytesWritten = write(fd, time_str, strlen(time_str));
    if (bytesWritten == -1)
    {
        perror("Error writing to file");
    }

    // Close the log file
    close(fd);
    */
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

int file_exists(const char *filename) {
    struct stat buffer;
    return stat(filename, &buffer) == 0;
}

FILE *get_file(char *fileName, int statusCode) {
    FILE *file;

    switch (statusCode) {
        case OK:
            break;
        case NOT_FOUND:
            fileName = "404.html";
            break;
        case UNSUPPORTED_MEDIA_TYPE:
            file = NULL;
            break;
        case UNAUTHORIZED:
            fileName = "400.html";
            break;
        default:
            break;
    }

    file = fopen(fileName, "r");

    return file;
}

char *render_static_file(FILE *file, long *len) {
    if (file == NULL) {
        fprintf(stderr, "Failed to render. File is null\n");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    *len = fsize * sizeof(char);
    fseek(file, 0, SEEK_SET);

    char *temp = calloc(sizeof(char), (fsize + 1));
    char ch;
    int i = 0;
    while ((ch = fgetc(file)) != EOF) {
        temp[i] = ch;
        i++;
    }
    fclose(file);
    return temp;
}

void *handle_request(void *client_fd) {
    int client_socket = *((int *)client_fd);
    char buffer[BUFFER_SIZE] = {0};
    enum HttpStatusCode statusCode = OK;
    char reasonPhrase[100] = "OK";

    size_t i = 0;

    // read client request
    int ret = read(client_socket, buffer, BUFFER_SIZE - 1);

    if (ret == 0 || ret == -1) {
        logMessage("failed to read client request");
        return NULL;
    }

    if (ret > 0 && ret < BUFFER_SIZE) {
        buffer[ret] = 0;
    } else {
        buffer[0] = 0;
    }

    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
        logMessage("Only simple GET operation supported");
        return NULL;
    }
    // null terminate after the second space to ignore extra stuff
    for (i = 4; i < BUFFER_SIZE; i++) {
        if (buffer[i] == ' ') {
            buffer[i] = 0;
            break;
        }
    }

    logMessage("read request %s", buffer);

    /* check for illegal parent directory use .. */
    if (strstr(buffer, "..")) {
        logMessage("Parent directory (..) path names not supported");
        statusCode = FORBIDDEN;
        strcpy(reasonPhrase, "Forbidden");
    }

    // TODO show files in a directory
    if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0",6)) /* expand no filename to index file */
        strcpy(buffer, "GET /index.html");

    /* work out the file type and check if it's supported */
    long len;
    const char *fstr = (char *)0;
    char *extension = 0;

    // Find the position of the dot
    char *dotPosition = strchr(&buffer[4], '.');

    if (dotPosition != NULL) {
        extension = dotPosition + 1;
    }

    fstr = getMimeType(extension);

    if (fstr == 0) {
        logMessage("file extension type not supported.");
        statusCode = UNSUPPORTED_MEDIA_TYPE;
        strcpy(reasonPhrase, "Unsupported Media Type");
    }

    char *file_name = &buffer[5];

    if (!file_exists(file_name)) {
        logMessage("failed to find file %s", file_name);
        statusCode = NOT_FOUND;
        strcpy(reasonPhrase, "Not Found");
    }

    FILE *file = get_file(file_name, statusCode);
    char *file_data = render_static_file(file, &len);

    if (file_data == NULL) {
        logMessage("failed to open file %s", file_name);
    }

    logMessage("SEND");
    snprintf(buffer, BUFFER_SIZE,
            "HTTP/1.1 %d %s\r\nServer: nweb/%d.0\r\nContent-Length: "
            "%ld\r\nConnection: close\r\nContent-Type: %s\r\n\r\n",
            statusCode, reasonPhrase, VERSION, len,
            fstr); /* Header + a blank line */

    (void)send(client_socket, buffer, strnlen(buffer, BUFFER_SIZE), 0);

    (void)send(client_socket, file_data, len, 0);

    sleep(1); /* allow socket to drain before signalling the socket is closed */
    close(client_socket);
    free(file_data);
    return NULL;
}

// Custom report function
void report(struct sockaddr_in *serverAddress) {
    char hostBuffer[INET6_ADDRSTRLEN]; 
    char serviceBuffer[NI_MAXSERV];   // defined in <netdb.h>
    socklen_t addr_len = sizeof(*serverAddress);
    int err = getnameinfo(
        (struct sockaddr *) serverAddress,
        addr_len,
        hostBuffer,
        sizeof(hostBuffer),
        serviceBuffer,
        sizeof(serviceBuffer),
        NI_NUMERICHOST | NI_NUMERICSERV);
        
    if (err != 0) {
        fprintf(stderr, "getnameinfo: %s\n", gai_strerror(err));
    }

    logMessage("\n\tServer listening on http://%s:%s\n", hostBuffer, serviceBuffer);
}

// Main function
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    char *docroot = "docroot";

    // Parse the port number and doc root from the command-line argument
    if (argc >= 2) {
        port = atoi(argv[1]);
    }

    if (argc >= 3) {
        docroot = argv[2];
        if (chdir(docroot) == -1) {
            (void)printf("Error: Can't Change to directory %s\n", docroot);
            exit(4);
        }
    }

    if (docroot == NULL) {
        printf("Error: docroot is null\n");
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

    while (1) {
        int *client_fd = calloc(sizeof(int), 1);

        // Accept incoming connection
        if ((*client_fd =
                 accept(http_server.socket, (struct sockaddr *)&client_address,
                        &http_server.address_len)) < 0) {
            perror("Accept failed");
            free(client_fd);
            continue;
        }

        logMessage("Connection accepted from %s:%d\n",
                   inet_ntoa(client_address.sin_addr),
                   ntohs(client_address.sin_port));

        // Create a new thread to handle client request
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_request, client_fd) != 0) {
            perror("pthread_create");
            close(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(thread_id);
    }

    return EXIT_SUCCESS;
}