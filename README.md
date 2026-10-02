# Army of Darkness Defense for PS Vita (unofficial port) 1.1 — experimental homebrew trophies

A port of the Android game *Army of Darkness Defense* **1.1.1** to the PlayStation Vita, by **PiSCES**.
It is a so-loader port: the game's own ARMv7 code from the Android APK (`libgame.so`, `libfmodex.so`) runs
natively on the Vita, and this loader supplies the Android, JNI, OpenGL ES and audio interfaces it expects.

**This release contains none of the original game's runtime files**: no `libgame.so`, no `libfmodex.so` and
no game assets. You need your own copy of the Android game (the APK file of version 1.1.1); a small, offline
preparation tool turns it into the data folder the port needs. The release does include presentation
artwork for the Vita bubble, LiveArea and cover, supplied by the port author (see "Credits and licenses").

Not affiliated with or endorsed by Backflip Studios, MGM or Sony. *Army of Darkness Defense* was developed
and published by Backflip Studios; *Army of Darkness* and its characters belong to their respective owners.

## What you need

On the Vita (the port checks these at start and shows an error message if one is missing):
- A PS Vita or PS TV that runs homebrew (HENkaku/Ensō with taiHEN). Tested on a PS Vita; PS TV is untested.
- **kubridge.skprx** listed under `*KERNEL` in `ur0:tai/config.txt` (or `ux0:tai/config.txt`), then a reboot.
  Otherwise: "kubridge.skprx is not installed".
- **libshacccg.suprx** at `ur0:data/libshacccg.suprx` or `ur0:data/external/libshacccg.suprx` (extract it with
  ShaRKBR33D). Otherwise: "libshacccg.suprx is not installed".

On a PC (Windows, macOS or Linux):
- **Python 3.8 or newer** (tested with Python 3.12 on Linux and with Python 3 on macOS). Nothing else: the
  tool uses only the Python standard library and no network.
- Your **Army of Darkness Defense 1.1.1** Android APK (package `com.backflipstudios.android.aodd`,
  versionCode 7018). The tested file has
  sha256 `4800eac5d52807a7c0d0b4f3e41489a1ce68de7baa0d50b8aa79c4207ebf6bf4`.
  An APK that was re-packed but contains the identical 1.1.1 game files is also accepted: the tool then
  checks each of the 1499 needed files individually. Other versions are refused.

## 1. Prepare the data folder (on the PC)

Unzip `aodd-prepare-kit-1.1.zip`, then run:

    python3 prepare_data.py path/to/your.apk out

(On Windows, `py prepare_data.py path\to\your.apk out`.) On success it prints
`OK: out/aodd (1449 files) matches the known-good data tree`. The tool:
- refuses the APK unless it is exactly the 1.1.1 game data, and refuses unsafe archives;
- extracts only `libgame.so`, `libfmodex.so` and `assets/`;
- applies the port's offline profile (below);
- checks every output file against the known-good tree before it creates `out/aodd`.

It never changes the APK and never writes outside `out`. `python3 prepare_data.py --check out/aodd`
re-verifies a prepared folder. **Do not share the prepared folder**: it contains the game.

## 2. Install on the Vita

1. **Keep your saves.** Progress is stored in `ux0:data/aodd/files/`. Back it up before changing anything.
2. If `ux0:data/aodd/` already exists (an earlier install), delete everything in it **except `files/`**.
   Old game files left behind from a different data set could change the game's behaviour.
3. Copy the *contents* of `out/aodd/` into `ux0:data/aodd/` (VitaShell over USB or FTP). The prepared
   `files/` folder is empty, so it cannot overwrite your saves.
4. Install `aod_vita.vpk` with VitaShell. The bubble is "Army of Darkness Defense" (title ID `AODD00001`).

## Controls

| Vita | Game |
|---|---|
| Front touch screen | the game's touch controls |
| Circle or Select | the Android Back button |
| Start | Android Menu |

The other buttons, the sticks, the rear touch pad and motion sensors are not used by the game.

## The offline profile (applied by the preparation tool)

The Android game expects Google Play services, ad networks, analytics and online leaderboards, none of
which exist on the Vita. The profile edits only the game's Lua scripts and configuration files; the native
libraries (`libgame.so`, `libfmodex.so`) are not modified:
- ads, video ads, analytics, crash reporting and install tracking are removed, together with their
  service configurations, keys and identifiers;
- the leaderboard, "more games", rating, privacy and legal web buttons are removed;
- the publisher server address is replaced with an address that never resolves;
- **the coin store works offline**: each gold pack shows `FREE` and adds the original gold amount
  locally. Nothing is bought, nothing is charged and no network is used. "Restore Purchases" is removed.

The game verifies its configuration files with a key stored in its own `libgame.so`. The tool reads that
key from *your* copy at run time to re-sign the files it edits; the key is not included in this release.

## Files the port uses on the Vita

| path | meaning |
|---|---|
| `ux0:data/aodd/libgame.so`, `libfmodex.so`, `assets/` | the prepared game data (required) |
| `ux0:data/aodd/files/` | your saves |
| `ux0:data/aodd/aod_log.txt` | a log, rewritten at every launch (include it in bug reports) |
| `ux0:data/aodd/shaders/` | a copy of each game shader source, saved when it is first compiled (a file in `shaders/override/` with the same name is compiled instead) |
| `ux0:data/aodd/nosound` | optional empty file: starts the game without sound |
| `ux0:data/aodd/diag.enable` | optional empty file: turns on extra graphics/audio diagnostics (slower) |

## Known limitations

- Hardware testing (one PS Vita, by the author) used the pre-release build "boot-13": the game boots, menus
  and gameplay work, and music and sound effects play. The released `eboot.bin` differs from that build only
  in two diagnostic source-file path strings, which were normalised for privacy; the program code is
  identical. The released build itself has not been run on a PS Vita; an independent emulator regression
  check of it is separate from this README. A complete campaign, suspend/resume, PS TV and long sessions have not been
  tested systematically, and no performance measurements have been made; some slowdown can occur.
- Only version 1.1.1 is supported.
- Online leaderboards, Game Center and cloud services do not exist in this port. Optional local homebrew trophies are described below.

## Experimental homebrew trophies (1.1)

**Native trophy unlocks, NoTrpDrm registration and actual device behavior are untested and unverified.**
This update is released experimentally without further gameplay testing. The earlier 1.0 testing above
covers the base port only; it does not establish that trophies work. Host queue/storage tests and an
isolated Vita build passed, but there is no verified earned native unlock or trophy popup.

The add-on preserves all 52 original achievement keys and conditions, with stable IDs 0–51 and
communication ID `AODD00001`. Completed events go to a local durable queue and optional `sceNpTrophy`
backend. It does not enable Game Center, PlayStation Network or any removed online service, and does
not automatically award unearned trophies. There is no bulk award for past progress.

**Upgrade both the VPK and prepared data.** Re-run `aodd-prepare-kit-1.1.zip` on your own supported APK,
then copy the prepared game data while preserving `ux0:data/aodd/files/` and your existing saves.
The 1.0 preparation kit does not contain the earned-event bridge required by this release.
For optional NoTrpDrm setup, use its own official instructions linked in `docs/TROPHIES.md`; this release
installs no plugins or device configuration. Missing-service fallback is designed to queue earned events,
but that native behavior is also unverified. An empty `ux0:data/aodd/no-trophies` file disables registration
and the local journal. Back up `files/` before installing this experimental update.

Rear touch pad and additional button support remain planned; controls are unchanged in this release.
See `docs/TROPHIES.md` for the journal, identity and validation limits.

## Building from source

See `BUILDING.md`. It needs the **softfp** VitaSDK (`nightly-softfp`).

## Contact and links

- Author: PiSCES, piranesi.ai@outlook.com.
- Source repository: https://github.com/P1R4N351/army-of-darkness-defense-vita
- Release page: https://github.com/P1R4N351/army-of-darkness-defense-vita/releases/tag/v1.1.0

## Credits and licenses

- Port: **PiSCES**. The port's code, tests and tooling were written almost entirely by AI coding agents
  (Anthropic Claude), directed by PiSCES and tested by PiSCES on a real PS Vita.
- Based on [soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate) by Volodymyr
  Atamanenko (and Andy Nguyen, Rinnegatamante), using so_util, FalsoJNI, vitaGL and vitaShaRK, kubridge,
  the VitaSDK and the libraries listed in `THIRD_PARTY_NOTICES.md`.
- The port's own source code is under the MIT License (`LICENSE.txt`). `eboot.bin` statically links
  libraries under their own licenses, one of them GPL-3.0 (SceShaccCgExt), so `eboot.bin` as a whole is
  distributed under the GPL-3.0. Notices and license texts are in `THIRD_PARTY_NOTICES.md` and `LICENSES/`.
  The complete corresponding source is `aod-vita-1.1-source.tar.gz` (the port) and
  `aod-vita-1.1-dependency-sources.tar.gz` (the linked VitaSDK libraries, with their build recipes).
- The bubble, LiveArea and cover artwork is presentation art supplied by the port author; see "Presentation
  artwork" in `THIRD_PARTY_NOTICES.md`. It is **not** covered by the MIT License or any other license here.
- The game itself (its code, art, audio and text) is not part of this release and is not licensed by it.
  FMOD Ex is © Firelight Technologies and is part of the game's own files.
