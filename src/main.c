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
#include <dirent.h>
#include <errno.h>

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
    char *actualFileName = fileName;

    switch (statusCode) {
        case OK:
            break;
        case NOT_FOUND:
            actualFileName = "404.html";
            break;
        case UNSUPPORTED_MEDIA_TYPE:
            file = NULL;
            break;
        case UNAUTHORIZED:
            actualFileName = "400.html";
            break;
        default:
            break;
    }

    file = fopen(actualFileName, "rb");  // Open in binary mode
    if (file == NULL) {
        logMessage("Failed to open file: %s (errno: %d)", actualFileName, errno);
    }

    return file;
}

char *render_static_file(FILE *file, long *len) {
    if (file == NULL) {
        fprintf(stderr, "Failed to render. File is null\n");
        return NULL;
    }

    // Get file size
    if (fseek(file, 0, SEEK_END) != 0) {
        logMessage("Failed to seek to end of file (errno: %d)", errno);
        return NULL;
    }
    
    long fsize = ftell(file);
    if (fsize == -1) {
        logMessage("Failed to get file size (errno: %d)", errno);
        return NULL;
    }
    *len = fsize;
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        logMessage("Failed to seek back to start of file (errno: %d)", errno);
        return NULL;
    }
        
    // Allocate buffer
    char *temp = malloc(fsize + 1);
    if (temp == NULL) {
        logMessage("Failed to allocate memory for file");
        return NULL;
    }
    
    // Read file content in binary mode
    size_t bytes_read = fread(temp, sizeof(char), fsize, file);
    
    if (bytes_read != fsize) {
        if (ferror(file)) {
            logMessage("Error reading file (errno: %d)", errno);
        } else if (feof(file)) {
            logMessage("Unexpected EOF while reading file");
        }
        free(temp);
        return NULL;
    }
    logMessage("Successfully read file content");
    fclose(file);
    return temp;
}

void *handle_request(void *client_fd) {
    int client_socket = *((int *)client_fd);
    char buffer[BUFFER_SIZE] = {0};
    enum HttpStatusCode statusCode = OK;
    char reasonPhrase[100] = "OK";
    long len;  // Declare length variable

    // Read client request
    ssize_t total_read = 0;
    ssize_t bytes_read;
    
    // Read the initial request line
    bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0) {
        logMessage("Failed to read request (errno: %d)", errno);
        return NULL;
    }
    total_read = bytes_read;
    buffer[total_read] = '\0';
    
    while (total_read < BUFFER_SIZE - 1) {
        if (strstr(buffer, "\r\n\r\n")) {
            break;  // Found end of headers
        }
        
        bytes_read = read(client_socket, buffer + total_read, BUFFER_SIZE - total_read - 1);
        if (bytes_read <= 0) {
            break;  // No more data or error
        }
        
        total_read += bytes_read;
        buffer[total_read] = '\0';
    }
    
    if (total_read >= BUFFER_SIZE - 1) {
        logMessage("Request too large");
        return NULL;
    }

    // Parse request method
    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
        logMessage("Only simple GET operation supported");
        return NULL;
    }

    // Parse request line
    char *eol = strstr(buffer, "\r\n");
    if (!eol) {
        logMessage("Invalid HTTP request format - no CRLF");
        return NULL;
    }
    *eol = '\0';  // Temporarily terminate at end of first line


    // Find HTTP version
    char *http_ver = strstr(buffer + 4, " HTTP/");
    if (!http_ver) {
        logMessage("Invalid HTTP request format - no HTTP version");
        return NULL;
    }
    *http_ver = '\0';  // Terminate at end of path

    /* check for illegal parent directory use .. */
    if (strstr(buffer, "..")) {
        logMessage("Parent directory (..) path names not supported");
        statusCode = FORBIDDEN;
        strcpy(reasonPhrase, "Forbidden");
    }

    // Get requested path (skip "GET ")
    char *path = &buffer[4];
    char file_name[BUFFER_SIZE];
    
    // Handle root path request
    if (!strcmp(path, "/") || !strcmp(path, "/\0")) {
        strcpy(file_name, "index.html");
        logMessage("Root path request, using: %s", file_name);
    } else {
        // Remove leading slash for all other paths
        strcpy(file_name, path + 1);
        logMessage("Request path: %s, using file: %s", path, file_name);
    }


    // Find file extension for MIME type
    char *extension = NULL;
    char *dotPosition = strrchr(file_name, '.');
    if (dotPosition != NULL) {
        extension = dotPosition + 1;
    }
    const char *fstr = getMimeType(extension);
    logMessage("File extension: %s, MIME type: %s", extension ? extension : "none", fstr);

    // Try to open the requested file
    logMessage("Attempting to open file: %s", file_name);
    FILE *file = fopen(file_name, "rb");  // Open in binary mode
    if (file == NULL) {
        logMessage("failed to find file %s in directory %s", file_name, cwd);
        statusCode = NOT_FOUND;
        strcpy(reasonPhrase, "Not Found");
        // Try to open error page
        file = fopen("404.html", "rb");  // Open in binary mode
        if (file == NULL) {
            logMessage("Failed to open 404.html (errno: %d)", errno);
            return NULL;
        }
    }

    char *file_data = render_static_file(file, &len);
    if (file_data == NULL) {
        logMessage("failed to render file %s", file_name);
        return NULL;
    }

    // Format and send HTTP response headers
    snprintf(buffer, BUFFER_SIZE,
            "HTTP/1.1 %d %s\r\n"
            "Server: nweb/%d.0\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n"
            "Content-Type: %s\r\n"
            "\r\n",
            statusCode, reasonPhrase, VERSION, len, fstr);
    
    
    // Send response
    size_t header_len = strnlen(buffer, BUFFER_SIZE);
    size_t total_len = header_len + len;
    char *response = malloc(total_len);
    if (!response) {
        logMessage("Failed to allocate response buffer");
        free(file_data);
        return NULL;
    }

    // Copy headers and content into single buffer
    memcpy(response, buffer, header_len);
    memcpy(response + header_len, file_data, len);
    free(file_data);

    // Send complete response
    ssize_t total_sent = 0;
    while (total_sent < total_len) {
        ssize_t sent = send(client_socket, response + total_sent, total_len - total_sent, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;  // Interrupted, try again
            logMessage("Failed to send response (errno: %d)", errno);
            free(response);
            return NULL;
        }
        total_sent += sent;
    }

    free(response);
    close(client_socket);
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

    // Parse the port number and doc root from the command-line arguments
    if (argc == 3) {
        // Both port and docroot provided
        port = atoi(argv[1]);
        docroot = argv[2];
    } else if (argc != 1) {
        printf("Usage: %s [port] [docroot]\n", argv[0]);
        printf("  port: Port number (default: 8080)\n");
        printf("  docroot: Document root directory (default: docroot)\n");
        exit(EXIT_FAILURE);
    }

    // Get current working directory
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd() error");
        exit(EXIT_FAILURE);
    }
    printf("Starting directory: %s\n", cwd);
    
    // Build absolute path to docroot
    char abs_docroot[1024];
    if (docroot[0] == '/') {
        strncpy(abs_docroot, docroot, sizeof(abs_docroot) - 1);
    } else {
        snprintf(abs_docroot, sizeof(abs_docroot), "%s/%s", cwd, docroot);
    }
    printf("Document root: %s\n", abs_docroot);
    
    // Verify docroot exists and contains index.html
    struct stat st;
    if (stat(abs_docroot, &st) == -1 || !S_ISDIR(st.st_mode)) {
        printf("Error: Document root %s is not a directory\n", abs_docroot);
        exit(EXIT_FAILURE);
    }
    
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/index.html", abs_docroot);
    if (!file_exists(index_path)) {
        printf("Error: index.html not found in %s\n", abs_docroot);
        exit(EXIT_FAILURE);
    }
    
    // Change to docroot directory
    if (chdir(abs_docroot) == -1) {
        printf("Error: Can't change to directory %s\n", abs_docroot);
        exit(EXIT_FAILURE);
    }
    printf("Changed to directory: %s\n", abs_docroot);

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
