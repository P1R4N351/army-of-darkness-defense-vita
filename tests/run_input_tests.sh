#!/usr/bin/env bash
# NOTE: this file relaxes P10 rule (IFS) because all for-loops iterate over
# explicit bash arrays, not word-split variables; file-scope IFS is safe.
# P10 rule 5 (checked returns): every cc and binary invocation exits script on
# failure via set -e; logs are preserved on both success and failure paths.
set -euo pipefail
IFS=$'\n\t'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SRC="${REPO_ROOT}/source/aod"
RUN_BASE="${SCRIPT_DIR}/run"
STAMP="$(date +%Y%m%dT%H%M%S)"
LOG_DIR="${RUN_BASE}/input_harness_${STAMP}"
mkdir -p "${LOG_DIR}"

echo "=== AOD input harness ==="
echo "Log directory: ${LOG_DIR}"

command -v cc >/dev/null 2>&1 || { echo "ERROR: cc not found"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "ERROR: python3 not found"; exit 1; }

# ── common release flags (exact order as specified) ───────────────────
# -lm is NOT in REL_FLAGS; do_compile appends it after all sources per P10.
REL_FLAGS=(
    -std=c99 -Wall -Wextra -Werror
    -O3 -ffast-math -fno-fast-math
    "-I${REPO_ROOT}/source"
    "-I${SRC}"
)
SAN_FLAGS=( -g -fsanitize=address,undefined -fno-omit-frame-pointer )

# ── machine-readable harness manifest ────────────────────────────────
MANIFEST_FILE="${LOG_DIR}/harness_manifest.json"
_MANIFEST_NEEDS_COMMA=""

manifest_open()  { printf '{"entries":[\n' > "${MANIFEST_FILE}"; _MANIFEST_NEEDS_COMMA=""; }
manifest_close() { printf '\n]}\n' >> "${MANIFEST_FILE}"; }

# record_compile: append one entry; args are the full cc argv (no "cc" prefix).
# NOTE: paths must not contain " or \ (standard compiler flag/path constraint).
record_compile() {
    local label="$1"
    shift
    local json_args json_hashes first_hash arg h
    json_args="$(printf '"%s",' "$@")"
    json_args="[${json_args%,}]"
    json_hashes="{"
    first_hash="1"
    for arg in "$@"; do
        case "${arg}" in
            *.c)
                if [ -f "${arg}" ]; then
                    h="$(sha256sum "${arg}" | cut -d' ' -f1)"
                    [ "${first_hash}" = "1" ] || printf ',' >> "${MANIFEST_FILE}"
                    [ "${first_hash}" = "1" ] && json_hashes="${json_hashes}\"${arg}\":\"${h}\""
                    [ "${first_hash}" != "1" ] && json_hashes="${json_hashes},\"${arg}\":\"${h}\""
                    first_hash="0"
                fi
                ;;
        esac
    done
    json_hashes="${json_hashes}}"
    [ -n "${_MANIFEST_NEEDS_COMMA}" ] && printf ',\n' >> "${MANIFEST_FILE}"
    printf '  {"label":"%s","argv":%s,"source_hashes":%s}' \
        "${label}" "${json_args}" "${json_hashes}" >> "${MANIFEST_FILE}"
    _MANIFEST_NEEDS_COMMA="1"
}

# ── require a fixture file; fails clearly if pending ─────────────────
require_fixture() {
    local f="$1"
    if [ ! -f "${f}" ]; then
        echo "ERROR: required fixture not found (pending parent freeze): ${f}"
        exit 1
    fi
}

# ── compile a binary; args after label and out are passed to cc ───────
# -lm is appended after all sources so the linker resolves math symbols.
do_compile() {
    local label="$1" out="$2"
    shift 2
    local clog="${LOG_DIR}/${label}.compile.log"
    echo "--- [compile] ${label}"
    record_compile "${label}" "$@" -lm -o "${out}"
    cc "$@" -lm -o "${out}" 2>&1 | tee "${clog}"
}

# ── run a binary with full sanitizer halt options ──────────────────────
do_run() {
    local label="$1" bin="$2" asan_opts="${3:-}"
    local rlog="${LOG_DIR}/${label}.run.log"
    echo "--- [run]     ${label}"
    if [ -n "${asan_opts}" ]; then
        ASAN_OPTIONS="${asan_opts}:halt_on_error=1" \
        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
            "${bin}" 2>&1 | tee "${rlog}"
    else
        "${bin}" 2>&1 | tee "${rlog}"
    fi
}

BINS_DIR="${LOG_DIR}/bins"
mkdir -p "${BINS_DIR}"
manifest_open

# ── mandatory pre-mutation coverage: config tests ─────────────────────
echo "=== [pre/1] run_input_config_tests ==="
bash "${SCRIPT_DIR}/run_input_config_tests.sh" \
    2>&1 | tee "${LOG_DIR}/run_input_config_tests.log"

# ── mandatory pre-mutation coverage: trophies tests (plain + sanitize) ─
echo "=== [pre/2] run_trophies_test ==="
bash "${SCRIPT_DIR}/run_trophies_test.sh" \
    2>&1 | tee "${LOG_DIR}/run_trophies_test.log"

echo "=== [pre/3] run_trophies_test (AODD_SANITIZE=1) ==="
AODD_SANITIZE=1 bash "${SCRIPT_DIR}/run_trophies_test.sh" \
    2>&1 | tee "${LOG_DIR}/run_trophies_test_sanitize.log"

# ── fixture 1: touch_ids_test ─────────────────────────────────────────
echo "=== [1/8] touch_ids_test ==="
require_fixture "${SCRIPT_DIR}/touch_ids_test.c"
do_compile "touch_ids_rel" "${BINS_DIR}/touch_ids_rel" \
    "${REL_FLAGS[@]}" \
    "${SRC}/touch_ids.c" "${SCRIPT_DIR}/touch_ids_test.c"
do_run "touch_ids_rel" "${BINS_DIR}/touch_ids_rel"
do_compile "touch_ids_san" "${BINS_DIR}/touch_ids_san" \
    "${REL_FLAGS[@]}" "${SAN_FLAGS[@]}" \
    "${SRC}/touch_ids.c" "${SCRIPT_DIR}/touch_ids_test.c"
do_run "touch_ids_san" "${BINS_DIR}/touch_ids_san" "detect_leaks=1"

# ── fixture 2: input_queue_test ───────────────────────────────────────
echo "=== [2/8] input_queue_test ==="
require_fixture "${SCRIPT_DIR}/input_queue_test.c"
do_compile "input_queue_rel" "${BINS_DIR}/input_queue_rel" \
    "${REL_FLAGS[@]}" \
    "${SRC}/input_queue.c" "${SRC}/touch_ids.c" \
    "${SCRIPT_DIR}/input_queue_test.c"
do_run "input_queue_rel" "${BINS_DIR}/input_queue_rel"
do_compile "input_queue_san" "${BINS_DIR}/input_queue_san" \
    "${REL_FLAGS[@]}" "${SAN_FLAGS[@]}" \
    "${SRC}/input_queue.c" "${SRC}/touch_ids.c" \
    "${SCRIPT_DIR}/input_queue_test.c"
do_run "input_queue_san" "${BINS_DIR}/input_queue_san" "detect_leaks=1"

# ── fixture 3: input_test (core) ──────────────────────────────────────
echo "=== [3/8] input_test ==="
require_fixture "${SCRIPT_DIR}/input_test.c"
do_compile "input_core_rel" "${BINS_DIR}/input_core_rel" \
    "${REL_FLAGS[@]}" \
    "${SRC}/input.c" "${SRC}/input_queue.c" "${SRC}/touch_ids.c" \
    "${SCRIPT_DIR}/input_test.c"
do_run "input_core_rel" "${BINS_DIR}/input_core_rel"
do_compile "input_core_san" "${BINS_DIR}/input_core_san" \
    "${REL_FLAGS[@]}" "${SAN_FLAGS[@]}" \
    "${SRC}/input.c" "${SRC}/input_queue.c" "${SRC}/touch_ids.c" \
    "${SCRIPT_DIR}/input_test.c"
do_run "input_core_san" "${BINS_DIR}/input_core_san" "detect_leaks=1"

# ── fixture 4: input_fault_test (core) ────────────────────────────────
echo "=== [4/8] input_fault_test ==="
require_fixture "${SCRIPT_DIR}/input_fault_test.c"
do_compile "input_fault_rel" "${BINS_DIR}/input_fault_rel" \
    "${REL_FLAGS[@]}" \
    "${SRC}/input.c" "${SRC}/input_queue.c" "${SRC}/touch_ids.c" \
    "${SCRIPT_DIR}/input_fault_test.c"
do_run "input_fault_rel" "${BINS_DIR}/input_fault_rel"
do_compile "input_fault_san" "${BINS_DIR}/input_fault_san" \
    "${REL_FLAGS[@]}" "${SAN_FLAGS[@]}" \
    "${SRC}/input.c" "${SRC}/input_queue.c" "${SRC}/touch_ids.c" \
    "${SCRIPT_DIR}/input_fault_test.c"
do_run "input_fault_san" "${BINS_DIR}/input_fault_san" "detect_leaks=1"

# ── fixture 5: input_modes_test (core) ────────────────────────────────
echo "=== [5/8] input_modes_test ==="
require_fixture "${SCRIPT_DIR}/input_modes_test.c"
do_compile "input_modes_rel" "${BINS_DIR}/input_modes_rel" \
    "${REL_FLAGS[@]}" \
    "${SRC}/input.c" "${SRC}/input_queue.c" "${SRC}/touch_ids.c" \
    "${SCRIPT_DIR}/input_modes_test.c"
do_run "input_modes_rel" "${BINS_DIR}/input_modes_rel"
do_compile "input_modes_san" "${BINS_DIR}/input_modes_san" \
    "${REL_FLAGS[@]}" "${SAN_FLAGS[@]}" \
    "${SRC}/input.c" "${SRC}/input_queue.c" "${SRC}/touch_ids.c" \
    "${SCRIPT_DIR}/input_modes_test.c"
do_run "input_modes_san" "${BINS_DIR}/input_modes_san" "detect_leaks=1"

# ── fixture 6: input_launch_test ──────────────────────────────────────
echo "=== [6/8] input_launch_test ==="
require_fixture "${SCRIPT_DIR}/input_launch_test.c"
do_compile "input_launch_rel" "${BINS_DIR}/input_launch_rel" \
    "${REL_FLAGS[@]}" "-I${SCRIPT_DIR}/input_sdk_stubs" \
    "${SRC}/input_launch.c" "${SCRIPT_DIR}/input_launch_test.c"
do_run "input_launch_rel" "${BINS_DIR}/input_launch_rel"
do_compile "input_launch_san" "${BINS_DIR}/input_launch_san" \
    "${REL_FLAGS[@]}" "${SAN_FLAGS[@]}" "-I${SCRIPT_DIR}/input_sdk_stubs" \
    "${SRC}/input_launch.c" "${SCRIPT_DIR}/input_launch_test.c"
do_run "input_launch_san" "${BINS_DIR}/input_launch_san" "detect_leaks=1"

# ── fixture 7: input_adapter_test ─────────────────────────────────────
echo "=== [7/8] input_adapter_test ==="
require_fixture "${SCRIPT_DIR}/input_adapter_test.c"
do_compile "input_adapter_rel" "${BINS_DIR}/input_adapter_rel" \
    "${REL_FLAGS[@]}" "-I${SCRIPT_DIR}/input_sdk_stubs" \
    "${SRC}/input_vita.c" "${SRC}/input.c" "${SRC}/input_queue.c" \
    "${SRC}/touch_ids.c" "${SCRIPT_DIR}/input_adapter_test.c"
do_run "input_adapter_rel" "${BINS_DIR}/input_adapter_rel"
do_compile "input_adapter_san" "${BINS_DIR}/input_adapter_san" \
    "${REL_FLAGS[@]}" "${SAN_FLAGS[@]}" "-I${SCRIPT_DIR}/input_sdk_stubs" \
    "${SRC}/input_vita.c" "${SRC}/input.c" "${SRC}/input_queue.c" \
    "${SRC}/touch_ids.c" "${SCRIPT_DIR}/input_adapter_test.c"
do_run "input_adapter_san" "${BINS_DIR}/input_adapter_san" "detect_leaks=1"

# ── fixture 8: input_gl_test ──────────────────────────────────────────
echo "=== [8/8] input_gl_test ==="
require_fixture "${SCRIPT_DIR}/input_gl_test.c"
do_compile "input_gl_rel" "${BINS_DIR}/input_gl_rel" \
    "${REL_FLAGS[@]}" "-I${SCRIPT_DIR}/input_gl_stubs" "-I${SRC}" \
    "${SRC}/input_overlay.c" "${SCRIPT_DIR}/input_gl_test.c"
do_run "input_gl_rel" "${BINS_DIR}/input_gl_rel"
do_compile "input_gl_san" "${BINS_DIR}/input_gl_san" \
    "${REL_FLAGS[@]}" "${SAN_FLAGS[@]}" \
    "-I${SCRIPT_DIR}/input_gl_stubs" "-I${SRC}" \
    "${SRC}/input_overlay.c" "${SCRIPT_DIR}/input_gl_test.c"
do_run "input_gl_san" "${BINS_DIR}/input_gl_san" "detect_leaks=1"

manifest_close
echo "Manifest: ${MANIFEST_FILE}"

# ── all 8 fixtures passed; invoke mutation runner ─────────────────────
echo ""
echo "=== All 8 fixture profiles passed. Running mutation suite. ==="
python3 "${SCRIPT_DIR}/test_input_mutations.py" \
    2>&1 | tee "${LOG_DIR}/mutation_runner.log"

echo ""
echo "=== Harness complete. Logs: ${LOG_DIR} ==="