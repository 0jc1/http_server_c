#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080 // default port
#define BUFFER_SIZE 1024

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
    read(client_socket, buffer, BUFFER_SIZE - 1);

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
        if (argc == 3) {
            docroot = argv[2];
        }
    }

    if (port <= 0 || port > 65535)
    {
        printf("Invalid port number: %s\n", argv[1]);
        return 1; // Return an error code
    }

    int server_socket, client_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t address_len = sizeof(server_address);
    int backlog = 5;

    // Create socket
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

    printf("Server listening on 127.0.0.1:%d...\n", PORT);

    while (1)
    {
        // Accept incoming connection
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_address, &address_len)) < 0)
        {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        printf("Connection accepted from %s:%d\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

        // Handle the HTTP request
        handle_request(client_socket);
    }

    return 0;
}