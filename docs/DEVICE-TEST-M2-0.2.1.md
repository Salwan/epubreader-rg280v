# Milestone 2 feedback build — 0.2.1

The `0.2.0` reader passed all device checks the user performed on RG280V / Adam
v2.1. This follow-up covers the warmer palettes and revised font controls.

## Install

Copy `dist/epubreader-m2-0.2.1.opk` to the card's `apps/` folder, replacing the
previous reader OPK to keep one launcher. Launch **EPUB Reader (M2)**. Existing
slice settings, per-book text positions, and document caches are reused.

At launch, `$HOME/books/milestone2.epub` is opened if present; otherwise the
bundled EPUB 2 specimen is used. An extracted package accepts `--book FILE`.

## Updated controls

| Context / button | Action |
|---|---|
| Reading: Left / Right or L1 / R1 | Previous / next page |
| Reading: L2 / R2 | Previous / next theme |
| X | Open / close popup; opening selects Reopen book |
| Popup: Up / Down | Select Reopen book, Font size, or Exit application |
| Popup: Left / Right on **Font size** | Decrease / increase small → medium → large; clamp at the ends |
| Popup: A | Activate Reopen book or Exit application |
| Popup: B / X | Close the popup |
| Reading: B / brief A | Save/reopen or retry the current book |
| Reading: Up / Down | Open bundled EPUB 2 / EPUB 3 specimen |
| Select + Start | Save and exit everywhere, including during pagination |

Font-size changes apply immediately and retain the original text anchor. The
menu stays open with Font size highlighted; its value and header page count
update when pagination completes. A on that row has no effect. Y is no longer
a font shortcut, and the future books list displays font size read-only.
Other popup rows consume page-turn keys. While the reader is busy, menu
navigation, B/X close, and Exit application remain responsive; further reader
requests are ignored until the current operation completes (press again once it
finishes).

## Focused checks

- [ ] Amber text/highlights are redder, compensating for the panel's cool bias.
      Foreground `#eda342` → `#ed9038`, accent `#ffb851` → `#ffa744`.
- [ ] Warm2 paper looks warmer; background `#ffddb0` → `#ffd0a0`, with a matching
      warmer popup panel. Report if either palette needs another small adjustment.
- [ ] Press X, then Down to highlight **Font size**. Left/Right selects all
      three presets; small/large clamp, A does not change size, and the menu
      stays open on the same row. Repeated changes preserve the text anchor.
- [ ] Up/Down navigates the popup rather than switching specimens. Left/Right
      on another row and L1/R1 while the popup is open do not turn pages. B/X
      closes the menu; normal page turns resume, and Y leaves size unchanged.
- [ ] The menu is readable in amber and warm2. Close/reopen it during pagination;
      exit/relaunch confirms the selected size and reading location were saved.

The malformed-book and prolonged-session checks remain deferred to normal use,
as requested. The original annotated results remain in `DEVICE-TEST-M2.md`.
Logs and state remain under `$HOME/.epubreader/slice/`.
