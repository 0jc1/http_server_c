#!/bin/bash

make clean
make all
make client

timeout 10s ./client
./server 8080 docroot
./client 8080 &