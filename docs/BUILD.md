# Building the Milestone 2 reader

## Host prerequisites (Fedora 42, x86_64 Linux)

```sh
sudo dnf install cmake ninja-build gcc-c++ python3 curl tar xz bzip2 file \
    SDL-devel SDL_ttf-devel mpg123-devel alsa-lib-devel squashfs-tools \
    freetype-devel libpng-devel libjpeg-turbo-devel zlib-devel
```

Fedora's SDL 1.2 package may resolve to sdl12-compat; the device uses the actual
OpenDingux SDL 1.2 fork. `mksquashfs` **4.6.1** is required for the documented
byte-for-byte OPK reproduction. Downloads, SDK, build trees, and staging are
project-local and ignored by Git. Initial target bootstrap needs about 1 GB of
disk space and network access. No target dependencies are resolved from host
libraries.

## Cross-build and package

From the repository root:

```sh
bash scripts/build-opk.sh
```

The script verifies the SDK/font/CRengine/xxHash archive SHA256 values from
`third_party/manifest.json`, verifies extracted reader sources against their
archives, extracts and relocates the SDK under
`third_party/sdk/gcw0-toolchain`, configures CMake for MIPS32r2/o32 hard float,
builds, audits the ELF and full shared-library dependency closure, and packages:

```text
dist/epubreader-m2-0.2.1.opk
dist/epubreader-m2-0.2.1.opk.sha256
```

The engine and hashing library are built statically. Rendering features are
explicit and identical on host/device; FreeType, zlib, PNG, and JPEG are resolved
from the target SDK only. `JOBS=4` is the default build concurrency and can be
overridden in the environment. After bootstrap, builds/package creation work
offline with the verified archives and checked-in assets/notices.
Only the two original prose specimens are bundled from `assets/books/`; other
EPUBs placed there for local testing are excluded by the packager.

Assets and license notices are checked in. The font download is checksum-verified
and ensures the four bundled files match upstream. The stereo test MP3 and icon
can be regenerated with `python3 scripts/generate-assets.py` (requires ffmpeg
with libmp3lame), but normal builds reuse the checked-in specimens. Regeneration
with a different encoder version may intentionally change the MP3 bytes.

SquashFS packaging uses gzip, 128 KiB blocks, root ownership, fixed modes/times,
no xattrs/exports, one compressor worker, and `SOURCE_DATE_EPOCH=1663804800`.
Package timestamps and compile-time content do not depend on the wall clock.

### Independent clean-build reproducibility check

```sh
export OD_SDK="$PWD/third_party/sdk/gcw0-toolchain"
export SOURCE_DATE_EPOCH=1663804800
cmake -S . -B build/repro-m2 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rg280v.cmake \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/repro-m2 --parallel 4
python3 scripts/package.py --build build/repro-m2 --output dist/repro-m2.opk
cmp dist/epubreader-m2-0.2.1.opk dist/repro-m2.opk
```

The pinned SDK and squashfs-tools version are part of reproducibility. ELF and
library inspection is performed with the SDK's `mipsel-linux-readelf`; the
packager refuses host binaries and unresolved target libraries. `$ORIGIN/lib`
and the launcher-local `LD_LIBRARY_PATH` locate the bundled libraries. Firmware
provides the uClibc loader/libc (see `third_party/README.md` for Adam ABI evidence).

## Host build, preview, and checks

```sh
python3 scripts/bootstrap.py --host
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure
build/host/epubreader --assets "$PWD/assets"
```

The preview is 640×480, representing the panel's 320×240 square-pixel geometry
at 2× desktop scale. The application always composes a native 320×480 canvas;
columns are doubled for the desktop preview. `--native-window` exposes the raw,
visually tall framebuffer instead. The reader composes CRengine's 640×478 page
into the first 478 native rows using horizontal 2:1 box filtering, then draws
the progress bar in native rows 478–479.

Desktop controls: arrows; A/B/X/Y keys; Q/W for L1/R1; E/R for L2/R2; Escape
for Select and Enter for Start. Actual device key equivalents also work.

Useful options:

```sh
build/host/epubreader --help
build/host/epubreader --assets "$PWD/assets" \
    --state-dir "$PWD/tmp/manual-reader-state" --book "/path/to/book.epub"
```

Without `--book`, the reader uses `$HOME/books/milestone2.epub` if present and
otherwise opens the bundled EPUB 2. Slice controls: left/right or Q/W turn pages;
E/R changes theme; X opens the popup; highlight Font size with Up/Down and
adjust it with Left/Right. B closes the popup, or saves/reopens when reading;
Up/Down outside the popup opens the bundled EPUB 2/3 specimens; Escape+Enter
saves/exits. Positions
and font/theme preferences are under `$HOME/.epubreader/slice/` by default.

CTest uses dummy SDL video/audio and isolated build-local state. The reader tests
check EPUB 2/3 metadata and spine order, every numbered text token across page
boundaries, relative PNG/JPEG rendering, font re-pagination without anchor drift,
theme redraw without pagination changes, boundary clamping, serialized position
restoration in a fresh process, renamed/changed files, invalid books/bookmarks,
document and neighbor-image cache hits, interrupted-cache rebuilding, framed IPC,
and bounded worker cancellation. Popup checks cover font-only adjustment,
clamped endpoints, busy-reader input arbitration, and held-key consumption across
overlay transitions. Reader smoke uses the production popup dispatch path and
produces 13 native BMPs under `build/host/reader-screenshots/` (three fonts, three
font-row selections, six themes, and the warm2 popup).

The original platform diagnostic is also built as `epubreader-diagnostic`. Its
tests check input timing/chords, pixel transforms, crash-interrupted state
replacement, and real MP3
decoding/callback consumption, pause/resume, invalid-track recovery, and stale-job
stop suppression. Smoke rendering produces 36 native BMP specimens under
`build/host/screenshots/`: each theme/screen plus each font path/size. Missing
host ALSA hardware does not block display/font/audio dummy-driver checks.

Optional host memory/undefined-behavior check:

```sh
sudo dnf install libasan libubsan
cmake -S . -B build/sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEPUBREADER_SANITIZE=ON
cmake --build build/sanitize
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitize --output-on-failure
```

Stock QEMU user-mode is not an acceptance substitute for this SDK: its runtime
uses Ingenic indexed-load instructions (`lxw`) that the installed QEMU 9.2.4
does not execute, even with the XBurstR1 CPU model. The target is verified by
cross-build/ELF/package audits here and actual RG280V testing by the user.

## Implementation boundaries

- `src/app/`: SDL lifetime, event loop, log/state checkpoints, shutdown.
- `src/ui/`: RGB888 composition, final native indicators, cached TrueType faces,
  original 5×14 native UI bitmap glyphs and six palettes.
- `src/platform/`: SDL semantic mapping/timing, passive evdev observation (no
  grab), ALSA inventory/observation and one startup-restore path.
- `src/audio/`: mpg123 worker, bounded PCM ring, precomputed chirp, SDL callback.
- `src/storage/`: versioned diagnostic state and fsync/rename replacement.
- `src/reader/`: CRengine adapter, content/layout identity, 64 MiB document cache,
  three-page native-image cache, authoritative XPointer anchors, and worker IPC.

The reader worker is an isolated child process, started before SDL/audio threads.
CRengine has no reliable load/layout cancellation API; the main thread pumps a
nonblocking socket and can terminate/reap the worker in a bounded interval.
Progress messages keep loading visible, and all engine/font/cache access stays
in the child. Cache prefetch happens after publishing the requested page. Font
changes retain the original text anchor until a deliberate page turn, preventing
drift across repeated small/medium/large cycles. Completed positions are saved
after a 250 ms debounce and explicitly before reopen/switch/exit.

`python3 scripts/generate-epub-fixtures.py` regenerates the checked-in original
EPUB specimens deterministically; see `tests/fixtures/README.md`. To package the
platform diagnostic from a current target build, use
`python3 scripts/package.py --diagnostic`. The accepted historical M1 artifacts
and test records retain their original versions.

See [DEVICE-TEST-M2-0.2.1.md](DEVICE-TEST-M2-0.2.1.md) for reader installation and controls,
and [VERIFICATION-M2.md](VERIFICATION-M2.md) for the verified artifact/checks.
[DEVICE-TEST.md](DEVICE-TEST.md) records the passed original on-device platform
acceptance; [DEVICE-TEST-0.1.1.md](DEVICE-TEST-0.1.1.md) covers its visual follow-up.
