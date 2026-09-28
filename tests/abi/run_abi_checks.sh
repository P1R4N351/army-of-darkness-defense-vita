#!/bin/sh
# bionic struct stat ABI: NDK truth vs our Vita-built stat64_bionic (boot-02 regression).
set -e
cd "$(dirname "$0")/../.."
ROOT=$(cd ../.. && pwd)
NDKCC=$ROOT/tools/android-ndk-r27d/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang
VCC=$ROOT/tools/vitasdk-softfp/bin/arm-vita-eabi-gcc
$NDKCC -c tests/abi/bionic_stat_ndk.c -Itests/abi -o /tmp/aod_abi_ndk.o
$VCC -mfloat-abi=softfp -std=gnu11 -D_GNU_SOURCE -D__POSIX_VISIBLE=999999 -Isource -Ilib -Itests/abi -c tests/abi/stat_layout_vita.c -o /tmp/aod_abi_vita.o
echo "abi: bionic struct stat (NDK) == stat64_bionic (Vita build): 13 offsets + size + 3 widths"
