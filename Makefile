CC = gcc
CFLAGS = -Wall -O1 
LDFLAGS = 

SERVER_SRC = src/server.c src/main.c
CLIENT_SRC = src/client.c

all: server client

server: $(SERVER_SRC)
	$(CC) $(CFLAGS) -pthread $^ -o $@

client: $(CLIENT_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f server 
	rm -f client

.PHONY: clean
