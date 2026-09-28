# Dependency sources for Army of Darkness Defense for PS Vita 1.0

This archive holds the complete source of every VitaSDK library statically linked into `eboot.bin`, in
the exact versions used, plus the build recipes and patches that produced the installed packages. The
port's own source, including vitaGL, is in `aod-vita-1.0-source.tar.gz`.

How each version was established:
- Each installed VitaSDK package records the sha256 of the recipe that built it (`pkgbuild_sha256sum`). All
  17 match the `VITABUILD` files in `recipes/`, taken from https://github.com/vitasdk/packages at commit
  `771ad4363f357f65d6ecedae9de920e17cb959de`; those recipes are unchanged since the packages were built.
- Every upstream archive in `upstream/` matches the sha256 in its recipe.
- Git sources are exported at the commit pinned by the recipe (`git/`).
- The VitaSDK core components (newlib `892f530`, pthread-embedded `11d2e57`) are pinned by the SDK's
  `version_info.txt`.

| library | version | license | source in this archive | recipe |
|---|---|---|---|---|
| SceShaccCgExt | 1.0.1 | GPL-3.0 | `upstream/SceShaccCgExt-v1.0.1.tar.gz` | `recipes/SceShaccCgExt` |
| vitaShaRK | 1.7 | LGPL-3.0 | `upstream/vitaShaRK-v.1.7.tar.gz` | `recipes/vitaShaRK` |
| pthread-embedded | 11d2e57 | LGPL-2.1+ (Vita parts MIT) | `git/pthread-embedded-11d2e5722d98.tar` | VitaSDK core (its own CMakeLists.txt) |
| libsndfile | 1.2.2 | LGPL-2.1+ | `upstream/libsndfile-1.2.2.tar.xz` | `recipes/libsndfile` |
| mpg123 | 1.33.7 | LGPL-2.1 | `upstream/mpg123-1.33.7.tar.bz2` | `recipes/mpg123` (+ `mpg123.patch`) |
| LAME | 4.0 | LGPL-2.0+ | `upstream/lame-4.0.tar.gz` | `recipes/lame` |
| opensles | e35b063 | Apache-2.0 | `git/opensles-e35b0630ac4c.tar` | `recipes/opensles` |
| libvorbis | 1.3.7 | BSD-3-Clause | `upstream/libvorbis-1.3.7.tar.gz` | `recipes/libvorbis` |
| libogg | 1.3.6 | BSD-3-Clause | `upstream/libogg-1.3.6.tar.xz` | `recipes/libogg` |
| FLAC | 1.3.4 | BSD-3-Clause | `upstream/flac-1.3.4.tar.xz` | `recipes/flac` |
| Opus | 1.6.1 | BSD-3-Clause | `upstream/opus-1.6.1.tar.gz` | `recipes/opus` |
| FreeType | 2.14.3 | FTL | `upstream/freetype-2.14.3.tar.xz` | `recipes/freetype` |
| libpng | 1.6.58 | libpng | `git/libpng-3061454d980d.tar` (tag v1.6.58) | `recipes/libpng` (+ `libpng.patch`) |
| zlib | 1.3.2 | zlib | `upstream/zlib-1.3.2.tar.xz` | `recipes/zlib` (+ `zlib-no-pic.diff`) |
| bzip2 | 1.0.8 | bzip2 | `upstream/bzip2-1.0.8.tar.gz` | `recipes/bzip2` (+ `bzip2.pc`) |
| math-neon | 0faab81 | MIT | `git/math-neon-0faab814782c.tar` | `recipes/libmathneon` |
| taiHEN stubs | 0.11 | MIT (`licenses/taihen-LICENSE`) | `upstream/taihen.tar.gz` | `recipes/taihen` |
| kubridge stubs | 417ddde | none published upstream | `git/kubridge-417ddde9a744.tar` | `recipes/kubridge` (+ `vita-headers-886.diff`) |
| newlib | 892f530 | newlib/libgloss (`licenses/newlib-*`) | not bundled: upstream https://github.com/vitasdk/newlib at `892f530fa7d696dbca74abfb8fccfec7d21d269d` | VitaSDK core |

newlib is not bundled here because none of its licenses requires its source to be distributed. Its
license files are included in `licenses/`, and it is available at the pinned commit above.

The system-library import stubs come from vita-headers `5e1e7d38d766e4c1634a77f6e5249caab8c8f9cb`
(`licenses/vita-headers-LICENSE.md`, MIT).

To rebuild a library: unpack its recipe directory, place or point the source next to it as the
`VITABUILD` `source=` line expects, apply any patch listed there, and run `vita-makepkg` with the softfp
VitaSDK. Then relink the port (`BUILDING.md`).
