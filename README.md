# http_server_c
This project is a lightweight, prototype HTTP server written in POSIX C The motivation behind this project to learn more about HTTP, servers, and low level programming in a Unix-based environment. Based on concepts from tinyhttpd and Nigel's web server, it is designed to serve static files, though it does not support dynamic content generation. It currently has many limitations: no support for persistant connections, TLS/SSL encryption, chunked transfers, or compression/decompression.

## Features

- No dependencies
- HTTP/1.0/1.1 support
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
https://dev-notes.eu/2018/06/http-server-in-c/

https://notes.eatonphil.com/web-server-basics-http-and-sockets.html


