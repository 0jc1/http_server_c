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
    const char *file_to_open = fileName;

    switch (statusCode) {
        case OK:
            break;
        case NOT_FOUND:
            file_to_open = "404.html";
            break;
        case UNSUPPORTED_MEDIA_TYPE:
            return NULL;
        case UNAUTHORIZED:
            file_to_open = "400.html";
            break;
        default:
            break;
    }

    if (!file_to_open) {
        return NULL;
    }

    return fopen(file_to_open, "r");
}

char *render_static_file(FILE *file, long *len) {
    if (file == NULL) {
        fprintf(stderr, "Failed to render. File is null\n");
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Failed to seek file\n");
        fclose(file);
        return NULL;
    }

    long fsize = ftell(file);
    if (fsize == -1) {
        fprintf(stderr, "Failed to get file size\n");
        fclose(file);
        return NULL;
    }

    *len = fsize * sizeof(char);
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to seek back to start of file\n");
        fclose(file);
        return NULL;
    }

    char *temp = calloc(sizeof(char), (fsize + 1));
    if (temp == NULL) {
        fprintf(stderr, "Failed to allocate memory for file content\n");
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(temp, 1, fsize, file);
    if (bytes_read != (size_t)fsize) {
        fprintf(stderr, "Failed to read entire file\n");
        free(temp);
        fclose(file);
        return NULL;
    }

    fclose(file);
    return temp;
}

void *handle_request(void *client_fd) {
    int client_socket = *((int *)client_fd);
    char buffer[BUFFER_SIZE] = {0};
    enum HttpStatusCode statusCode = OK;
    char reasonPhrase[100] = "OK";
    char *file_data = NULL;
    FILE *file = NULL;
    long len = 0;
    char *request_path = NULL;

    // read client request
    int ret = read(client_socket, buffer, BUFFER_SIZE - 1);

    if (ret == 0 || ret == -1) {
        logMessage("failed to read client request");
        goto cleanup;
    }

    if (ret > 0 && ret < BUFFER_SIZE) {
        buffer[ret] = 0;
    } else {
        buffer[0] = 0;
        goto cleanup;  // Buffer overflow protection
    }

    // Parse request method
    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
        logMessage("Only simple GET operation supported");
        goto cleanup;
    }

    // Parse request line
    char *eol = strstr(buffer, "\r\n");
    if (!eol) {
        logMessage("Invalid HTTP request format - no CRLF");
        goto cleanup;
    }
    *eol = '\0';  // Temporarily terminate at end of first line

    // Find HTTP version
    char *http_ver = strstr(buffer + 4, " HTTP/");
    if (!http_ver) {
        logMessage("Invalid HTTP request format - no HTTP version");
        goto cleanup;
    }
    *http_ver = '\0';  // Terminate at end of path

    // Get request path (skip "GET ")
    request_path = buffer + 4;

    /* check for illegal parent directory use .. */
    if (strstr(request_path, "..")) {
        logMessage("Parent directory (..) path names not supported");
        statusCode = FORBIDDEN;
        strcpy(reasonPhrase, "Forbidden");
    }

    // Default to index.html for root path
    if (strcmp(request_path, "/") == 0) {
        request_path = "/index.html";
    }

    /* work out the file type and check if it's supported */
    char *extension = NULL;
    char *dotPosition = strrchr(request_path, '.');
    if (dotPosition != NULL) {
        extension = dotPosition + 1;
    }

    const char *fstr = getMimeType(extension);
    if (fstr == NULL) {
        logMessage("file extension type not supported");
        statusCode = UNSUPPORTED_MEDIA_TYPE;
        strcpy(reasonPhrase, "Unsupported Media Type");
    }

    // Skip leading '/' in path
    char *file_path = request_path + 1;
    if (!file_exists(file_path)) {
        logMessage("failed to find file %s", file_path);
        statusCode = NOT_FOUND;
        strcpy(reasonPhrase, "Not Found");
    }

    file = get_file(file_path, statusCode);
    if (file == NULL) {
        logMessage("failed to open file %s", file_path);
        statusCode = INTERNAL_SERVER_ERROR;
        strcpy(reasonPhrase, "Internal Server Error");
        goto cleanup;
    }

    file_data = render_static_file(file, &len);
    if (file_data == NULL) {
        logMessage("failed to read file %s", file_path);
        statusCode = INTERNAL_SERVER_ERROR;
        strcpy(reasonPhrase, "Internal Server Error");
        goto cleanup;
    }

    // Format and send HTTP response headers
    snprintf(buffer, BUFFER_SIZE,
            "HTTP/1.1 %d %s\r\nServer: nweb/%d.0\r\nContent-Length: "
            "%ld\r\nConnection: close\r\nContent-Type: %s\r\n\r\n",
            statusCode, reasonPhrase, VERSION, len,
            fstr); /* Header + a blank line */

    ssize_t header_sent = send(client_socket, buffer, strnlen(buffer, BUFFER_SIZE), 0);
    if (header_sent < 0) {
        logMessage("Failed to send header");
        goto cleanup;
    }

    ssize_t body_sent = send(client_socket, file_data, len, 0);
    if (body_sent < 0) {
        logMessage("Failed to send body");
        goto cleanup;
    }

cleanup:
    if (file != NULL && file_data == NULL) {
        // Only close if render_static_file hasn't already closed it
        fclose(file);
    }
    if (file_data != NULL) {
        free(file_data);
    }
    if (client_socket >= 0) {
        close(client_socket);
    }
    if (client_fd != NULL) {
        free(client_fd);
    }
    sleep(1); /* allow socket to drain after closing */
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
    
    // Verify docroot exists
    struct stat st;
    if (stat(abs_docroot, &st) == -1 || !S_ISDIR(st.st_mode)) {
        printf("Error: Document root %s is not a directory\n", abs_docroot);
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
