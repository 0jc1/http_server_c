#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define VERSION 23
#define PORT 8080 // default port
#define BUFFER_SIZE 8096

struct MimeType
{
    char *extension;
    char *type;
} mimeTypes[] = {
    {"gif", "image/gif"},
    {"jpg", "image/jpg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/png"},
    {"css", "text/css"},
    {"ico", "image/ico"},
    {"zip", "image/zip"},
    {"gz", "image/gz"},
    {"tar", "image/tar"},
    {"htm", "text/html"},
    {"html", "text/html"},
};

// Function to get MIME type based on file extension
const char * getMimeType(const char * fileExtension) {
    for (int i = 0; mimeTypes[i].extension != 0; i++) {
        int len = strlen(mimeTypes[i].extension);
        if (!strncmp(fileExtension, mimeTypes[i].extension, len)) {
            return mimeTypes[i].type;
        }
    }
    return 0;
}

void logMessage(const char * format, ...) {
    va_list arg;
    va_start(arg, format);

    va_list argCopy;
    va_copy(argCopy, arg);

    int len = vsnprintf(NULL, 0, format, argCopy);
    va_end(argCopy);

    if (len < 0) {
        return;
    }

    char buf[len + 1];
    len = vsnprintf(buf, sizeof buf, format, arg);
    if (len < 0) {
        return;
    }

    time_t t = time(NULL);
    if (t == -1) {
        return;
    }
    struct tm * tm = localtime( & t);
    if (tm == NULL) {
        return;
    }
    char time_str[len + 13]; // Assuming HH:MM:SS format
    sprintf(time_str, "%02d:%02d:%02d ", tm -> tm_hour, tm -> tm_min, tm -> tm_sec);
    strcat(time_str, buf);

    printf("%s\n", time_str);

    //output to log file
    /*
    int fd = open("server.log", O_WRONLY | O_APPEND | O_CREAT | O_NONBLOCK, S_IRUSR | S_IWUSR);

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

void handle_request(int client_socket) {
    char buffer[BUFFER_SIZE] = {
        0
    };
    size_t i = 0;

    long ret = read(client_socket, buffer, BUFFER_SIZE - 1);

    if (ret == 0 || ret == -1) {
        logMessage("failed to read client request");
    }

    if (ret > 0 && ret < BUFFER_SIZE) {
        buffer[ret] = 0;
    } else {
        buffer[0] = 0;
    }

    for (i = 0; i < ret; i++) { // remove CF and LF characters
        if (buffer[i] == '\r' || buffer[i] == '\n') {
            buffer[i] = '*';
        }
    }

    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
        logMessage("Only simple GET operation supported");
        return;
    }

    for (i = 4; i < BUFFER_SIZE; i++) { // null terminate after the second space to ignore extra stuff
        if (buffer[i] == ' ') {
            buffer[i] = 0;
            break;
        }
    }

    logMessage("read request %s", buffer);

    for (size_t j = 0; j < i - 1; j++) {
        /* check for illegal parent directory use .. */
        if (buffer[j] == '.' && buffer[j + 1] == '.') {
            logMessage("Parent directory (..) path names not supported");
            return;
        }
    }

    if (!strncmp( & buffer[0], "GET /\0", 6) || !strncmp( & buffer[0], "get /\0", 6)) /* convert no filename to index file */
        (void) strcpy(buffer, "GET /index.html");

    /* work out the file type and check we support it */
    long len;
    const char * fstr = (char * ) 0;
    char * extension = 0;

    // Find the position of the dot
    char * dotPosition = strchr( & buffer[4], '.');

    if (dotPosition != NULL) {
        extension = dotPosition + 1;
    }

    fstr = getMimeType(extension);

    if (fstr == 0) {
        logMessage("file extension type not supported");
    }

    int file_fd;
    if ((file_fd = open( & buffer[5], O_RDONLY)) == -1) {
        /* open the file for reading */

        logMessage("failed to open file %s", & buffer[5]);
    }

    logMessage("SEND");
    len = (long) lseek(file_fd, (off_t) 0, SEEK_END); /* lseek to the file end to find the length */
    lseek(file_fd, (off_t) 0, SEEK_SET); /* lseek back to the file start ready for reading */
    sprintf(buffer, "HTTP/1.1 200 OK\r\nServer: nweb/%d.0\r\nContent-Length: %ld\r\nConnection: close\r\nContent-Type: %s\r\n\n", VERSION, len, fstr); /* Header + a blank line */

    write(client_socket, buffer, strlen(buffer));

    /* send file in 8KB block - last block may be smaller */
    while ((ret = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
        (void) write(client_socket, buffer, ret);
    }
    sleep(1); /* allow socket to drain before signalling the socket is closed */

    close(client_socket);
}

int main(int argc, char * argv[]) {
    int port = PORT;
    char * docroot;

    // Parse the port number and doc root from the command-line argument
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (argc >= 3) {
            docroot = argv[2];
            if (chdir(docroot) == -1) {
                (void) printf("ERROR: Can't Change to directory %s\n", docroot);
                exit(4);
            }
        }
    }

    if (port <= 0 || port > 65535) {
        printf("Invalid port number: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    int server_socket, client_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t address_len = sizeof(server_address);

    // Create network socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    int true1 = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, & true1, sizeof(int));

    // Bind the socket
    if (bind(server_socket, (struct sockaddr * ) & server_address, sizeof(server_address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, SOMAXCONN) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    logMessage("Server listening on 127.0.0.1:%d...\n", port);

    while (1) {
        // Accept incoming connection
        if ((client_socket = accept(server_socket, (struct sockaddr * ) & client_address, & address_len)) < 0) {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        logMessage("Connection accepted from %s:%d\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

        // Handle the HTTP request
        handle_request(client_socket);
    }

    return 0;