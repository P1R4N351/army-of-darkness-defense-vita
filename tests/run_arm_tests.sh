#!/bin/sh
# Builds tests/jni_bridge_test.c for 32-bit ARM softfp (Android NDK, static bionic) and runs it
# under qemu-arm inside a throwaway container (32-bit bionic needs pid <= 65535).
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
NDK=$ROOT/tools/android-ndk-r27d
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi30-clang
FT=$ROOT/tools/ft-arm
cd "$ROOT/src/aod-vita"
$CC -static -O1 -g -w -mfloat-abi=softfp -DDATA_PATH=\"/w/data/aodd/\" -Isource -Ilib -I$FT/include/freetype2 \
    tests/jni_bridge_test.c tests/fjni_logger_host.c source/aod/jni_bridge.c source/aod/hash.c source/aod/text_render.c \
    lib/sha1/sha1.c lib/falso_jni/FalsoJNI.c lib/falso_jni/FalsoJNI_ImplBridge.c lib/falso_jni/converter.c source/java.c \
    $FT/lib/libfreetype.a -lm -o "$ROOT/tools/jni_bridge_test"
timeout 120 docker run --rm --network none --security-opt seccomp=unconfined -e AOD_FONTS=1 -e VERBOSE="$VERBOSE" \
    -v "$ROOT":/w aod-vita-qemu:local /bin/qemu-arm-static /w/tools/jni_bridge_test
docker run --rm --network none -v "$ROOT":/w aod-vita-qemu:local /bin/qemu-arm-static --version >/dev/null 2>&1 || true
