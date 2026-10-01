#!/bin/bash

# create all build directories if it does not exist
if [ ! -d "build" ]; then
    mkdir "build"
fi
cd build
if [ ! -d "shaders" ]; then
    mkdir "shaders"
fi
if [ ! -d "cache" ]; then
    mkdir "cache"
fi
if [ ! -d "expanded" ]; then
    mkdir "expanded"
fi
cd cache
if [ ! -d "shaders" ]; then
    mkdir "shaders"
fi
cd ..
cd ..

# download builder
if [ ! -f "build/tiny_linux.bin" ] || [ "$1" == "-u" ] || [ "$2" == "-u" ]; then
    if [ -f "build/tiny_linux.bin" ]; then
        echo "Updating tiny builder..."
        rm build/tiny_linux.bin
    else
        echo "Downloading tiny builder..."
    fi
    cd build
    curl -L -s -o "tiny_linux.bin" "https://github.com/JHeflinger/tiny/raw/refs/heads/main/bin/tiny_linux.bin"
    chmod +x tiny_linux.bin
    cd ..
fi

# run builder
PROD=""
if [ "$1" == "-p" ] || [ "$2" == "-p" ] || [ "$3" == "-p" ]; then
    PROD="-prod"
fi
./build/tiny_linux.bin $PROD
if [ $? -ne 0 ]; then
    exit 1
fi
