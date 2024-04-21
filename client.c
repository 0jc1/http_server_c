#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <URL>\n", argv[0]);
        return 1;
    }

    char *url = argv[1];
    char *host = strtok(url, "/");
    char *path = strtok(NULL, "/");

    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        fprintf(stderr, "Error: Unable to resolve host\n");
        return 1;
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8080);
    memcpy(&server_address.sin_addr.s_addr, server->h_addr, server->h_length);

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        fprintf(stderr, "Error: Unable to create socket\n");
        return 1;
    }

    if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        fprintf(stderr, "Error: Connection failed\n");
        return 1;
    }

    char request[BUFFER_SIZE];
    sprintf(request, "GET /%s HTTP/1.1\r\nHost: %s\r\n\r\n", path, host);
    if (send(client_socket, request, strlen(request), 0) < 0) {
        fprintf(stderr, "Error: Unable to send request\n");
        return 1;
    }

    char response[BUFFER_SIZE];
    int bytes_received;
    while ((bytes_received = recv(client_socket, response, BUFFER_SIZE - 1, 0)) > 0) {
        response[bytes_received] = '\0';
        printf("%s", response);
    }

    close(client_socket);
    return 0;
}
