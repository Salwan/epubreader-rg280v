# RG280V / Adam v2.1 — visual feedback update

**Artifact:** `epubreader-diagnostic-m1-0.1.1.opk`  
**Launcher name:** EPUB Reader Diagnostic

Milestone 1's full device acceptance passed with version `0.1.0`. The original
checked checklist and inline user notes are preserved in
[DEVICE-TEST.md](DEVICE-TEST.md). This build applies the requested visual changes.

## Changes and selected defaults

- The bottom half-progress bar occupies **two native rows, y=478–479**.
- New **warm2** palette: very warm cream paper (`#ffddb0`) and dark brown ink
  (`#35271d`), inspired by roughly 4000K reading light / a warmly lit e-reader.
  The RGB palette is a visual approximation of that look.
- **Amber** text/accent colors are shifted toward orange to counter the device's
  yellowish appearance.
- The selected reader path is **640×480 → 320×480, horizontal 2:1 box filter**.
- All UI text, including the footer, uses the selected **5×14 native bitmap**.
  The Fonts screen retains the 5×7 comparison specimen and alternative rendering
  path for diagnostics.

Theme cycling is **dark → warm → amber → matrix → light → warm2**. The new theme
is appended so existing saved theme indices retain their meaning. Warm2 is saved
and restored by the existing versioned diagnostic state.

## Install and controls

Copy the new OPK to the external card's `apps/` folder (normally
`/media/sdcard/apps/`) or `/media/data/apps/`. Remove the previous diagnostic OPK
from that folder if needed to avoid two identical launcher names. Launch **EPUB
Reader Diagnostic** in Adam's Apps list; existing settings remain available.

| Control | Action |
|---|---|
| L1 / R1 | Previous / next diagnostic screen |
| L2 / R2 | Previous / next theme |
| Y | Cycle 12 / 14 / 16 logical-pixel font preset |
| X on Display | Cycle display patterns |
| X on Fonts | Compare box-filtered and vertically doubled rendering paths |
| X on Audio | Chirp |
| Brief A on Audio | Play / pause / resume MP3 |
| Hold A on Audio | Next track once per hold |
| Up / Down on Audio | Previous / next track |
| B on Audio | Stop playback |
| Up / Down on Mixer | Select observed playback-volume control |
| Physical volume buttons | Firmware adjusts volume; application observes/saves |
| Select + Start | Save and exit |

## Focused visual follow-up

- [ ] The two-row bottom indicator is visible in **light** and **warm2**, and
      remains unobtrusive in the dark palettes.
- [ ] **Warm2** has the desired warm paper / dark ink appearance at a comfortable
      brightness. Report whether it should be warmer/cooler or lighter/darker.
- [ ] **Amber** looks amber/orange rather than yellow; report if more adjustment
      would help.
- [ ] The 5×14 footer is readable and fits above the progress bar.
- [ ] Warm2 remains selected after Select+Start and relaunch.

The earlier accepted device observations are `PCM,0`, range 0–31, no playback
switch; physical volume buttons affect that control once per press. Audio output
was 44100 Hz / 2 channels / S16 with zero underruns. Logs and state remain in
`$HOME/.epubreader/diagnostic/` (`diagnostic.log`, `stderr.log`, `state.ini`).
