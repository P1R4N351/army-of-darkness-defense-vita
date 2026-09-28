# Building Army of Darkness Defense for PS Vita

The 1.0 release `eboot.bin` (sha256 `dcc82f4503916d04a951d690a29f856dbed603ccb92c1daf1df979ec9a89a116`) was
built from this source with the toolchain below.

## Toolchain (exact versions used for 1.0)

- VitaSDK **softfp** build (`vita_softfp`, float-abi=softfp; the `nightly-softfp` channel), built 2026-08-28:
  vitasdk-core 0.20260828.315, newlib 892f530, pthread-embedded 11d2e57, vita-headers 5e1e7d3,
  vita-toolchain eacff34; GCC 15.2.0.
- VitaSDK packages (vdpm):

  | package | version | package | version |
  |---|---|---|---|
  | opensles | 0.0.0.r41.ge35b063-1 | libsndfile | 1.2.2-1 |
  | mpg123 | 1.33.7-1 | lame | 4.0-1 |
  | libvorbis | 1.3.7-2 | libogg | 1.3.6-2 |
  | flac | 1.3.4-1 | opus | 1.6.1-1 |
  | freetype | 2.14.3-1 | libpng | 1.6.58-1 |
  | zlib | 1.3.2-2 | bzip2 | 1.0.8-1 |
  | vitaShaRK | 1.7-1 | SceShaccCgExt | 1.0.1-1 |
  | kubridge | 0.0.0.r19.g417ddde-1 | taihen | 0.11-1 |
  | libmathneon | 0.0.0.r11.g0faab81-1 | | |

- CMake 3.14 or newer (3.28.3 used).
- Submodules at the pinned commits: `lib/vitagl` 9c23758, `lib/so_util` c473237, `lib/falso_jni` 083d5a0,
  `lib/falso_ndk` 7dceb3b (not linked). The source archive already contains them.

## Build

    export VITASDK=/path/to/vitasdk-softfp PATH=$VITASDK/bin:$PATH
    git submodule update --init --recursive     # not needed for the source archive
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j4
    # -> build/eboot.bin, build/aod_vita.vpk

vitaGL is built from `lib/vitagl` by the CMake script with `SOFTFP_ABI=1 NO_SPLASHSCREEN=1 SAFE_UNIFORMS=1
NO_DEBUG=1`. To relink with a modified library, see "Relinking and rebuilding" in `THIRD_PARTY_NOTICES.md`; the exact
sources and recipes of the VitaSDK libraries are in `aod-vita-1.0-dependency-sources.tar.gz`.

The release eboot maps the source directory in `__FILE__` to `/usr/src/army-of-darkness-defense-vita`
(`-fmacro-prefix-map` in `CMakeLists.txt`), so the loadable image does not depend on where you build. The eboot
container bytes can still differ, through ELF file offsets of non-loaded data.

## Tests

- `release/tests/`: the data preparation tool (Python standard library). The tests that need the game run
  when `AODD_APK` points to your 1.1.1 APK:
  `AODD_APK=/path/to/your.apk python3 -m unittest discover -s release/tests -v`.
- `tests/run_all.sh`: host and 32-bit ARM tests of the loader modules. It needs gcc, the Android NDK r27d
  (armv7a softfp) and qemu-arm in a docker image; see the individual `tests/run_*.sh` scripts.

## Layout

| path | content |
|---|---|
| `source/` | the loader (from soloader-boilerplate) and the port's Android/JNI/GL/audio bridges (`source/aod/`) |
| `lib/` | so_util, FalsoJNI, FalsoNDK, vitaGL (submodules), fios, libc_bridge, sha1 |
| `release/` | `prepare_data.py` (data preparation from the user's APK), the offline profile (`release/offline/`), expected hashes, tests |
| `extras/livearea/` | bubble and LiveArea images (see `THIRD_PARTY_NOTICES.md`, "Presentation artwork") |
| `docs/` | engineering notes (audio investigation) and the upstream boilerplate README |
| `tools/` | development helpers (LiveArea resizing, diagnostics) |

## Architecture (source/aod/)
| file | role |
|---|---|
| `jni_bridge.c` | Replaces FalsoJNI's name-only dispatch after `jni_init()`: (class, name, signature) method table, receiver objects, static-int table, `getClass/getName`, real SHA-1/SHA-256/MD5 digests for the signed service configs and asset manifest, and Java-faithful failure replies (IAP/ads/push/advertising-id/video/email/dialog) queued and delivered after `nativeUpdate()`. |
| `gen_static_ints.h` | Generated from the decompiled Java (`public static final int` of the classes libgame references). |
| `hash.c` | SHA-256 and MD5 (SHA-1 is `lib/sha1`). |
| `text_render.c` | FreeType implementation of `bf_ui.TextRendering` (platform UI text; in-game text is libgame's own FreeType). |
| `port_vita.c` | Paths, locale, disk space, uptime, and PlatformAlert on the system message dialog. |
| `newlib_glue.c` | `_getentropy_r` missing from nightly softfp newlib. |

Loader changes to the boilerplate:
- Loads `libfmodex.so` at 0x99000000, then `libgame.so` at 0x98000000. Game imports of FMOD
  resolve to fmodex exports via DT_NEEDED.
- Real `__gnu_Unwind_Find_exidx` over both modules (it was `ret0`), plus per-module
  `__exidx_start/__exidx_end`.
- `dlsym` returned the address of the table slot instead of the symbol. That's fixed, so FMOD's
  `dlopen("libOpenSLES.so")`/`dlsym(slCreateEngine, SL_IID_*)` reach the VitaSDK OpenSL ES
  library.
- `AAsset_seek` returned `fseek`'s 0 instead of the new offset. That's fixed.
- `sysconf` (was `ret0`), `__assert2`, `_exit`, `uname`, `times`, `writev`, `inet_addr` etc.
- The kuser helper patch applies to both modules. The logger is always on and tee'd to a file.

