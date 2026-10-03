#!/usr/bin/env bash
# NOTE: this file relaxes P10 rule (IFS) because all for-loops iterate
# over explicit arrays, not word-split variables; IFS is safe at file scope.
set -euo pipefail
IFS=$'\n\t'

# ── locate repo root (this script lives in tests/) ────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${REPO_ROOT}/source"

command -v cc      >/dev/null 2>&1 || { echo "ERROR: cc not found"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "ERROR: python3 not found"; exit 1; }

# ── temp dir with guaranteed cleanup ─────────────────────────────────
TMPDIR_BASE="$(mktemp -d -t aod_input_test_XXXXXX)"
cleanup() { rm -rf "${TMPDIR_BASE}"; }
trap cleanup EXIT

# ── common flags ─────────────────────────────────────────────────────
COMMON_FLAGS=(
  "-DAOD_INPUT_SETTINGS_HOST_TEST"
  "-D_POSIX_C_SOURCE=200809L"
  "-std=c11" "-Wall" "-Wextra" "-Werror"
  "-O3" "-ffast-math" "-fno-fast-math"
  "-I${SRC_DIR}"
)
SANITIZE_FLAGS=("-g" "-fsanitize=address,undefined" "-fno-omit-frame-pointer")

PROD_SRCS=(
  "${SRC_DIR}/aod/input_config.c"
  "${SRC_DIR}/aod/configurator.c"
)
FAULT_WRAP_FLAGS=(
  "-Wl,--wrap=mkstemp"  "-Wl,--wrap=fdopen"
  "-Wl,--wrap=fwrite"   "-Wl,--wrap=fflush"
  "-Wl,--wrap=fileno"   "-Wl,--wrap=fsync"
  "-Wl,--wrap=fclose"   "-Wl,--wrap=rename"
  "-Wl,--wrap=fopen"    "-Wl,--wrap=fread"
  "-Wl,--wrap=ferror"
  # FORTIFY_SOURCE replaces fread with __fread_chk when buffer size is known
  # at compile time.  nm -u storage-asan.o confirms "U __fread_chk" is emitted
  # by the production compiler.  Wrap the fortified symbol so fault injection
  # fires correctly under -D_FORTIFY_SOURCE=2 and ASAN builds.
  "-Wl,--wrap=__fread_chk"
)

# ── helper: compile unit binary ───────────────────────────────────────
compile_unit() {
  local outdir="$1" extra_flags=("${@:2}")
  cc "${COMMON_FLAGS[@]}" "${extra_flags[@]}" \
    "${PROD_SRCS[@]}" \
    "${SCRIPT_DIR}/input_config_test.c" \
    -o "${outdir}/unit_test"
}

# ── helper: compile fault binary ─────────────────────────────────────
compile_fault() {
  local outdir="$1" extra_flags=("${@:2}")
  cc "${COMMON_FLAGS[@]}" "${extra_flags[@]}" \
    "${FAULT_WRAP_FLAGS[@]}" \
    "${PROD_SRCS[@]}" \
    "${SCRIPT_DIR}/input_config_faults.c" \
    -o "${outdir}/fault_test"
}

# ── pass 1: release build ─────────────────────────────────────────────
echo "=== [1/5] Unit test (release) ==="
D1="${TMPDIR_BASE}/unit_release"
mkdir -p "${D1}"
compile_unit "${D1}"
F1="${TMPDIR_BASE}/fixture_unit_release"
mkdir -p "${F1}"
"${D1}/unit_test" "${F1}"

echo "=== [2/5] Fault test (release) ==="
D2="${TMPDIR_BASE}/fault_release"
mkdir -p "${D2}"
compile_fault "${D2}"
F2="${TMPDIR_BASE}/fixture_fault_release"
mkdir -p "${F2}"
"${D2}/fault_test" "${F2}"

# ── pass 2: sanitizer build ───────────────────────────────────────────
echo "=== [3/5] Unit test (ASAN+UBSan) ==="
D3="${TMPDIR_BASE}/unit_asan"
mkdir -p "${D3}"
compile_unit "${D3}" "${SANITIZE_FLAGS[@]}"
F3="${TMPDIR_BASE}/fixture_unit_asan"
mkdir -p "${F3}"
ASAN_OPTIONS=detect_leaks=1 "${D3}/unit_test" "${F3}"

echo "=== [4/5] Fault test (ASAN+UBSan) ==="
D4="${TMPDIR_BASE}/fault_asan"
mkdir -p "${D4}"
compile_fault "${D4}" "${SANITIZE_FLAGS[@]}"
F4="${TMPDIR_BASE}/fixture_fault_asan"
mkdir -p "${F4}"
ASAN_OPTIONS=detect_leaks=1 "${D4}/fault_test" "${F4}"

# ── Python ctypes controller ──────────────────────────────────────────
echo "=== [5/5] Python ctypes controller ==="
python3 "${SCRIPT_DIR}/test_input_config.py"

echo ""
echo "All test passes completed."