# EPUB Reader 0.4.0 — Controls

Target: RG280V / Adam v2.1. All A selections are **brief presses, activated on
release**. A hold of 600 ms is consumed once and does not also activate a short
press. Physical volume is handled by firmware; integrated music arrives with M5.

## Global controls

| Button | Action |
|---|---|
| Select + Start, either press order | Save the last completed position/session and exit, including during reader work |
| L2 / R2 on Books List or unobstructed reader | Previous / next theme; wraps through dark, warm, amber, matrix, light, warm2 |

Held Up/Down moves immediately, repeats after 350 ms, then every 80 ms. Left/Right
acts once per press. Screen/overlay transitions consume held A and directional
repeat; release and press again in the new context. Focus loss/suspend resets
held state without changing the reading position or slider preview.

## Books List, library menu, and folders

| Context / button | Action |
|---|---|
| List: Up / Down | Previous / next book, clamped at the ends |
| List: Left / Right | Previous / next visible page; first / last item on a single-page list |
| List: A | Open selected book |
| List: X | Open / close library menu, initially selecting Rescan library |
| Library menu: Up / Down | Select Rescan library, Books folder, Music folder, Exit application |
| Library menu: A | Activate selected action |
| Library menu: B / X | Close menu |
| Folder chooser: Up / Down | Select Use this folder, parent, or a subdirectory |
| Folder chooser: Left / Right | Move one visible page of directory rows |
| Folder chooser: A | Use current folder, go to parent, or enter selected directory |
| Folder chooser: B | Cancel without changing settings |

EPUB discovery is nonrecursive and case-insensitive. The list displays font size
read-only. Music-folder configuration is saved for M5.

## Reading and popup

| Context / button | Action |
|---|---|
| Reading: Left / L1 | Previous page |
| Reading: Right / R1 | Next page |
| Reading: X | Open / close header and popup, initially selecting Back to book list |
| Reading: Select | Open position-jump slider when a page is ready and the worker is idle |
| Popup: Up / Down | Select Back to book list, Font size, Exit application |
| Popup: Left / Right on Font size | Decrease / increase small → medium → large; clamp at ends |
| Popup: A | Activate Back / Exit; consumed on Font size |
| Popup: B / X | Close popup |

Font changes preserve the original text anchor and keep the same menu row
selected. Other popup keys are consumed, including Select, page-turn shoulders,
and theme shoulders. During opening, jumping, and pagination, additional reader
requests are ignored; X/menu navigation/close, Back, and exit remain available.
Brief/long A in unobstructed reading is reserved for M5 music and currently inert.

## Jump slider

| Button | Action |
|---|---|
| Left / Right | Target −1 / +1 percentage point |
| Up / Down | Target +10 / −10 percentage points; hold repeats |
| Brief A | Jump to the displayed page and close slider |
| B / Select | Cancel; retain the exact original page and text anchor |
| X | Cancel and open the popup with Back selected |
| Long A, L1/R1, L2/R2, Y, Start alone | Consumed without changing the page/theme/font |

Targets clamp to 0–100%; 0 is the first page and 100 the final page. The initial
target retains the current page exactly, including after returning to the
opening value. Confirming that page is a no-op. One-page books open at 100%; every
target still names their only page. Previewing 100% never marks a book finished.

The target's page number comes from the actual layout. Adjustments change only
the overlay; the page and two-row reading-progress bar stay at the origin until
confirmation succeeds. Several percentages can select the same page. The saved
progress is the returned page's actual position, which can differ from the target.
Committed final-page jumps complete multi-page books; one-page completion still
requires a forward page-turn attempt. Cancel/exit saves no uncommitted preview.

## Desktop equivalents

| RG280V | Desktop key |
|---|---|
| D-pad | Arrow keys |
| A / B / X / Y | A / B / X / Y |
| L1 / R1 | Q / W |
| L2 / R2 | E / R |
| Select / Start | Escape / Enter |

Device SDL mappings also work: A=left Ctrl, B=left Alt, X=Space, Y=left Shift,
L1=Tab, R1=Backspace, L2=PageUp, R2=PageDown. Escape+Enter is global save/exit.
