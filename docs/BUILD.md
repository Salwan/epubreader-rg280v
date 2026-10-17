# Building the Milestone 5.5 reader

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

The script verifies the SDK/font/CRengine/xxHash/JSON archive SHA256 values from
`third_party/manifest.json`, verifies extracted reader sources against their
archives, extracts and relocates the SDK under
`third_party/sdk/gcw0-toolchain`, configures CMake for MIPS32r2/o32 hard float,
builds, audits the ELF and full shared-library dependency closure, and packages:

```text
dist/epubreader-m5.5-0.5.7.opk
dist/epubreader-m5.5-0.5.7.opk.sha256
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
cmake -S . -B build/repro-m5.5-0.5.7 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rg280v.cmake \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/repro-m5.5-0.5.7 --parallel 4
python3 scripts/package.py --build build/repro-m5.5-0.5.7 --output dist/repro-m5.5-0.5.7.opk
cmp dist/epubreader-m5.5-0.5.7.opk dist/repro-m5.5-0.5.7.opk
```

The pinned SDK and squashfs-tools version are part of reproducibility. ELF and
library inspection is performed with the SDK's `mipsel-linux-readelf`; the
packager refuses host binaries and unresolved target libraries. `$ORIGIN/lib`
and the launcher-local `LD_LIBRARY_PATH` locate the bundled libraries. Firmware
provides the uClibc loader/libc (see `third_party/README.md` for Adam ABI evidence).

### Confirmed production shutdown

Version 0.5.7 enables the device-confirmed normal firmware backend for critical
low-battery shutdown. It requires a native device/MIPS build, the observed
`jz-battery/usb-charger` profile, and fresh confirmed on-battery percentage ≤2%.
Host builds and any `--smoke-test` / `--power-fixture` run cannot enable real
firmware commands. The observer snapshot is checked again before both `/bin/sync`
and `/sbin/poweroff`. See [the accepted shutdown record](VERIFICATION-SHUTDOWN-M5.5.md).
The temporary shortcut, build option, test launcher and flavor manifest are retired.

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

Without `--book`, the app opens the Books List or restores the saved reader
session. Device defaults are `/media/sdcard/books` and `/media/sdcard/music`;
host defaults are `$HOME/books` and `$HOME/music`. **X → Books folder / Music
folder** configures them. Saved folder choices take precedence over defaults.
`--books-dir DIR` and `--music-dir DIR` also override
and save these settings. All paths can contain spaces; quote shell arguments.

MP3 discovery runs once at startup on a cancellable scanner thread, using the
configured/overridden music path. Music-folder changes in the app apply on the
next launch. Brief reader A starts/pauses/resumes; a 600 ms hold skips once, stays
paused when paused, and does nothing while stopped. Each book starts stopped;
Back stops and discards its playlist. Music continues under overlays and reader
work; overlay A is consumed by its own action. The popup shows a stable sorted
catalog track number/count and playback state. Failed tracks are excluded until
restart; an exhausted catalog stops cleanly. Books List selection changes chirp.

On device, `PCM,0` is preferred and restored once, then observed/persisted through
the production config. Physical volume keys remain firmware-owned. Host preview
defaults to observation-only and retains saved device mixer preferences. Options
`--mixer-card CARD`, `--restore-mixer`, and `--observe-only` support explicit mixer
testing. Smoke mode never opens/restores a hardware mixer; dummy SDL audio is used
by automated audio checks. Mixer errors do not disable an otherwise working reader.

M5.5 persists **Music Repeat** (ALL / ONE / STOP) without restarting playback;
natural EOF follows that mode, while manual skip always advances the shuffle.
Books use current-position Reading → Unread → Read grouping, independently of
sticky completion. The popup includes battery percentage/source and cable/status;
the reader has a static warning frame below 15%. Two-sample ≤5% / ≤2% policy runs
before screen input/work. Critical book → list → config saves have explicit async
acknowledgement, one retry, and a five-second shutdown preparation budget.
The production 2% backend uses the normal shutdown sequence confirmed by the user
from the OPK on Adam v2.1. Spawn/exit/timeout errors retain the stopped-list fallback.
`--power-fixture DIR` injects a sysfs-shaped tree for manual testing; it cannot
enable shutdown. Host preview otherwise has unavailable power data.

List controls: Up/Down selects (with repeat), Left/Right pages, brief A opens,
E/R changes theme, and X opens settings/rescan/exit. Reader controls: Left/Right
or Q/W turns pages; E/R changes theme; X opens the popup. The first row returns
to the list. Highlight Font size or Music Repeat with Up/Down and adjust with Left/Right; B/X
closes the popup. Escape+Enter saves/exits everywhere. Folder selection uses
arrows/A with Use this folder and parent rows; B cancels.

Escape (Select) in an idle reader opens the jump slider. Left/Right adjusts
−/+1%, Up/Down +/−10%, brief A confirms, B/Escape cancels, and X switches to the
popup. Movement previews the exact target page without engine work. Confirming
sends one page request; cancel/no-op confirmation retains the exact origin.
See [CONTROLS.md](CONTROLS.md) for all contexts and physical/desktop mappings.

Device production state defaults to `/media/sdcard/epubreader/`: `config.json`,
`session.json`, `books/*.json`, rebuildable `cache/`, and logs. Host preview uses
`$HOME/.epubreader/`. The 0.5.6 SD-card integration belongs to M5.5. Existing
installations can copy `$HOME/.epubreader` to the new root before first launch;
see [device setup/upgrade](DEVICE-TEST-M5.5.md#better-file-system-integration--setup-and-upgrade).
Saved config retains its previous content folders; choose new SD folders in-app.
M2 `slice/state.ini` and
`slice/books/*.position` are imported idempotently. With `--state-dir DIR`, only
`DIR/slice/` is considered for migration. For an isolated list preview use:

```sh
build/host/epubreader --assets "$PWD/assets" \
    --state-dir "$PWD/tmp/manual-library-state" --books-dir "$PWD/assets/books"
```

CTest has **nine checks**, retaining the eight M5 checks and adding `power-core`,
using isolated build-local state and dummy SDL
video/audio where needed. Production storage checks exercise migration, schemas,
atomic replacement, failed saves/retry, and book-before-session ordering. Storage
checks also cover device/host defaults, SD-style checkpoints/cache and
persisted custom folders after changing the firmware HOME. Library
checks cover discovery/signatures/cache, duplicates, deterministic 1,000-entry
sorting/selection, rename/removal, and navigation boundaries. Application checks
exercise the production controller with real child workers: list/reader resume,
independent books, replaced/missing/renamed targets, one-page/sticky completion,
folder settings, font controls, cancellation, and native scrollbar/text rendering.
M4 additionally exercises production Input → Controller dispatch, slider origin/
endpoint previews, short/long A, repeat/focus/tick-wrap behavior, both quit-chord
orders in nine screen/overlay/work contexts, single-request commits, immediate
restart, canceled work, sticky completion, and worker-failure recovery. Those M4
scenarios emit 32 native BMPs under `build/host/library-screenshots/`, including all six themes'
list/menu/chooser and slider/long-metadata/notice specimens.

M5 adds `music-core`: fixed-seed shuffle/permutation and failure-exhaustion tests,
startup discovery/symlinks/cancellation, real mono/stereo/22050/44100/48000 Hz
conversion and tail drain, silent paused skips, stale-event suppression, pause /
stop races with EOF, and volume restoration/debounce decisions. Production
controller checks use a counting fake for input arbitration and real music with
the child reader for continued EOF transitions during page/font/jump work. They
add 13 music popup BMPs, including all six themes and error/unavailable states.
Production reader smoke plays actual PCM and exercises paused skip, adding
`music-paused.bmp`. Storage checks cover failed volume saves and preserving the
latest mixer/theme/font/folder values through retry/restart.

M5.5 adds fixture-sysfs parsing, units/bounds, median smoothing, freshness/suspend,
threshold/episode boundaries, and fake-process fixed-argv shutdown outcomes in
`power-core`. Real short MP3 checks extend to all repeat EOF outcomes and EOF/mode/
manual-skip races. Critical storage checks distinguish drained failures from
successful tickets, preserve book/session/config order, and verify priority/retry.
Controller checks cover low-power transitions across all nine screen/work contexts,
5%→2% promotion, blocking/failing IO, the five-second deadline, charger/unknown/
stale/recovered cancellation, explicit retry/reopen, and held-A consumption.
They add 24 six-theme popup/slider/frame/modal BMPs for **69** library screenshots;
reader smoke retains **44** and the diagnostic retains **36**. Frame checks verify
the explicit row-477 warning exception and unchanged two-row progress bar.

The 0.5.7 production checks add the confirmed Adam final-command guard (including
charger insertion during sync), preserve a pending Back bookmark before critical
shutdown, and ensure the retired Y shortcut is inert. `reader-smoke` uses two
isolated synthetic-power launches to verify hardware commands stay disabled and
ordinary logs identify only the current launch. No automated check can invoke
real system shutdown.

The reader tests
check EPUB 2/3 metadata and spine order, every numbered text token across page
boundaries, relative PNG/JPEG rendering, font re-pagination without anchor drift,
theme redraw without pagination changes, boundary clamping, serialized position
restoration in a fresh process, renamed/changed files, invalid books/bookmarks,
document and neighbor-image cache hits, interrupted-cache rebuilding, framed IPC,
and bounded worker cancellation. Popup checks cover font-only adjustment,
clamped endpoints, busy-reader input arbitration, and held-key consumption across
overlay transitions. Reader smoke uses the production popup dispatch path and
produces 44 native BMPs under `build/host/reader-screenshots/` (three fonts, three
font-row selections, each theme's reading/menu plus four slider targets, the
warm2 information popup, and the M5 paused-music popup).
Reader checks exercise all 101 layout-derived targets for each EPUB 2/3 specimen
and the one-page book, codec roundtrips/malformed navigation data, and map
stability through font/theme work. Preview maps use rendered document height,
not a page-count approximation; only text anchors are persisted.

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
- `src/audio/`: mpg123 worker, bounded PCM ring, typed generation-tagged terminal
  events, precomputed chirp/SDL callback, once-per-run scanner, shuffle model, and
  main-thread per-book music facade.
- `src/storage/`: versioned diagnostic state, production JSON config/session/book
  records, slice migration, and a serialized atomic-write/retry queue.
- `src/library/`: shared content fingerprints, batched nonrecursive discovery,
  metadata cache reconciliation, ordering, progress, and selection/viewport model.
- `src/app/controller.*`: screen/session transitions, job scheduling, generation
  filtering, completion rules, debounce, and folder settings.
- `src/reader/`: CRengine adapter, content/layout identity, 64 MiB document cache,
  three-page native-image cache, authoritative XPointer anchors, and worker IPC.

The reader worker is an isolated child process started through `posix_spawn` and
an internal executable mode, so cancellation/replacement is safe after SDL and
storage threads exist. CRengine has no reliable load/layout cancellation API;
the main thread pumps a nonblocking socket and can terminate/reap the worker in
a bounded interval. Metadata-only import initializes a minimal engine font
manager but no document layout, face registration, or pagination cache.
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

See [DEVICE-TEST-M5.5.md](DEVICE-TEST-M5.5.md) for installation and current evaluation,
[CONTROLS.md](CONTROLS.md) for controls, and
[VERIFICATION-M5.5.md](VERIFICATION-M5.5.md) for M5.5 artifact/checks.
[POWER-PROFILE-M5.5.md](POWER-PROFILE-M5.5.md) records the observed supply profile,
confirmed normal shutdown and remaining percentage-calibration observations. The
OPK includes its hardware-read-only `probe-power.sh` report tool;
at 2% the guarded device backend requests normal firmware shutdown after save
preparation. The packaged manual records the confirmed device result.
[DEVICE-TEST-M5.md](DEVICE-TEST-M5.md) preserves the accepted audio milestone.
[DEVICE-TEST-M4.md](DEVICE-TEST-M4.md) preserves the accepted interface milestone.
[DEVICE-TEST-M3.md](DEVICE-TEST-M3.md) preserves the accepted library milestone.
[DEVICE-TEST.md](DEVICE-TEST.md) records the passed original on-device platform
acceptance; [DEVICE-TEST-0.1.1.md](DEVICE-TEST-0.1.1.md) covers its visual follow-up.
