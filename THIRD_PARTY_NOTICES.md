# Third-party notices: Army of Darkness Defense for PS Vita 1.0

`eboot.bin` in `aod_vita.vpk` is one statically linked executable. The components below are in it. Each
was identified from the symbols in the linked ELF, not only from the build script. Full license texts are
in `LICENSES/`.

## License of eboot.bin as a whole

`eboot.bin` statically links **SceShaccCgExt**, which is licensed **GPL-3.0**. The executable as a whole is
therefore distributed under the terms of the **GNU GPL version 3** (`LICENSES/GPL-3.0.txt`). Every other
component is compatible with that: MIT, BSD, zlib, FTL, Apache-2.0, LGPL-2.1(+)/LGPL-3.0 and newlib's
permissive licenses. The port's own source files remain available under the MIT License, which permits
this use.

The complete corresponding source is:
- the port: `aod-vita-1.0-source.tar.gz`, which includes vitaGL, so_util, FalsoJNI and FalsoNDK;
- every statically linked VitaSDK library: `aod-vita-1.0-dependency-sources.tar.gz`, which contains the exact
  upstream archives and commits, the VitaSDK build recipes and patches that built them, and verification
  hashes (see `DEPENDENCY-SOURCES.md` in it);
- the build steps: `BUILDING.md`.

## Components linked into eboot.bin

Identified from the symbols in the linked ELF. Versions come from the installed VitaSDK packages, and each
package's recipe was matched to it by sha256.

| component | version | license | copyright | license text in `LICENSES/` |
|---|---|---|---|---|
| Port code | 1.0 | MIT | © 2026 PiSCES | `LICENSE` |
| soloader-boilerplate (base of `source/`, `lib/fios`, `lib/libc_bridge`) | 0a382f0 | MIT | © 2021 Andy Nguyen, © 2021-2022 Rinnegatamante, © 2022-2024 Volodymyr Atamanenko | `LICENSE` |
| so_util | c473237 | MIT | © 2022 Andy Nguyen | `lib/so_util/LICENSE` |
| FalsoJNI | 083d5a0 (v1.4) | MIT | © 2022 Volodymyr Atamanenko | `lib/falso_jni/LICENSE` |
| sha1 | - | public domain | Brad Conte | - |
| vitaGL | 9c23758 | LGPL-3.0 | © Rinnegatamante and contributors | `LGPL-3.0.txt`, `GPL-3.0.txt` |
| vitaShaRK | 1.7 | LGPL-3.0 | © 2017-2020 Rinnegatamante | `vitaShaRK-LICENSE.txt` |
| **SceShaccCgExt** | 1.0.1 | **GPL-3.0** | © bythos14 | `SceShaccCgExt-LICENSE.txt` |
| pthread-embedded (VitaSDK core) | 11d2e57 | LGPL-2.1-or-later; Vita port parts MIT | © 1998 John E. Bossom, © 1999-2005 Pthreads-win32 contributors, © 2008 Jason Schmidlapp, © 2016 Davee | `pthread-embedded-COPYING.txt`, `pthread-embedded-COPYING.vita.txt`, `LGPL-2.1.txt` |
| newlib (VitaSDK core) | 892f530 | newlib/libgloss licenses (BSD-style and similar) | Red Hat and others | `newlib-COPYING.NEWLIB.txt`, `newlib-COPYING.LIBGLOSS.txt` |
| OpenSL ES for Vita (frangarcj/opensles) | e35b063 | Apache-2.0 (AOSP sources); Khronos license (headers) | © The Android Open Source Project; © 2007-2009 The Khronos Group Inc. | `Apache-2.0.txt` |
| libsndfile | 1.2.2 | LGPL-2.1-or-later | © 1999-2016 Erik de Castro Lopo and contributors | `libsndfile-COPYING.txt` |
| mpg123 | 1.33.7 (with the VitaSDK recipe patch) | LGPL-2.1 | © 1995-2023 the mpg123 project | `mpg123-COPYING.txt`, `LGPL-2.1.txt` |
| LAME | 4.0 | LGPL-2.0-or-later | © 1999 Mark Taylor and the LAME developers | `lame-COPYING-LGPL-2.0.txt` |
| FreeType | 2.14.3 | FreeType License (FTL) | Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved. | `FreeType-FTL.txt` |
| libpng | 1.6.58 | libpng license | the libpng authors | `libpng-LICENSE.txt` |
| zlib | 1.3.2 | zlib license | © 1995-2026 Jean-loup Gailly and Mark Adler | `zlib-LICENSE.txt` |
| bzip2 | 1.0.8 | bzip2 license (BSD-style) | © 1996-2019 Julian Seward | `bzip2-LICENSE.txt` |
| libogg | 1.3.6 | BSD-3-Clause | © Xiph.Org Foundation | `libogg-COPYING.txt` |
| libvorbis, libvorbisenc | 1.3.7 | BSD-3-Clause | © Xiph.Org Foundation | `libvorbis-COPYING.txt` |
| FLAC (libFLAC) | 1.3.4 | BSD-3-Clause | © 2000-2009 Josh Coalson, © 2011-2016 Xiph.Org Foundation | `flac-COPYING.Xiph.txt` |
| Opus | 1.6.1 | BSD-3-Clause | © Xiph.Org Foundation, Skype Limited and others | `opus-COPYING.txt` |
| math-neon (libmathneon) | 0faab81 | MIT | © 2015 Lachlan Tychsen-Smith | `math-neon-MIT.txt` |
| taiHEN import stubs | 0.11 | MIT | © 2016 Yifan Lu | `taiHEN-MIT.txt` |
| SCE system library import stubs (vita-headers) | 5e1e7d3 | MIT | © 2017 vitasdk | `vita-headers-MIT.txt` |
| kubridge import stubs | 417ddde | **no license published upstream**. Only NID import stubs are linked, generated from its `exports.yml`; no kubridge code is in eboot.bin. kubridge.skprx itself is installed by the user. | bythos14 | - |
| libgcc / libstdc++ (GCC 15.2.0) | - | GPL-3.0 with the GCC Runtime Library Exception | Free Software Foundation | `GPL-3.0.txt` |

FalsoNDK (`lib/falso_ndk`, Apache-2.0) is in the source tree but is **not** linked (`NDK_PORT=OFF`).

## Relinking and rebuilding

- **The port, vitaGL, so_util and FalsoJNI:** edit the source archive and build it as described in
  `BUILDING.md`. vitaGL is compiled from `lib/vitagl` by the CMake script.
- **The VitaSDK packages** (vitaShaRK, SceShaccCgExt, opensles, libsndfile, mpg123, LAME, codecs, FreeType,
  libpng, zlib, bzip2, math-neon, stubs):
  1. take the package's recipe and patches from `recipes/<package>/` in the dependency-source archive,
     change the source, and build it with `vita-makepkg`;
  2. install the result into your softfp VitaSDK (`vdpm`/pacman, or copy the `.a` and headers into
     `$VITASDK/arm-vita-eabi`);
  3. rebuild the port. The linker picks up the new static library.
- **pthread-embedded and newlib** are part of the VitaSDK core: build pthread-embedded at the pinned commit
  with its own CMake (`-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake`), replace
  `$VITASDK/arm-vita-eabi/lib/libpthread.a`, then rebuild the port.

## Not part of this release

`libgame.so`, `libfmodex.so` and the `assets/` tree come from the user's own copy of the Android game and
are never distributed with the port. *Army of Darkness Defense* © Backflip Studios; *Army of Darkness*
and its characters belong to their respective owners (MGM). FMOD Ex © Firelight Technologies Pty Ltd.

## Presentation artwork

`extras/livearea/*.png` (the bubble icon, LiveArea background, startup card and pic0) and the release cover
image are resized from artwork supplied by the port author. They show the game's title, logo and
characters, are **not** covered by the MIT License or any other license in this file, and are included only
as presentation for this port; the underlying marks and characters belong to their respective owners.
