# RG280V EPUB Reader — Implementation Plan

## 1. Goal and confirmed scope

Build a single OpenDingux `.opk` application for reading English-prose EPUBs and listening to MP3s on the Anbernic RG280V. The application is designed for Adam v2.1 and uses its 320×480 native display and physical controls.

| Area | Target |
|---|---|
| Device | RG280V, Ingenic JZ4770, 512 MB RAM |
| Firmware | Adam v2.1; reported kernel `5.15.0-rc6-opendingux` |
| Display | Native 320×480 framebuffer; aspect-correct logical geometry of 320×240 |
| Books | English-prose, reflowable EPUBs |
| Default content folders | `$HOME/books` and `$HOME/music`, configurable in settings |
| Themes | Dark, warm, amber, matrix, light, and warm2; dark initially |
| Reader font | TrueType, UTF-8; small, medium, and large; medium initially |
| Library text | Small, sharp 5×14-native-pixel bitmap text, selected on-device |
| Reading navigation | D-pad left/right and L1/R1 page turns |
| Position navigation | Select opens/closes a jump slider |
| Indicators | Reader progress bar is two native rows; list scrollbar is two native columns |
| Distribution | One OPK; GPL-compatible licensing is acceptable |

`README.md` is the feature baseline. The page-turn controls and jump slider from `BRAINSTORM.md` are included per user confirmation. The user will perform on-device verification once testable OPK artifacts are available.

## 2. Research findings and technical choices

### 2.1 Build and platform

The OpenDingux GCW0 Buildroot configuration uses little-endian MIPS32r2, uClibc, and hardware floating point. This is the starting ABI target for the application.

Use the checksum-pinned OpenDingux GCW0 `2022-09-22` SDK in `third_party/manifest.json`. Its diagnostic OPK passed launch and platform testing on Adam v2.1. The historical `2021-10-22` candidate was unavailable from its original hosting domains.

Build with:

- CMake, with host and RG280V cross-compilation configurations.
- C++17, confirmed with the pinned GCC 11.1.0 compiler and bundled C++ runtime.
- Project-local build and dependency directories.
- An explicit dependency manifest recording upstream revisions, options, and checksums.

### 2.2 Display and controls: SDL 1.2

Use the OpenDingux SDL 1.2 integration for display, input, and audio. The OpenDingux SDL fork contains a KMS/DRM backend with scaling support. Commander and the locally inspected PdfViewer source demonstrate practical SDL use on this device family.

The original Commander repository has moved to [od-contrib/commander](https://github.com/od-contrib/commander). Its font rendering code demonstrates independent horizontal and vertical DPI, useful for RG280V's non-square pixel geometry. The local PdfViewer source confirmed SDL 1.2, RGB565 drawing, buffered presentation, keyboard-style controls, and historical GCW0/uClibc build integration. Milestone 1 subsequently confirmed the new application's 320×480 native-resolution path on the RG280V.

### 2.3 EPUB engine: headless CRengine

Use the [KOReader CRengine fork](https://github.com/koreader/crengine), built as a headless rendering library with a small application-specific adapter.

The document-view API provides EPUB loading and metadata, page-mode layout and page counts, font size and style controls, rendering to application-owned buffers, page navigation, document progress, and reading-position bookmarks. Its DOM API can serialize an XPointer position and restore it after the document is reloaded.

Treat the serialized text location as the authoritative bookmark. Page numbers are derived data because a font-size change re-paginates a book.

Build only the engine and dependencies needed for EPUB prose. The initial dependency set should include FreeType, ZIP/compression support, PNG/JPEG support, text-breaking support as selected, and the engine's required hashing library. Pin feature options and dependency versions so host libraries cannot silently affect the device build.

CRengine is GPLv2-licensed. GPL-compatible licensing has been accepted for the application.

### 2.4 MP3 playback: mpg123 and SDL audio

Use mpg123 for MP3 decoding, following the model demonstrated by GMU: a worker decodes MP3 data into PCM, a bounded ring buffer holds decoded audio, and an SDL audio callback consumes the PCM. Mix the short UI chirp through the same audio subsystem.

### 2.5 Physical volume: ALSA observation

The RG280V device tree assigns keycodes to dedicated volume buttons. The OpenDingux power daemon handles these buttons and adjusts ALSA volume directly.

The application should restore its saved mixer level (default 50%), observe and persist subsequent ALSA mixer changes, and avoid independently adjusting volume on those same physical presses. This prevents a single press from causing two increments. Milestone 1 verified `PCM,0` on Adam v2.1: raw range 0–31, no playback switch; the physical volume buttons change this control once per press. Other observed controls (`HEADPHONES,0`, `LINE IN BYPASS,0`) are not used for the reader's persisted volume. Audio output was 44100 Hz, stereo, S16 with zero reported underruns.

### 2.6 Packaging

An OPK is a SquashFS image containing the executable, resources, and launcher metadata. Follow established OpenDingux packaging examples for the desktop entry, icon, and `mksquashfs` options.

Bundle fonts, themes, application-specific resources, required libraries as appropriate, and license notices. Resolve immutable assets relative to the application package and store writable state under `$HOME`.

## 3. Application architecture

Use a small state machine with explicit subsystem ownership:

```text
Application
├── Platform
│   ├── SDL display and presentation
│   ├── Physical input mapping
│   └── ALSA volume observation
├── Library
│   ├── EPUB discovery and metadata cache
│   └── Sorting and reading-status lookup
├── Reader engine
│   ├── CRengine adapter
│   ├── Pagination and page rendering
│   └── Bookmark serialization/restoration
├── Interface
│   ├── Books list
│   ├── Reader
│   ├── Header and popup menu
│   └── Jump slider
├── Audio
│   ├── MP3 decoder and PCM buffer
│   ├── Shuffle playlist
│   └── Navigation chirp
└── Persistence
    ├── Settings
    ├── Per-book state
    └── Startup-resume state
```

### 3.1 Execution ownership

- **Main thread:** input, interface state, display, and shutdown.
- **Reader worker:** EPUB loading, pagination, rendering, and all CRengine access.
  Milestone 2 uses an isolated child process, started before SDL/audio threads,
  because the engine has no reliable cancellation API. Nonblocking framed IPC
  carries progress and completed page images; termination/reaping is bounded.
- **Audio worker:** MP3 decoding and track transitions.
- **Audio callback:** bounded PCM consumption and mixing only.

The main thread submits reader jobs and receives completed page images with their bookmark, page number, page count, and progress. All engine access goes through the reader worker. The UI must remain responsive during indexing and pagination. Global exit must remain available during loading, with bounded cancellation and shutdown.

### 3.2 Proposed repository structure

```text
CMakeLists.txt
cmake/
    toolchains/
src/
    app/
    platform/
    library/
    reader/
    ui/
    audio/
    storage/
assets/
    fonts/
    themes/
    styles/
packaging/
scripts/
tests/
    fixtures/
docs/
third_party/
tmp/
```

Add build instructions, a dependency manifest, controls documentation, and an on-device acceptance checklist as implementation proceeds.

## 4. Display, typography, and caching

### 4.1 Native-resolution pipeline

Separate logical layout from native framebuffer coordinates:

- Design interface placement around 320×240 logical geometry.
- Present through the native 320×480 framebuffer.
- Draw 5×14 bitmap interface text directly to the native surface.
- Draw the reader progress bar after composition at the final two native rows (478–479).
- Draw the list scrollbar at exactly two native columns.

The device-selected production path renders at 640×480 square-pixel geometry, then applies horizontal 2:1 box filtering to 320×480 while preserving vertical samples. Scale fonts, margins, and CSS physical units consistently when integrating CRengine. Milestone 1's comparison with 320×240 vertical doubling established the user's preference for the box-filtered path; the diagnostic retains the comparison specimens.

The display prototype includes native bitmap text, TrueType reading samples, both rendering paths, circles, checkerboards, fine lines, and all six theme palettes. Device feedback selected 5×14 bitmap text and requested a two-row progress bar, warmer amber, and a `warm2` light-paper palette inspired by approximately 4000K reading light.

### 4.2 Reading typography

Initial font-size candidates are 12, 14, and 16 logical pixels; tune these on the device. Bundle a redistributable font family with regular, bold, italic, and bold-italic faces.

Preserve useful structure in English prose, including paragraphs, headings, emphasis, lists, blockquotes, explicit breaks, chapter boundaries, and inline illustrations. Apply a reader stylesheet that makes the chosen font size and theme authoritative while retaining meaningful book structure.

EPUB package processing must respect `META-INF/container.xml`, the package manifest, spine reading order, and relative resource paths. CRengine supplies this processing; verify representative EPUB 2 and EPUB 3 prose books. DRM-protected books are out of scope. EPUB font obfuscation and encrypted DRM are distinct; support font obfuscation if the selected engine supports it without disproportionate effort.

### 4.3 Caching

- Read metadata without paginating the entire library.
- Paginate a book when it is first opened.
- Use engine document caching where practical.
- Keep a bounded cache of the current and neighboring page images.
- Load each font preset lazily.
- A theme change invalidates rendered page images, not pagination.

Pagination cache keys must include book identity, engine version, font identity, font preset, viewport, DPI, margins, and layout stylesheet version.

## 5. Feature behavior and controls

### 5.1 Books list

Implement:

- Case-insensitive `.epub` discovery in the configured books folder.
- Title display with filename fallback.
- Alphabetical ordering within unfinished and finished groups.
- Progress percentage and bitmap finished checkmark.
- Current theme and read-only font preset display; font changes are made in the reader popup.
- Highlighted selection and two-native-column list scrollbar.
- Selection preservation when returning from a book or when list ordering changes.

| Control | Action |
|---|---|
| Up / Down | Previous / next book |
| Hold Up / Down | Move once, wait, then repeat |
| Left / Right | Move one visible list page |
| Left / Right on a single-page list | Select first / last item |
| A | Open selected book |
| L2 / R2 | Previous / next theme |
| Select + Start | Save and exit |

Start with approximately 350 ms before list key repeat and 80 ms between repetitions, then tune on-device. Play a short, low-volume warm chirp only when selection changes.

### 5.2 Reader

Normal reading shows only book content and a low-contrast progress bar that is exactly two native framebuffer rows high. Hyperlinks remain visually rendered but are not focusable or activatable.

| Control | Action |
|---|---|
| Left / L1 | Previous page |
| Right / R1 | Next page |
| L2 / R2 | Previous / next theme |
| X | Open or close header and popup menu |
| Select | Open or close jump slider |
| Brief A press | Start, pause, or resume music |
| Hold A | Skip one track |
| Select + Start | Save and exit |

Clamp page turns at the beginning and end of a book. Preserve the underlying reading location when opening overlays.

### 5.3 Popup menu

- Draw a translucent overlay with native-resolution bitmap text.
- Show title, author when available, filename, current page, and page count in a top header panel.
- Select the first action by default.
- Up/Down moves; A activates; B or X closes.
- Provide actions to return to the books list and exit the application.
- Include a **Font size** row between those actions. When highlighted, Left/Right
  decreases/increases small → medium → large, clamping at the ends. A does not
  alter this setting. Apply changes immediately while keeping the menu open and
  the row highlighted; retain the authoritative text anchor through pagination
  and save the confirmed preset/position. Other menu rows consume Left/Right
  without turning pages. Font size cannot be changed from the books list.
- Menu navigation, close, and exit remain responsive during pagination; ignore
  additional font requests until the current reader job completes.

### 5.4 Jump slider

Proposed interaction:

- Select opens the slider at the current reading position.
- Left/Right changes its target by 1%.
- Up/Down changes its target by 10%.
- Show target percentage and page number.
- A commits the jump.
- B or Select cancels.
- X cancels the slider and opens the popup menu.

Changing the target updates the slider display without repeatedly rendering full pages. Commit by navigating the engine to the target document position, then save the resulting bookmark.

### 5.5 Input arbitration

Map physical buttons to semantic actions and apply explicit priority:

1. Global quit chord.
2. Active overlay controls.
3. Current screen controls.
4. Repeat and hold behavior.

Short A is detected on release. Trigger long A once after an initial 600 ms threshold, then suppress the short action. Consume button presses across screen transitions, prevent the A press that opens a book from starting music, and reset held-state tracking after suspension or focus loss. Verify physical face-button labels on hardware before fixing mappings; historical reference programs differ in some button labels.

## 6. Persistence and library identity

Store versioned settings and per-book state under:

```text
$HOME/.epubreader/
    config.json
    session.json
    books/
    cache/
```

### 6.1 Settings

Persist the selected theme, font preset, mixer volume, and books/music paths.

### 6.2 Per-book state

Persist a stable book identity and known path, serialized text bookmark, last-known progress, finished status, cached metadata, and cache/layout version information.

Use filesystem metadata for fast change detection and a content fingerprint when a book is first indexed or opened after a change. Preserve reading state across renames when the content fingerprint matches.

### 6.3 Session behavior

- First launch opens the books list.
- Exiting while reading records a reader-resume session.
- On next launch, restore the same book at its text location; music remains stopped.
- Returning to the list records a list session while retaining the book bookmark.
- If the resume book is missing or unreadable, show an informative message and open the list.

Write state atomically using temporary-file replacement. Save settings on change and checkpoint reading position after page changes with a short debounce, plus explicit saves when leaving a book and on exit.

After changing font size, restore the page containing the stored text anchor. Finished status is independent of pagination. Proposed completion rule: reaching the final page marks the book finished; a one-page book becomes finished when the user attempts to advance beyond it.

## 7. Embedded music player

### 7.1 Discovery and playback states

Scan `.mp3` files once at application startup. Use three playback states: Stopped, Playing, and Paused. Opening any book creates a stopped music session. Returning to the books list stops playback and discards that session.

### 7.2 Shuffle behavior

- Generate a shuffled permutation of all discovered tracks when playback starts.
- Pause/resume retains the current track and playlist.
- At playlist completion, generate another permutation.
- With multiple tracks, avoid repeating the same track at a shuffle-cycle boundary.

Long A behavior:

- Playing: advance once.
- Paused: advance once and remain paused.
- Stopped: no action.

Handle empty folders, one-track playlists, removed files, and corrupt tracks. Bound retries through an invalid playlist so playback cannot spin rapidly.

### 7.3 Audio reliability

- Convert decoded PCM to the negotiated output format.
- Keep decoding, file access, and allocation out of the audio callback.
- Output silence on ring-buffer underrun and count underruns for diagnostics.
- Use generation identifiers so stale decoder completion events cannot restart playback after stop or skip.
- Mix the navigation chirp through the same audio subsystem.
- Music continues while reader overlays are open; A actions within overlays are consumed by the overlay.

## 8. Implementation milestones and OPK deliverables

Each milestone produces a testable artifact as early as possible. The first hardware-checkable deliverable is the platform diagnostic OPK. The user will perform the on-device tests after receiving the OPK and checklist; host-side results do not replace those checks.

### Milestone 1 — Platform diagnostic OPK

**Implementation status:** accepted on RG280V / Adam v2.1 with version `0.1.0`;
all device checklist items passed. The user's annotated results remain in
`docs/DEVICE-TEST.md`. Version `0.1.1` implements the visual feedback; its focused
follow-up is `docs/DEVICE-TEST-0.1.1.md`. Cross-build/packaging and host checks are
documented in `docs/BUILD.md`. The official 2022-09-22 GCW0 SDK is pinned in
`third_party/manifest.json`.

**Deliverables:**

- Reproducible cross-build and OPK packaging.
- Native-resolution display test patterns.
- Physical button/event diagnostic screen.
- Bitmap and TrueType font samples.
- Audio chirp and MP3 playback test.
- ALSA mixer observation and persistence diagnostic.
- Device test checklist and instructions for copying/running the OPK.

**Exit criteria:** OPK builds reproducibly on the host; its intended behavior is documented; user testing confirms launch on Adam, correct geometry, input mappings, and one volume adjustment per press. Any device-specific corrections feed into the next build.

### Milestone 2 — EPUB rendering vertical slice

**Implementation status:** accepted on RG280V / Adam v2.1 with version `0.2.0`.
All device checks performed by the user passed; malformed-book and prolonged
reading checks are intentionally deferred to normal-use usability feedback.
Version `0.2.1` warms amber/warm2 and implements the revised popup font-size
control, based on the confirmed anchor-preserving in-book re-pagination. The
annotated acceptance remains in `docs/DEVICE-TEST-M2.md`; focused follow-up is
`docs/DEVICE-TEST-M2-0.2.1.md`, with host/build evidence in
`docs/VERIFICATION-M2.md`. EPUB 2/3 specimen shortcuts remain slice test controls.

**Deliverables:**

- Headless CRengine integration.
- One EPUB loaded, paginated, rendered, and navigated.
- All three font presets.
- Serialized text-position save and restore.
- Initial document and page caching.
- A testable OPK containing the reader slice.

**Exit criteria:** representative EPUBs render correctly; reopening restores the same text location; font preset changes preserve the anchor. User checks the display and responsiveness on the device once this OPK is provided.

### Milestone 3 — Library and persistence

**Implementation status:** accepted on RG280V / Adam v2.1 with version `0.3.0`.
Host/build verification is complete; the user verified all core features and
confirmed the app is fully usable on the device. The production library,
JSON storage and slice migration, session resume, completion rules, metadata
indexing, folder settings, and bitmap list are included. See
[`docs/VERIFICATION-M3.md`](docs/VERIFICATION-M3.md) and the supplied
[`docs/DEVICE-TEST-M3.md`](docs/DEVICE-TEST-M3.md) checklist.

**Detailed implementation plan:** [Milestone 3 plan](docs/PLAN-M3.md), covering
production storage and slice migration, asynchronous metadata indexing, library
identity/selection, startup sessions, the usable books-list slice, and verification.

**Deliverables:**

- Folder scanning and metadata caching.
- Progress and finished-book tracking.
- Sorting and selection restoration.
- Settings, atomic state writes, and startup resume.
- A testable OPK and updated acceptance checklist.

**Exit criteria:** empty, large, changed, missing, and partially read libraries behave predictably in host tests; user can verify persistence and library behavior with the OPK.

### Milestone 4 — Complete interface

**Implementation status:** accepted on RG280V / Adam v2.1 with version `0.4.0`.
The user tested M4 and confirmed all features are operational and correct.
Host/build results are recorded in [M4 verification](docs/VERIFICATION-M4.md),
with device acceptance in [the M4 test record](docs/DEVICE-TEST-M4.md). The jump
slider uses layout-derived page previews and the existing atomic bookmark/session
path; all reader overlays share explicit input arbitration.

**Detailed implementation plan:** [Milestone 4 plan](docs/PLAN-M4.md), based on
the accepted M3 interface. It covers the jump slider, layout-derived target
previews, overlay/input arbitration, native-pixel verification, and M4 delivery.

**Deliverables:**

- Bitmap books list.
- Six themes, including warm2.
- Reader progress bar.
- Header and popup menu.
- Jump slider.
- Key repeat, long press, and quit-chord handling.
- A testable OPK and control reference.

**Exit criteria:** documented interactions pass host input/state tests; user can verify controls, theme quality, overlay placement, and exact native-pixel indicator dimensions on the device.

### Milestone 5 — Integrated music

**Implementation status:** implemented in `0.5.0` from accepted M4 `0.4.0`;
host/build verification is complete and the OPK is ready for device evaluation.
See [M5 verification](docs/VERIFICATION-M5.md)
and the [device audio checklist](docs/DEVICE-TEST-M5.md).

**Detailed implementation plan:** [Milestone 5 plan](docs/PLAN-M5.md), covering
startup-only discovery, per-book shuffle sessions, generation-safe audio events,
short/long A integration, volume persistence, navigation chirps, compact popup
track information, and host/device audio verification. Target artifact:
`dist/epubreader-m5-0.5.0.opk`.

**Deliverables:**

- Startup-only track scan.
- Shuffle sessions and automatic playlist continuation.
- Short/long A behavior.
- Physical-volume persistence.
- List-navigation chirp.
- Compact popup track count, playback state, and current track number.
- A testable OPK and audio test checklist.

**Exit criteria:** host playback tests pass; user verifies smooth playback during reading and transitions, with no accidental starts and no duplicate volume steps.

### Milestone 6 — Hardening and release

**Deliverables:**

- Regression checks and representative EPUB fixtures.
- Performance measurements and cache tuning.
- Final self-contained OPK artifact.
- Build, installation, controls, troubleshooting, and license documentation.
- Final device acceptance checklist.

**Exit criteria:** clean builds reproduce the package; host acceptance checks pass; user completes on-device acceptance with the final OPK and reports any device-only issues for correction.

## 9. Verification strategy

### 9.1 Host checks

Automate behavior that can be tested reliably off-device:

- Bookmark restoration after reload and re-pagination.
- Page-boundary continuity.
- Sorting, selection preservation, and finished grouping.
- Atomic-state recovery and schema-version handling.
- Deterministic repeat, hold, and chord timing.
- Shuffle permutations, pause/resume, end-of-file, and invalid-track exhaustion.
- Suppression of A actions across overlays and screen transitions.

Use EPUB fixtures with multiple chapters, nested resource paths, UTF-8 punctuation, emphasis, illustrations, and oversized paragraphs. Build both the host version and the target OPK for each relevant milestone.

### 9.2 User-performed on-device checks

The user can test the physical device once an OPK artifact is supplied. Include a concise checklist with each artifact, covering:

- Whether the OPK installs/launches on Adam v2.1.
- Display geometry, font appearance, aspect correction, themes, and native-pixel indicators.
- Physical button mappings, holds, repeats, and exit during loading.
- Resume after application restart and device reboot.
- Volume persistence and interaction with firmware volume handling.
- Music during sustained page turning and screen transitions.
- Suspend/resume behavior.
- A prolonged reading/music session.

Collect the firmware result, OPK version, steps taken, expected behavior, actual behavior, and any photos or logs useful for reproducing a problem. Fix device-only issues and provide a new OPK for retesting.

### 9.3 Proposed performance targets

Measure and tune from Milestones 1–2 rather than treating these initial values as device-verified facts:

- Cached page turns around 200 ms or better.
- Prompt input response and responsive menus during loading.
- Bounded memory and cache growth.
- No audio underruns during normal reading.

## 10. Main risks and mitigations

| Risk | Mitigation |
|---|---|
| SDK/compiler or ABI mismatch | Prove the target build and launch path with the diagnostic OPK before integrating large libraries; pin the SDK. |
| CRengine build size or performance | Build only required engine features, render one representative book early, measure on the RG280V, and bound page caches. |
| Non-square-pixel text quality | Compare native and logical rendering paths through user-tested OPKs and keep layout/render transforms explicit. |
| Font-size changes shift the page | Persist serialized text anchors and restore the containing page after re-pagination. |
| Button mappings differ from assumptions | Include a button diagnostic in the first OPK and finalize mapping from user device results. |
| Duplicate physical volume changes | Observe ALSA changes rather than handling the volume key independently; confirm the mixer control on the device. |
| Reading work blocks interface | Keep engine work on its worker and provide cancellation and bounded shutdown. |
| Music produces callback stalls or stale events | Use a bounded PCM ring, worker decoding, a minimal callback, and generation identifiers. |
| Interrupted state writes corrupt progress | Use versioned schemas and atomic replacement. |
| Fonts or linked dependencies have incompatible terms | Record licenses and notices in the dependency manifest and package before release. |

## 11. Recommended starting sequence

1. Establish the cross-build environment and dependency manifest.
2. Build and package the platform diagnostic OPK.
3. Provide the artifact and device checklist for user testing.
4. Resolve SDK, display, button, and mixer findings from that test.
5. Build the EPUB rendering vertical slice and test it on-device through a subsequent OPK.
6. Add library/persistence, interface, music, then release hardening in milestone order.

The diagnostic and EPUB vertical-slice OPKs resolve the highest-cost uncertainties—SDK compatibility, display quality, controls, rendering performance, bookmark persistence, and audio integration—before the rest of the application is built around them.

## 12. Research references

- [OpenDingux repositories](https://github.com/OpenDingux)
- [OpenDingux GCW0 Buildroot configuration](https://github.com/OpenDingux/buildroot/blob/opendingux/configs/od_gcw0_defconfig)
- [OpenDingux SDL](https://github.com/OpenDingux/SDL)
- [RG280V device tree](https://github.com/OpenDingux/linux/blob/jz-5.15/arch/mips/boot/dts/ingenic/rg280v.dts)
- [OpenDingux power daemon](https://github.com/pcercuei/pwswd)
- [Commander source](https://github.com/od-contrib/commander)
- [GMU music player](https://github.com/denis-n-kuznetsov/gmu)
- [KOReader CRengine](https://github.com/koreader/crengine)
- [EPUB 3.3 specification](https://www.w3.org/TR/epub-33/)
- The locally referenced PdfViewer source was inspected with permission for SDL, display, controls, and GCW0 build patterns.
