# Input Settings

The Input Settings screen is accessible as a standalone entry in the PS Vita
LiveArea alongside the main AOD launcher.  It requires no game APK, library,
OBB, trophy data, or NoTrpDrm-specific prerequisite to function; it reads and
writes only its own configuration file.  Standalone Settings performs no trophy or NoTrpDrm initialization.

The screen does require the standard VitaGL and homebrew runtime prerequisites
present in the parent project.  SceShaccCgExt is linked transitively and
carries TaiHEN shader support.  This document does not claim a
zero-prerequisite native runtime or the absence of all plugins; runtime
prerequisites have not been independently verified in full.

---

## Modes

Three persistent input modes are available.  Front touch remains available in
all modes and is not affected by the selection here.

| Mode | Description |
|---|---|
| **Rear touch (left/right)** | An intentional contact whose first point lands inside the left zone (x 25–45 %, y 25–75 % of the reported rear panel active area) is owned by the left channel; a contact whose first point lands inside the right zone (x 55–75 %, y 25–75 %) is owned by the right channel.  Outer grip contacts and a held contact that slides into or out of a zone do not acquire a new zone; zone ownership is fixed at the moment of initial contact and is released only when that contact lifts.  Left/right opposition is neutral.  Outer held grip contacts must not disable normal front-touch or button pointer input. |
| **Shoulder buttons (L/R)** | L and R shoulder buttons act as left / right input.  Left/right opposition is neutral. |
| **Both** | Rear touch zones and shoulder buttons are both active under the rules above.  Same-side sources are aggregated across both rear contacts and shoulder buttons on the same side: for example, a held left-rear contact plus a held L button both own the left channel; releasing either does not release the other, and the left channel stays active until all left-side owners have lifted.  The same applies to the right side.  Left/right opposition is neutral.  Front pointer input remains available in all modes. |

These are the implemented input semantics as described for reviewer reference.
No native hardware proof or emulator verification is implied.

**Default:** Shoulder buttons.  This default is applied when the configuration
file is absent; no corruption error is shown in that case.

---

## Configuration File

Settings are stored at:

```
ux0:data/aodd/input-controls.cfg
```

The directory `ux0:data/aodd/` is created only when Save is attempted; a
failed save may leave the dedicated directory, but old config stays unchanged.
Cancel creates nothing.

### File Format

The file uses a versioned closed-text format.  The complete content is exactly:

```
AOD_INPUT_CONFIG_V1
mode=<value>
```

where `<value>` is one of `rear`, `shoulders`, or `both`.  Two lines, no
trailing whitespace beyond the newlines, no other content.

### Load Behaviour

| Condition | Result |
|---|---|
| File absent | Defaults to Shoulders; no error shown. |
| File present, valid | Loads saved choice. |
| File present, malformed or unreadable | Defaults to Shoulders; visible load error shown in UI. |

---

## Write Safety

Saving writes to a bounded unique temporary file via `mkstemp` (avoiding stale
fixed-name `.tmp` races), followed by `fflush`, `fsync` via `fileno`, and
`fclose`, all with checked return values.  The final commit is an atomic
`rename` onto the target path.  Any pre-commit failure leaves the previous
configuration file untouched.

No directory `fsync` is performed after the rename.  Native Vita filesystem
durability across power loss is not proven; do not claim that guarantee.

---

## UI Behaviour

Navigation uses D-pad Up/Down to move between the three rows.  Cross (✕)
confirms Save; Circle (○) cancels.  Front touch rows select the input mode;
separate Save and Cancel buttons perform their respective actions.  If front
sampling is unavailable, fall back to physical controls; a failed read requires
a fresh release before the next input is recognised.

**Visual states:**

- **Amber outline** — currently selected (pending) row.
- **Cyan saved-choice indicator** — the row that is committed to the file.

On a successful Save or a Cancel, the screen exits back to LiveArea.  Cancel
leaves the prior saved choice unchanged and creates no file or directory.
There is no automatic game launch from this screen.

**Save failure:** The UI remains open with the pending selection kept.  The
previously committed choice is unchanged.  A visible error message is shown.
The user may retry or cancel.

**Input fences:** An invalid or multi-touch contact does not produce a fresh
Save until the contact is fully released.  Physical Cancel has priority.

---

## Build Notes

- Controller API header: `source/aod/input_settings.h`
- Tests use production translation units; no replica source is produced.
- Test runner: `tests/run_input_config_tests.sh`
- Planned build passes: strict Release (`-O3 -ffast-math -fno-fast-math` per
  source), storage fault injection, ASan+UBSan, Python ctypes controller.
- Hardware is unavailable and emulator/native evidence is pending.  Integration
  owners record pass/fail results separately.  Do not publish a package based
  on this document alone.
- The settings build uses per-source `-fno-fast-math` even when the parent
  CMake target enables `-ffast-math` globally.  CMake packaging, configurator
  binary, and LiveArea init are owned by the parent project.

---

## LiveArea Artwork

Button and panel images are generated by:

```
python3 tools/build_input_settings_art.py --output-dir extras/livearea
```

Output: `input-settings.png` (160×80 RGBA button) and
`input-settings-screen.png` (960×544 RGBA panel).

**Font provenance:** DejaVu Sans is derived from Bitstream Vera Sans.
Bitstream Vera is copyright Bitstream Inc.; DejaVu additions are in the public
domain.  Full license: `/usr/share/doc/fonts-dejavu-core/copyright`.  No font
binary is redistributed; the generator loads the font from the path supplied
via `--font` (default: installed system path).

Static art does not paint the amber selection outline or cyan saved-choice
indicator; those are drawn by the native controller overlay at runtime.

Existing saved assets and original art are not modified by the generator.

---

## Out of Scope

- No trophy unlock or removal.
- No online service interaction.
- No plugin or device changes.
- User-provided APK/OBB files are private and are never packaged.