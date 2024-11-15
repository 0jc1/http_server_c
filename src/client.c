#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // for close
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        fprintf(stderr, "Error: Unable to create socket\n");
        return EXIT_FAILURE;
    }

    if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        fprintf(stderr, "Error: Connection failed\n");
        return EXIT_FAILURE;
    }

    char request[BUFFER_SIZE];
    snprintf(request, BUFFER_SIZE, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    if (send(client_socket, request, strnlen(request, BUFFER_SIZE), 0) < 0) {
        fprintf(stderr, "Error: Unable to send request\n");
        return EXIT_FAILURE;
    }

    char response[BUFFER_SIZE];
    int bytes_received;
    while ((bytes_received = recv(client_socket, response, BUFFER_SIZE - 1, 0)) > 0) {
        response[bytes_received] = '\0';
        printf("%s", response);
    }

    close(client_socket);
    return EXIT_SUCCESS;
}
