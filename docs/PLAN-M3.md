# Milestone 3 — Library and persistence implementation plan

**Status:** implemented in `0.3.0`; host/build verification is complete and device
acceptance is pending. See [verification](VERIFICATION-M3.md) and the
[device checklist](DEVICE-TEST-M3.md). The design and ordered work below record
the implementation baseline; folder controls/rendering are integrated into the
controller and books-list modules rather than separate chooser/control files.

**Target deliverable:** `dist/epubreader-m3-0.3.0.opk`, with host verification and
an RG280V / Adam v2.1 acceptance checklist.

## 1. Outcome and scope

Turn the accepted EPUB rendering slice into a library-driven reader. A fresh
installation opens the Books List, users can open any discovered EPUB, each book
retains its position and completion status, and subsequent launches restore the
last reader/list session.

The requirements come from `README.md`, sections 5–6 of `PLAN.md`, and the
Milestone 3 deliverables and exit criteria in section 8. Milestone 2's accepted
device behavior is recorded in `DEVICE-TEST-M2.md`; the 0.2.1 palettes and popup
font controls are the current implementation baseline.

M3 includes a functional bitmap Books List and the navigation needed to exercise
library/persistence on hardware. This brings forward the necessary part of M4's
books-list deliverable. M4 then completes the interface, including the jump
slider and broader interaction/visual acceptance. M5 consumes the music-folder
and volume settings established here when integrating playback and chirps.

## 2. Current implementation and integration points

| Existing code | M3 implication |
|---|---|
| `src/app/reader_main.cpp` directly opens `milestone2.epub` or a bundled specimen. | Introduce explicit list, opening, reading, and error/session transitions. |
| `src/storage/state.*` stores diagnostic-shaped settings in `state.ini`; the reader uses `$HOME/.epubreader/slice/`. | Add production config/session/book schemas and a retryable slice migration. |
| `src/storage/position.*` stores identity, path, and anchor in `<id>.position`. | Extend durable book state with progress, completion, and metadata/version information. |
| `src/reader/engine.cpp` computes a 32-character XXH3-128 fingerprint during every open. | Share the exact fingerprint algorithm with indexing so renames preserve existing state. |
| `ReaderResult` contains metadata, anchor, layout key, zero-based page/count, and progress in **0–10000** units. | Checkpoint successful results; page numbers remain derived from layout. |
| `ReaderWorker` accepts one job, uses bounded nonblocking IPC, and can terminate its child. It cannot restart it or inspect metadata separately. | Add typed indexing jobs, request generations, and safe worker replacement. |
| `ReaderControls` has Reopen / Font size / Exit and specimen shortcuts. | Connect the first menu row to the library and route list/reader controls by screen. |
| `Input` already implements 350/80 ms Up/Down repeat, short/long A, quit chords, and transition consumption. | Reuse it for the list and folder chooser. |
| `Canvas` has the selected 5×14-native bitmap font, six palettes, and pixel primitives. Its text function is ASCII-only. | Build the list with these primitives and define UTF-8-safe title presentation. |
| `CMakeLists.txt` defines four host checks; packaging hardcodes M2 names/manuals. | Add library/storage/session coverage and update release/package wiring. |

There is currently no `src/library/` subsystem or library/session model.

## 3. Behavioral decisions

### Discovery, identity, and ordering

- Scan the configured books directory **nonrecursively**, matching `.epub`
  case-insensitively. Include regular files and symlinks resolving to regular
  files; skip directories and broken links. Paths with spaces are ordinary paths.
- Enumerate at startup, when returning from a reader, after a books-folder change,
  and through an explicit **Rescan library** action.
- Display metadata title, falling back to the filename stem when blank or
  unavailable. Keep malformed/unreadable candidates visible with a concise error
  status so the user can identify and retry them.
- Sort unfinished before finished, then ASCII-case-insensitive display title,
  then filename and full path as deterministic tie-breakers. Preserve original
  UTF-8 strings in storage; the English-prose baseline uses this documented,
  locale-independent ordering.
- Use content fingerprints as book identities and paths as library-entry keys.
  Identical copies appear as separate paths sharing one reading/completion record.
  Replacing a file with different content creates a different identity.
- Cache a path's device/inode, size, and high-resolution modification/change times
  where supported. Reuse its fingerprint/metadata when that signature and the
  extractor version match; inspect new or changed files. Always revalidate the
  fingerprint on a full open before selecting a saved bookmark.
- Compare file metadata before/after inspection; discard/retry an observation if
  the file changed during the read. A failed or interrupted directory enumeration
  must not classify all unseen entries as deleted.
- A successful scan removes missing paths from the visible list, while durable
  per-book records remain available for a later reappearance or rename.

### Progress and completion

- The serialized text anchor is authoritative. Store the last-known 0–10000
  progress, page/count, font/layout key, and finished flag with it. Only update
  these from a successful result belonging to the current reader generation.
- A multi-page book becomes finished after deliberate user navigation reaches
  the final page, or a forward attempt while already there. A one-page book
  becomes finished on the first forward attempt. Loading, theme redraw, and
  font re-pagination alone do not establish completion.
- Completion is sticky and independent of pagination. Reading backward updates
  the bookmark/progress while retaining the finished checkmark and group.
- Capture the command associated with each result: a clamped final-page turn is
  significant even though its page number did not change. This also handles a
  resumed final page without deriving completion from a rounded percentage.

### Selection and sessions

- Preserve selection by path while metadata arrives or sorting changes; follow
  the fingerprint when a selected file is renamed. With several matching paths,
  use the previous path if present, otherwise the first deterministic match.
  If the entry disappeared, clamp its previous row index into the new list.
- Preserve the selected entry and top visible entry across reader/list
  transitions and restarts; adjust the viewport to keep selection visible.
- With no production session, start at the list. Exiting from the reader stores
  a reader session; returning to the list stores a list session immediately.
- Resume by verified identity: try the saved path, then matching indexed paths
  in the configured folder. A different book now occupying that path must never
  receive the old book's anchor or be silently resumed.
- A missing/unreadable resume book produces a message and the list. A failed
  open does not replace the last successful bookmark or become a resume loop.
- Retain `--book FILE` as an explicit startup override and smoke/manual-test
  entry point. It can open a file outside the library and uses the same per-book
  storage/session rules; returning to the list shows the configured folder.

## 4. Storage design and migration

Use the production root `$HOME/.epubreader/`; `--state-dir DIR` redirects the
entire production state root for tests and manual previews.

```text
config.json                 # schema version, theme, font, books/music folders, volume
session.json                # schema version, list/reader mode, identity/path, selection
books/<fingerprint>.json    # durable bookmark, progress, finished, metadata snapshot
cache/library.json          # rebuildable path signatures, identities, inspection status
cache/documents/            # existing bounded engine document/layout cache
reader.log
stderr.log
```

Implementation details:

1. Add typed `Config`, `Session`, and `BookState` models and a storage facade.
   Store stable theme/font names, absolute content paths, and a schema version
   in each file. Book records include the fingerprint algorithm/version,
   last-known path, anchor, metadata/extractor version, and derived layout data.
2. Add a checksum-pinned header-only `nlohmann/json` dependency through the
   existing manifest/bootstrap and license-notice mechanisms. Use the same
   dependency on host and target; prove the cross-build in the first step.
3. Apply defaults of dark, medium, `$HOME/books`, `$HOME/music`, and 50% volume.
   Preserve the existing raw mixer snapshot fields for M5's `Mixer` integration.
4. Validate types, enums, ranges, fingerprint format, path/string lengths, and
   the existing 16 KiB anchor bound. Handle files independently so a bad session
   does not discard bookmarks. Report unsupported schema versions and preserve
   their files; preserve malformed files before replacing them with defaults.
5. Reuse `atomicWrite`'s write → fsync → rename → directory-fsync sequence,
   extracting filesystem helpers from diagnostic settings as needed. Propagate
   directory open/fsync failures as well as write/rename failures. Ignore leftover
   temporary files during reads.
6. Serialize writes through one storage queue. Coalesce checkpoints per book,
   keep large catalog serialization/I/O off the input loop, and acknowledge
   completed writes to the controller. Explicit transitions/exit flush the last
   completed page; failed saves retain dirty state and produce visible/logged
   errors. Record a reader session only after its book checkpoint is committed.
   An immediate reopen waits for that book's pending checkpoint acknowledgement
   before the worker reads it, while the UI continues handling input. Each file
   is atomic; recovery must also tolerate interruption between files.
7. Preserve the 250 ms page-checkpoint debounce. Save theme changes and successful
   font changes promptly; failed font jobs must not persist an unconfirmed preset.

### Upgrade from the slice

- On production startup, import valid `slice/state.ini` preferences and
  `slice/books/*.position` records into the new schemas. For a redirected root,
  use only its own `slice/` child as the legacy source.
- Keep the existing XXH3-128 representation, so imported bookmarks resolve even
  when the book has moved. Imported records begin with unknown progress and
  unfinished status; refresh derived fields on the next successful open.
- Import only missing production records. Re-running after an interrupted
  migration must never overwrite newer production progress. Retain legacy files
  as migration sources, and make migration errors observable and retryable.
- M2 has no session file, so the first M3 launch opens the list after migration.
  Document the expected cold rebuild of derived caches under the production root.

## 5. Indexing and worker architecture

### Metadata without pagination

Extract fingerprinting into `src/library/identity.*` and add
`src/reader/metadata.*`. The pinned engine exposes
`ImportEpubDocument(..., metadataOnly=true)` in `epubfmt.h`. Read title/author
through a temporary document's properties, without `LVDocView::LoadDocument`,
`checkRender`, or page-image generation. All CRengine access stays in the child.

The implementation of metadata-only import explicitly permits success for an
EPUB with an invalid spine. Treat **metadata inspected** and **successfully
opened for reading** as distinct statuses. Full-open failures should update the
entry's error without discarding useful metadata or another book's state.

### Jobs, cancellation, and scheduling

- Extend framed IPC with explicit result kinds for directory batches, inspected
  entries, progress, pages, and errors. Carry request IDs and catalog/reader
  generations in every response, including progress and failures. Preserve the
  existing 2 MiB frame bound and validate bounded batches/strings on both ends.
- Use a resumable directory cursor with bounded batches, followed by one-file
  inspection jobs. The controller schedules foreground opens/turns ahead of
  indexing and pauses background indexing while reading. Render cached entries
  and filename fallbacks immediately as batches arrive.
- Refactor the child entry point into an internal executable mode and launch it
  with `posix_spawn` plus an explicitly inherited socket. This permits safe
  cancellation/replacement after SDL or storage threads exist. Tests receive the
  production executable path rather than trying to execute a test binary as a
  worker. Dispatch worker mode before UI/log initialization, retaining the
  existing parent-death cleanup guarantee.
- Returning to the list, changing folders, or opening a selected book can cancel
  obsolete indexing/loading work. Reuse bounded terminate/reap behavior and start
  a fresh child as needed; late responses cannot mutate the new screen/session.
- Give individual metadata jobs a watchdog and cache their failure against the
  observed file signature. Continue through a bad library without an automatic
  retry loop; explicit rescan/retry or a changed file permits another attempt.
  Tune the initial watchdog using target timings and record it in verification.
- Keep one active engine-owning child. Release its open document on leaving the
  reader. The main process owns models and durable writes; the child may read a
  book record after verifying its identity to choose the correct saved anchor.

## 6. Application and minimum device interface

Introduce a testable controller under `src/app/` and a library model under
`src/library/`; make `reader_main.cpp` responsible for platform lifetime and
driving their effects. Separate the pending open target from the last displayed
book so cancellation/errors never save one book's page under another path.

### Books List

- Draw native 5×14 bitmap title rows, selection, progress percentage, and a small
  finished checkmark. Show active theme and read-only font preset in the header,
  with the selected filename/status available for ambiguous or failed entries.
- Define rows-per-page once for rendering and navigation. Up/Down clamps and
  repeats through `Input`; Left/Right selects one visible page backward/forward.
  On a single-page list they select first/last respectively. A opens selection;
  L2/R2 changes and saves theme; Select+Start saves/exits everywhere.
- Draw the overflowing-list scrollbar in exactly two native columns, with a
  bounded thumb. Provide explicit scanning, empty, unavailable-folder, and
  partial-error states; disable open when no selectable entry exists.
- Decode UTF-8 before bitmap conversion/clipping. Map common Latin accents and
  smart punctuation to available glyphs, and show one fallback glyph per
  unsupported code point. Store and sort using the original title.

### Folder settings and reader transitions

- X on the list opens a small menu: **Rescan library**, **Books folder**,
  **Music folder**, and **Exit application**. A activates, B/X closes.
- Folder settings use a directory-only chooser with **Use this folder**, parent,
  and subdirectory rows. Reuse list movement/page controls, A to activate, B to
  cancel, and the global quit chord. Allow navigating to mounted-card folders.
  Save the selected absolute path; changing books folder starts a new scan.
- Change the reader popup's first row to **Back to book list**, followed by the
  existing Font size and Exit rows. Returning works during loading/pagination:
  save the last completed page, invalidate pending results, and show the list.
- Preserve the accepted font-row behavior: Left/Right only, clamped endpoints,
  same row selected, and original text anchor retained through repeated changes.
- Route production reading actions through the screen controller, replacing the
  slice's specimen/reopen shortcuts with list-driven navigation. Keep explicit
  error recovery in the UI and specimen access through normal test entry points.
- Consume A/repeat state at list, reader, popup, and chooser transitions. Retain
  focus/suspend resets and quit-chord priority over busy/overlay state.

## 7. Ordered implementation work

Each step includes its focused tests before moving to the next dependency.

| Step | Work and principal files | Completion evidence |
|---|---|---|
| 1. Production storage | Add `src/storage/{config,session,book,store}.*`, schema serialization, migration, and shared atomic helpers; update dependency bootstrap/notices and `CMakeLists.txt`. | JSON cross-build succeeds; schema, migration, interrupted-write, and save-order tests pass. |
| 2. Worker inspection/lifecycle | Add `src/library/identity.*`, `src/reader/metadata.*`; extend `reader/{types,worker,engine}.*` and internal worker startup. | Metadata jobs do not paginate; worker cancellation/replacement and existing reader rendering/anchor tests pass. |
| 3. Library model | Add `src/library/{scanner,catalog}.*`, cache reconciliation, ordering, duplicate/rename handling, and selection/viewport restoration. | Empty/large/changed/partial libraries pass deterministic headless tests; unchanged files hit metadata cache. |
| 4. Session/controller integration | Add `src/app/controller.*`; connect new book storage to opens/results, checkpoint debounce, completion commands, startup decisions, and stale-result filtering. | Fresh-process list/reader resume, failed opens, completion, and cross-book isolation pass. |
| 5. Usable device slice | Add `src/ui/{books_list,library_controls,folder_chooser}.*`; update reader popup/composition and `reader_main.cpp`; wire the M3 version/launcher/package and draft device manual. | Production-path smoke checks cover list → reader → list, folder changes, input consumption, and native screenshots; build the first M3 OPK. |
| 6. Delivery verification | Complete build instructions, README status, and M3 verification/device documents. | Final host checks, independent target packaging, package audit/checksum, and acceptance checklist are ready. |

Names above are proposed module boundaries; use the existing small C++17 style
and avoid tying headless library/storage/controller tests to SDL initialization.

## 8. Verification and acceptance

### Host coverage to add

Add dedicated library/storage/controller checks to CTest and extend the existing
reader integration/smoke checks where worker and persistence contracts change.
Generate fixtures under build-local directories using the original project
EPUBs and fixture generator. Cover:

- **Discovery:** absent/empty/unreadable directories, mixed-case extensions,
  spaces and UTF-8 paths, directories/symlinks, malformed books, file changes
  during inspection, and interrupted scans. Use at least 1,000 entries for
  ordering/navigation tests, with a smaller real-EPUB set for metadata I/O.
- **Cache and identity:** cold versus unchanged scans with inspection/hash
  counters, changed signatures, renamed/reappearing books, identical copies,
  different content replacing the same path, stale extractor versions, corrupt
  cache recovery, and full-open verification despite a cached identity.
- **Library state:** deterministic ordering/ties, sticky finished grouping,
  selection during incremental title changes, rename/removal/reorder, and
  viewport/page-navigation boundaries, including a one-page list.
- **Persistence:** defaults/roundtrips, unknown or malformed schemas, untouched
  committed data when temporary writes are interrupted, surfaced write/fsync/
  rename failures, interruption between book/session commits, and repeatable
  migration with a mixture of valid, invalid, and already-imported records.
- **Reader/session:** independent positions for several books; restart into list
  versus reader; renamed/missing/replaced/unreadable resume targets; failed open
  after a successful book; invalid anchors; exit/back during indexing/open/font
  work; and obsolete worker results after transitions or folder changes.
- **Completion:** a normal final-page arrival, clamped forward attempts,
  one-page completion, backward navigation after completion, font changes near
  the end, and restart persistence. Assert the 0–10000 progress conversion.
- **Input/display:** production list/reader/chooser transitions, held A/directions,
  existing repeat timings and quit chords, UTF-8-safe clipping, and a scrollbar
  exactly two native columns wide. Retain M2's font-anchor/cache/image checks.

Run the documented Debug CTest workflow and ASan/UBSan workflow, then the pinned
MIPS cross-build. Produce native screenshots for populated/empty/error lists,
finished grouping, scrolled selection, folder chooser, and reader popup in dark
and warm2; inspect them in aspect-correct host geometry. Record cold/warm scan,
first-page, save, cancellation, and memory measurements with entry counts.

### OPK and documentation

- Bump the application to `0.3.0`; update `scripts/package.py` from M2 artifact
  naming/checklist selection and update `packaging/epubreader.gcw0.desktop` to M3.
- Deliver `dist/epubreader-m3-0.3.0.opk` and its `.sha256`, including the M3 manual
  and dependency notices. Keep the explicit original-specimen packaging allowlist.
- Verify target ELF/library closure, SquashFS extraction, launcher syntax, and
  checksum; compare packages from two independently configured target trees.
- Add `docs/VERIFICATION-M3.md` with actual commands/results and
  `docs/DEVICE-TEST-M3.md` with installation, folder configuration, controls,
  migration notes, state paths, and a concise unchecked device checklist.

### User-performed RG280V acceptance

The checklist should ask the user to verify:

1. First launch opens the configured library; an empty/missing folder gives an
   actionable message, and folders can be changed to a mounted card.
2. Mixed/large libraries show readable titles, progress/checkmarks, correct
   ordering, page navigation/repeat, and selection restoration.
3. Several books retain independent positions; exiting/relaunching and rebooting
   restore the correct screen, book, text anchor, theme, and font preset.
4. Imported M2 positions/settings work, and a renamed book retains its progress;
   removed, replaced, and malformed books recover predictably to the list.
5. Completion survives reopening/font changes, and returning to the list moves
   finished entries while preserving selection.
6. Back to list and Select+Start remain responsive during indexing/loading/font
   changes; suspend/resume and held buttons cause no accidental transitions.

M3 is ready for delivery when its host/build checks pass and the OPK plus checklist
are supplied. Record device acceptance only after the user's results establish
the roadmap's library/persistence behavior on RG280V / Adam v2.1.
