
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

#define PORT 8080 // default port
#define BUFFER_SIZE 8096

#define LOG_INFO 0
#define LOG_WARNING 1
#define LOG_ERROR 2

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

void logMessage(int level, const char *format, ...) {
    va_list args;
    va_start(args, format);

    char timestamp[20];
    time_t t;
    struct tm *tm_info;

    time(&t);
    tm_info = localtime(&t);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    const char *levelString = NULL;
    switch (level) {
        case LOG_INFO:
            levelString = "INFO";
            break;
        case LOG_WARNING:
            levelString = "WARNING";
            break;
        case LOG_ERROR:
            levelString = "ERROR";
            break;
        default:
            levelString = "UNKNOWN";
    }

    int logFile = open("server.log", O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (logFile == -1) {
        perror("Error opening log file");
        va_end(args);
        exit(EXIT_FAILURE);
    }

    dprintf(logFile, "[%s] [%s] ", timestamp, levelString);
    vdprintf(logFile, format, args);
    dprintf(logFile, "\n");

    // Print to console
    printf("[%s] [%s] ", timestamp, levelString);
    vprintf(format, args);
    printf("\n");

    close(logFile);

    va_end(args);
}



// Function to get MIME type based on file extension
const char *getMimeType(const char *fileExtension, size_t numMimeTypes)
{
    for (size_t i = 0; i < numMimeTypes; ++i)
    {
        if (strcmp(fileExtension, mimeTypes[i].extension) == 0)
        {
            return mimeTypes[i].type;
        }
    }
    return "application/octet-stream"; // Default MIME type for unknown or binary files
}


void handle_request(int client_socket)
{
    char buffer[BUFFER_SIZE] = {0};

    long ret = read(client_socket, buffer, BUFFER_SIZE - 1); 

    if (ret == 0 || ret == -1)
    {
        logMessage(LOG_WARNING,"failed to read browser request");
    }

    if (ret > 0 && ret < BUFFER_SIZE)
    {                    
        buffer[ret] = 0; 
    }
    else
    {
        buffer[0] = 0;
    }

    for (size_t i = 0; i < ret; i++)
    { // remove CF and LF characters 
        if (buffer[i] == '\r' || buffer[i] == '\n')
        {
            buffer[i] = '*';
        }
    }

    logMessage(LOG_INFO,"read request");

    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4))
    {
        //   log_warning("Only simple GET operation supported");
    }

    // Process the HTTP request
    const char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, World!\r\n";
    write(client_socket, response, strlen(response));

    close(client_socket);
}


int main(int argc, char *argv[])
{
    int port = PORT;
    char *docroot;

    // Parse the port number and doc root from the command-line argument
    if (argc >= 2)
    {
        port = atoi(argv[1]);
        if (argc >= 3)
        {
            docroot = argv[2];
            if (chdir(docroot) == -1)
            {
                (void)printf("ERROR: Can't Change to directory %s\n", docroot);
                exit(4);
            }
        }
    }

    if (port <= 0 || port > 65535)
    {
        printf("Invalid port number: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    int server_socket, client_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t address_len = sizeof(server_address);
    int backlog = 64;

    // Create network socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    // Bind the socket
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, backlog) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    logMessage(LOG_INFO,"Server listening on 127.0.0.1:%d...\n", port);

    while (1)
    {
        // Accept incoming connection
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_address, &address_len)) < 0)
        {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        logMessage(LOG_INFO,"Connection accepted from %s:%d\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

        // Handle the HTTP request
        handle_request(client_socket);
    }

    return 0;
}