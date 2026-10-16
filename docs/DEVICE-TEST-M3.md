# Milestone 3 device test — EPUB Reader 0.3.0

Target: **RG280V / Adam v2.1**.

**Result: Milestone 3 accepted with version `0.3.0`.** The user verified all core
features, confirmed the app is fully usable on the device, and authorized marking
the milestone accepted.

> I've verified all core features, app is now fully usable on the device.

The detailed checklist below is retained as a scenario reference; acceptance is
recorded by the user's overall core-feature confirmation above.

## Install and library folders

1. Copy `dist/epubreader-m3-0.3.0.opk` to the card's `apps/` folder, replacing the
   previous reader OPK. Launch **EPUB Reader (M3)**.
2. Put EPUB files directly in `$HOME/books`, or press **X → Books folder** in the
   list to choose a different directory, including a mounted card. Folder scanning
   is nonrecursive and `.epub` matching ignores case. Music folder is configured
   through the same menu for the later music milestone.
3. An installation without a production session opens the Books List. Valid M2
   preferences and per-book anchors under `$HOME/.epubreader/slice/` are imported
   automatically. Imported progress is refreshed on opening a book. M2 had no
   startup-session file, so its upgrade initially opens the list.
4. Settings and new bookmarks live directly under `$HOME/.epubreader/`. Migration
   is repeatable and retains its source files; existing production records win.
   The production document cache starts cold.

The bundled specimens can still be opened with an extracted package's launcher:
`./launch.sh --book /full/path/book.epub`. Normal launch uses the configured
library or resumes the last successful reading session.

## Controls

| Context / button | Action |
|---|---|
| Books List: Up / Down | Previous / next book; hold repeats after 350 ms, then every 80 ms |
| Books List: Left / Right | Previous / next visible page; first / last entry on a one-page list |
| Books List: brief A | Open selected book |
| Books List: X | Open / close library menu: Rescan library, Books folder, Music folder, Exit application |
| Library menu: Up / Down, A, B/X | Select, activate, close |
| Folder chooser: arrows | Select rows / page through directories |
| Folder chooser: A | Use this folder, go to parent, or enter selected subdirectory |
| Folder chooser: B | Cancel without changing settings |
| Reading: Left / Right or L1 / R1 | Previous / next page |
| List or reading: L2 / R2 | Previous / next theme |
| Reading: X | Open / close header and popup; first row is Back to book list |
| Reader popup: Up / Down | Select Back to book list, Font size, or Exit application |
| Reader popup: Left / Right on Font size | Adjust small / medium / large, preserving the anchor; clamp at ends |
| Reader popup: A | Activate Back to book list or Exit application; font row consumes A |
| Reader popup: B / X | Close popup |
| Everywhere: Select + Start | Save last completed position/session and exit |

The font preset is read-only in the Books List. Reading shows only book content
and the two-native-row progress bar. The old B/A-reopen and Up/Down-specimen
shortcuts have been replaced by library-driven navigation. Playback/chirps arrive
with M5; A actions in this build only activate interface selections.

## Expected library and persistence behavior

- Metadata is indexed without paginating every book. Scanning/indexing status is
  visible and the list can be navigated while it updates. Unchanged rescans reuse
  cached metadata. Explicit Rescan retries entries with an inspection error.
- Titles sort case-insensitively within unfinished/finished groups; finished
  books appear last with a checkmark. Titles fall back to filenames. Unsupported
  bitmap characters use one fallback glyph, and common accents are transliterated.
- Identical copies share reading/completion state. Renaming unchanged content
  retains its state. Different bytes at the same path are a different book.
- A multi-page book becomes finished when deliberately navigating to the last
  page (or attempting to advance there). A one-page book becomes finished only
  on a forward attempt. Font changes/opening alone do not mark it finished.
  Completion stays set when reading backward; percentage reflects the current
  page, so it can subsequently fall below 100%.
- Returning to the list preserves selection, saves the bookmark, and records a
  list session. Exiting from reading records a reader session. Resume verifies
  content identity; missing/replaced/unreadable targets recover to the list.
- A missing/unreadable folder or failed book displays a message. X opens settings
  or Back to book list during work; the quit chord is always available. Index
  jobs have a 30-second watchdog; failure is cached until rescan or a file change.

## Acceptance checklist

- [ ] Launches on Adam v2.1 and returns cleanly to the frontend.
- [ ] First/upgrade launch opens the list and imports M2 theme, font, and anchors.
- [ ] Empty/missing folders show a useful message. Books folder can be changed
      to the card; cancel leaves it unchanged. Music-folder setting also persists.
- [ ] Your EPUB 2/3 library lists titles or filename fallbacks, progress, and
      completion checkmarks. A corrupt book is identifiable and does not prevent
      opening another book.
- [ ] A large library scrolls/pages/repeats correctly, with a two-native-column
      scrollbar. Selection remains visible during indexing and after returning
      from a book or a completion-driven reorder.
- [ ] Six themes and the read-only font label look correct in the list. Dark and
      warm2 popup/chooser text is readable at the physical 5×14 bitmap geometry.
- [ ] Read two books at different positions; reopen each immediately and after
      exiting/relaunching. Their text anchors remain independent.
- [ ] Exit while reading, relaunch, then repeat after reboot: same book, anchor,
      theme, and font are restored. Exit from the list: relaunch returns there.
- [ ] Rename a partially read book: it retains position/progress and can resume.
      Replace/remove the resume file: the wrong book is never silently resumed,
      and the list recovers with a message.
- [ ] Navigate to the last page: completion persists on reopen and font changes.
      A one-page book completes only after a forward attempt.
- [ ] Font-row Left/Right retains the original anchor and menu selection; A is
      consumed, and returning to the original size restores the original page.
- [ ] Back to book list and Select+Start respond during cold indexing, opening,
      and font pagination. The last displayed position survives cancellation.
- [ ] Suspend/resume and held A/directions cause no stuck inputs, leaked page
      turns, or unexpected screen/menu transitions.

## Logs, state, and feedback

```text
$HOME/.epubreader/
    config.json              # theme/font, folders, reserved volume snapshot
    session.json             # list/reader session and list selection/viewport
    books/<identity>.json    # text anchor, derived progress/layout, finished flag
    cache/library.json       # rebuildable path signatures and metadata/errors
    cache/documents/         # bounded 64 MiB engine cache
    reader.log               # INDEX counts/timing and PAGE anchors/cache/timing
    stderr.log               # errors, including save failures
```

Page checkpoints are debounced by 250 ms and flushed on explicit transitions/
exit. Malformed JSON is preserved as `.invalid[.N]`; newer unsupported schemas
are reported and not overwritten. A failed save remains queued for retry.
Logs are overwritten on launch: copy them before relaunching when reporting an
issue. Derived caches may be removed while closed; keep `books/` for bookmarks.

Report OPK version/checksum, firmware/kernel, entry count, cold/warm scan feel,
reading/resume/rename results, and exact expected/actual steps. Include logs or
photos where useful. Host checks do not establish device acceptance.
