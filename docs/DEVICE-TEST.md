# RG280V / Adam v2.1 — Milestone 1 device test

**Artifact:** `epubreader-diagnostic-m1-0.1.0.opk`  
**Launcher name:** EPUB Reader Diagnostic  
**Purpose:** establish native display, controls, audio, SDK compatibility, and mixer behavior.

**Result:** accepted by the user on RG280V / Adam v2.1; all checklist items below
passed. The inline `->` comments record the device feedback on version `0.1.0`.
For the updated visual build, see [the 0.1.1 follow-up](DEVICE-TEST-0.1.1.md).

## Install and launch

1. Copy the OPK from `dist/` to the external SD card's `apps/` folder (normally
   `/media/sdcard/apps/`), or the internal `/media/data/apps/` folder.
   Copy the OPK itself; no extraction, SDK, fonts, or libraries are needed.
2. In Adam's SimpleMenu, find **EPUB Reader Diagnostic** in Apps. Refresh/restart
   the frontend if it has cached the application list. Commander can also launch
   the package. It should open on **Display**, with music stopped.
3. Optional: put a few MP3s directly in `$HOME/music` before launch. The bundled
   12-second quiet stereo tone is always track 1. User tracks are discovered
   once at startup, case-insensitive `.mp3`, nonrecursive.

On first launch the selected playback mixer control is restored to **50% of its
raw range**. Subsequent launches restore its saved raw stereo level and its
switches when present. This percentage is raw-range based, not perceptual dB.

## Controls

Face-button labels are initial RG280V device-tree assumptions. Use **Button
Events** to report discrepancies rather than relying on the assumed label.

| Control | Action |
|---|---|
| L1 / R1 | Previous / next screen: Display, Fonts, Button Events, Audio, ALSA Mixer |
| L2 / R2 | Previous / next theme: dark, warm, amber, matrix, light |
| Y | Cycle font preset: 12, 14, 16 logical pixels |
| X on Display | Cycle overview, 1×1 native checkerboard, 1×2 native checkerboard, greyscale/colors |
| X on Fonts | Switch 640×480 horizontally reduced / 320×240 vertically doubled |
| X on Audio | Play a 45 ms warm chirp, including over music |
| Brief A on Audio | Start / pause / resume selected MP3; action occurs on release |
| Hold A on Audio | Select next track once after 600 ms; active playback skips, paused playback remains paused |
| Up / Down on Audio | Previous / next track; repeats after 350 ms, then every 80 ms |
| B on Audio | Stop playback and clear buffered PCM |
| Up / Down on ALSA Mixer | Choose which playback-volume control to observe/save (does not change its volume) |
| Physical volume buttons | Firmware changes ALSA; application observes and saves |
| Select + Start | Save and exit, in either press order, from every screen |

Music continues when moving between diagnostic screens; leaving/restarting the
application stops it. There is no automatic shuffle/continuation in this diagnostic.

## Acceptance checklist

### Launch and geometry

- [x] Launches on Adam v2.1, returns cleanly to the frontend with Select+Start.
- [x] Button Events reports a **320×480** SDL surface; report video driver,
      bit depth, and pitch. RGB565 is requested; a valid SDL 32-bit fallback is logged.
- [x] Display fills the panel with no letterboxing, stretching, or cropped header/footer.
- [x] Left circle looks round. Right reference looks horizontally wide. A photo
      of both provides a useful aspect-ratio comparison.
- [x] Both checkerboards and fine vertical lines remain distinguishable. Report
      any filtering, flicker, missing alternating rows, or uneven stripes.
- [x] Bottom half-progress indicator is at the final native row and **one row high**.
      Right scrollbar is **two native columns wide**; thumb changes with screen. -> the bottom indicator line is almost invisible in the light theme only (ok visibility in dark themes), make it 2 pixels high for better visibility. Right scrollbar line is perfectly visible.
- [x] Greyscale ramp and red/green/blue swatches have expected colors.
- [x] Evaluate all five theme palettes and note the most readable. -> all themes are readable. I'd like amber to be a bit more amber (the screen makes it look more yellow-ish), and add a new theme "warm2" which will be a very warm background color (around 4000K temperature) with dark foreground color, kind of simulating the kindle display.

### Typography

- [x] Compare both rendering paths with X at all three font sizes (Y).
- [x] Record which path has clearer serifs and less shimmer on the actual screen. -> "640x480 / HORIZONTAL 2:1 BOX FILTER" path is my preference
- [x] Regular, bold, italic, and bold-italic samples appear correctly.
- [x] Curly quotes, em dash, ellipsis, café, and naïve are visible without missing glyph boxes.
- [x] Native 5×7 and 5×14 bitmap samples are sharp; report which height is useful. -> 5x14 is correct aspect ratio and more readable.
- [x] Theme and size remain selected after saving/exiting/relaunching.

### Buttons

- [x] On Button Events, tap every D-pad direction, A/B/X/Y, L1/R1/L2/R2,
      Select, Start, Volume+/Volume−. Capture actual physical label → SDL symbol
      and/or evdev code. SDL volume events may be absent; passive evdev observation
      and mixer changes still diagnose them.
- [x] `HELD` clears after release. Face-button assumptions: A=left Ctrl,
      B=left Alt, X=Space, Y=left Shift; L1=Tab, R1=Backspace,
      L2=PageUp, R2=PageDown, Select=Escape, Start=Return.
- [x] Holds repeat once after a delay; short A and long A do not both fire.
- [x] Select+Start works with either button pressed first, while playing and paused.
- [x] Suspend/resume leaves the app responsive, without stuck keys or accidental skips.

### Audio

- [x] X on Audio plays one quiet, click-free chirp.
- [x] A plays the bundled stereo tone; it cycles notes for ~12 seconds then stops.
- [x] Pause/resume retains position; B stops immediately. Hold A skips only once.
- [x] Your own MP3s play, including a different sample rate and a mono track if available.
- [x] A missing/corrupt MP3 reports failure and another track can still be selected/played.
- [x] Music continues across screens/theme/font changes without audible interruptions.
- [x] Check speaker and headphones. Note output frequency/channels and underrun
      count (startup buffering and normal end-of-track drain are not counted). -> output: "44100 HZ / 2 CH / S16", no underruns (0)
- [x] Test a few minutes of navigation with music; observe underruns and suspend/resume behavior.

### Volume observation and persistence

- [x] On ALSA Mixer, record control name, raw L/R, range, switch L/R, and
      **APP SETTER CALLS** after launch. These calls should stay constant thereafter. -> control name "PCM,0", raw L/R = 16/16, range = 0..31, switch L/R: -1, -1
- [x] Tap Volume+ once (do not hold). Record raw before/after and physical-volume
      key-down count. Repeat Volume− once. Audio changes once per press, with
      **no increase in application setter calls**.
- [x] The observed raw control changes. If it does not, use Up/Down to examine
      the other controls; report which one follows the firmware volume buttons.
      All discovered playback-volume controls and changes are logged. -> 3 playback controls switchable, volume buttons change "PCM,0" which is playing the audio, but volume buttons don't effect "HEADPHONES,0" and "LINE IN BYPASS,0". Not an issue, "PCM,0" is the only one I care about and it works.
- [x] State reports saved atomically. Exit, change system volume outside the app,
      relaunch: the selected control returns to its saved raw level.
- [x] If practical, also test mute/minimum and relaunch. Report whether the
      firmware uses a separate DAC mute switch that needs later integration. -> not necessary, there's no mute function
- [x] Relaunch after reboot restores the level; music remains stopped.

Change polls are sampled at 100 ms; they are **not** a count of ALSA setter calls.
One poll may coalesce several ALSA events. The event log and before/after raw
levels are the evidence for a single firmware step. A held volume key can
legitimately repeat in firmware.

## Logs and feedback

Each launch writes:

```text
$HOME/.epubreader/diagnostic/
    diagnostic.log   # platform info, control inventory, raw events, mixer changes
    stderr.log       # SDL/ALSA/decoder failures and fatal startup errors
    state.ini        # versioned diagnostic settings and selected control snapshot
```

Logs are overwritten on next launch; copy them before retesting. Diagnostic state
is separate from the future reader's config/bookmarks. Removing `state.ini`
resets theme/size and the initial 50% restore. Storage failures are displayed on
the mixer screen and logged.

If startup fails before these files are created, collect the frontend's launch
error or run the extracted package's `launch.sh` in a terminal to see loader errors.

Report:

```text
Firmware/kernel:
Artifact version and SHA256:
Launch result:
Display driver/geometry/bpp/pitch:
Preferred font path/size/theme:
Physical button label -> observed symbol/code:
Mixer control/range; Volume+ before/after; Volume- before/after:
App setter calls before/after volume presses:
Audio output/underruns:
Persistence and suspend/resume results:
Steps / expected / actual:
Attached photos and logs:
```

Milestone 1's on-device exit criteria passed: launch, correct geometry/input
mappings, and one volume adjustment per press were confirmed by the user.
