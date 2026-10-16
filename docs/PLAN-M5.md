# Milestone 5 — Integrated music implementation plan

**Status:** accepted on RG280V / Adam v2.1 with version `0.5.0`, following the
user's confirmation that M5 was tested and verified on-device. Implementation
and host/build verification are complete.
Built from device-accepted M4 `0.4.0`. See [verification](VERIFICATION-M5.md) and
the [device checklist](DEVICE-TEST-M5.md). The design below records the
implementation baseline; the controller's music port supports a counting fake,
and volume restore/checkpoint/debounce decisions are shared with host tests.

**Target deliverable:** `dist/epubreader-m5-0.5.0.opk`, its SHA256 file, updated
controls/build documentation, host verification, and an RG280V audio checklist.

## 1. Outcome and project baseline

Integrate background MP3 playback into the production reader: a startup-only
music catalog, per-book stopped/playing/paused sessions, shuffle continuation,
brief/held A controls, persisted physical volume, and Books List navigation
chirps. Add the requested compact track information to the reader popup.

Requirements come from [README.md](../README.md), sections 5.5–7 and the M5
deliverables of [PLAN.md](../PLAN.md). [BRAINSTORM.md](../BRAINSTORM.md) requests
track count/current track number; [the M4 plan](PLAN-M4.md#9-new-feature-requests-to-schedule-explicitly)
assigns that field to M5. The accepted starting point is recorded in
[M4 verification](VERIFICATION-M4.md), [M4 device acceptance](DEVICE-TEST-M4.md),
and [the control reference](CONTROLS.md).

The application is C++17/SDL 1.2. Its main thread owns input, screen models, and
320×480 native composition; a replaceable `posix_spawn` child owns CRengine;
a serialized storage thread writes versioned JSON atomically. mpg123, SDL audio,
and ALSA already build for the pinned MIPS32r2/uClibc SDK.

### M5 requirements versus the current implementation

| Deliverable | Existing implementation | M5 work |
|---|---|---|
| Startup-only scan | `discoverMp3()` does sorted, nonrecursive, case-insensitive discovery; the diagnostic calls it once. | Run one responsive production scan of the configured folder and report scan outcomes. |
| Playback and shuffle | `Audio` has a decoder thread, bounded PCM ring, callback consumption, pause/resume, EOF drain, and generation invalidation. The diagnostic selects tracks manually. | Add a testable shuffle/session model and typed terminal events for automatic continuation/recovery. |
| Brief/held A | `Input` produces short A on release and one long A at 600 ms. Production reader A is currently inert. | Route these actions only from unobstructed reading and preserve overlay/transition consumption. |
| Volume persistence | `Mixer` restores once and subsequently observes ALSA; production JSON already stores volume/raw mixer fields. | Instantiate it in production, checkpoint observations into config, and verify firmware-only key handling. |
| Navigation chirp | `Audio::chirp()` mixes a precomputed 45 ms warm chirp through the same callback. | Trigger it when user navigation changes the selected book. |
| Track information | The reader popup has book/page information and a job/cache row. | Show playback state, discovered count, and stable current track number. |
| Packaging/tests | M4 has seven CTest checks and reproducible OPK packaging; audio tests exercise the diagnostic backend. | Add production music coverage, version `0.5.0`, M5 package/manual selection, and device audio acceptance. |

M1 verified real MP3 playback and `PCM,0` on Adam v2.1: raw range 0–31, no
playback switch, 44100 Hz stereo S16 output, and no duplicate physical-volume
steps. Those results establish the platform baseline; M5 must verify audio
alongside the production reader and its worker/storage load.

## 2. Behavioral contract

### 2.1 Startup catalog and folder settings

- Capture `Store::config.music` **after** loading/migrating preferences and
  applying `--music-dir`. Default remains `$HOME/music`.
- Enumerate that directory once per application run, nonrecursively. Match `.mp3`
  case-insensitively; include regular files and symlinks resolving to regular
  files; skip subdirectories, broken links, and other file types. Support spaces
  and UTF-8 paths. Do not decode every file during discovery.
- Publish an immutable, deterministically path-sorted catalog. Its one-based
  indices are the displayed track numbers; shuffle order is separate. The count
  means discovered candidates, not a promise that every file is decodable.
- Perform traversal on a startup-only scanner thread, with cancellation checked
  between entries and before publishing. Keep it independent of the CRengine
  child so library indexing/cancellation cannot lose the music catalog. Publish
  completion/errors to the main thread without blocking input.
- Distinguish Scanning, Ready, Empty, and Failed scan outcomes. Discard incomplete
  results after an enumeration failure rather than presenting them as complete.
  A missing/unreadable folder leaves reading usable and gives a concise popup/log
  explanation. A during scanning does not queue playback to start later.
- Changing **Music folder** saves the new path and shows **Restart to apply music
  folder**. The current run retains its captured catalog. Books-folder rescans,
  reader/list transitions, and playback starts never rescan music. Added tracks
  appear next launch; removed tracks are handled when playback attempts them.
- Production playback uses only the configured catalog. The diagnostic's bundled
  test tone remains a diagnostic/test asset rather than an implicit user track.

### 2.2 Reader music sessions and controls

Create a fresh **Stopped** session when an initial book open succeeds, including
startup resume and `--book`. Reader page results, theme redraws, font pagination,
and jumps must not recreate that session. Returning to the list, abandoning an
open, changing books, and app exit stop playback, clear buffered music, discard
the playlist, and invalidate outstanding decoder results.

| Context / action | Behavior |
|---|---|
| Unobstructed reader, brief A, Stopped | If scan/audio are ready and tracks are available, generate a shuffle and start its first eligible track. |
| Unobstructed reader, brief A, Playing | Pause without losing track position, queued PCM, or shuffle order. |
| Unobstructed reader, brief A, Paused | Resume the same track and playlist. |
| Unobstructed reader, hold A, Playing | Advance once to the next eligible shuffled track and keep playing. |
| Unobstructed reader, hold A, Paused | Advance once, selecting the next track **already paused** at its beginning. |
| Unobstructed reader, hold A, Stopped | Consume without starting or creating a playlist. |
| Popup or slider, any A | Existing overlay behavior only; no music toggle or skip. |
| Library, menus, chooser, or initial book opening | Existing selection behavior only; no music start/skip. |
| Select+Start, either order | Stop/invalidate music and save/exit with global priority. |

Music continues while the popup/slider is open and through page turns, theme
changes, font work, and jumps. A **fresh** unobstructed reader press can control
music during an in-flight reader job if a valid page is already displayed;
reader work must not block audio controls. Initial opening has no such page.

Retain 600 ms long-A timing and release suppression. Held A/directional state
is consumed through `Controller::consumeTransition()` on screen and overlay
transitions. Reset held input on focus loss/suspend as M4 does. Preserve the
Playing/Paused intent through suspend; resuming a stopped session must never
start music. Check actual device audio recovery during acceptance.

### 2.3 Shuffle, EOF, and bad tracks

- Generate a permutation of eligible catalog indices with `std::shuffle` and a
  PRNG seeded once per app launch. Inject a fixed seed in model tests.
- Visit every eligible track once per cycle. Pause/resume retains the permutation;
  manual skip advances within it. At natural cycle completion, generate another
  permutation automatically. With more than one eligible track, ensure its first
  track differs from the previous cycle's last track; a one-track catalog repeats.
- Natural EOF advances only after the decoder has delivered its final PCM and
  the callback has drained it. Manual skip discards old buffered PCM immediately.
- Keep the session's requested Playing/Paused intent separate from the backend's
  transient terminal state. If pause races with EOF, select the next track paused;
  if stop races with EOF, do not continue. Test both event orders.
- Treat open/decode failure and an EOF with zero decoded frames as failed
  candidates. Exclude them for the rest of the app run, log the reason, and
  advance while retaining the session's Playing/Paused intent. Restart retries
  the startup catalog. Keep displayed indices/count stable after failures.
- Schedule at most one new decoder attempt per main-loop update. Bound failure
  recovery to the remaining eligible candidates; never regenerate an all-invalid
  playlist indefinitely. When none remain, settle at Stopped with an explanatory
  snapshot. Another A reports that outcome without a retry loop.

## 3. Audio backend changes and execution ownership

Keep one `Audio` instance and one SDL output device for both music and chirps.
Build on its existing 32768-interleaved-sample ring rather than adding another
decoder or audio callback.

### Typed, generation-safe lifecycle

`AudioStatus` currently exposes human-readable messages; EOF and errors both
eventually become Stopped. Add a bounded, pollable terminal result carrying the
track generation and a typed **Finished** or **Failed** reason. Consume each
terminal result once. The music service must not infer EOF from message strings
or from `Playback::Stopped`, which can also mean an explicit stop.

- A start/skip/stop changes generation and clears obsolete terminal state as well
  as obsolete PCM. Publish PCM, status, and terminal outcomes only for the active
  generation. The session also becomes inactive before leaving the book, so an
  already-published old completion cannot continue playback on the list or in
  another book.
- Add explicit pause/resume and an initial-playback-mode parameter for start.
  Paused skip must use one atomic **start paused** command; the diagnostic's
  current `start()` then `toggle()` sequence can briefly expose Playing to the
  callback. Keep diagnostic controls compatible with the refined API.
- Obtain consistent status snapshots under a documented lock order. Do not pair
  an old track path with a new generation/playback state. Keep 64-bit frame
  counters protected by the SDL audio lock; the target uses native-width atomics
  for generations, not new 64-bit atomics requiring `libatomic`.
- Retain mpg123 conversion to the negotiated S16 mono/stereo output. The callback
  performs bounded consumption, saturating chirp mixing, and silence on underrun;
  it never scans files, decodes, allocates, writes logs, or advances playlists.
- Preserve startup priming, final-tail drain, bounded PCM, and underrun counts
  that exclude startup/normal EOF drain. Playlist decisions stay on the main
  thread; file access and decode stay on the audio worker.

### Proposed module boundaries

| Files | Responsibility |
|---|---|
| `src/audio/audio.*` | Existing SDL/mpg123 backend; generation-tagged terminal outcomes, coherent snapshots, explicit pause/resume, and atomic start-paused. |
| `src/audio/catalog.*` | Structured discovery result and cancellable once-per-run scanner; share discovery rules with the diagnostic. |
| `src/audio/playlist.*` | Small SDL-independent shuffle/cursor/failure model with injectable seed. |
| `src/audio/music.*` | Main-thread session facade over `Audio`, catalog, and playlist; lifecycle, toggle/skip, terminal consumption, chirp, and UI snapshot. |
| `src/app/controller.*` | Inject a small music interface; book lifecycle hooks, action effects, unconditional service ticking, selection chirps, and UI change tracking. |
| `src/ui/reader_controls.*` | Add music-toggle/skip effects after overlay arbitration and before the reader-busy guard, gated on an existing displayed page. |
| `src/app/reader_main.cpp` | Own mixer/audio/music lifetime, capture startup settings, poll/checkpoint volume, and coordinate shutdown. |
| `src/ui/{books_list,reader_slice}.*` | Pass a read-only music snapshot into production reader composition and render compact popup information. |

The injected music interface permits an inert/counting fake in headless
controller tests; real SDL/backend tests exercise the same production facade.
Keep these modules small and avoid changing the reader IPC protocol.

## 4. Controller, chirp, and native interface integration

- Hook `Controller::open()`, successful Opening → Reader completion, `scan()` /
  `failOpen()`, Back, Quit, and `shutdown()` into the session lifecycle. Make
  stop/end-session idempotent and initialize a book session exactly once.
- Tick the music facade and collect snapshot changes **before**
  `Controller::tick()` can return for a busy reader worker. EOF/failure recovery
  and popup status must continue during pagination. Mark the UI changed when
  visible music state changes, without redrawing for every PCM/frame counter.
- In the unobstructed Books List action branch, compare selected entry paths
  before/after Up/Down or Left/Right. Emit one chirp only when navigation changes
  the selected book, including key repeats/page jumps. Empty lists, clamped
  boundaries, async sorting/indexing, selection restoration, and overlay/menu
  actions do not chirp. Reuse the existing precomputed sound without queuing
  a backlog of chirps.
- Replace the reader popup's job/cache row with one compact music row, keeping
  job/cache details in logs. Examples: `MUSIC: PLAYING 7/24`, `MUSIC: PAUSED 7/24`,
  `MUSIC: STOPPED / 24 TRACKS`, `MUSIC: SCANNING`, and `MUSIC: NO MP3 FILES`.
  Here `7` means the stable sorted-catalog index, not position in the shuffle.
- Put concise scan/playback failure detail in the popup's existing status area
  and logs, respecting higher-priority reader/save errors. Music informational
  updates must not create a permanent status overlay during normal reading or
  overwrite the slider's controls.
- Use existing palette colors and 5×14-native text/UTF-8-safe clipping. The header
  fits inside its current panel; retain normal reading as content plus progress
  in native rows 478–479. Review the popup in all six themes and include large
  track counts, stopped/scanning/empty/unavailable/error states.

## 5. Physical volume and persistence

Reuse `platform/mixer.*` and `Store::config.preferences`; JSON already includes
volume, mixer ID, raw stereo levels/range, and optional switches. Playback state,
shuffle order, current track, and scan catalog are transient, so no schema
migration is expected.

1. In production device startup, restore the selected mixer once before any
   audible chirp/playback. Prefer **PCM,0** when no control was saved, honor a
   valid saved control, and log any fallback. Default is 50% of the raw range;
   reuse exact L/R levels when the saved control/range matches. For the verified
   0–31 range, checkpoint the actual quantized result immediately after restoration
   rather than assuming an exact representable 50% level or waiting for a change.
2. Poll ALSA approximately every 100 ms on the main thread. Physical key events
   never call volume setters; firmware remains the owner of those adjustments.
   Record control/range, restore outcome, observed changes, and startup-only
   setter count for device verification.
3. On selected-control changes, checkpoint into the live config preferences and
   debounce `Store::saveConfig()` by 250 ms. Compare the selected snapshot so a
   change to an unrelated control does not trigger unnecessary writes. Use the
   existing async atomic queue/retry/error path, preserving current theme/font
   and folder values.
4. On exit, stop/invalidate music, poll/checkpoint the final mixer snapshot, then
   run controller checkpoint/config flush. Cancel/join the scanner and join/close
   audio before `SDL_Quit()`, including exception unwinding. Do not wait for a
   whole catalog traversal or an MP3's natural end to exit.
5. Host preview defaults to observation-only and does not replace saved device
   mixer preferences with a host-card snapshot. Carry over diagnostic options
   `--mixer-card`, `--restore-mixer`, and `--observe-only` for explicit testing;
   dummy smoke/tests disable hardware restoration. Report audio and mixer failures
   independently: an unavailable mixer need not disable working music output,
   and an unavailable audio device leaves reading usable.

## 6. Ordered implementation work

Each step includes focused verification before proceeding.

| Step | Work | Completion evidence |
|---|---|---|
| 1. Backend lifecycle | Refine `audio.*` terminal reporting, snapshot locking, explicit pause/resume, and start-paused; update diagnostic callers. | Real MP3 tail drain, pause, silent paused skip, stop/restart races, and typed outcomes pass under dummy SDL audio. |
| 2. Catalog and shuffle | Add `catalog.*`, `playlist.*`, and the music facade; capture startup folder, scan off-thread once, and implement eligibility/failure bounds. | Deterministic discovery/permutation/cycle tests; no repeated scans; empty/all-invalid termination and stale-result rejection. |
| 3. Production integration | Inject music into controller; wire lifecycle/effects, tick before busy return, and chirp on real selection changes. | Production Input → Controller tests prove no accidental starts/skips, correct busy-reader behavior, continued overlay playback, and immediate stop on list/exit. |
| 4. Volume and popup | Add main-loop mixer restoration/checkpointing, shutdown ordering, and the compact popup track row/restart notice. | Config roundtrips/failure recovery preserve other settings; mocked volume observations and six-theme popup evidence pass. |
| 5. First M5 device build | Set `0.5.0`, update M5 launcher/CLI/window/log/package/manual identity, controls, and audio checklist. | Pinned cross-build/package audits pass; OPK is available for focused real-reader/audio feedback. |
| 6. Delivery verification | Complete host/sanitizer checks, independent OPK reproduction, measurements, and verification documentation. | Reproducible final OPK/checksum, actual host results, and device checklist are supplied. |

## 7. Verification plan

### Focused host coverage

Add `tests/music_tests.cpp` as a dedicated `music-core` CTest check and extend
existing controller, diagnostic, storage, and reader-smoke coverage. Retain the
existing seven checks. Use a fixed-seed model, a counting fake at the controller
boundary, and real mpg123/SDL dummy-audio playback where backend behavior matters.

- **Discovery:** mixed-case extensions, spaces/UTF-8, ignored nested folders,
  regular symlinks/broken links, missing/empty/unreadable directories, enumeration
  failure, and cancellation. Count scan invocations across book switches,
  library rescans, starts, folder changes, and restart. Verify delayed completion
  never starts playback and a changed folder applies only next launch.
- **Playlist/session:** zero/one/many tracks, complete permutations, shuffle-cycle
  boundary nonrepeat, pause/resume without reshuffle, playing/paused skip, stopped
  hold no-op, fresh stopped sessions for repeated opens and startup resume.
  Verify exclusion/exhaustion for removed, corrupt, and zero-frame tracks and
  one-attempt-per-update recovery. Ignore duplicate/obsolete terminal results.
- **Backend:** actual PCM consumption, negotiated sample-rate/channel conversion,
  bounded ring, retained paused position, start-paused with zero played frames,
  chirp mixing, EOF tail preservation/automatic advance, and failures without a
  spin. Stress stop/skip/end-session around decode completion so stale PCM/events
  cannot resurrect playback. Preserve the diagnostic's 12-second drain check.
- **Production dispatch:** short A on release, a single 600 ms long A, suppressed
  release afterward, duplicate keydowns, tick wraparound, and A held across list →
  opening → reader, popup/slider open/close, and jump confirmation. Overlay A must
  retain its existing action with zero music commands. Cover fresh A during
  page/font/jump work, initial-open suppression, focus/suspend reset, and both
  quit-chord orders while stopped/playing/paused and during scan/reader work.
- **Lifecycle/chirp:** music continues under both overlays and reader work, stops
  before list/reopen/quit, and stays stopped after a failed/missing resume target.
  Assert exactly one chirp per actual user selection change and none at empty or
  clamped boundaries, during menu navigation, or from asynchronous reordering.
- **Volume/storage:** defaults, matching raw restore versus range fallback,
  selected snapshot updates, 100 ms polling/250 ms debounce, final exit capture,
  save failure/retry, and coexistence with theme/font/folder saves. Use a small
  fake mixer boundary or extracted decision helpers; real firmware setter/step
  behavior belongs in device acceptance.
- **Rendering:** stopped/playing/paused/scanning/empty/failed/unavailable snapshots,
  large counts, six themes, and music error alongside reader/save notices. Assert
  normal reading and slider/progress pixels remain unchanged by music updates.

Keep fixtures and generated state under the build tree or project `tmp/`.
Reuse the original tone; add short original mono/stereo/different-rate MP3 test
fixtures with provenance so continuation tests are fast and deterministic. Keep
their checked-in bytes usable without ffmpeg at test/build time, following
`scripts/generate-assets.py`, and document regeneration in `tests/fixtures/README.md`.
Configure dummy audio for production smoke and any real-audio integration check;
never use the host's actual music directory.

### Build, package, and measurement evidence

Use [BUILD.md](BUILD.md), updating milestone paths after implementation:

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
cmake -S . -B build/sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEPUBREADER_SANITIZE=ON
cmake --build build/sanitize --parallel 4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitize --output-on-failure
bash scripts/build-opk.sh
export OD_SDK="$PWD/third_party/sdk/gcw0-toolchain"
export SOURCE_DATE_EPOCH=1663804800
cmake -S . -B build/repro-m5 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rg280v.cmake \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/repro-m5 --parallel 4
python3 scripts/package.py --build build/repro-m5 --output dist/repro-m5.opk
cmp dist/epubreader-m5-0.5.0.opk dist/repro-m5.opk
git diff --check
```

Audit target ELF/ABI and complete library closure, including actual mpg123/ALSA
linkage; retain the pinned SDK, dependency notices, SquashFS 4.6.1 reproducibility,
and original-book allowlist. Keep new audio fixtures under `tests/fixtures/` so
the packager's broad assets copy does not bundle test playlists. Update
`scripts/package.py` and `packaging/epubreader.gcw0.desktop` to M5 and package the
M5 checklist as `DEVICE-TEST.txt`, plus updated `CONTROLS.txt`.

Record scan size/time, first-play and EOF-transition latency, skip/stop response,
ring occupancy/underruns, and exit time during scan/decode. Exercise sustained
page turns, jumps, font pagination, and saves while playing; compare reader
responsiveness with the M4 baseline. Keep PCM bounded and catalog/playlist memory
proportional to track count. Host timings do not establish RG280V performance;
record device underruns and audible transition behavior separately.

## 8. Delivery and RG280V acceptance

Update `README.md`, `docs/BUILD.md`, and `docs/CONTROLS.md` with actual M5 status,
A controls, folder restart semantics, and track-number meaning. Add
`docs/VERIFICATION-M5.md` containing measured results and artifact checksum/size,
and `docs/DEVICE-TEST-M5.md` containing install/state compatibility and these
initially unchecked scenarios:

1. M5 launches on Adam v2.1 with existing M4 settings, bookmarks, completion, and
   list/reader resume intact. Every resumed/new book begins with music stopped.
2. The configured startup catalog count is correct. Test an empty/missing folder,
   mixed-case MP3 names, a music-folder change, and added files: only restarting
   updates the catalog. No test tone appears as an extra production track.
3. Brief A starts/pauses/resumes. Holding A skips exactly once without toggling on
   release; paused skip stays silent and resumes the new track at its beginning.
   Holding A while stopped does nothing.
4. Several tracks play in shuffle cycles with automatic continuation and no
   immediate boundary repeat when alternatives exist. A single track repeats;
   bad/removed tracks are skipped, and an all-invalid catalog settles cleanly.
5. Popup/slider A performs only its interface action. Entering/leaving overlays,
   held inputs during book opening, and jump confirmation never start music.
   Popup state/count/current track remain readable in all six themes.
6. Playback remains smooth through normal page turning, theme/font changes,
   jumps, and state saves. Back to list stops immediately; reopening requires
   an explicit start. List selection chirps are brief and absent at boundaries.
7. Physical Volume+/− changes `PCM,0` once per press with no increase in app
   setter calls after startup. Saved raw volume returns after relaunch/reboot;
   exercise speaker and headphone output. Music remains stopped after restart.
8. Select+Start in both orders exits promptly during playback, scanning, and
   reader work; saved reading position and final volume survive. Suspend/resume
   preserves intent without stuck input or accidental starts/skips.
9. Complete a 15–30 minute reading/music session and report audible dropouts,
   underruns, latency, CPU/memory observations where available, and logs. Compare
   with the accepted M4 reading experience.

M5 is ready for evaluation when its host/build checks pass and the OPK, checksum,
controls, and audio checklist are available. Mark device acceptance only after
the user's RG280V results. Send the project-required Gotify notification when
that build is ready, including project name, milestone/version, and agent name.

The battery observation/low-power actions and revised library grouping requests
in [BRAINSTORM.md](../BRAINSTORM.md), together with the new Music Repeat row, are
now assigned to [Milestone 5.5](PLAN-M5.5.md) before M6 hardening.
