#!/bin/sh
# Portable build without make.  Set CC and DISPARITY=1 as needed.
#   ./build.sh          build demo + tests
#   ./build.sh test     build, then run the tests
#   DISPARITY=1 ./build.sh
#   PROFILE=16 ./build.sh test    narrow packing for a model built with in_sz=16
set -e

CC=${CC:-gcc}
OPT=${OPT:--O2}
WARN="-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion"
PROFILE=${PROFILE:-32}
CFLAGS="-std=c11 $WARN $OPT -Isrc -DRZN_PACK_PROFILE=$PROFILE"

CORE="src/rzn_spiral.c src/rzn_pack.c src/rzn_frame.c src/rzn_fovea.c src/rzn_agi_sink.c"
if [ "$DISPARITY" = "1" ]; then
    CFLAGS="$CFLAGS -DRZN_ENABLE_DISPARITY=1"
    CORE="$CORE src/rzn_disparity.c"
fi

mkdir -p build

echo "building demo..."
$CC $CFLAGS $CORE src/demo_main.c -o build/rzn_demo

echo "building tests..."
$CC $CFLAGS $CORE test/test_rzn.c -o build/rzn_test

echo "ok (profile $PROFILE) -> build/rzn_demo, build/rzn_test"

if [ "$1" = "test" ]; then
    ./build/rzn_test
fi
