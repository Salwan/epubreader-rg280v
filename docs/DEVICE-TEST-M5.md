# Milestone 5 device test — EPUB Reader 0.5.0

Target: **RG280V / Adam v2.1**. Host/build evidence
is recorded in [VERIFICATION-M5.md](VERIFICATION-M5.md); the complete device and
desktop controls are in [CONTROLS.md](CONTROLS.md).

**Result: Milestone 5 accepted with version `0.5.0`.** The user confirmed
on-device testing and verification:

> I've tested and verfied Milestone 5 on device.

The detailed checklist below is retained as a scenario reference; acceptance is
recorded by the user's overall milestone-verification confirmation above.

## Install and compatibility

1. Copy `dist/epubreader-m5-0.5.0.opk` to the card's `apps/` folder, replacing the
   previous reader OPK. Launch **EPUB Reader (M5)**.
2. Existing M4/M3 JSON settings, sessions, bookmarks, and completion are reused;
   no state reset/schema migration is required. M2 slice import remains available.
3. Put MP3s directly in `$HOME/music`, or choose **X → Music folder** from the
   Books List and **restart the application**. Music is scanned only at launch.
   `.mp3` matching ignores case, folders are nonrecursive, and regular-file
   symlinks are supported. Files added later appear on the next launch.
4. Open a book. Music always starts stopped, including when resuming a book after
   exit/reboot. Open X to see discovered count and playback state.

The package includes this checklist as `DEVICE-TEST.txt` and full controls as
`CONTROLS.txt`. Existing M4 slider/page/font behavior is retained.

## Focused controls and expected behavior

| Context / action | Behavior |
|---|---|
| Reading: brief A (release before 600 ms) | Start, pause, or resume music |
| Reading: hold A for 600 ms | Skip once; remain paused when paused; no action when stopped |
| Popup/slider: A | Existing selection/jump action only, never music |
| Back to book list | Stop immediately and discard the music session |
| Physical Volume+/− | Firmware changes PCM; app observes and remembers it |
| Select+Start, either order | Save/exit everywhere, stopping music |

Playback shuffles the startup catalog, visits each eligible track once, and
automatically generates another cycle. With alternatives, the cycle boundary
does not immediately repeat the last track. One track repeats normally.
Pause/resume retains position and order. Skip discards old buffered audio;
paused skip must be silent until resumed. Bad/removed tracks are excluded for
this application run; an all-invalid catalog stops instead of retrying forever.

Popup `MUSIC: PLAYING 7/24` means sorted-catalog track 7 of 24 discovered candidates,
not shuffle position 7. Track numbers/count stay stable after playback failures.
Informational fields appear only in the popup. Music continues under overlays
and through reading work; fresh reader A can control it during page/font/jump
work once the first page is available.

The initial volume is approximately 50% of the raw mixer range, quantized to the
hardware. Later launches restore saved raw L/R levels. Expected Adam control is
`PCM,0`, range 0–31, no playback switch. Setter calls occur only during startup
restoration; physical keys never invoke a second app adjustment.

## Acceptance checklist

- [x] Launches/exits on Adam v2.1; existing theme/font, list selection, reader
      session, independent anchors, completion, and jump behavior remain correct.
- [x] Popup count matches your nonrecursive MP3 folder, including mixed-case names
      and spaces. No bundled diagnostic tone appears as an extra track.
- [x] Empty/missing folders produce a clear popup status and leave reading usable.
      Adding files or changing Music folder takes effect only after restart.
- [x] A brief press starts, pauses at the current position, and resumes it.
      Opening/resuming another book never starts music automatically.
- [x] A hold skips once with no pause/toggle on release. Holding while stopped is
      inert. Paused skip stays completely silent and resumes the new track from
      its beginning.
- [x] Several short/normal tracks continue automatically in shuffle cycles; no
      immediate cycle-boundary repeat occurs with alternatives. One track repeats.
- [x] Mono and different-rate MP3s play correctly. Corrupt/removed tracks are
      skipped, and an all-invalid folder settles at stopped with an explanation.
- [x] Popup and slider A perform only their interface actions. Held A while
      opening/closing overlays or loading a book causes no unintended playback.
      Track state/count and failure text are readable in all six themes.
- [x] Music continues smoothly during page turns, theme/font changes, slider
      preview/commit, and state saves. Fresh A can pause during reader work.
- [x] Back stops music immediately; returning to the list never restarts it.
      List movement chirps briefly only when selection changes, including repeat
      and page movement; boundaries/menu navigation remain silent.
- [x] Physical Volume+/− changes `PCM,0` once per press. `MIXER START` and
      `MIXER EXIT` setter counts match despite physical presses. Test speaker and
      headphone output and saved raw levels after relaunch and reboot.
- [x] Both Select+Start orders exit promptly while playing/paused, scanning,
      opening, and paginating. Final volume and last completed anchor survive.
- [x] Suspend/resume retains Playing/Paused intent without stuck keys, extra
      skips, or starting a stopped session. Reader controls and audio recover.
- [x] Read/listen for 15–30 minutes. Report audible dropouts, increasing memory,
      slow page response, unexpected skips, and underruns from the final log.

## Logs and feedback

State remains under `$HOME/.epubreader/`: `config.json`, `session.json`, `books/`,
and rebuildable `cache/`. Music state/playlists are transient. Reading checkpoints
debounce for 250 ms; selected mixer changes are polled about every 100 ms and saved
after 250 ms quiet, with a final snapshot on exit.

`reader.log` includes MUSIC SCAN/SCANNED/TRACK, PAGE/INDEX, control ranges,
MIXER START/EXIT setter counts, observed raw changes, and AUDIO EXIT output format /
underruns. `stderr.log` contains reader, decoder, mixer, and save failures. Logs
are replaced on launch, so copy them before restarting when reporting issues.

Report OPK version/SHA256, firmware/kernel, music count/sample rates, exact
buttons/hold durations, expected/actual behavior, before/after raw volume and
setter counts, speaker/headphone and suspend results, and logs/photos as useful.
