#!/bin/bash

make clean
make all
./server 8080 docroot &
_pid=$!
sleep .5
./client 8080
kill -INT ${_pid}
