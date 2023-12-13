C=gcc
CFLAGS=-Wall

all: http_server

http_server:
	$(CC) $(CFLAGS) -o server server.c

clean:
	rm -f server
