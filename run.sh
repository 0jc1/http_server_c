#!/bin/bash

make clean
make all
./bin/server 8080 docroot &
_pid=$!
sleep .5
./bin/client 8080
kill -INT ${_pid}
