#!/bin/bash

make clean
make all
make client

./server 8080 docroot
./client 8080 &