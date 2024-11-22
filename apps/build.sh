#!/bin/bash

# Build the app
echo "Building the app..."

CC=gcc
CFLAGS="-Wall -Wextra -Werror -Wpedantic -std=c99 -g -O0"

$CC $CFLAGS opcua_pub.c `pkg-config --cflags --libs open62541` -o opcua-pub
$CC $CFLAGS opcua_sub.c `pkg-config --cflags --libs open62541` -o opcua-sub