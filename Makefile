CC = gcc
CFLAGS = -Wall -O1 -fPIE -fstack-clash-protection -fstack-protector-all -fcf-protection=full
LDFLAGS = -Wl,-pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack

SERVER_SRC = src/server.c src/main.c
CLIENT_SRC = src/client.c

all: server client

server: bin/server
bin/server: $(SERVER_SRC)
	$(CC) $(CFLAGS) -pthread $^ -o $@

client: bin/client
bin/client: $(CLIENT_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f server 
	rm -f client

.PHONY: clean
