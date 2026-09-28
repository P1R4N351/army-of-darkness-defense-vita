#!/bin/sh
# Host test of the real source/aod/uniform_remap.c against a vitaGL-modelled mock backend.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
gcc -std=gnu11 -O1 -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -include stdlib.h -Isource tests/uniform_remap_test.c source/aod/uniform_remap.c -o "$T/uremap"
"$T/uremap"; rc=$?
rm -rf "$T"; exit $rc
