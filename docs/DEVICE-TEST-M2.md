# Milestone 2 device test — EPUB Reader 0.2.0

Target: **RG280V / Adam v2.1**. Version `0.2.0` accepted: all checks performed
by the user passed. Malformed-book and prolonged-session checks are deliberately
deferred to normal-use usability feedback. The user's checks and palette note
below are the acceptance record. Version `0.2.1` addresses that note and the
revised font-size design; see [the focused follow-up](DEVICE-TEST-M2-0.2.1.md).

## Install and launch

1. Copy `dist/epubreader-m2-0.2.0.opk` to the external card's `apps/` folder.
2. Launch **EPUB Reader (M2)** from Applications.
3. On a fresh launch it opens the bundled EPUB 2 prose specimen. Down opens the
   EPUB 3 specimen; Up returns to EPUB 2. Each remembers its own text position.

For your own book, copy a reflowable, unprotected English EPUB to
**`$HOME/books/milestone2.epub`** on the device (Commander can locate your home
directory). That file is opened automatically at launch instead of the specimen.
Other filenames can be supplied with `--book /full/path/book.epub` when running
an extracted package's `launch.sh` from a terminal. Spaces in paths are supported.

## Slice controls

| Button | Action |
|---|---|
| Left / L1 | Previous page |
| Right / R1 | Next page |
| Y | Cycle small → medium → large; preserve text anchor |
| L2 / R2 | Previous / next theme |
| X | Toggle information/help panel, including page count and job time |
| B | Save and reopen the same EPUB from its stored text bookmark |
| Brief A | Retry/reopen the current EPUB |
| Up / Down | Open bundled EPUB 2 / EPUB 3 specimen |
| Select + Start | Save the last displayed position and exit, including during loading |

The controls above record the tested **0.2.0 slice**. Starting with `0.2.1`, font
size is adjusted with Left/Right on the reader popup's highlighted Font size
row; the books list will display the size read-only. Page/theme/font requests
are ignored while a reader job is busy; menu navigation/close and exit remain
active. A progress/status panel appears while loading or paginating. Normal
reading shows content and the final
two native rows of progress only.

## Acceptance checklist

### Display and EPUB structure

- [x] Launches successfully; text fills the intended aspect-correct geometry.
- [x] Curly quotes, café, naïve, em dash, and ellipsis render correctly.
- [x] Regular, bold, italic, and bold-italic text are distinct and readable.
- [x] Turn through the opening pages: a green PNG square and a blue JPEG square
      appear with square physical proportions and their original colors.
- [x] The blockquote, line break, and two list items retain their structure.
- [x] Chapters occur in order 1 → 2 → 3 in both specimens. No missing/repeated
      lines at page boundaries, including the long numbered-word paragraphs.
- [x] All three font sizes are readable; report the preferred size and margins.
- [x] All six themes have the right background **and foreground**. In warm2,
      letters must be dark brown. Theme changes preserve the page count/location. -> amber foreground colors are still not amber enough, make them a bit more red. Also warm2 is not warm enough, make it warmer (more red as well). It seems this screen has a slight cold temperature bias.
- [x] Bottom progress indicator is exactly two native rows and low contrast.

### Position restoration and caches

- [x] Read several pages, note the first sentence, and press B. It reopens at
      that location, with the same page number for the same size.
- [x] At a mid-paragraph location, cycle Y through all three sizes several
      times. The original text anchor remains on the displayed page. Returning
      to the original size returns to the original page without cumulative drift.
- [x] Exit with Select+Start and relaunch. The same book (as selected by the
      startup path above), text location, font size, and theme are restored.
- [x] Repeat after reboot. Books retain independent positions when switched.
- [x] A second open is faster or comparable. Logs report document-cache hits;
      steady neighboring turns report page-cache hits.
- [x] At either end, attempts to turn beyond the boundary stay on that page.

### Responsiveness and error handling

- [x] D-pad and L1/R1 correspond to the physical labels verified in Milestone 1.
- [x] X opens/closes its panel during loading and font re-pagination.
- [x] Select+Start exits promptly during a cold open of a large book or a font
      change. Relaunch retains the last completed/displayed position.
- [x] Suspend/resume leaves no stuck buttons; page turns still work afterward.
- [x] Test your own representative EPUB 2 and EPUB 3 prose books.
- [ ] A malformed/unreadable `milestone2.epub` shows an error; Up/Down can open
      the working bundled specimens and Select+Start still exits.
- [ ] Read for 15–30 minutes with page turns and font/theme changes. Report
      crashes, progressively slower responses, or unusually long loading times.

These two unchecked device checks are intentionally deferred by the user;
any issues found during the next few days of ordinary reading will be reported
as usability bugs. They do not block the accepted rendering vertical slice.

## Logs and state

```text
$HOME/.epubreader/slice/
    reader.log          # open, page/count, font/theme, elapsed job ms, cache hits, anchor
    stderr.log          # engine/SDL/storage errors
    state.ini           # versioned slice font/theme preferences
    books/*.position    # content identity, known path, serialized text anchor
    cache/documents/    # engine-managed document/layout cache; 64 MiB budget
```

Logs are overwritten on launch; copy them before relaunching. The page-image
cache holds only the current and neighboring pages. Derived cache files may be
removed while the application is closed to compare a cold open. Do not remove
`books/` when testing position restoration. This state is separate from the
accepted platform diagnostic and will be migrated into the final persistence
schema in the library milestone.

Report OPK version/checksum, firmware/kernel, book name/EPUB version, first-open
and repeated-open timings, page-turn feel, preferred font/theme, restoration
results, and exact steps/expected/actual for issues. Attach logs and photos.
Host timings and ELF audits do not establish device display/performance acceptance.
