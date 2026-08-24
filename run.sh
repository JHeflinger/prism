#!/bin/bash

./build.sh $1 $2 $3
if [ $? -ne 0 ]; then
    exit 1
fi
./build/tiny_linux.bin -r
