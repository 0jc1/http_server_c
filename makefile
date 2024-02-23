C = gcc
CFLAGS = -Wall -pthread

all: http_server

http_server:
	$(CC) $(CFLAGS) -o server server.c

clean:
	rm -f server
