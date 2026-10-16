# Milestone 3 verification — 2026-10-16

## Delivered artifact

```text
dist/epubreader-m3-0.3.0.opk
5,365,760 bytes (5.12 MiB including SquashFS padding)
SHA256: 18b6d7b02365e7c6b26788a6ec5025cc5372016ed98ec4ee132d010b3011fb78
```

**Implementation and host/build verification are complete. Device acceptance is
pending.** Install and test using [DEVICE-TEST-M3.md](DEVICE-TEST-M3.md).

## Build and package checks

| Check | Result |
|---|---|
| Host Debug build and CTest | 7/7 passed |
| ASan/UBSan build and CTest (`UBSAN_OPTIONS=halt_on_error=1`) | 7/7 passed |
| Final storage/concurrency and close-on-exec fixes | Complete Debug and sanitizer suites rerun: 7/7 each |
| Pinned MIPS cross-build | Passed with GCC 11.1.0 / OpenDingux GCW0 2022-09-22 SDK |
| Independent target tree `build/repro-m3-final` | Passed; its package is byte-identical to `build/rg280v`'s deliverable |
| Source/dependency verification | SDK, fonts, engine, xxHash, and JSON archive hashes and extracted sources verified |
| Target ELF and ABI | ELF32 little-endian MIPS32r2/o32; hard-float ABI confirmed with SDK readelf |
| Runtime library closure | 14 bundled target libraries; firmware supplies only loader/libc |
| SquashFS/manual/launcher | Full extraction: 53 files, 8 directories; gzip / 128 KiB blocks, fixed ownership/modes/timestamps, executable launcher passes `sh -n` |
| Package checksum and reproduction | `.sha256` verification and `cmp` passed |
| Shell syntax and whitespace | `bash -n scripts/build-opk.sh`, launcher syntax, and `git diff --check` passed |

The dependency addition is checksum-pinned nlohmann/json 3.12.0, header-only with
its MIT notice packaged. Target linking exposed unsupported 64-bit atomic
operations in the initial session counter; the production counter uses native
32-bit atomics and requires no additional libatomic runtime.

Commands used follow [BUILD.md](BUILD.md), including:

```sh
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
cmake --build build/sanitize --parallel 4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitize --output-on-failure
bash scripts/build-opk.sh
# Independently configured with the same pinned toolchain/options:
cmake --build build/repro-m3-final --parallel 4
python3 scripts/package.py --build build/repro-m3-final --output dist/repro-m3.opk
cmp dist/epubreader-m3-0.3.0.opk dist/repro-m3.opk
```

The initially combined sanitizer build/test command exceeded its outer shell
time limit during the diagnostic checks. The subsequent complete sanitizer run
finished successfully; final affected checks also completed successfully.

## Behavioral coverage

### Production storage

- Stable-name theme/font config, absolute content folders, reserved volume/mixer
  fields, list/reader sessions, and independent per-book JSON records.
- M2 preference/XPointer migration, unknown initial progress, repeated migration,
  preservation of newer production records, and invalid legacy version reporting.
- Atomic file replacement and ignored interrupted temporary files; malformed
  committed JSON is preserved and defaults recovered without losing book state.
- Unsupported JSON schemas are reported and protected from overwrite.
- Failed bookmark writes cannot advance a reader session. Failed jobs remain
  retryable; retrying an older checkpoint cannot replace a newer list session.
- A concurrent reader observes only complete old/new documents while 100
  alternating-size atomic replacements run. Reads open the file before sizing
  it, preventing mixed old-inode size/new-inode contents during rename.

### Discovery, indexing, and catalog

- Case-insensitive nonrecursive discovery, spaces/UTF-8 paths, regular-file
  symlinks, skipped directories/broken links, directory-only chooser discovery,
  and missing-folder errors.
- Shared XXH3-128 content identity and stat-signature metadata cache reuse;
  changed files invalidate cached identity/metadata. Full opens revalidate
  fingerprints independently of the path cache.
- Deterministic **1,000-entry model** ordering, finished grouping, selection and
  viewport restoration, rename/removal reconciliation, failed-scan retention,
  empty navigation, and one-page-list first/last behavior.
- A **27-entry real-file integration library** exercises cold and warm indexing,
  including malformed ZIP/invalid-spine candidates and identical copies.
  Unchanged returns perform zero metadata inspections and use 27 cache hits.
- Metadata-only engine import produces title/author without page images,
  pagination, or document-cache initialization. It requires a minimal font
  manager because CRengine's temporary DOM destructor accesses it; no font-face
  registration or reader layout is performed for this path.
- Metadata success for an invalid spine remains distinct from full-open success.
  Its failed full open recovers to the list and another book can be opened.

### Sessions, reader transitions, and completion

- Production controller with fresh stores and reader processes restores reader
  versus list sessions, settings, independent book anchors, and immediate reopen
  before the normal 250 ms debounce has elapsed.
- Renamed startup targets are found by identity; missing/replaced targets recover
  to the list without silently resuming different content. Old durable records
  survive library removal/replacement.
- Back to list during submitted font pagination cancels the old worker and
  preserves the last completed position. Request/generation-tagged IPC prevents
  old work from mutating a new screen/session.
- One-page books show 100% position but become finished only on a forward attempt.
  Multi-page books finish on deliberate final-page navigation; backward movement
  before debounce retains sticky completion. Font/menu cycles preserve anchors.
- Upstream `getPosEndPagePercent()` can report negative progress for one-page
  books and the last page's start for final pages. The adapter bounds progress
  and reports the displayed final-page end as 10000.
- Existing EPUB 2/3 continuity, images, boundary clamping, font/theme/cache,
  serialized restoration, worker cancellation, and diagnostic input/audio checks
  continue to pass.

## Native UI and host observations

- Production reader smoke emits **13 native BMPs** for font sizes/selected font
  rows, six themes, and popup composition in `build/host/reader-screenshots/`.
- Application integration emits **8 native BMPs** in
  `build/host/library-screenshots/`: dark/scrolled/finished/warm2 lists, reader
  popup, folder chooser, empty list, and unavailable folder.
- The list scrollbar is asserted to occupy exactly two native columns. UTF-8
  conversion/clipping checks cover accents, smart punctuation, unsupported
  characters, and storage truncation at code-point boundaries.
- Dark/warm2 list, folder chooser, and reader popup previews were inspected after
  horizontal doubling to the aspect-correct 640×480 host view. Long chooser
  paths retain their tail so the active directory remains identifiable.

Representative **host** observations during verification:

| Observation | Measured result |
|---|---|
| Cold 27-entry inspection | 174–193 ms in recorded runs |
| Unchanged 27-entry scans | Roughly 20–60 ms; zero inspections, 27 hits |
| First medium specimen open | 75 ms in the dedicated observation run |
| Book + session checkpoint/flush | 13 ms in the final Debug storage check |
| Active worker terminate/reap | 5 ms; automated bound is below 500 ms |
| Full controller scenario under `/usr/bin/time -v` | 3.55 s wall time; maximum reported process RSS 21,324 KiB |

Timings vary with simultaneous builds and filesystem load. RSS is the maximum
reported process figure, not a summed concurrent UI/worker memory measurement.
The 30-second index-job watchdog is an initial target setting; its device timing
and the actual library/reader feel remain part of on-device evaluation.

## Device acceptance

The supplied [M3 checklist](DEVICE-TEST-M3.md) covers installation, actual card
folders and libraries, migration, restart/reboot persistence, rename/replacement
recovery, completion, bitmap geometry, controls, cancellation, and suspend/resume.
These boxes remain unchecked until the user reports RG280V / Adam v2.1 results.

Milestone 2's accepted rendering checks and its explicitly deferred ordinary-use
follow-up remain recorded in [DEVICE-TEST-M2.md](DEVICE-TEST-M2.md).
