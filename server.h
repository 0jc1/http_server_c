#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <sys/socket.h>

typedef struct HTTP_Server
{
	int socket;
	int port;
	socklen_t address_len;
	struct sockaddr_in *address;
} HTTP_Server;

void init_server(HTTP_Server *http_server, int port);

#endif