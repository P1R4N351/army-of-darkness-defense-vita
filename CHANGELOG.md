# 1.1.0 — experimental homebrew trophies

Adds an optional local homebrew trophy bridge for all 52 original achievement keys, a recoverable
local earned-event journal, a generated trophy pack and a matching own-APK preparation kit.
Original predicates, thresholds and game-save counters remain unchanged. Stable application and
communication identity remains AODD00001; trophy IDs remain 0–51. No unearned auto-unlocks or online
services are enabled. Existing saves must be backed up and preserved when updating prepared data.

Native trophy unlocks, NoTrpDrm registration, popups and device behavior are untested and unverified.
Released experimentally at the author's request without further gameplay testing. Source host checks
and isolated Vita cross-build pass; these do not prove runtime trophy operation. Earlier bounded
emulator comparisons reached tutorial victory with both base and candidate and observed a shared host
SIGSEGV, but did not earn or verify a native unlock. No hardware validation of the add-on has occurred.

Install the VPK and regenerate data using aodd-prepare-kit-1.1.zip and your own supported Android 1.1.1
APK. No game data or private libraries are included. Rear touch pad and additional button support
remain planned; this release preserves existing controls. The immutable v1.0.0 release remains available.

# Changelog

## 1.0 (v1.0.0), first release

Army of Darkness Defense (Android 1.1.1) for PS Vita, by PiSCES.

- So-loader port: the game's own ARMv7 `libgame.so` and `libfmodex.so` run natively, with Android/JNI,
  OpenGL ES (vitaGL) and OpenSL ES bridges.
- Graphics: correct viewport under the softfp ABI; the game's GLSL shaders are compiled at run time.
- Audio through FMOD Ex and OpenSL ES:
  - sound effects;
  - music streams;
  - the output runs at FMOD's own sample rate, so pitch and speed are correct.
- Touch input; Circle/Select act as Back and Start as Menu.
- `prepare_data.py` builds the data folder from the user's own APK, offline, with the offline profile:
  - no ads, analytics or social services;
  - local, free coin-store purchases;
  - every output file hash-checked.
- Diagnostics are off by default (`ux0:data/aodd/diag.enable` turns them on).
