# Milestone 1 dependencies

`manifest.json` pins the entire target SDK archive, so its headers and binary
libraries cannot vary independently. The cross-build uses only this sysroot;
host pkg-config paths are cleared. All library build options and source revisions
are available in the pinned [Buildroot release](https://github.com/OpenDingux/buildroot/tree/od-2022.09.22).
The SDK includes Ingenic/XBurst instructions in its runtime libraries.

The 2021-10-22 SDK candidate in `PLAN.md` was unavailable (both historical
hosting domains failed DNS resolution). The selected official 2022-09-22 SDK
has GCC 11.1.0 and uClibc-ng 1.0.38. Adam v2.1's repository identifies its
OpenDingux base as 2022-02-13, Buildroot revision
`26145a93f2e17d0df86ae20b7af455ea155e169c`; that revision also uses uClibc-ng
1.0.38 and the GCW0 native-thread/hard-float configuration. This is ABI evidence;
the device checklist still establishes actual launch/display/audio compatibility.

Target libraries are supplied unmodified by the SDK and copied, stripped, into
the OPK, including SDL, SDL_ttf, mpg123, ALSA, FreeType, PNG, zlib, iconv, DRM,
udev, tslib, libstdc++, and libgcc. The firmware supplies `/lib/ld-uClibc.so.0`
(loader SONAME `ld-uClibc.so.1`)
and `libc.so.0`; no application libraries are installed into firmware paths.
ALSA uses the firmware's routing/configuration and SDL's linked ALSA backend.
`runtime-libraries.json` in the OPK records each bundled SONAME, checksum,
transitive dependencies, and the explicitly firmware-provided dependencies.

## Licensing and source

The application is GPL-2.0-or-later. Its complete source is this repository.
All third-party notices are bundled inside `licenses/` in the OPK and checked
in under `notices/`; `sources.json` records their upstream URLs and hashes.
The DejaVu font license is additionally kept beside the four font faces.
The original bitmap glyphs, icon, and tone are covered by the application license.

| Library | Version / source | License |
|---|---|---|
| SDL | OpenDingux/SDL `97557d6bd` | LGPL-2.1+ |
| SDL_ttf | libsdl-org/SDL_ttf `70b2940cc75e92aab02a67d2f827caf2836a2c74` | Zlib |
| mpg123 | 1.25.15, https://www.mpg123.de/download/ | LGPL-2.1 |
| ALSA | 1.2.5.1, https://github.com/alsa-project/alsa-lib/tree/v1.2.5.1 | LGPL-2.1+ |
| FreeType | 2.10.4, https://github.com/freetype/freetype/tree/VER-2-10-4 | FTL (selected) |
| libpng | 1.6.37, https://github.com/pnggroup/libpng/tree/v1.6.37 | libpng-2.0 |
| zlib | 1.2.11, https://github.com/madler/zlib/tree/v1.2.11 | Zlib |
| libiconv | 1.15, https://ftp.gnu.org/gnu/libiconv/ | LGPL-2.0+ (library only) |
| libdrm | 2.4.107, https://dri.freedesktop.org/libdrm/ | MIT |
| libudev | eudev 3.2.10, https://github.com/eudev-project/eudev/tree/v3.2.10 | LGPL-2.1+ (library only) |
| tslib | 1.22, https://github.com/libts/tslib/tree/1.22 | LGPL-2.1+ (library only) |
| libstdc++, libgcc | GCC 11.1.0, https://gcc.gnu.org/releases.html | GPL-3.0+ with GCC Runtime Library Exception |
| DejaVu Serif | 2.37, https://github.com/dejavu-fonts/dejavu-fonts/releases/tag/version_2_37 | Bitstream Vera; DejaVu changes public domain |

FreeType notice: portions of this software are copyright © 2020 The FreeType
Project (www.freetype.org). All rights reserved.

Shared libraries remain replaceable in an extracted/repacked OPK. Retain the
repository/source revisions and notices when distributing derived packages.
`scripts/fetch-notices.py` is a maintainer tool, not a build-time network dependency.
