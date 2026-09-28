#!/bin/sh
# All offline tests: host C (digests vs hashlib, touch ids) + 32-bit ARM softfp JNI bridge under qemu.
set -e
cd "$(dirname "$0")/.."
DATA=../../data/aodd
gcc -O2 -Isource -Ilib tests/hash_test.c source/aod/hash.c lib/sha1/sha1.c -o /tmp/aod_hash_test
n=0
for f in "$DATA"/assets/__asset_manifest.json "$DATA"/assets/serviceconfigs/*.json /dev/null "$DATA"/libfmodex.so; do
  [ -e "$f" ] || continue
  a=$(/tmp/aod_hash_test "$f")
  b=$(python3 -c "import hashlib,sys;d=open(sys.argv[1],'rb').read();print(hashlib.sha1(d).hexdigest(),hashlib.sha256(d).hexdigest(),hashlib.md5(d).hexdigest())" "$f")
  [ "$a" = "$b" ] || { echo "FAIL digest $f"; exit 1; }
  n=$((n+1))
done
echo "digests: $n files x 3 algorithms match hashlib"
gcc -Wall -Isource tests/touch_ids_test.c source/aod/touch_ids.c -o /tmp/aod_touch_test && /tmp/aod_touch_test
tests/abi/run_abi_checks.sh
tests/run_asset_test.sh
tests/run_uniform_test.sh
tests/run_gl_diag_test.sh
tests/run_gxm_diag_test.sh
tests/run_audio_diag_test.sh
tests/viewport_abi/run_viewport_abi_test.sh
tests/run_opensl_compat_test.sh
tests/run_fmod_diag_test.sh
tests/run_stacksize_test.sh
tests/run_arm_tests.sh > /tmp/aod_arm_test.log 2>&1 || { grep -E '^FAIL' /tmp/aod_arm_test.log; tail -3 /tmp/aod_arm_test.log; exit 1; }
grep -E '^FAIL|checks passed' /tmp/aod_arm_test.log
