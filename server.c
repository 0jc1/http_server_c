#include "server.h"

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

void init_server(HTTP_Server *http_server, int port)
{
    http_server->port = port;

    int server_socket, true1;

    // Create network socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = INADDR_ANY;
    socklen_t address_len = sizeof(server_address);

    true1 = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &true1, sizeof(int)); // set option to reuse local addresses

    // Bind the socket
    if (bind(server_socket, (struct sockaddr *)&server_address, address_len) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, SOMAXCONN) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    http_server->address_len = address_len;
    http_server->socket = server_socket;
    printf("HTTP Server Initialized\nPort: %d\n", http_server->port);
}