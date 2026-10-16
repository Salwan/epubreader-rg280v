# Milestone 4 — Complete interface implementation plan

**Status:** accepted on RG280V / Adam v2.1 with version `0.4.0`, following the
user's confirmation that all M4 features are operational and correct. See
[verification](VERIFICATION-M4.md) and the [device acceptance record](DEVICE-TEST-M4.md).
The design below records the implementation baseline, built on accepted M3
`0.3.0`. The compact slider model remains in `ReaderControls`; production/test
dispatch shares `Controller::consumeTransition`.

**Target deliverable:** `dist/epubreader-m4-0.4.0.opk`, its SHA256 file, a complete
control reference, host verification, and an RG280V acceptance checklist.

## 1. Outcome and project baseline

Complete the reader interface around the accepted library and persistence
implementation. The principal new interaction is a position-jump slider with
cheap target previews, explicit confirmation/cancellation, and reliable input
arbitration. Finish M4 with coherent overlays, production-path interaction tests,
and native-pixel visual evidence across all six themes.

Requirements are defined in [README.md](../README.md), sections 4–5 and the M4
deliverables of [PLAN.md](../PLAN.md). The accepted behavior and verification
baseline are in [DEVICE-TEST-M3.md](DEVICE-TEST-M3.md),
[VERIFICATION-M3.md](VERIFICATION-M3.md), and [BUILD.md](BUILD.md).

The application is C++17 using SDL 1.2, headless CRengine, and versioned JSON
storage. The main process owns input, models, and native composition. One
`posix_spawn` child owns all CRengine work; framed, nonblocking IPC supports
bounded cancellation. A serialized storage thread performs atomic writes.
The pinned OpenDingux SDK produces the MIPS32r2/uClibc OPK.

### M4 requirements versus the accepted implementation

| M4 deliverable | Current state | Remaining work |
|---|---|---|
| Bitmap Books List | M3 implements titles, progress/checkmarks, ordering, selection, paging, folders, and a two-column scrollbar. | Extend interface acceptance to all themes and overlay/held-input transitions. |
| Six themes, including warm2 | Present, persisted, and used by reader/list/chooser; amber/warm2 include device feedback. | Apply existing palettes to the slider and verify every interface surface. |
| Reader progress bar | Drawn in native rows 478–479 using 0–10000 progress. | Assert production-composition dimensions and separation from slider preview. |
| Header and popup | Title, author, filename, page/count, Back / Font size / Exit, and translucent panels exist. | Integrate slider transitions, review layout/help, and update milestone labels. |
| Jump slider | `Action::Select` exists but has no reader action or renderer. | Implement the model, target mapping, controls, composition, and persistence integration. |
| Repeat, long press, quit chord | `Input` implements 350/80 ms Up/Down repeat, 600 ms A hold, quit in either order, and transition consumption. | Verify these through the complete screen/overlay state machine. |
| OPK and control reference | Packaging, launcher, CLI, and manuals identify M3. | Deliver version `0.4.0` with M4 packaging and documentation. |

## 2. Integration points

| Existing files | Planned change |
|---|---|
| `src/ui/reader_controls.*` | Replace the popup-only state with mutually exclusive None / Menu / Jump overlay state. Expose slider target/preview state and a Jump effect. |
| `src/app/controller.*` | Supply the last completed page/layout to controls, submit confirmed jumps, consume overlay transitions, and checkpoint successful results. |
| `src/reader/engine.cpp`, `types.hpp` | Derive a bounded percentage-to-page map from the rendered layout and attach it to page results. Reuse `ReaderCommand::Page` for commits. |
| `src/reader/worker.cpp` | Extend result serialization/validation for navigation metadata; retain request/generation checking and the 2 MiB frame bound. |
| `src/ui/reader_slice.*` | Compose the jump panel over the existing page, with native bitmap text, target/page labels, track/marker, and controls. |
| `src/ui/books_list.cpp` | Continue routing the production reader through the same compositor; exercise list/menu/chooser visual and input regressions. |
| `src/platform/input.*`, `src/app/reader_main.cpp` | Apply existing timing/consumption to every overlay transition; extend production smoke scenarios and CLI help. |
| `tests/{reader,controller,diagnostic}_tests.cpp`, `CMakeLists.txt` | Add focused slider and end-to-end event-sequence coverage, retaining the existing seven CTest checks. |
| `scripts/package.py`, `packaging/epubreader.gcw0.desktop` | Update artifact/manual selection and launcher identity to M4. |

Keep the small existing module style. A compact headless `src/ui/jump_slider.*`
model is appropriate if target arithmetic would otherwise obscure
`ReaderControls`; use one model for controls, rendering, and tests.

## 3. Jump-slider behavior

### 3.1 Interaction contract

The slider is a reader overlay, not another screen or a persisted session mode.
It opens only with a successfully displayed page and an idle reader worker.

| Context / action | Behavior |
|---|---|
| Reading: Select | Open at the current displayed position; capture its page, anchor, layout, and target percentage. |
| Jump: Left / Right | Decrease / increase target by 1 percentage point. |
| Jump: Up / Down | Increase / decrease target by 10 percentage points. Existing Up/Down repeat applies. |
| Jump: brief A | Confirm one jump and close the overlay after the request is accepted. |
| Jump: B or Select | Cancel and return to the unchanged displayed page. |
| Jump: X | Cancel the target and open the popup with Back to book list selected. |
| Jump: long A | Consume; no confirmation or second action on release. |
| Jump: L1/R1, L2/R2, Y, Start alone | Consume; no page turn, theme change, or font change. |
| Everywhere: Select + Start | Save/exit with priority over every overlay and reader job. |

Clamp the target to integer percentages 0–100. Zero selects the first page;
100 selects the last. Render the exact target page as `PAGE n / total`, using
one-based presentation and zero-based engine values. Several percentages may
select the same page in a short book; this is valid.

Initialize the first page at 0 and the final page at 100; a one-page book opens
at 100, matching its existing progress. For an interior page, use displayed
progress divided by 100, rounded down and clamped to 1–99. This reserves 0/100
for the actual endpoints even in a very long book. Preserve the exact origin
page and anchor at that opening value. Confirming an untouched target, or
returning to the opening target before confirming, is a no-op. This avoids a
page shift due to rounding or the current progress value describing the page's
end.

After a successful submission, the old page remains visible until the new page
arrives, with the existing working indication. Further navigation requests are
ignored while busy. X, popup navigation/close, Back to book list, and global exit
retain their existing responsiveness. Select during initial opening, page work,
or font pagination is consumed without queuing a slider for later.

Select while the popup is open is consumed by that popup. B/X closes it; a fresh
Select then opens the slider. Menu and slider can never be visible together.

### 3.2 Accurate, inexpensive target previews

The existing `ReaderResult.progress` is **0–10000**, not 0–100. In
`ReaderEngine::snapshot()` it derives from CRengine's page-end document-height
percentage, with the final page explicitly reported as 10000. Consequently,
`percentage * pageCount / 100` is not an exact inverse for books with uneven
page heights or chapter breaks.

Recommended implementation:

1. In the engine adapter, generate a fixed **101-entry percentage-to-page map**
   after successful layout. The pinned engine exposes `GetFullHeight()`,
   `getPageList()->FindNearestPage(y, 0)`, and `getPageStartY()`. Match the
   document-coordinate lookup used by `DCMD_GO_POS`, using wide intermediate
   arithmetic for `height * percentage / 100`.
2. Produce page indices in the same numbering domain as the adapter's
   `getPageCount()` / `goToPage()` calls; handle any internal-page conversion in
   the adapter. Explicitly set the 0/100 endpoints, clamp bounds, and verify
   monotonicity. Preview construction must not navigate the live document or
   replace its authoritative text anchor.
3. Store the map with the completed page/layout, rebuilding on open and font
   pagination. A theme redraw reuses it. A one-page book maps every entry to 0.
   Include bounded navigation metadata in successful page IPC results and
   validate it on both sides. The additional payload is approximately 404 bytes
   if represented as 101 32-bit integers.
4. Slider movement reads this map locally. Treat the captured opening target as
   the exact origin page for that slider session. No engine request, render,
   prefetch, or bookmark write is caused by moving the target.
5. On confirmation, validate the captured identity/layout against the displayed
   result and submit **one** existing `ReaderCommand::Page` request. If the target
   page is already displayed, close without a job and preserve its anchor.

This keeps the shown page number equal to the committed page, retains bounded
IPC, and avoids introducing a second percentage-navigation command. Pages,
percentages, and the map are derived layout data; serialized text anchors remain
the authoritative saved location.

### 3.3 Persistence, completion, and failure

- Slider preview state is transient. Cancel, opening the popup, exiting with an
  uncommitted preview, and restart all retain the last completed text location.
  An already-dirty origin checkpoint may still flush during a preview.
- A successful jump result follows `Controller::receive()`'s existing book,
  catalog, and session path, including the 250 ms checkpoint debounce and explicit
  transition/exit flush. Save the **returned** anchor/progress, not the requested
  percentage. Quantization to a containing page means these may differ.
- A committed jump to a multi-page book's final page marks it finished. The
  existing `ReaderCommand::Page` completion condition already supports this.
  Merely previewing 100%, canceling, or opening a final-page slider does not.
  One-page completion still requires a forward page-turn attempt.
- Sticky completion, independent book state, and book-before-session write order
  continue through the existing store. No JSON schema migration is needed for
  transient navigation metadata.
- Keep the last displayed result until a matching successful worker result
  arrives. A failed jump reports an error through the existing popup/notice path
  without replacing the durable anchor. A worker failure must still permit Back
  to book list and exit.
- Back/exit during an accepted jump checkpoints the last result already received
  by the controller, invalidates unfinished work, and suppresses late results.
  Closing or canceling a preview has no reader job to cancel.

## 4. Input arbitration and overlay state

Use explicit overlay state rather than independent booleans, and expose one
overlay-change signal to the application. `Controller::transition()` currently
detects popup open/close; expand it to catch None ↔ Jump and Jump → Menu as well.

Preserve the dispatch order: global quit → active overlay → screen → repeat/hold.
Every screen or overlay transition calls `Input::consumeA()` and
`Input::consumeRepeat()` through the production event-loop path. In particular,
Jump → Menu must consume held inputs even though both states are overlays.

Retain short A on release and a single long A at 600 ms, suppressing short A
afterward. Overlay A actions must never leak to the underlying reader. The M5
music bindings will consume these same semantic actions once integrated.

Keep the device-accepted repeat policy: Up/Down acts immediately, repeats after
350 ms, then every 80 ms; Left/Right remains one action per press. Suppress SDL /
firmware key-repeat duplicates. Focus loss and the existing suspend-gap check
reset held state; resuming an open overlay preserves its target but requires fresh
button presses. Verify tick wraparound and both quit-chord press orders.

## 5. Native rendering and interface completion

Compose at **320×480 native pixels**, with the existing horizontally filtered
640×478 reader image and final native progress rows. The aspect-correct desktop
preview is 640×480; inspect that view as well as raw native screenshots.

- Draw the slider as a compact translucent lower-page panel using the existing
  palette panel/background/text/accent colors and 5×14-native bitmap text.
  A starting panel rectangle is `(8, 320, 304, 148)`, ending at row 467 and leaving
  the progress indicator clear. Keep labels and track within its bounds.
- Show `JUMP TO: n%`, `PAGE n / total`, a target track/marker, and concise
  Left/Right ±1%, Up/Down ±10%, A jump, B/Select cancel, X menu hints. Use available
  bitmap glyphs (`+/-` if needed), with at most the measured available cells per
  line. Distinguish target position from the unchanged reading-progress bar.
- Give slider and status rendering explicit ownership of the lower panel area.
  The current status panel starts at row 404; it must not paint over the slider.
  Route any persistent notice to a reserved line or separate nonoverlapping
  region. A working panel can replace the slider after a confirmed submission.
- Draw the progress indicator after overlay composition at rows **478–479**.
  Add production-level assertions for its two-row height and bounded fill width.
- Reuse the header/popup's translucency and font behavior. Review spacing,
  clipping, long UTF-8 titles/authors/filenames, blank author, and page/count
  values. Native bitmap conversion should continue using `bitmapText()`.
- Replace M3-specific reader labels and CLI/window/log identity with M4. Keep
  version/job/cache information in the existing information panel, leaving normal
  reading focused on content and progress.
- Verify Books List, library menu, chooser, reader header/popup, and slider in
  dark, warm, amber, matrix, light, and warm2. Use the accepted palette values as
  the starting point; tune only in response to actual visual findings.

## 6. Ordered implementation work

Each step includes its focused verification before proceeding.

| Step | Work and principal files | Completion evidence |
|---|---|---|
| 1. Navigation metadata | Extend `reader/{types,engine,worker}.*` with bounded layout-derived target maps and IPC validation. | EPUB 2/3, uneven-layout, short/one-page, font/theme, anchor-preservation, and IPC roundtrip checks pass. |
| 2. Overlay/slider model | Extend `ui/reader_controls.*`, optionally extracting `ui/jump_slider.*`; implement exclusivity, target math, origin/no-op rules, and effects. | Deterministic action tests cover all adjustments, endpoints, cancel/confirm, busy states, long A, and menu transitions. |
| 3. Controller integration | Wire target state and Jump → Page dispatch in `app/controller.*`; expand transition consumption in the production path. | Real-worker checks establish one commit request, no preview navigation, correct saves/completion, restart, and cancellation. |
| 4. Complete native interface | Add slider composition in `ui/reader_slice.*`; review header/help/status layout and all existing interface surfaces. | Native screenshots and pixel/clipping assertions pass; inspect aspect-correct previews. |
| 5. First M4 device build | Update CMake to `0.4.0`, package naming/manual selection, launcher/CLI labels, and write the control reference/device checklist. | Pinned target build and package audit pass; first M4 OPK is ready for focused device feedback. |
| 6. Delivery verification | Run full host/sanitizer checks, independently reproduce the OPK, complete documentation and evidence. | Reproducible package/checksum, actual verification results, and unchecked M4 acceptance checklist are supplied. |

## 7. Verification plan

### Focused host coverage

Extend the current tests instead of creating a parallel application harness:

- **Reader engine / IPC:** all 101 map values are in bounds and monotonic;
  0/100 target first/last; previewed targets equal committed pages. Cover EPUB 2/3,
  uneven chapter/page heights, multiple percentages mapping to one page, and a
  one-page book. Regenerate maps for each font, preserve them across themes, and
  prove metadata construction does not move the current page/anchor. Reject
  malformed map lengths/indices while preserving frame and generation checks.
- **Controls:** opening state, ±1/±10 steps, clamps, direction convention,
  one-based labels, no-op origin/same-page commits, B/Select cancel, X → Menu,
  menu default selection, ignored busy opens, and suppression of unrelated keys.
- **Production input sequences:** drive `Input` → `Controller` with the same
  transition-consumption helper used by `reader_main.cpp`. Cover held A across
  list/open/menu/jump transitions; long A then release; Up/Down repeat across
  Jump → Menu → Reader; duplicate keydowns; focus/suspend reset; tick wraparound;
  and both quit-chord orders in every screen/overlay, including opening/font work.
- **State/persistence:** preview and cancel preserve exact anchor/page/finished
  state; a confirmed jump is saved on debounce and immediate exit/back; a fresh
  process resumes its returned anchor. Cancel/quit with an uncommitted target
  resumes the origin. Cover two independent books, font changes after jumping,
  sticky final-page completion, and unchanged one-page completion rules.
- **Busy/error paths:** repeated A does not submit duplicate work; X/menu/back
  and quit stay responsive during a jump. Obsolete results after leaving a book
  cannot overwrite the next screen/book. Failed requests retain the prior
  displayed/durable result and show an actionable notice.
- **Rendering:** assert the actual production bar occupies only rows 478–479,
  the overflowing list scrollbar occupies columns 318–319, slider panels avoid
  indicators, and normal reading has no overlay. Capture all six themes and
  boundary/midpoint/origin targets, long metadata, short/one-page books, working
  status, and reader/list/chooser menus.

Use original project fixtures and deterministic generated variants under the
build tree for uneven-page and short-book cases. Preserve the existing EPUB
continuity, images, font-anchor/cache, storage, library, and diagnostic checks.

### Build and artifact checks

Follow [BUILD.md](BUILD.md), with M4 evidence paths/names:

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
cmake -S . -B build/repro-m4 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rg280v.cmake \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/repro-m4 --parallel 4
python3 scripts/package.py --build build/repro-m4 --output dist/repro-m4.opk
cmp dist/epubreader-m4-0.4.0.opk dist/repro-m4.opk
git diff --check
```

Record the final SHA256, byte size, ELF ABI/library closure, complete SquashFS
extraction, launcher syntax, packaged M4 manual, dependency notices, and
original-specimen allowlist. Reuse pinned SDK and squashfs-tools 4.6.1; keep all
build/staging/test data project-local.

Record preview input/dispatch timing separately from jump/render latency and
bounded worker cancellation. Host observations do not establish RG280V display
quality or device responsiveness.

## 8. Delivery documents and device acceptance

Update `README.md` and `docs/BUILD.md` with actual implementation status and new
controls. Add `docs/CONTROLS.md` as the full device/desktop context reference,
linked from the README and M4 checklist. Add `docs/VERIFICATION-M4.md` with measured
results and `docs/DEVICE-TEST-M4.md` with installation/state compatibility,
version/checksum, controls, and these unchecked scenarios:

1. M4 launches on Adam v2.1 with the existing M3 settings, library, session, and
   independent book anchors; it exits cleanly to the frontend.
2. Select opens at the current position. Each direction follows the documented
   ±1/±10 convention, clamps at 0/100, and shows a readable target/page number.
   Preview adjustments remain responsive without turning the underlying page.
3. A reaches the shown page. B/Select leaves the exact original page; X opens
   the normal popup. Cancel/no-op confirmation causes no anchor drift.
4. Exit/relaunch after a jump resumes the resulting text anchor; exit while
   previewing resumes the origin. Font changes preserve the newly saved anchor.
   Final-page and one-page completion retain the documented rules.
5. All six themes have readable list/menu/chooser/header/slider surfaces. Normal
   reading has only text and the low-contrast two-native-row bar; the scrollbar
   is two native columns. Warm2 and amber retain their device-tuned appearance.
6. Repeat, short/long A, held-button transitions, both quit-chord orders, and
   suspend/resume cause no unintended selections, turns, or stuck buttons.
7. X/menu/back and Select+Start work during opening, jumping, and font pagination;
   canceling unfinished work preserves the last completed displayed location.

M4 is ready for evaluation when the host/build checks pass and its OPK, checksum,
controls, and checklist are available. Record **device acceptance only after the
user's RG280V results**. Send the project-required Gotify delivery notification
with project name, milestone/version, and agent name when the build is ready.

## 9. New feature requests to schedule explicitly

At M4 planning time, [BRAINSTORM.md](../BRAINSTORM.md) contained requests needing
explicit scheduling. The recommendations below record that baseline. Track
information subsequently shipped in M5; power management is now assigned to
[Milestone 5.5](PLAN-M5.5.md) before M6.

- **Music track counts/current track number:** include this compact popup field
  in M5, alongside startup scanning and real playback session state. M4 should
  leave the header layout easy to extend with an additional status row.
- **Battery information and low-power behavior:** requested behavior is popup
  percentage/charging state, a static approximately two-native-pixel red frame
  below 15% while discharging, save/return-to-list with a notice at 5%, and
  app-wide shutdown at 2% while discharging. Recommended scheduling is a dedicated
  power-management follow-up: it spans hardware discovery, app-wide persistence
  transitions, and device shutdown as well as interface rendering.

The M5.5 plan supplies the prerequisite battery/charger investigation, comparison
with Adam's bundled RG350 test, conditional normal-shutdown support, explicit
unknown-reading and threshold/re-arm behavior, durable-save ordering, and mocked
host/device checks. The accepted M4/M5 implementation has no battery observer or
system-shutdown integration; those capabilities belong to this follow-up.
