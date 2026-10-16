# Milestone 5 verification — 2026-10-16

## Delivered artifact

```text
dist/epubreader-m5-0.5.0.opk
5,402,624 bytes (5.15 MiB including SquashFS padding)
SHA256: 05cb02ca9f669283aa24e2aa0cf19cee92184198f7dd7de0289a3266e268b522
```

**M5 implementation and host/build verification are complete; ready for device
evaluation.** Device acceptance is pending the user's
[RG280V audio checklist](DEVICE-TEST-M5.md). The full device/desktop reference is
[CONTROLS.md](CONTROLS.md). Existing M4/M3 production JSON schemas and M2 import
are retained; playback sessions, catalog, shuffle, and track numbers are transient.

## Build and delivery checks

| Check | Result |
|---|---|
| Host Debug CTest | **8/8 passed** |
| ASan/UBSan CTest (`UBSAN_OPTIONS=halt_on_error=1`) | **8/8 passed** |
| Final additional boundary coverage | Music/application checks passed on host and sanitizers after adding large/unreadable catalogs, live stop timing, music-folder restart notice, and playing/paused quit chords |
| Pinned MIPS cross-build | Passed; GCC 11.1.0, OpenDingux GCW0 2022-09-22 SDK |
| Independent fresh target tree | `build/repro-m5` passed; OPK byte-identical to the `build/rg280v` deliverable |
| Sources/dependencies | SDK/font/CRengine/xxHash/JSON hashes and extracted reader sources verified; no dependency additions |
| ELF/ABI/runtime closure | ELF32 little-endian MIPS32r2/o32, hard float; 14 bundled target libraries; firmware loader/libc only; no `libatomic` dependency |
| Actual audio linkage | Executable directly needs `libmpg123.so.0` and `libasound.so.2`; bundled closure and library hashes verified |
| SquashFS extraction | 54 files, 8 directories; extracted contents match staging |
| Manuals/launcher/assets | M5 checklist/controls match source, M5 launcher identity, shell syntax, original-book allowlist, no short music test fixtures packaged |
| Package integrity | `.sha256` verification and independent package `cmp` passed |
| Notices/documentation/whitespace | Dependency notice hashes, local documentation links, and `git diff --check` passed |

Commands follow [BUILD.md](BUILD.md):

```sh
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
cmake -S . -B build/sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEPUBREADER_SANITIZE=ON
cmake --build build/sanitize --parallel 4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitize --output-on-failure
bash scripts/build-opk.sh
# Independently configured with the pinned toolchain as in BUILD.md:
cmake --build build/repro-m5 --parallel 4
python3 scripts/package.py --build build/repro-m5 --output dist/repro-m5.opk
cmp dist/epubreader-m5-0.5.0.opk dist/repro-m5.opk
unsquashfs -d tmp/opk-audit-m5 dist/epubreader-m5-0.5.0.opk
diff -r build/rg280v/opk-stage tmp/opk-audit-m5
sh -n tmp/opk-audit-m5/launch.sh
bash -n scripts/build-opk.sh
python3 tmp/audit-m5.py
git diff --check
```

Checksum verification was run from `dist/` with
`sha256sum -c epubreader-m5-0.5.0.opk.sha256`. ELF/library inspection used the
pinned SDK's `mipsel-linux-readelf`. The local audit additionally validates
packaged manuals/controls, all runtime/notice hashes, assets, screenshot counts,
and documentation links. SDK and squashfs-tools 4.6.1 remain reproducibility inputs.

## Behavioral coverage

### Catalog, shuffle, and audio lifecycle

- Startup scanning is independent of CRengine and publishes once. Tests cover
  mixed-case names, spaces/UTF-8, regular symlinks, broken links, nested folders,
  FIFO exclusion, empty/missing/unreadable folders, cancellation, and 1,000 entries.
  Adding a file during a running session does not change its captured count.
- Fixed-seed tests exercise 100 seven-track permutations with cycle-boundary
  nonrepeat, zero/one-track catalogs, and failed-track exclusion across resets.
  Playback tests verify complete automatic cycles without another scan.
- Real original short MP3s cover 44100 Hz stereo, 22050 Hz mono, and 48000 Hz
  stereo conversion into negotiated 44100 Hz stereo S16. Every decoded frame
  reaches the callback before Finished; the original 12-second diagnostic drain
  check also passes.
- Explicit pause/resume preserves position; atomic start-paused and paused skip
  produce zero played music frames until resume. Chirp consumption cannot resume
  paused music. Stopped long A has no playback action.
- Typed terminal events are consumed once and tagged by generation. Repeated
  start/stop stress, stale completion, stop after EOF, and pause after already
  published EOF preserve the requested session intent. Old PCM/events cannot
  restart another book or the list.
- Corrupt, zero-frame, and removed tracks exhaust with a stable stopped/error
  snapshot. Failed candidates remain excluded until app restart; no background
  retry loop or all-invalid shuffle regeneration occurs.

### Production controls, persistence, and continued playback

- Input → Controller tests check short A on release, the 600 ms threshold, one skip
  per hold, suppressed release, A held during opening or overlay transitions,
  focus reset, popup/font-row consumption, and slider confirmation without music
  leakage. Existing repeat/tick-wrap and all 101 slider-map checks remain passing.
- Music controls/ticking remain available during reader work with a displayed
  page. Initial loading cannot start music. Font/page/jump results do not recreate
  the book's session; reopening and startup reader resume begin stopped.
- A real music facade/backend runs alongside the real CRengine child during page
  turns, font pagination, jump commit, and EOF continuation. Back clears PCM;
  a late terminal result cannot restart list playback. Both quit-chord orders
  stop playing/paused fake sessions across all nine existing screen/work contexts.
- Books List selection/page movement chirps once only when selection changes;
  clamped boundaries, indexing/reordering, and menu movement do not chirp.
- Music-folder commit persists its path and displays the restart notice without
  beginning another music session. The catalog remains startup-only.
- Shared mixer decision tests cover initial quantized percent, exact raw stereo
  restoration, changed-range fallback, unchanged/absent snapshots, 250 ms debounce,
  tick wrap, and preserving font/theme. Storage checks cover volume save failure,
  newer-snapshot retry, and raw-volume/folder/font/theme persistence after reopen.
  Firmware step/setter behavior remains a device check.

### Native rendering and visual review

- Controller checks emit **45 native BMPs** under
  `build/host/library-screenshots/`: the M4 specimens plus 13 M5 music popup views.
  They cover all six themes, large track counts, paused/stopped/scanning/empty /
  failed/unavailable states, and higher-priority save-error text.
- Reader smoke emits **44 native BMPs** under `build/host/reader-screenshots/`,
  including actual paused-skip state, fonts, six-theme menus, and slider targets.
  The existing diagnostic retains its 36 native specimens.
- Normal reading remains content plus the exact native-row 478–479 progress bar;
  music informational changes add no persistent reading overlay. Slider controls
  and native indicators retain their M4 pixel checks.
- Aspect-correct 640×480 music popup previews were inspected in all six themes,
  with failure/save-error status examples. The compact row fits inside the existing
  header; errors use the separate lower status area.

## Host observations

Recorded Debug observations (dummy SDL output; timings are not device claims):

| Observation | Result |
|---|---|
| Discovery of 1,000 local candidates | 4 ms |
| First PCM playback from a normal start | 21 ms |
| Explicit stop submission while playing | 4 µs; queued music cleared |
| 44100 Hz stereo short fixture | 14,976 decoded = 14,976 played frames |
| 22050 Hz mono short fixture | 16,128 decoded = 16,128 played output frames |
| 48000 Hz stereo short fixture | 14,817 decoded = 14,817 played output frames |
| Real reader/music integration | 16th reader request, automatic music continuation, 44100 Hz / 2 channels, zero underruns |

PCM remains bounded to 32768 interleaved S16 samples (64 KiB). Scanner cancellation
has a host assertion below 500 ms for its test directory. The device checklist
asks for audible continuity, actual physical-volume behavior, suspend recovery,
and a sustained 15–30 minute reading/music session.

## Device acceptance

Pending the user's RG280V / Adam v2.1 results. M1 established the audio/mixer
platform behavior, and M4 remains the accepted interface baseline; this M5 build
requires the supplied production-audio acceptance checks before marking M5 accepted.
