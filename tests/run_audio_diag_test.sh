#!/bin/sh
# audio_diag host test, plain and under UBSan.
cd "$(dirname "$0")/.."
T=$(mktemp -d)
rc=0
gcc -std=gnu11 -O1 -Wall -Wno-unused-parameter -Isource tests/audio_diag_test.c source/aod/audio_diag.c -o "$T/t" && "$T/t" || rc=1
gcc -std=gnu11 -O1 -g -fsanitize=alignment,signed-integer-overflow,undefined -fno-sanitize-recover=all -Wall -Wno-unused-parameter \
    -Isource tests/audio_diag_test.c source/aod/audio_diag.c -o "$T/u" || rc=1
[ -x "$T/u" ] && { "$T/u" > "$T/u.out" 2>&1 || rc=1; tail -1 "$T/u.out" | sed 's/$/ [ubsan]/'; }
# concurrency: documented writer/reader ownership must be data-race free (ThreadSanitizer, no recovery)
gcc -std=gnu11 -O1 -g -fsanitize=thread -pthread -Wall -Wno-unused-parameter -Isource tests/audio_diag_race_test.c source/aod/audio_diag.c \
    -o "$T/r" || rc=1
if [ -x "$T/r" ]; then
	# setarch -R: TSan cannot map its shadow memory under this kernel's high mmap ASLR entropy
	TSAN_OPTIONS=halt_on_error=1 setarch "$(uname -m)" -R "$T/r" > "$T/r.out" 2>&1; trc=$?
	tail -1 "$T/r.out" | sed 's/$/ [tsan]/'
	[ $trc -eq 0 ] || { rc=1; grep -m3 -E "WARNING: ThreadSanitizer|FATAL" "$T/r.out"; }
fi
rm -rf "$T"; exit $rc
