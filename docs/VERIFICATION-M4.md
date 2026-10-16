# Milestone 4 verification — 2026-10-16

## Delivered artifact

```text
dist/epubreader-m4-0.4.0.opk
5,369,856 bytes (5.12 MiB including SquashFS padding)
SHA256: 5b170390925f95319c7704c2c0f389d3a4d4aa1c5eb14ab46df7310359e1a802
```

**Milestone 4 is accepted on RG280V / Adam v2.1 with version `0.4.0`.** The user
tested M4 and confirmed that all features are operational and correct; see the
[device acceptance record](DEVICE-TEST-M4.md). The full control reference is
[CONTROLS.md](CONTROLS.md). The delivered OPK contains the original device
checklist and control reference; this subsequent acceptance is recorded in the
repository documentation.

## Build and package checks

| Check | Result |
|---|---|
| Host Debug CTest | **7/7 passed** |
| ASan/UBSan CTest (`UBSAN_OPTIONS=halt_on_error=1`) | **7/7 passed** |
| Pinned MIPS cross-build | Passed with GCC 11.1.0 / OpenDingux GCW0 2022-09-22 SDK |
| Fresh target tree `build/repro-m4` | Passed; OPK is byte-identical to the `build/rg280v` deliverable |
| Source/dependency checks | SDK/font/CRengine/xxHash/JSON archive hashes and extracted sources verified |
| ELF / ABI / runtime closure | ELF32 little-endian MIPS32r2/o32, hard float; 14 bundled target libraries, firmware loader/libc only |
| SquashFS extraction | 54 files, 8 directories; extracted files match staging |
| Manual / controls / launcher | M4 checklist and complete controls match source; M4 desktop entry and shell syntax verified |
| Dependency notices / assets | Notice hashes and target library hashes verified; only original EPUB 2/3 specimens bundled |
| Reproduction / checksum | Independent package `cmp` and `.sha256` verification passed |
| Whitespace / documentation | `git diff --check` and local documentation-link checks passed |

The dependency set and production JSON schemas are unchanged. Navigation maps
are bounded, transient layout metadata, rebuilt after open/font pagination and
sent through the same executable's worker protocol. M3 settings/bookmarks and
the idempotent M2 migration path are retained.

Commands follow [BUILD.md](BUILD.md):

```sh
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
cmake -S . -B build/sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEPUBREADER_SANITIZE=ON
cmake --build build/sanitize --parallel 4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitize --output-on-failure
bash scripts/build-opk.sh
# Fresh build/repro-m4 configured with the pinned toolchain as in BUILD.md:
cmake --build build/repro-m4 --parallel 4
python3 scripts/package.py --build build/repro-m4 --output dist/repro-m4.opk
cmp dist/epubreader-m4-0.4.0.opk dist/repro-m4.opk
unsquashfs -d tmp/opk-audit-m4 dist/epubreader-m4-0.4.0.opk
sh -n tmp/opk-audit-m4/launch.sh
bash -n scripts/build-opk.sh
git diff --check
```

Checksum verification was run from `dist/` with
`sha256sum -c epubreader-m4-0.4.0.opk.sha256`. Package metadata and full library
closure were inspected with the pinned SDK's `mipsel-linux-readelf`.

## Behavioral coverage

### Navigation metadata and overlays

- Every successful page includes exactly 101 monotonic, bounded targets derived
  from rendered document height. Single-visible-page engine numbering matches
  the page commands; endpoints are first/last and all one-page targets are zero.
- Real-worker tests commit **all 101 targets** for each EPUB 2/3 specimen and the
  one-page book. Preview/commit page numbers match. The long specimens' chapter
  breaks/images/uneven page heights distinguish this mapping from a page-count
  approximation. Maps survive theme redraw and return to their original values
  after font cycling; authoritative anchors remain on their displayed pages.
- Result-codec roundtrips and malformed map lengths/indices are checked, including
  receiver rejection. Existing 2 MiB framing and request/generation validation
  remain active.
- None / Menu / Jump is explicit, mutually exclusive overlay state. Controls
  cover ±1/±10 steps, 0/100 clamps, captured-origin no-ops, same-page no-ops,
  B/Select cancel, X → Back-selected popup, busy-open suppression, unrelated keys,
  long-A consumption, and stale-layout rejection.

### Production input, persistence, and recovery

- `Input` → `Controller` uses the same transition-consumption method as the SDL
  event loop. Tests cover held A across an opening overlay, duplicate keydowns,
  350 ms repeat, held direction across Jump → Menu → Reader, single long A plus
  suppressed release, focus/suspend held-state reset, and tick wraparound.
- Both quit-chord press orders are checked in **nine contexts**: library, library
  menu, folder chooser, opening, reader, popup, slider, font work, and jump work.
- One thousand local preview adjustments produce **no reader request** or anchor
  change. Confirmation increments the worker request sequence exactly once;
  repeated A and Select while busy do not queue another operation.
- Successful jumps survive immediate shutdown/fresh-process resume. Font cycles
  preserve the new anchor. Cancel, quit with an uncommitted target, and Back
  during an accepted but unfinished jump retain the last completed position.
- Previewing 100% does not finish a book; a successful final-page jump does.
  Completion stays sticky after moving backward. One-page slider confirmation
  remains a no-op and still requires a forward turn for completion.
- Worker-unavailable submission retains the displayed anchor, shows recovery
  information, and permits Back to list. Existing independent-book, renamed /
  missing/replaced target, indexing, storage-retry, and atomic-write checks pass.

### Native rendering and visual review

- Reader smoke emits **43 native BMPs** in `build/host/reader-screenshots/`: font
  presets/font rows, all six themes' reading and popup surfaces, four slider
  targets per theme, and the warm2 font-row popup.
- Controller checks emit **32 native BMPs** in
  `build/host/library-screenshots/`: prior library/error/selection specimens,
  all six themes' list/library-menu/chooser, slider origin/0/50/100, long metadata,
  and a notice above the slider.
- Production pixel checks assert exact reading-progress rows **478–479**, bounded
  fill, untouched row 477, no overlay during normal reading, two-column list
  scrollbar, and no metadata escaping the reader's panel edges.
- Single-line bitmap presentation folds embedded metadata/status newlines to
  spaces and preserves UTF-8-aware clipping. Multiline diagnostic text retains
  its existing behavior.
- Aspect-correct 640×480 previews were inspected for all slider themes and a
  list/menu/chooser matrix, plus dark long-metadata/notice and warm2 popup views.
  Notices occupy a separate region above the slider; its target does not alter
  the bottom reading-progress indicator.

## Host observations

The final recorded Debug run, concurrent with other builds, reported:

| Observation | Result |
|---|---|
| 1,000 local target adjustments, dispatch/model only | 1,911 µs total; no IPC/render/save requests |
| 50% jump in the medium EPUB 3 specimen | Page 42/82, returned progress 5037, engine job 42 ms |
| Final-page jump in the same specimen | Page 82/82, progress 10000, engine job 34 ms |
| Worker terminate/reap | 5 ms; existing automated bound is below 500 ms |

These timings exclude display presentation where stated and are host observations,
not device performance measurements.

## Device acceptance

The user tested Milestone 4 on RG280V / Adam v2.1 and confirmed that all features
are operational and correct, explicitly marking **Milestone 4 verified**. The
[device test record](DEVICE-TEST-M4.md) preserves that confirmation alongside the
detailed scenarios. Implementation, host/build verification, and on-device
acceptance are complete for version `0.4.0`.
