#!/usr/bin/env python3
"""
AOD input subsystem mutation test runner.
Compiles and runs targeted fixture cases for each mutant.
Exits nonzero if any mutant survives, is invalid, or a fixture is pending.

JNI bridge (jni_bridge.c) is NOT executed by this harness; it requires a
private ARM/Android runtime.  No claims are made about JNI private coverage.
"""
# NOTE: this file relaxes P10 rule 2 (no dynamic allocation) because Python
# manages memory; the rule applies to the C production code under test.

import json
import os
import sys
import hashlib
import shutil
import subprocess
import datetime
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT  = os.path.dirname(SCRIPT_DIR)
SRC_DIR    = os.path.join(REPO_ROOT, "source", "aod")
RUN_DIR    = os.path.join(SCRIPT_DIR, "run")
STAMP      = datetime.datetime.now().strftime("%Y%m%dT%H%M%S")
LOG_DIR    = os.path.join(RUN_DIR, "mutations_" + STAMP)

# Exact release flags as specified in the harness matrix.
# -lm is NOT in REL_FLAGS; compile_sources appends it after all sources.
_ISOURCE = "-I" + os.path.join(REPO_ROOT, "source")
_ISAOD   = "-I" + SRC_DIR
REL_FLAGS = [
    "-std=c99", "-Wall", "-Wextra", "-Werror",
    "-O3", "-ffast-math", "-fno-fast-math",
    _ISOURCE, _ISAOD,
]

# SDK stub include for adapter tests.
SDK_INC = "-I" + os.path.join(SCRIPT_DIR, "input_sdk_stubs")

# ── Mutant definitions ────────────────────────────────────────────────
# Fields: name, source (relative to SRC_DIR), needle, replacement,
# occurrence (expected exact count in source), prod_sources (relative to
# SRC_DIR), test_sources (relative to SCRIPT_DIR), extra_flags.
MUTANTS = [
    {
        "name": "M1_queue_emit_down_wrong_ordinal",
        "source": "input_queue.c",
        "needle":
            "case AOD_ACTION_DOWN: android_action = AOD_TOUCH_DOWN; break;",
        "replacement":
            "case AOD_ACTION_DOWN: android_action = AOD_TOUCH_MOVE; break;",
        "occurrence": 1,
        "prod_sources": [
            "input_vita.c", "input.c", "input_queue.c", "touch_ids.c",
        ],
        "test_sources": ["input_adapter_test.c"],
        "extra_flags": [SDK_INC],
    },
    {
        "name": "M2_queue_slotmap_before_capacity",
        "source": "input_queue.c",
        "needle": (
            "    /* Reject BEFORE touching the slot map so a full-queue DOWN"
            " doesn't consume\n"
            "     * a pointer slot that can never be freed by a subsequent"
            " UP. */\n"
            "    if (q->count >= AOD_QUEUE_CAPACITY) { return false; }\n"
            "\n"
            "    int ptr_id = aod_touch_map(vita_id, action);"
        ),
        "replacement": (
            "    /* MUTANT M2: capacity check moved after slot map. */\n"
            "    int ptr_id = aod_touch_map(vita_id, action);\n"
            "\n"
            "    if (q->count >= AOD_QUEUE_CAPACITY) { return false; }"
        ),
        "occurrence": 1,
        "prod_sources": ["input_queue.c", "touch_ids.c"],
        "test_sources": ["input_queue_test.c"],
        "extra_flags": [],
    },
    {
        "name": "M3_core_pending_cancel_bypass",
        "source": "input.c",
        "needle": (
            "    if (state->pending_cancel) {\n"
            "        if (!aod_input_cancel(state, emit, userdata))"
            " return false;\n"
            "    }"
        ),
        "replacement": (
            "    /* MUTANT M3: pending_cancel retry suppressed. */\n"
            "    if (false && state->pending_cancel) {\n"
            "        if (!aod_input_cancel(state, emit, userdata))"
            " return false;\n"
            "    }"
        ),
        "occurrence": 1,
        "prod_sources": ["input.c", "input_queue.c", "touch_ids.c"],
        "test_sources": ["input_fault_test.c"],
        "extra_flags": [],
    },
    {
        # M4: front SDK sceTouchPeek rc guard weakened: rc!=1 -> rc<=0 so
        # rc=2 (malformed/extra buffer) is no longer rejected.
        # adapter12 cases exercise exactly rc=2 paths.
        "name": "M4_adapter_front_rc_check_weakened",
        "source": "input_vita.c",
        "needle": (
            "    int rc = sceTouchPeek(port, &td, 1);\n"
            "    if (rc != 1) { return; }\n"
            "    if (s_copy_front(frame, &td)) {"
        ),
        "replacement": (
            "    int rc = sceTouchPeek(port, &td, 1);\n"
            "    if (rc <= 0) { return; }"
            " /* MUTANT M4: rc!=1->rc<=0, rc=2 accepted */\n"
            "    if (s_copy_front(frame, &td)) {"
        ),
        "occurrence": 1,
        "prod_sources": [
            "input_vita.c", "input.c", "input_queue.c", "touch_ids.c",
        ],
        "test_sources": ["input_adapter_test.c"],
        "extra_flags": [SDK_INC],
    },
    {
        "name": "M5_core_direction_same_side_or_to_xor",
        "source": "input.c",
        "needle": "    bool l_active = l_shld || l_rear;",
        "replacement":
            "    bool l_active = l_shld ^ l_rear; /* MUTANT M5: OR->XOR */",
        "occurrence": 1,
        "prod_sources": ["input.c", "input_queue.c", "touch_ids.c"],
        "test_sources": ["input_modes_test.c"],
        "extra_flags": [],
    },
    {
        # M6: rear left-zone lower-x boundary moved from 0.25 to 0.0 so
        # contacts at the panel left edge (outside the intended grip zone)
        # are incorrectly accepted as left-direction triggers.
        # modes tests cover fresh-outer-grip boundary cases.
        "name": "M6_rear_left_zone_lower_bound_edge",
        "source": "input.c",
        "needle": "    if (nx >= 0.25f && nx <= 0.45f) return -1;",
        "replacement": (
            "    if (nx >= 0.0f && nx <= 0.45f) return -1;"
            " /* MUTANT M6: left zone starts at edge */"
        ),
        "occurrence": 1,
        "prod_sources": ["input.c", "input_queue.c", "touch_ids.c"],
        "test_sources": ["input_modes_test.c"],
        "extra_flags": [],
    },
    {
        "name": "M7_touchids_up_uses_move_ordinal",
        "source": "touch_ids.c",
        "needle":
            "\tif (action == 2 && slot >= 0) slot_owner[slot] = 0;",
        "replacement":
            "\tif (action == 1 && slot >= 0) slot_owner[slot] = 0;"
            " /* MUTANT M7 */",
        "occurrence": 1,
        "prod_sources": ["touch_ids.c"],
        "test_sources": ["touch_ids_test.c"],
        "extra_flags": [],
    },
]


def sha256_file(path):
    """Return hex SHA-256 of file at path."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def compile_sources(out_bin, all_sources, extra_flags, log_path):
    """Compile all_sources to out_bin; -lm appended after sources. Returns (rc, argv)."""
    # -lm must follow all source files so the linker resolves math symbols.
    cmd = (["cc"] + REL_FLAGS + extra_flags
           + all_sources + ["-lm", "-o", out_bin])
    with open(log_path, "w") as lf:
        lf.write("CMD: " + " ".join(cmd) + "\n\n")
        result = subprocess.run(cmd, stdout=lf, stderr=subprocess.STDOUT)
    return result.returncode, cmd


def run_test_binary(bin_path, log_path):
    """Run binary; return returncode."""
    with open(log_path, "w") as lf:
        result = subprocess.run(
            [bin_path], stdout=lf, stderr=subprocess.STDOUT
        )
    return result.returncode


def apply_needle(src_text, needle, replacement, expected_count):
    """
    Return (mutated_text, None) on success.
    Return (None, error_msg) when occurrence count mismatches.
    """
    count = src_text.count(needle)
    if count != expected_count:
        msg = ("needle found " + str(count) + " time(s), "
               "expected " + str(expected_count))
        return None, msg
    return src_text.replace(needle, replacement, expected_count), None


def check_fixtures_exist(mutant):
    """Return list of missing fixture paths."""
    missing = []
    for ts in mutant["test_sources"]:
        p = os.path.join(SCRIPT_DIR, ts)
        if not os.path.isfile(p):
            missing.append(p)
    return missing


def build_source_list(mutant, mutated_src_path):
    """
    Build absolute source list: mutated TU replaces original;
    all other prod TUs are unchanged originals.
    """
    sources = []
    mut_base = mutant["source"]
    for ps in mutant["prod_sources"]:
        if ps == mut_base:
            sources.append(mutated_src_path)
        else:
            sources.append(os.path.join(SRC_DIR, ps))
    for ts in mutant["test_sources"]:
        sources.append(os.path.join(SCRIPT_DIR, ts))
    return sources


def run_baseline(mutant, scratch_dir, log_dir):
    """
    Compile and run the unmodified baseline for this mutant's fixture.
    Returns (compile_rc, run_rc, compile_argv, compile_log, run_log).
    run_rc is None if compile failed.
    """
    sources = []
    for ps in mutant["prod_sources"]:
        sources.append(os.path.join(SRC_DIR, ps))
    for ts in mutant["test_sources"]:
        sources.append(os.path.join(SCRIPT_DIR, ts))
    out_bin = os.path.join(scratch_dir, "baseline_bin")
    clog    = os.path.join(log_dir, mutant["name"] + "_baseline_compile.log")
    rlog    = os.path.join(log_dir, mutant["name"] + "_baseline_run.log")
    crc, argv = compile_sources(out_bin, sources, mutant["extra_flags"], clog)
    if crc != 0:
        return crc, None, argv, clog, rlog
    rrc = run_test_binary(out_bin, rlog)
    return crc, rrc, argv, clog, rlog


def run_one_mutant(mutant, scratch_dir, log_dir):
    """
    Execute one mutant cycle.
    Returns dict with status, hashes, rcs, argv, and log paths for report.json.
    """
    _NONE_FIELDS = {
        "baseline_compile_rc": None, "baseline_run_rc": None,
        "mutant_compile_rc": None,   "mutant_run_rc": None,
        "baseline_compile_argv": [], "mutant_compile_argv": [],
        "baseline_compile_log": "",  "baseline_run_log": "",
        "mutant_compile_log": "",    "mutant_run_log": "",
    }

    missing = check_fixtures_exist(mutant)
    if missing:
        d = {"status": "pending",
             "details": "fixture(s) not yet in mirror: " + str(missing),
             "original_hash": None, "mutated_hash": None}
        d.update(_NONE_FIELDS)
        return d

    src_path = os.path.join(SRC_DIR, mutant["source"])
    with open(src_path) as f:
        original_text = f.read()
    orig_hash = sha256_file(src_path)

    mutated_text, err = apply_needle(
        original_text, mutant["needle"],
        mutant["replacement"], mutant["occurrence"])
    if err:
        d = {"status": "invalid",
             "details": "needle error in " + mutant["source"] + ": " + err,
             "original_hash": orig_hash, "mutated_hash": None}
        d.update(_NONE_FIELDS)
        return d

    mutated_path = os.path.join(scratch_dir, mutant["source"])
    with open(mutated_path, "w") as f:
        f.write(mutated_text)
    mut_hash = hashlib.sha256(mutated_text.encode()).hexdigest()

    b_scratch = os.path.join(scratch_dir, "baseline")
    os.makedirs(b_scratch, exist_ok=True)
    b_crc, b_rrc, b_argv, b_clog, b_rlog = run_baseline(
        mutant, b_scratch, log_dir)

    if b_crc != 0:
        d = {"status": "invalid",
             "details": "baseline compile failed (rc=" + str(b_crc) + ")",
             "original_hash": orig_hash, "mutated_hash": mut_hash}
        d.update(_NONE_FIELDS)
        d.update({"baseline_compile_rc": b_crc, "baseline_compile_argv": b_argv,
                  "baseline_compile_log": b_clog, "baseline_run_log": b_rlog})
        return d
    if b_rrc != 0:
        d = {"status": "invalid",
             "details": "baseline test failed (rc=" + str(b_rrc) + ")",
             "original_hash": orig_hash, "mutated_hash": mut_hash}
        d.update(_NONE_FIELDS)
        d.update({"baseline_compile_rc": b_crc, "baseline_run_rc": b_rrc,
                  "baseline_compile_argv": b_argv,
                  "baseline_compile_log": b_clog, "baseline_run_log": b_rlog})
        return d

    all_sources = build_source_list(mutant, mutated_path)
    mut_bin  = os.path.join(scratch_dir, "mutant_bin")
    mut_clog = os.path.join(log_dir, mutant["name"] + "_mutant_compile.log")
    mut_rlog = os.path.join(log_dir, mutant["name"] + "_mutant_run.log")

    c_rc, m_argv = compile_sources(
        mut_bin, all_sources, mutant["extra_flags"], mut_clog)
    if c_rc != 0:
        d = {"status": "invalid",
             "details": "mutant compile failed (rc=" + str(c_rc) + ")",
             "original_hash": orig_hash, "mutated_hash": mut_hash}
        d.update(_NONE_FIELDS)
        d.update({"baseline_compile_rc": b_crc, "baseline_run_rc": b_rrc,
                  "baseline_compile_argv": b_argv, "mutant_compile_rc": c_rc,
                  "mutant_compile_argv": m_argv,
                  "baseline_compile_log": b_clog, "baseline_run_log": b_rlog,
                  "mutant_compile_log": mut_clog})
        return d

    t_rc = run_test_binary(mut_bin, mut_rlog)
    status  = "killed"   if t_rc != 0 else "survived"
    details = ("test exited " + str(t_rc) + " (nonzero) — killed"
               if t_rc != 0 else "test exited 0 — SURVIVED (coverage gap)")

    return {
        "status": status, "details": details,
        "original_hash": orig_hash, "mutated_hash": mut_hash,
        "baseline_compile_rc": b_crc, "baseline_run_rc": b_rrc,
        "mutant_compile_rc": c_rc,    "mutant_run_rc": t_rc,
        "baseline_compile_argv": b_argv, "mutant_compile_argv": m_argv,
        "baseline_compile_log": b_clog,  "baseline_run_log": b_rlog,
        "mutant_compile_log": mut_clog,  "mutant_run_log": mut_rlog,
    }


def print_summary(results):
    """Print per-mutant report and aggregate counts."""
    counts = {"killed": 0, "survived": 0, "invalid": 0, "pending": 0}
    print("\n=== Mutation Summary ===")
    for r in results:
        counts[r["status"]] += 1
        tag = "OK" if r["status"] == "killed" else "!!"
        print("  [" + tag + "] " + r["name"] + ": " + r["status"].upper())
        print("       src=" + r["source"])
        if r["original_hash"]:
            print("       orig_sha256=" + r["original_hash"][:16] + "...")
        if r["mutated_hash"]:
            print("       mut_sha256=" + r["mutated_hash"][:16] + "...")
        print("       " + r["details"])
    print("")
    print("  Killed:   " + str(counts["killed"]))
    print("  Survived: " + str(counts["survived"]))
    print("  Invalid:  " + str(counts["invalid"]))
    print("  Pending:  " + str(counts["pending"]))
    print("  Total:    " + str(len(results)))
    return counts


def _build_report_entry(mutant, result):
    """Build one report.json mutant entry with all required fields."""
    return {
        "name":                  result["name"],
        "source":                result["source"],
        "needle":                mutant["needle"],
        "replacement":           mutant["replacement"],
        "original_sha256":       result.get("original_hash") or "",
        "mutated_sha256":        result.get("mutated_hash") or "",
        "baseline_compile_rc":   result.get("baseline_compile_rc"),
        "baseline_run_rc":       result.get("baseline_run_rc"),
        "mutant_compile_rc":     result.get("mutant_compile_rc"),
        "mutant_run_rc":         result.get("mutant_run_rc"),
        "status":                result["status"],
        "details":               result["details"],
        "baseline_compile_log":  result.get("baseline_compile_log", ""),
        "baseline_run_log":      result.get("baseline_run_log", ""),
        "mutant_compile_log":    result.get("mutant_compile_log", ""),
        "mutant_run_log":        result.get("mutant_run_log", ""),
        "baseline_compile_argv": result.get("baseline_compile_argv") or [],
        "mutant_compile_argv":   result.get("mutant_compile_argv") or [],
    }


def main():
    os.makedirs(LOG_DIR, exist_ok=True)
    print("Mutation log dir: " + LOG_DIR)
    print("Running " + str(len(MUTANTS)) + " mutants\n")

    results = []
    overall_scratch = tempfile.mkdtemp(dir=RUN_DIR, prefix="mut_scratch_")

    for mutant in MUTANTS:
        scratch = os.path.join(overall_scratch, mutant["name"])
        os.makedirs(scratch, exist_ok=True)
        print("  [" + mutant["name"] + "] ", end="", flush=True)
        result = run_one_mutant(mutant, scratch, LOG_DIR)
        result["name"]   = mutant["name"]
        result["source"] = mutant["source"]
        results.append(result)
        print(result["status"].upper() + " — " + result["details"])

    counts = print_summary(results)

    # ── write full machine-readable report (no truncation) ────────────
    report = {
        "stamp":   STAMP,
        "log_dir": LOG_DIR,
        "mutants": [_build_report_entry(MUTANTS[i], results[i])
                    for i in range(len(results))],
    }
    report_path = os.path.join(LOG_DIR, "report.json")
    with open(report_path, "w") as rf:
        json.dump(report, rf, indent=2)
    print("Report: " + report_path)

    if counts["pending"] > 0:
        print("\nNOTE: Pending mutants require frozen parent fixture sync.")

    bad = counts["survived"] + counts["invalid"]
    if bad > 0:
        print("\nFAIL: " + str(bad) + " mutant(s) survived or invalid.")
        sys.exit(1)
    if counts["pending"] > 0:
        print("\nFAIL: " + str(counts["pending"])
              + " pending — full kill unconfirmed.")
        sys.exit(2)

    print("\nPASS: all valid mutants killed.")
    sys.exit(0)


if __name__ == "__main__":
    main()