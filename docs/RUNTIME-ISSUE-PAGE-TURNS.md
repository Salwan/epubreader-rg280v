# Runtime issue: blank page and repeated PAGE 3

## Current status

**Resolved and accepted in 0.5.8.** The user verified the fix on the physical
RG280V and explicitly accepted it on 2026-10-17. See
[device acceptance](VERIFICATION-0.5.8.md#device-acceptance).

The regression now advances past the supplied book's blank
pages 3 and 5, preserves page 3 through theme redraw, and navigates backward and
forward over the opening sequence. Both Debug and ASan/UBSan suites pass all
13 enabled checks. See [0.5.8 verification](VERIFICATION-0.5.8.md) for the OPK.

The sections below preserve the **pre-fix 0.5.7 reproduction** and root-cause
evidence. Rerunning the commands now exercises the fixed implementation.

## Pre-fix setup

**Reproduced before an implementation fix**, using the EPUB supplied in
`BRAINSTORM.md` → Runtime Issues, on the `0.5.7` implementation (baseline `847bac2`).

- Host Debug build, production input/controller and real child-process worker.
- Fresh, build-local state/document caches; medium font and dark theme.
- Source EPUB: `tmp/stuck-on-page3.epub`, read in place.
- EPUB SHA256:
  `82daa160b22fc1f73dad450280b0d1d6b0f5c0ab3e3eca8bc097e4c168fe6e7d`.
- Layout: **1,422 pages** at this preset/viewport.

## Reproduction

From the project root:

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DEPUBREADER_PAGE_TURN_BOOK="$PWD/tmp/stuck-on-page3.epub"
cmake --build build/host --parallel 4
ctest --test-dir build/host -R '^reader-page-turns-' --output-on-failure
```

`reader-page-turns-reported` is an ordinary regression: it failed before the fix
and **passes in 0.5.8**.
It is opt-in because the supplied local EPUB is outside the checked-in original
fixtures. The two original EPUB 2/3 controls and the new self-contained blank-page
regression are enabled by default.

The initial reproduction cleared its isolated state, opened the book, alternated twelve D-pad Right
and R1 presses, committed a 10% slider jump, then turned backward six times. R1 is
sent as the actual device SDL Backspace mapping. Every press is released and
every reader job finishes before the next input; each returned Turn result must
have a new request ID. Popup/page BMPs and diagnostic trace are saved along the way.

## Observed behavior

All page numbers below are one-based, as shown in the popup.

| Step | Expected | Actual |
|---|---|---|
| Open | Cover, page 1 | Cover renders, page 1 |
| D-pad Right | Page 2 | Title renders, page 2 |
| R1 | Page 3 | Entire content area blank, page 3 |
| Next D-pad Right | Page 4 | Blank page 3 again |
| Nine further alternating R1/Right presses | Advance beyond page 3 | Every result still reports blank page 3 |
| Jump slider → 10% → A | Render target page | Page 146 renders prose |
| Six D-pad Left presses | Previous pages | Pages 145 through 140 render normally |

The blank page has **zero non-background pixels** in the 320×478 content area,
zero diagnostic text bytes, `anchorOnPage=false`, and unchanged progress of
16/10000. The popup also visibly displays **PAGE 3 / 1422** after repeated turns.
The jump result has 32,761 non-background content pixels and `anchorOnPage=true`.

This reproduces both the reported stall and the jump-slider workaround on the
host using the same reader/controller path as the device.

## Isolated cause

On failure the test also drives `ReaderEngine` directly, using separate fresh
state directories to compare sequential turns with and without the worker's
post-response `prefetch()` call:

```text
Without prefetch: 1 → 2 → 3 → 4 → 5 → 6 → 7
With prefetch:    1 → 2 → 3 → 3 → 3 → 3 → 3
Bookmark roundtrip from blank page 3: restored page 2
```

The blank page's generated bookmark is a structural element:

```text
/body[1]/DocFragment[2]/body[1]/div[1]/div[1]/div[1].0
```

It resolves to **page 2**, not page 3. The relevant interaction is:

1. `src/reader/engine.cpp` derives the next page from `view->getCurPage()` and
   records `view->getBookmark()` after the turn.
2. `src/reader/worker.cpp` publishes the result, then calls `engine->prefetch()`.
3. `ReaderEngine::prefetch()` renders neighbors, returns with `goToPage(current)`,
   then calls `restore(e.anchor)`.
4. That bookmark restoration relocates the engine from blank page 3 to page 2.
   The next forward request advances from page 2 to page 3 again, creating the loop.

CRengine's `getBookmark()` explicitly has fallback behavior when no XPointer
resolves inside a page. Its `goToBookmark()` / `checkPos()` restore the page
containing the pointer; they do not guarantee that an empty page's bookmark
roundtrips to the same page. The adapter's assumption that prefetch restoration
preserves the current page is therefore violated here.

## Verification and evidence

- Before adding the focused test: existing host suite **9/9 passed**.
- After adding the two EPUB 2/3 sequential controls and enabling the supplied book:
  **11 passed, 1 failed**, with only `reader-page-turns-reported` failing.
- The supplied-book failure was repeated with fresh state and reproduced each time.
- Production runtime sources were not modified for this reproduction.

The initial run generated evidence under `build/host/page-turns-reported/`:

- `trace.txt`: page numbers, expected targets, pixel counts, anchor information,
  direct-engine prefetch comparison and bookmark roundtrip.
- `2-R1.bmp`: first blank page 3.
- `3-dpad-right-popup.bmp`: popup still showing page 3 after another forward turn.
- `13-jump-10-percent.bmp`: correctly rendered prose after the workaround.
- `comparison.png`: aspect-correct title / blank / stuck popup / jumped prose strip,
  generated manually from the BMPs with ImageMagick.

CTest's full output is also retained in `build/host/Testing/Temporary/LastTest.log`.
Reruns replace the trace and test log with current results. The expanded 0.5.8 test
also includes blank-page theme redraw and backtracking before the slider jump,
so current screenshot sequence numbers differ from this initial reproduction.
The standalone `build/host/page-turn-tests` executable accepts assets, EPUB path,
isolated output directory, and worker executable, in that order.
