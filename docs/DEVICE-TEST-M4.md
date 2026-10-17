# Milestone 4 device test — EPUB Reader 0.4.0

Target: **RG280V / Adam v2.1**. Implementation/host verification are recorded in
[VERIFICATION-M4.md](VERIFICATION-M4.md).

**Result: Milestone 4 accepted with version `0.4.0`.** The user tested M4 and
confirmed that all features are operational and correct:

> I have tested Milestone 4 and can confirm all features are operational and correct. Good job! Milestone 4 is verified.

The detailed checklist below is retained as a scenario reference; acceptance is
recorded by the user's overall feature-verification confirmation above.

## Install and compatibility

1. Copy `dist/epubreader-m4-0.4.0.opk` to the card's `apps/` folder, replacing the
   previous reader OPK. Launch **EPUB Reader (M4)**.
2. Existing M3 config, library cache, sessions, and per-book text anchors under
   `$HOME/.epubreader/` are reused. No state reset or schema migration is required.
   M2 slice migration remains available for older upgrades.
3. Put EPUBs directly in `$HOME/books`, or choose **X → Books folder** from the
   list. Folder scans are nonrecursive; `.epub` matching ignores case. Music
   folder can be configured for the later integrated music milestone.

The package contains this checklist as `DEVICE-TEST.txt` and the complete
[control reference](CONTROLS.md) as `CONTROLS.txt`.

## Focused controls

- List: Up/Down selects, Left/Right pages, brief A opens, X opens settings.
- Reading: Left/Right or L1/R1 turns pages, L2/R2 changes theme, X opens popup.
- Popup: Up/Down selects Back / Font size / Exit; Left/Right adjusts highlighted
  font size; A activates Back/Exit; B/X closes. Select is consumed here.
- **Select opens the jump slider:** Left/Right changes −/+1%; Up/Down changes
  +/−10%; brief A jumps; B/Select cancels; X cancels and opens the popup.
- **Select+Start saves/exits everywhere, in either press order.**

Up/Down repeat starts at 350 ms and continues every 80 ms. Short A acts on release;
a 600 ms hold is consumed without also confirming. Reader A music bindings arrive
with M5. Theme/page shoulders are consumed by the slider and popup.

## Expected slider behavior

The opening target is the current page: first=0%, last=100%, interior=rounded-down
displayed progress clamped to 1–99%. A one-page book opens at 100%. Returning to
that opening value retains the exact origin. Confirming the currently displayed
page is a no-op. Adjustments use a layout-derived map and show the exact page that
will be selected; short books can map several percentages to the same page.

Previewing does not turn the page, change the bottom progress bar, or replace a
bookmark. On confirmation, one reader request is submitted and the old page stays
visible until completion. Its returned text anchor/progress is saved normally;
actual progress may differ from the requested percentage. A final-page jump
finishes a multi-page book. A one-page book still needs a forward-turn attempt.

X/popup/back/exit remain available during work. Select during loading/pagination
does not queue a slider. Exiting before a jump result has completed preserves
the last completed displayed position. An uncommitted preview is never resumed.

## Acceptance checklist

- [x] Launches on Adam v2.1 and exits cleanly to the frontend. Existing M3 theme,
      font, list selection, reading session, and independent book anchors survive.
- [x] Select opens at the displayed location; Left/Right changes −/+1%, Up/Down
      +/−10%, with correct repeat and 0/100 clamping. Preview is responsive.
- [x] The shown page number matches the committed page. Try 0%, 50%, and 100%,
      a short book, and a one-page book. A is activated only on brief release.
- [x] B and Select retain the exact original page/anchor. Returning the target to
      its opening value and confirming causes no drift. X opens Back-selected popup.
- [x] Previewing 100% and canceling leaves completion unchanged. A committed
      final-page jump completes a multi-page book; backward turns keep completion.
      One-page slider changes do not finish the book; forward turn does.
- [x] Jump, immediately exit/relaunch, and repeat after reboot: the returned text
      anchor resumes. Exit while previewing: the original location resumes.
- [x] Font-size changes after a jump preserve the new anchor and selected row;
      returning to the original font restores its page. Several books stay independent.
- [x] List, library menu, chooser, popup/header, and slider are readable in all six
      themes. Warm2/amber retain their accepted appearance. Long titles/filenames
      fit, and status notices do not overlap slider controls.
- [x] Normal reading shows only content plus the low-contrast bar at native rows
      478–479. Preview leaves this bar unchanged. List scrollbar is two columns wide.
- [x] Holding A across overlay transitions causes no confirmation; long A does
      not jump or trigger another action on release. Held directions do not leak
      from slider → popup → reader or list → reader.
- [x] Both Select+Start press orders exit from list, chooser, reader, popup, slider,
      and during opening/font/jump work, preserving the last completed location.
- [x] Back to list during a pending jump is responsive and preserves the prior
      completed page; reopening succeeds. Suspend/resume leaves no stuck input
      or accidental jump and preserves an open slider's target.

## Logs and feedback

State remains under `$HOME/.epubreader/`: `config.json`, `session.json`, `books/`,
and rebuildable `cache/`. Page checkpoints debounce for 250 ms and flush on
explicit transitions/exit. `reader.log` records PAGE anchors/progress/timings and
INDEX activity; `stderr.log` records errors. Logs are replaced on launch, so copy
them before restarting when reporting a problem.

Report version/checksum, firmware/kernel, book/page/font/theme, exact buttons and
hold durations, expected/actual behavior, restart result, and photos/logs where
useful. Host results do not replace this device acceptance.
