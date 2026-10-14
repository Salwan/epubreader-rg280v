# Building Milestone 1

## Host prerequisites (Fedora 42, x86_64 Linux)

```sh
sudo dnf install cmake ninja-build gcc-c++ python3 curl tar xz bzip2 file \
    SDL-devel SDL_ttf-devel mpg123-devel alsa-lib-devel squashfs-tools
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

The script verifies the SDK/font archive SHA256 values from
`third_party/manifest.json`, extracts and relocates the SDK under
`third_party/sdk/gcw0-toolchain`, configures CMake for MIPS32r2/o32 hard float,
builds, audits the ELF and full shared-library dependency closure, and packages:

```text
dist/epubreader-diagnostic-m1-0.1.1.opk
dist/epubreader-diagnostic-m1-0.1.1.opk.sha256
```

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
OD_SDK="$PWD/third_party/sdk/gcw0-toolchain" cmake -S . -B build/repro -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rg280v.cmake \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/repro
python3 scripts/package.py --build build/repro --output dist/repro.opk
cmp dist/epubreader-diagnostic-m1-0.1.1.opk dist/repro.opk
```

The pinned SDK and squashfs-tools version are part of reproducibility. ELF and
library inspection is performed with the SDK's `mipsel-linux-readelf`; the
packager refuses host binaries and unresolved target libraries. `$ORIGIN/lib`
and the launcher-local `LD_LIBRARY_PATH` locate the bundled libraries. Firmware
provides the uClibc loader/libc (see `third_party/README.md` for Adam ABI evidence).

## Host build, preview, and checks

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure
build/host/epubreader-diagnostic --assets "$PWD/assets"
```

The preview is 640×480, representing the panel's 320×240 square-pixel geometry
at 2× desktop scale. The application always composes a native 320×480 canvas;
columns are doubled for the desktop preview. `--native-window` exposes the raw,
visually tall framebuffer instead. Host mixer access is observation-only by
default; `--restore-mixer` explicitly enables the device startup-restore path.

Desktop controls: arrows; A/B/X/Y keys; Q/W for L1/R1; E/R for L2/R2; Escape
for Select and Enter for Start. Actual device key equivalents also work.

Useful options:

```sh
build/host/epubreader-diagnostic --help
build/host/epubreader-diagnostic --assets "$PWD/assets" \
    --state-dir "$PWD/tmp/manual-state" --music-dir "$PWD/tmp/music"
```

CTest uses dummy SDL video/audio and isolated build-local state. It checks input
timing/chords, pixel transforms, crash-interrupted state replacement, and real MP3
decoding/callback consumption, pause/resume, invalid-track recovery, and stale-job
stop suppression. Smoke rendering produces 36 native BMP specimens under
`build/host/screenshots/`: each theme/screen plus each font path/size. Missing
host ALSA hardware does not block display/font/audio dummy-driver checks.

Optional host memory/undefined-behavior check:

```sh
sudo dnf install libasan libubsan
cmake -S . -B build/sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEPUBREADER_SANITIZE=ON
cmake --build build/sanitize
ctest --test-dir build/sanitize --output-on-failure
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

This artifact implements only the platform milestone. EPUB parsing/rendering
and the reader's library/menu/music-shuffle behavior begin in later milestones.
See [DEVICE-TEST-0.1.1.md](DEVICE-TEST-0.1.1.md) for installation, controls, and
visual follow-up. [DEVICE-TEST.md](DEVICE-TEST.md) records the passed original
on-device acceptance and the user's feedback.
