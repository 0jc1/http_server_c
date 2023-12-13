C=gcc
CFLAGS=-Wall

all: http_server

http_server: http_server.c
	$(CC) $(CFLAGS) -o http_server http_server.c

clean:
	rm -f http_server
