#!/bin/sh
# opensl_compat test in the TARGET ABI: 32-bit ARM softfp (NDK r27d, static bionic) under qemu-arm,
# because the SDK's SLint32/SLuint32 are `long` (8 bytes on an LP64 host, 4 on the Vita and for FMOD).
# UB is trapped (-fsanitize-trap). Plus a host C++ check that the restated Android constants equal
# the SDK header's (that header only works as C++).
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
SRC=$ROOT/src/aod-vita
NDK=$ROOT/tools/android-ndk-r27d
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi30-clang
T=$(mktemp -d /tmp/aod-opensl.XXXXXX)
NAME=aod-opensl-test-$$
# `timeout` only stops the docker client; the named container is removed explicitly on every exit.
trap 'docker rm -f "$NAME" >/dev/null 2>&1; rm -rf "$T"' EXIT
mkdir -p "$T/inc" && ln -s "$ROOT/tools/vitasdk-softfp/arm-vita-eabi/include/SLES" "$T/inc/SLES"   # SLES headers only
$CC -static -O1 -g -mfloat-abi=softfp -fsanitize=undefined -fsanitize-trap=undefined -Wall -Wno-unused-parameter \
    -I"$T/inc" -I"$SRC/source" "$SRC/tests/opensl_compat_test.c" "$SRC/source/aod/opensl_compat.c" -o "$ROOT/tools/opensl_compat_test"
timeout 60 docker run --rm --name "$NAME" --network none --security-opt seccomp=unconfined -v "$ROOT":/w aod-vita-qemu:local \
    /bin/qemu-arm-static /w/tools/opensl_compat_test || { rc=$?; docker rm -f "$NAME" >/dev/null 2>&1; echo "opensl_compat_test: container failed or timed out (rc $rc), removed"; exit 1; }
g++ -std=gnu++17 -I"$T/inc" "$SRC/tests/opensl_compat_const_check.cpp" -o "$T/c"
"$T/c"
