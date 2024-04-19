C = gcc
CFLAGS = -Wall -pthread -O1

all: http_server

http_server:
	$(CC) $(CFLAGS) -o server main.c server.c

client:
	gcc -o client client.c -Wall

clean:
	rm -f server
	rm -f client
