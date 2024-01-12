# http_server_c
Basic and small HTTP server coded in posix C. The motivation behind this project to learn more about HTTP and low level programming in a Unix environment. Based on tinyhttpd and Nigel's web server.

It is able to host static files.

## Features

- No dependencies
- HTTP/1.1 support

## Usage
Compile
```
make all
```

Execute on port 8080
```
./server 8080 docroot
```
## References

https://notes.eatonphil.com/web-server-basics-http-and-sockets.html


