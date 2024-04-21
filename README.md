# http_server_c
Basic and small prototype HTTP server coded in Posix C. The motivation behind this project to learn more about HTTP, servers, and low level programming in a Unix environment. Based on tinyhttpd and Nigel's web server. It is able to host static files.

## Features

- No dependencies
- HTTP/1.1 support
- Multi-threaded

## Usage
Compile
```
make all
```

Start the server
```
./server <port> <path/to/docroot>
```
## References

https://notes.eatonphil.com/web-server-basics-http-and-sockets.html


