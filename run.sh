#!/bin/bash

make clean
make all
make client
./server 8080 docroot &
_pid=$!
sleep .5
./client 8080
kill -INT ${_pid}
