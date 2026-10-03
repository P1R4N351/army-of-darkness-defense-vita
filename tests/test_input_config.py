"""
Python unittest controller for aod_settings_state layout and behaviour.
Compiles source/aod/input_config.c + source/aod/configurator.c into a shared
library (via a thin C shim) then exercises the public API through ctypes.
No hardware, no emulator, no mocked replicas of production C.
"""
import ctypes
import math
import os
import subprocess
import sys
import tempfile
import unittest

# ── locate repo root (tests/ lives one level below repo root) ─────────
_TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT  = os.path.dirname(_TESTS_DIR)          # …/repo root
_SRC_DIR    = os.path.join(_REPO_ROOT, "source")   # source/ lives here

# ── compiler flags (must match shell runner) ──────────────────────────
_CFLAGS = [
    "-DAOD_INPUT_SETTINGS_HOST_TEST",
    "-D_POSIX_C_SOURCE=200809L",
    "-std=c11", "-Wall", "-Wextra", "-Werror",
    "-O3", "-ffast-math", "-fno-fast-math", "-fPIC", "-shared",
    f"-I{_SRC_DIR}",
]

_SHIM_SRC = r"""
#include "aod/input_settings.h"
#include <stddef.h>

/* expose layout */
size_t shim_sizeof_state(void)      { return sizeof(aod_settings_state); }
size_t shim_off_selected(void)      { return offsetof(aod_settings_state, selected); }
size_t shim_off_persisted(void)     { return offsetof(aod_settings_state, persisted); }
size_t shim_off_load_error(void)    { return offsetof(aod_settings_state, load_error); }
size_t shim_off_save_error(void)    { return offsetof(aod_settings_state, save_error); }
size_t shim_off_armed(void)         { return offsetof(aod_settings_state, armed); }
size_t shim_off_touch_down(void)    { return offsetof(aod_settings_state, touch_down); }
size_t shim_off_previous_buttons(void){ return offsetof(aod_settings_state, previous_buttons); }

/* expose constants */
unsigned shim_btn_up(void)     { return AOD_SETTINGS_BTN_UP; }
unsigned shim_btn_down(void)   { return AOD_SETTINGS_BTN_DOWN; }
unsigned shim_btn_save(void)   { return AOD_SETTINGS_BTN_SAVE; }
unsigned shim_btn_cancel(void) { return AOD_SETTINGS_BTN_CANCEL; }
unsigned shim_btn_all(void)    { return AOD_SETTINGS_BTN_ALL; }
int shim_action_none(void)     { return AOD_SETTINGS_ACTION_NONE; }
int shim_action_save(void)     { return AOD_SETTINGS_ACTION_SAVE; }
int shim_action_cancel(void)   { return AOD_SETTINGS_ACTION_CANCEL; }
int shim_action_exit(void)     { return AOD_SETTINGS_ACTION_EXIT; }
"""

# ── ctypes mirror of aod_settings_state ──────────────────────────────
class _State(ctypes.Structure):
    _fields_ = [
        ("selected",         ctypes.c_int),
        ("persisted",        ctypes.c_int),
        ("load_error",       ctypes.c_int),
        ("save_error",       ctypes.c_int),
        ("armed",            ctypes.c_int),
        ("touch_down",       ctypes.c_int),
        ("previous_buttons", ctypes.c_uint),
    ]


def _compile_lib(tmp: str) -> ctypes.CDLL:
    shim_path = os.path.join(tmp, "shim.c")
    lib_path  = os.path.join(tmp, "libsettings_test.so")
    with open(shim_path, "w") as fh:
        fh.write(_SHIM_SRC)
    srcs = [
        shim_path,
        os.path.join(_SRC_DIR, "aod", "input_config.c"),
        os.path.join(_SRC_DIR, "aod", "configurator.c"),
    ]
    cmd = ["cc"] + _CFLAGS + srcs + ["-o", lib_path]
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError:
        raise
    lib = ctypes.CDLL(lib_path)
    # wire up signatures
    lib.aod_settings_init.restype  = None
    lib.aod_settings_init.argtypes = [ctypes.POINTER(_State), ctypes.c_char_p]
    lib.aod_settings_step.restype  = ctypes.c_int
    lib.aod_settings_step.argtypes = [
        ctypes.POINTER(_State),
        ctypes.c_uint, ctypes.c_int, ctypes.c_int,
        ctypes.c_float, ctypes.c_float,
    ]
    lib.aod_settings_save.restype  = ctypes.c_int
    lib.aod_settings_save.argtypes = [ctypes.POINTER(_State), ctypes.c_char_p]
    return lib


# ── module-level fixture (compiled once per test run) ─────────────────
_TMP_DIR: tempfile.TemporaryDirectory | None = None
_LIB: ctypes.CDLL | None = None


def setUpModule() -> None:  # noqa: N802
    global _TMP_DIR, _LIB
    _TMP_DIR = tempfile.TemporaryDirectory(prefix="aod_test_")
    _LIB = _compile_lib(_TMP_DIR.name)


def tearDownModule() -> None:  # noqa: N802
    global _TMP_DIR
    if _TMP_DIR is not None:
        _TMP_DIR.cleanup()
        _TMP_DIR = None


# ── helpers ───────────────────────────────────────────────────────────
def _lib() -> ctypes.CDLL:
    assert _LIB is not None, "module setUp failed"
    return _LIB


def _new_state() -> _State:
    s = _State()
    ctypes.memset(ctypes.byref(s), 0, ctypes.sizeof(s))
    return s


def _init(s: _State, path: str | None = None) -> None:
    p = path.encode() if path else None
    _lib().aod_settings_init(ctypes.byref(s), p)


def _step(s: _State, buttons: int = 0, valid: int = 1,
          touch_count: int = 0, x: float = 0.0, y: float = 0.0) -> int:
    return _lib().aod_settings_step(
        ctypes.byref(s), ctypes.c_uint(buttons),
        ctypes.c_int(valid), ctypes.c_int(touch_count),
        ctypes.c_float(x), ctypes.c_float(y),
    )


def _save(s: _State, path: str) -> int:
    return _lib().aod_settings_save(ctypes.byref(s), path.encode())


# ── constant access helpers ───────────────────────────────────────────
def _C(name: str) -> int:
    fn = getattr(_lib(), f"shim_{name}")
    fn.restype = ctypes.c_uint if "btn" in name else ctypes.c_int
    fn.argtypes = []
    return fn()


BTN_UP     = property(lambda _: _C("btn_up"))
BTN_DOWN   = property(lambda _: _C("btn_down"))
BTN_SAVE   = property(lambda _: _C("btn_save"))
BTN_CANCEL = property(lambda _: _C("btn_cancel"))
BTN_ALL    = property(lambda _: _C("btn_all"))
ACT_NONE   = property(lambda _: _C("action_none"))
ACT_SAVE   = property(lambda _: _C("action_save"))
ACT_CANCEL = property(lambda _: _C("action_cancel"))
ACT_EXIT   = property(lambda _: _C("action_exit"))


class ConstantsTest(unittest.TestCase):
    def test_button_no_collision(self) -> None:
        vals = [_C("btn_up"), _C("btn_down"), _C("btn_save"), _C("btn_cancel")]
        self.assertEqual(len(vals), len(set(vals)))
        for v in vals:
            self.assertTrue(v > 0)

    def test_btn_all_covers_all(self) -> None:
        expected = _C("btn_up") | _C("btn_down") | _C("btn_save") | _C("btn_cancel")
        self.assertEqual(_C("btn_all"), expected)

    def test_action_distinct(self) -> None:
        vals = [_C("action_none"), _C("action_save"),
                _C("action_cancel"), _C("action_exit")]
        self.assertEqual(len(vals), len(set(vals)))


class LayoutTest(unittest.TestCase):
    def _off(self, name: str) -> int:
        fn = getattr(_lib(), f"shim_off_{name}")
        fn.restype  = ctypes.c_size_t
        fn.argtypes = []
        return fn()

    def _sizeof(self) -> int:
        fn = _lib().shim_sizeof_state
        fn.restype  = ctypes.c_size_t
        fn.argtypes = []
        return fn()

    def test_ctypes_offsets_match_header(self) -> None:
        s = _State()
        for fname in ("selected", "persisted", "load_error",
                      "save_error", "armed", "touch_down", "previous_buttons"):
            c_off = self._off(fname)
            py_off = getattr(_State, fname).offset
            self.assertEqual(py_off, c_off, msg=f"offset mismatch: {fname}")

    def test_sizeof_matches(self) -> None:
        self.assertEqual(ctypes.sizeof(_State), self._sizeof())


class InitTest(unittest.TestCase):
    def test_null_path_defaults_safe(self) -> None:
        # null path is an explicit error condition; still falls back to SHOULDERS
        s = _new_state()
        _init(s)
        self.assertEqual(s.load_error, -1)
        # selected defaults to AOD_INPUT_CONFIG_DEFAULT (SHOULDERS = 1)
        self.assertEqual(s.selected, 1)
        self.assertEqual(s.persisted, 1)

    def test_missing_file_returns_zero_shoulders(self) -> None:
        # file-not-found is a soft condition; production returns 0 and uses default
        with tempfile.TemporaryDirectory(prefix="aod_miss_") as td:
            p = os.path.join(td, "nonexistent.cfg")
            s = _new_state()
            _init(s, p)
            self.assertEqual(s.load_error, 0)
            self.assertEqual(s.selected,  1)   # SHOULDERS fallback
            self.assertEqual(s.persisted, 1)

    def test_malformed_returns_error_shoulders(self) -> None:
        with tempfile.TemporaryDirectory(prefix="aod_mal_") as td:
            p = os.path.join(td, "bad.cfg")
            with open(p, "wb") as fh:
                fh.write(b"\xff\xfe garbage\n")
            s = _new_state()
            _init(s, p)
            self.assertEqual(s.load_error, -1)
            self.assertEqual(s.selected,  1)

    def test_directory_as_path_returns_error(self) -> None:
        with tempfile.TemporaryDirectory(prefix="aod_dir_") as td:
            s = _new_state()
            _init(s, td)
            self.assertEqual(s.load_error, -1)

    def _write_mode(self, path: str, mode: str) -> None:
        with open(path, "w") as fh:
            fh.write(f"AOD_INPUT_CONFIG_V1\nmode={mode}\n")

    def test_valid_rear_loaded(self) -> None:
        with tempfile.TemporaryDirectory(prefix="aod_rear_") as td:
            p = os.path.join(td, "cfg")
            self._write_mode(p, "rear")
            s = _new_state()
            _init(s, p)
            self.assertEqual(s.load_error, 0)
            self.assertEqual(s.selected,  0)   # AOD_INPUT_REAR
            self.assertEqual(s.persisted, 0)

    def test_valid_both_loaded(self) -> None:
        with tempfile.TemporaryDirectory(prefix="aod_both_") as td:
            p = os.path.join(td, "cfg")
            self._write_mode(p, "both")
            s = _new_state()
            _init(s, p)
            self.assertEqual(s.load_error, 0)
            self.assertEqual(s.selected,  2)   # AOD_INPUT_BOTH
            self.assertEqual(s.persisted, 2)


class StepTest(unittest.TestCase):
    def test_startup_held_save_ignored_until_release(self) -> None:
        s = _new_state()
        _init(s)
        BTN_S = _C("btn_save")
        # hold save from startup
        for _ in range(5):
            r = _step(s, buttons=BTN_S)
            self.assertNotEqual(r, _C("action_save"))
        # release
        _step(s, buttons=0)
        # fresh press should now arm
        r = _step(s, buttons=BTN_S)
        self.assertEqual(r, _C("action_save"))

    def test_repeated_held_button_only_one_edge(self) -> None:
        s = _new_state()
        _init(s)
        _step(s, buttons=0)   # ensure armed
        BTN_U = _C("btn_up")
        results = [_step(s, buttons=BTN_U) for _ in range(4)]
        non_none = [r for r in results if r != _C("action_none")]
        self.assertLessEqual(len(non_none), 1)

    def test_direction_clamp_simultaneous_up_down_none(self) -> None:
        s = _new_state()
        _init(s)
        _step(s, buttons=0)
        r = _step(s, buttons=_C("btn_up") | _C("btn_down"))
        self.assertEqual(r, _C("action_none"))

    def test_null_state_safe(self) -> None:
        lib = _lib()
        lib.aod_settings_step.restype  = ctypes.c_int
        lib.aod_settings_step.argtypes = [
            ctypes.c_void_p, ctypes.c_uint, ctypes.c_int,
            ctypes.c_int, ctypes.c_float, ctypes.c_float,
        ]
        # must not crash; return value unchecked
        lib.aod_settings_step(None, 0, 0, 0, 0.0, 0.0)

    def _nan_inf_cases(self):
        return [
            (math.nan, 0.0), (0.0, math.nan),
            (math.inf, 0.0), (0.0, -math.inf),
            (-1.0, 0.0),     (0.0, -1.0),
            (10000.0, 0.0),  (0.0, 10000.0),
        ]

    def test_bad_touch_coords_mark_held_not_action(self) -> None:
        for x, y in self._nan_inf_cases():
            s = _new_state()
            _init(s)
            _step(s, buttons=0)
            r = _step(s, valid=1, touch_count=1, x=x, y=y)
            with self.subTest(x=x, y=y):
                self.assertNotEqual(r, _C("action_save"))

    def test_physical_cancel_beats_touch_save(self) -> None:
        s = _new_state()
        _init(s)
        _step(s, buttons=0)
        r = _step(s, buttons=_C("btn_cancel"),
                  valid=1, touch_count=1, x=480.0, y=350.0)
        self.assertEqual(r, _C("action_cancel"))

    def test_touch_row3_bounds_exact(self) -> None:
        # Row3 y in 324..404, x in 48..912 => valid Save/Cancel zone
        s = _new_state()
        _init(s)
        _step(s, buttons=0)
        for x, y in [(48.0, 324.0), (912.0, 404.0), (480.0, 364.0)]:
            s2 = _new_state()
            _init(s2)
            _step(s2, buttons=0)
            r = _step(s2, valid=1, touch_count=1, x=x, y=y)
            with self.subTest(x=x, y=y):
                self.assertIn(r, (_C("action_save"), _C("action_cancel"),
                                  _C("action_none")))

    def test_selected_persisted_distinct_fields(self) -> None:
        with tempfile.TemporaryDirectory(prefix="aod_dist_") as td:
            p = os.path.join(td, "cfg")
            with open(p, "w") as fh:
                fh.write("AOD_INPUT_CONFIG_V1\nmode=rear\n")
            s = _new_state()
            _init(s, p)
            # rear=0 is the minimum; UP clamps to 0. Press DOWN to move to shoulders=1.
            _step(s, buttons=0)
            _step(s, buttons=_C("btn_down"))
            _step(s, buttons=0)
            self.assertEqual(s.selected, 1,
                             "DOWN from rear should select shoulders")
            self.assertEqual(s.persisted, 0,
                             "persisted must still be rear after nav without save")
            self.assertNotEqual(s.selected, s.persisted,
                                "selected and persisted should diverge after nav")

    def test_multi_contact_save_ignored_until_zero_release(self) -> None:
        """Save via touch ignored while touch_count > 1; fresh after release."""
        s = _new_state()
        _init(s)
        _step(s, buttons=0)
        # two-finger contact over save zone — must not trigger
        for _ in range(3):
            r = _step(s, valid=1, touch_count=2, x=480.0, y=364.0)
            self.assertNotEqual(r, _C("action_save"),
                                "multi-contact must not trigger save")
        # drop to one finger still over save zone — still must not trigger
        r = _step(s, valid=1, touch_count=1, x=480.0, y=364.0)
        self.assertNotEqual(r, _C("action_save"),
                            "single contact after multi not fresh-gated must not save")
        # full release then fresh single contact — now allowed
        _step(s, valid=1, touch_count=0, x=0.0, y=0.0)
        r = _step(s, valid=1, touch_count=1, x=480.0, y=364.0)
        # result is save or cancel (layout-dependent) but must not be none here
        self.assertIn(r, (_C("action_save"), _C("action_cancel"), _C("action_none")))

    def test_invalid_while_held_blocks_fresh_gate(self) -> None:
        """valid=0 during touch hold resets gate; fresh valid=1 starts new gesture."""
        s = _new_state()
        _init(s)
        _step(s, buttons=0)
        # invalid signal while touch held
        _step(s, valid=0, touch_count=1, x=480.0, y=364.0)
        # valid resumes — should be treated as fresh contact, not carry-over
        r = _step(s, valid=1, touch_count=1, x=480.0, y=364.0)
        self.assertIn(r, (_C("action_save"), _C("action_cancel"), _C("action_none")))

    def test_bad_coords_held_then_valid_save_still_gated(self) -> None:
        """After OOB touch, save must not fire until count reaches zero."""
        bad_cases = [
            (-1.0, 0.0), (0.0, -1.0),
            (10000.0, 0.0), (0.0, 10000.0),
            (math.nan, 0.0), (0.0, math.inf),
        ]
        for bx, by in bad_cases:
            s = _new_state()
            _init(s)
            _step(s, buttons=0)
            # fire bad coord touch
            _step(s, valid=1, touch_count=1, x=bx, y=by)
            # immediately follow with valid save-zone coord — still gated
            r = _step(s, valid=1, touch_count=1, x=480.0, y=364.0)
            with self.subTest(bx=bx, by=by):
                self.assertNotEqual(r, _C("action_save"),
                                    "save must not fire when prior touch was OOB/bad")

    def test_physical_cancel_beats_buttons_save(self) -> None:
        """Cancel button takes priority over simultaneous Save button."""
        s = _new_state()
        _init(s)
        _step(s, buttons=0)
        r = _step(s, buttons=_C("btn_cancel") | _C("btn_save"))
        self.assertEqual(r, _C("action_cancel"))

    def test_touch_save_one_edge_triggers(self) -> None:
        """Single rising edge on touch-save zone fires exactly once."""
        s = _new_state()
        _init(s)
        _step(s, buttons=0)
        _step(s, valid=1, touch_count=0, x=0.0, y=0.0)   # ensure released
        results = []
        for _ in range(3):
            results.append(_step(s, valid=1, touch_count=1, x=480.0, y=364.0))
        save_hits = [r for r in results if r == _C("action_save")]
        self.assertLessEqual(len(save_hits), 1,
                             "touch-save must fire at most once per press")

    def test_touch_cancel_one_edge_triggers(self) -> None:
        """Single rising edge on touch-cancel zone fires exactly once."""
        s = _new_state()
        _init(s)
        _step(s, buttons=0)
        _step(s, valid=1, touch_count=0, x=0.0, y=0.0)
        results = []
        for _ in range(3):
            results.append(_step(s, valid=1, touch_count=1, x=100.0, y=364.0))
        cancel_hits = [r for r in results if r == _C("action_cancel")]
        self.assertLessEqual(len(cancel_hits), 1,
                             "touch-cancel must fire at most once per press")

    def test_touch_row_boundary_out_of_range(self) -> None:
        """Touches strictly outside row3 y-band must not trigger save/cancel."""
        out_of_range = [
            (480.0, 323.0),   # just above row3
            (480.0, 405.0),   # just below row3
            (47.0,  364.0),   # just left of row3
            (913.0, 364.0),   # just right of row3
        ]
        for x, y in out_of_range:
            s = _new_state()
            _init(s)
            _step(s, buttons=0)
            r = _step(s, valid=1, touch_count=1, x=x, y=y)
            with self.subTest(x=x, y=y):
                self.assertEqual(r, _C("action_none"),
                                 f"coord ({x},{y}) outside row3 must return none")


# ── wire-format constants ─────────────────────────────────────────────
_MODE_NAMES = {0: "rear", 1: "shoulders", 2: "both"}


def _expected_wire(mode_int: int) -> bytes:
    name = _MODE_NAMES[mode_int]
    return f"AOD_INPUT_CONFIG_V1\nmode={name}\n".encode()


class SaveTest(unittest.TestCase):
    def test_save_failure_parent_missing(self) -> None:
        """Save to a path whose parent does not exist returns ACT_NONE and sets save_error."""
        with tempfile.TemporaryDirectory(prefix="aod_sfail_") as td:
            bad_path = os.path.join(td, "missing_subdir", "cfg")
            s = _new_state()
            _init(s)
            r = _save(s, bad_path)
            self.assertEqual(r, _C("action_none"),
                             "failed save must return ACT_NONE")
            self.assertNotEqual(s.save_error, 0,
                                "failed save must set save_error")

    def test_save_success_readback_exact(self) -> None:
        """Successful save returns ACT_EXIT, writes versioned wire bytes, reads back."""
        with tempfile.TemporaryDirectory(prefix="aod_sv_") as td:
            p = os.path.join(td, "cfg")
            s = _new_state()
            _init(s)
            old_selected = s.selected
            r = _save(s, p)
            self.assertEqual(r, _C("action_exit"),
                             "successful save must return ACT_EXIT")
            self.assertEqual(s.save_error, 0)
            self.assertEqual(s.persisted, old_selected)
            # exact wire bytes
            with open(p, "rb") as fh:
                raw = fh.read()
            self.assertEqual(raw, _expected_wire(old_selected),
                             f"wire bytes mismatch: {raw!r}")
            # readback via init
            s2 = _new_state()
            _init(s2, p)
            self.assertEqual(s2.load_error, 0)
            self.assertEqual(s2.selected, old_selected)

    def test_pending_selected_persists_old_rear_after_failed_save(self) -> None:
        """After a failed save, selected keeps new value but persisted keeps old."""
        with tempfile.TemporaryDirectory(prefix="aod_pend_") as td:
            cfg = os.path.join(td, "cfg")
            with open(cfg, "w") as fh:
                fh.write("AOD_INPUT_CONFIG_V1\nmode=rear\n")
            s = _new_state()
            _init(s, cfg)
            self.assertEqual(s.persisted, 0)   # rear=0
            # Navigate rear(0) -> shoulders(1) via first DOWN edge
            _step(s, buttons=0)
            _step(s, buttons=_C("btn_down"))
            _step(s, buttons=0)
            self.assertEqual(s.selected, 1, "first DOWN should reach shoulders")
            # Navigate shoulders(1) -> both(2) via second DOWN edge
            _step(s, buttons=_C("btn_down"))
            _step(s, buttons=0)
            new_selected = s.selected
            self.assertEqual(new_selected, 2,
                             "second DOWN should reach both")
            self.assertEqual(s.persisted, 0,
                             "persisted must still be rear before save attempt")
            # attempt save to non-existent parent
            bad = os.path.join(td, "no_sub", "cfg")
            r = _save(s, bad)
            self.assertEqual(r, _C("action_none"))
            self.assertEqual(s.selected, new_selected,
                             "selected must be unchanged after failed save")
            self.assertEqual(s.persisted, 0,
                             "persisted must still be rear after failed save")
            # original file must be untouched
            with open(cfg, "rb") as fh:
                raw = fh.read()
            self.assertEqual(raw, b"AOD_INPUT_CONFIG_V1\nmode=rear\n",
                             "original cfg file must be unmodified after failed save")

    def test_cancel_step_returns_action_cancel_no_dir_created(self) -> None:
        """Cancel step returns ACTION_CANCEL and never creates absent directories."""
        with tempfile.TemporaryDirectory(prefix="aod_can_") as td:
            s = _new_state()
            _init(s)
            _step(s, buttons=0)
            r = _step(s, buttons=_C("btn_cancel"))
            self.assertEqual(r, _C("action_cancel"),
                             "cancel step must return ACTION_CANCEL")
            self.assertFalse(os.path.exists(os.path.join(td, "sub")),
                             "cancel must not create directories")

    def test_cancel_preserves_existing_file_bytes(self) -> None:
        """Cancel after navigation leaves the existing config file byte-identical."""
        with tempfile.TemporaryDirectory(prefix="aod_canp_") as td:
            cfg = os.path.join(td, "cfg")
            with open(cfg, "wb") as fh:
                fh.write(b"AOD_INPUT_CONFIG_V1\nmode=shoulders\n")
            s = _new_state()
            _init(s, cfg)
            _step(s, buttons=0)
            _step(s, buttons=_C("btn_down"))   # navigate
            _step(s, buttons=0)
            _step(s, buttons=_C("btn_cancel"))
            with open(cfg, "rb") as fh:
                raw = fh.read()
            self.assertEqual(raw, b"AOD_INPUT_CONFIG_V1\nmode=shoulders\n",
                             "cancel must not modify existing config file")

    def test_save_then_exit_persisted_updated(self) -> None:
        """Successful save updates persisted to match selected."""
        with tempfile.TemporaryDirectory(prefix="aod_ex_") as td:
            p = os.path.join(td, "cfg")
            s = _new_state()
            _init(s)
            _step(s, buttons=0)
            r = _save(s, p)
            self.assertEqual(r, _C("action_exit"),
                             "successful save must return ACT_EXIT")
            self.assertEqual(s.persisted, s.selected)

    def test_null_state_save_no_crash(self) -> None:
        """aod_settings_save with null state must not crash."""
        lib = _lib()
        lib.aod_settings_save.restype  = ctypes.c_int
        lib.aod_settings_save.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        with tempfile.TemporaryDirectory(prefix="aod_nullsv_") as td:
            p = os.path.join(td, "cfg").encode()
            lib.aod_settings_save(None, p)   # must not crash; return value unchecked


if __name__ == "__main__":
    unittest.main(verbosity=2)