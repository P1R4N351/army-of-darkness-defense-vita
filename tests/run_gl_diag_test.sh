#!/bin/sh
# gl_diag host test: plain build, then the same test under UBSan (misaligned loads and signed
# overflow in the loggers abort the run).
cd "$(dirname "$0")/.."
T=$(mktemp -d)
rc=0
gcc -std=gnu11 -O1 -Wall -Wno-unused-parameter -Isource tests/gl_diag_test.c source/aod/gl_diag.c source/aod/gxm_diag.c -o "$T/gldiag" || rc=1
[ $rc -eq 0 ] && { "$T/gldiag" || rc=1; }
gcc -std=gnu11 -O1 -g -fsanitize=alignment,signed-integer-overflow,undefined -fno-sanitize-recover=all -Wall -Wno-unused-parameter \
    -Isource tests/gl_diag_test.c source/aod/gl_diag.c source/aod/gxm_diag.c -o "$T/gldiag_ubsan" || rc=1
if [ $rc -eq 0 ]; then
	"$T/gldiag_ubsan" > "$T/ubsan.out" 2>&1 || rc=1
	tail -1 "$T/ubsan.out" | sed 's/$/ [ubsan]/'
	[ $rc -eq 0 ] || cat "$T/ubsan.out" | grep -E "runtime error|FAIL" | head -5
fi
rm -rf "$T"; exit $rc
