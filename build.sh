CC=gcc
CFLAGS=
LDFLAGS=
DEFINES=
INCLUDES="-Isrc"
BINDIR="bin"
SRCS="src/*.c"

if [ $# -eq 0 ] || [ "$1" = "help" ]; then
    echo "Usage: $0 [clean|build [debug|release]]"
    exit 0
fi

if [ "$1" = "clean" ]; then
    rm -rf $BINDIR
    exit 0
fi

if [ "$1" = "build" ]; then
    if [ ! -d "$BINDIR" ]; then
        mkdir $BINDIR
    fi

    if [ $# -eq 1 ] || [ "$2" = "debug" ]; then
        CFLAGS="-g -O0 -Wall -Wextra -Wpedantic -Wno-unused-function -Wno-unused-parameter"
        DEFINES="-DDEBUG -DLOG_LEVEL=500"
    elif [ "$2" = "release" ]; then
        CFLAGS="-O3 -march=native -Wall -Wextra -Wpedantic -Werror -Wno-unused-function -Wno-unused-parameter"
        DEFINES="-DLOG_LEVEL=300"
    fi
fi

$CC -O3 -march=native -shared -fPIC $INCLUDES -ldl $DEFINES -o $BINDIR/libktsn.so libktsn.c $SRCS
$CC $CFLAGS $INCLUDES ktsnd.c $(pkg-config --libs --cflags libdpdk) $SRCS $DEFINES -o $BINDIR/ktsnd
$CC $CFLAGS $INCLUDES apps/tsn_perf.c $SRCS -o $BINDIR/tsn-perf