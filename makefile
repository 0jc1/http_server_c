C = gcc
CFLAGS = -Wall -pthread -O1

all: http_server

http_server:
	$(CC) $(CFLAGS) -o server main.c server.c

clean:
	rm -f server
