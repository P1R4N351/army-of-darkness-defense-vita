#!/bin/sh
# fmod_diag host test, plain and under UBSan.
cd "$(dirname "$0")/.."
T=$(mktemp -d)
rc=0
gcc -std=gnu11 -O1 -Wall -Wno-unused-parameter -Isource tests/fmod_diag_test.c source/aod/fmod_diag.c -o "$T/t" || rc=1
[ -x "$T/t" ] && { "$T/t" || rc=1; }
gcc -std=gnu11 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -Wall -Wno-unused-parameter -Isource tests/fmod_diag_test.c source/aod/fmod_diag.c -o "$T/u" || rc=1
[ -x "$T/u" ] && { "$T/u" > "$T/u.out" 2>&1 || rc=1; tail -1 "$T/u.out" | sed 's/$/ [ubsan]/'; }
rm -rf "$T"; exit $rc
