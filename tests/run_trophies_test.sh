#!/bin/sh
# Linux host checks; no Vita SDK, emulator or game assets required.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
cd "$root"
flags='-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -fno-fast-math'
sanitize=''
if [ "${AODD_SANITIZE:-0}" = 1 ]; then sanitize='-fsanitize=address,undefined'; fi
# Word splitting is intentional for the fixed compiler flag lists.
${CC:-cc} $flags $sanitize -Isource tests/trophies_test.c source/aod/trophies.c \
    source/aod/trophy_storage.c -o "$scratch/core"
mkdir "$scratch/journal"
"$scratch/core" "$scratch/journal"
${CC:-cc} $flags -fPIC -shared -Isource source/aod/trophy_storage.c \
    tests/trophy_storage_faults.c -Wl,--wrap=fwrite,--wrap=fsync,--wrap=fclose \
    -o "$scratch/storage.so"
AODD_STORAGE_TEST_LIB="$scratch/storage.so" python3 -m unittest discover \
    -s tests -p test_trophy_storage.py -v
