#!/bin/sh
# Calibrated softfp/hard-float check of the sceGxmSetViewport path (boot-08 fix), under qemu-arm.
# Uses the real vitaGL shim source and the real adapter object from the pinned softfp VitaSDK.
set -e
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
SRC=$ROOT/src/aod-vita
NDK=$ROOT/tools/android-ndk-r27d
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi30-clang
SDK=$ROOT/tools/vitasdk-softfp
T=$(mktemp -d /tmp/aod-vpabi.XXXXXX)
trap 'rm -rf "$T"' EXIT
# vitaGL's shim, verbatim from the #ifdef HAVE_SOFTFP_ABI block in vgl.c
awk '/^#ifdef HAVE_SOFTFP_ABI/{f=1; next} f && /^#endif/{exit} f' "$SRC/lib/vitagl/source/vgl.c" > "$T/body.c"
grep -q "sceGxmSetViewport_sfp" "$T/body.c" || { echo "setup error: vitaGL shim not found in vgl.c"; exit 2; }
printf 'typedef struct SceGxmContext SceGxmContext;\n' | cat - "$T/body.c" > "$T/vitagl_sfp.c"
# the SDK's softfp adapter object
(cd "$T" && "$SDK/bin/arm-vita-eabi-ar" x "$SDK/arm-vita-eabi/lib/libSceGxm_stub.a" sceGxmSetViewport.o)
"$SDK/bin/arm-vita-eabi-nm" "$T/sceGxmSetViewport.o" | grep -q "U __vita_softfp_target_sceGxmSetViewport" ||
	{ echo "setup error: SDK sceGxmSetViewport.o is not a softfp adapter"; exit 2; }
$CC -static -O1 -mfloat-abi=softfp -mfpu=vfpv3 -marm -c "$T/vitagl_sfp.c" -o "$T/vitagl_sfp.o"
$CC -static -O1 -mfloat-abi=softfp -mfpu=vfpv3 -c "$SRC/tests/viewport_abi/target.c" -o "$T/target.o"
$CC -static -O1 -mfloat-abi=softfp -mfpu=vfpv3 -c "$SRC/source/aod/viewport_softfp.c" -o "$T/fix.o"
$CC -static -O1 -mfloat-abi=softfp -mfpu=vfpv3 "$SRC/tests/viewport_abi/viewport_abi_test.c" "$T/vitagl_sfp.o" "$T/sceGxmSetViewport.o" \
    "$T/fix.o" "$T/target.o" -o "$ROOT/tools/viewport_abi_test"
timeout 60 docker run --rm --network none --security-opt seccomp=unconfined -v "$ROOT":/w aod-vita-qemu:local /bin/qemu-arm-static /w/tools/viewport_abi_test
