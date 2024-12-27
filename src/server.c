#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "server.h"

void init_server(HTTP_Server *http_server, int port) {
    http_server->port = port;

    int true1;

    // Create network socket
    int server_socket = socket(
        AF_INET,      // Domain: specifies protocol family
        SOCK_STREAM,  // Type: specifies communication semantics
        0             // Protocol: 0 because there is a single protocol for the specified family
    );

    if (server_socket == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // INADDR_ANY
    socklen_t address_len = sizeof(server_address);

    true1 = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &true1, sizeof(int));  // set option to reuse local addresses

    // 6 is TCP's protocol number
    // enable this, much faster : 4000 req/s -> 17000 req/s
    //if (setsockopt(server_socket, 6, TCP_CORK, (const void *)&true1 , sizeof(int)) < 0)
    //    exit(EXIT_FAILURE);

    // Bind the socket
    if (bind(server_socket, (struct sockaddr *)&server_address, address_len) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, SOMAXCONN) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    http_server->address_len = address_len;
    http_server->socket = server_socket;
    http_server->address = (struct sockaddr_in *)&server_address;
    printf("HTTP Server Initialized\nPort: %d\n", http_server->port);
}