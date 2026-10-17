# Milestone 5.5 production build — EPUB Reader 0.5.7

Target: **RG280V / Adam v2.1**. **Normal firmware shutdown confirmed by the user.**
Version 0.5.7 promotes that verified mechanism to automatic critical-battery
handling and removes the temporary experiment. Other checklist scenarios remain
recorded individually below. See [verification](VERIFICATION-M5.5.md),
[controls](CONTROLS.md), and [power profile](POWER-PROFILE-M5.5.md).

## Install and current shutdown capability

Copy `dist/epubreader-m5.5-0.5.7.opk` into `/media/sdcard/apps` and launch
**EPUB Reader (M5.5)**. Replace the previous reader package and remove the obsolete
Shutdown Test OPK from the device's apps folder. M5/M3 JSON state and
M2 import are retained; carry existing state over using the step below. A missing `musicRepeat` starts
at ALL. Every newly opened/resumed book still starts with music stopped.

**Two fresh ≤2% samples on battery now prepare state and request normal firmware
shutdown** on the confirmed Adam profile. The app stops music/work, saves the last
completed anchor/list/config, and invokes `/bin/sync` followed by `/sbin/poweroff`.
Both commands are guarded by a fresh battery/profile recheck. USB power, Charging,
Full, unknown/conflicting or stale data suppress automatic action. The five-second
critical-energy deadline permits best-effort shutdown with an explicit incomplete
save report. Command errors leave a stopped list with an explanation.

There is no manual Y shortcut, special test launcher, or test build option in
0.5.7. Host preview and injected smoke/fixture inputs cannot execute real shutdown.

The package contains this checklist as `DEVICE-TEST.txt`, the control reference
as `CONTROLS.txt`, `POWER-PROFILE.txt`, and the read-only `probe-power.sh`.

## Confirmed shutdown result

The user confirmed:

> Yep the shutdown worked perfectly!

The supplied `tmp/shutdown-test/reader.log` confirms the `0.5.6-test` OPK ran as
`uid=1000`, `euid=1000`, `gid=100`, `egid=100`, from `/opk/epubreader`.
The critical checkpoint acknowledged success on attempt 1, retaining the same
serialized anchor as the last displayed page (11/708). `/bin/sync` and
`/sbin/poweroff` both exited with status 0; the result was `submitted`.
`stderr.log` has only the launch header and no errors. The user's observed actual
power-down confirms the mechanism beyond command submission. The controlled
experiment ran at approximately 75% battery; physical low-battery drain was not
needed. Threshold sequencing/failure/cancellation remains covered by injected
host tests. This result does not claim a completed physical 2% discharge test or
new battery-percentage calibration.

Historical evidence: [shutdown verification](VERIFICATION-SHUTDOWN-M5.5.md).

## Better File System Integration — setup and upgrade

Versions 0.5.6 and later use Adam's second SD card:

| Data | Device path |
|---|---|
| Config, sessions, bookmarks, completion, cache and logs | `/media/sdcard/epubreader/` |
| Fresh-install book default | `/media/sdcard/books/` |
| Fresh-install music default | `/media/sdcard/music/` |

For a fresh install, put EPUBs and MP3s into the content folders above. The app
creates its state directory on first launch. Existing saved folder settings take
precedence over these defaults.

To carry state over from M5 or the first M5.5 build:

1. In the old app, return to Books List and save/exit.
2. **Before first launch of the SD-card-based reader**, use Commander to copy the hidden
   `$HOME/.epubreader` directory to the second card and name it `epubreader`.
   The equivalent terminal command, when the destination does not yet exist, is:

   ```sh
   test ! -e /media/sdcard/epubreader && cp -a "$HOME/.epubreader" /media/sdcard/epubreader
   ```

3. Copy/move your EPUBs to `/media/sdcard/books` and MP3s to `/media/sdcard/music`.
   Copied config retains its old folder choices: select the new folders using
   **X → Books folder / Music folder**, then restart to refresh music.
4. Reopen books from the new folder. Content identities retain their saved anchors
   and checkmarks across the path change. State JSON schemas are unchanged.

The path change does not automatically move the old home directory or content.
`--state-dir`, `--books-dir`, and `--music-dir` remain explicit overrides. Host
preview retains `$HOME/.epubreader`, `$HOME/books`, and `$HOME/music` defaults.

## Evaluation checklist

- [x] First unplugged probe received: `jz-battery`, 3.966 V with 3.4–4.2 V design
      bounds, `usb-charger/online=0`, no capacity/status attributes. Probe runs as
      `od` when launched through Commander, as confirmed by the user;
      `/sbin/poweroff` and `/bin/sync` resolve to setuid-root BusyBox. These
      observations confirm the input profile, not successful reader shutdown.
- [x] USB-connected probe received: `power-probe-19700101-072002-536.txt`,
      4.122 V, `usb-charger/online=1`, the same 3.4–4.2 V design bounds and no
      capacity/status fields. Expected label is `BATTERY: ~90% / USB POWER`.
- [x] Normal firmware shutdown confirmed from the actual reader OPK by the user;
      save acknowledgement on attempt 1, both commands exit 0, no stderr error.
- [x] Temporary Y shortcut/build machinery removed for production 0.5.7;
      automated checks cover Y being inert and host/fixture shutdown isolation.
- [x] Upgrade from M5 retains theme, font, volume, folders, selected entry,
      bookmarks, sticky completion, and reader/list startup session. Missing
      repeat preference is ALL; every resumed/new book has stopped music.
- [x] Fresh install uses `/media/sdcard/books` and `/media/sdcard/music`.
      `config.json`, `session.json`, `books/`, `cache/`, `reader.log` and `stderr.log`
      are under `/media/sdcard/epubreader`. Custom folder choices survive restart.
- [x] The one-time state copy retains settings/bookmarks/completion. After choosing
      the new SD content folders, reopening restores the same anchors. Reusing the
      SD card with a changed/reflashed firmware HOME retains the reader's state.
- [x] Launch the updated `probe-power.sh` through **Commander**, using the same
      action as the received reports, when full. No
      keyboard, terminal entry, or script arguments are needed. Each run saves a
      new timestamped report under `/media/sdcard/epubreader`. Keep the reports recording kernel,
      supply/type/attributes/units/design bounds, UID/GID,
      resolved poweroff/sync paths and permissions. Compare each state with Adam's
      bundled **RG350 test**, recording its percentage/cable display too.
- [x] Popup battery is stable and plausible. `~` identifies a kernel-design
      voltage estimate. USB-only signal says USB POWER; Charging/Full are claimed
      only when actually reported. Unknown/unavailable and stale refresh states
      are clear. Suspend/resume refreshes without stale automatic actions.
- [x] Below 15%, a static red two-native-pixel frame remains visible through normal
      reading, popup, and slider in all six themes; rows 478–479 remain the progress
      bar. USB power removes the warning without changing the reading anchor.
- [x] Two fresh ≤5% samples interrupt reader/opening once, stop music, and show
      the cached list notification with the observed percentage (possibly <5%).
      SAVING changes to POSITION SAVED only after acknowledgement. The saved anchor
      is the last completed page, never an uncommitted slider target or unfinished
      page/font/jump result. Selected book/viewport/checkmarks survive.
- [x] A/B dismisses the notification without also opening the book. Explicit
      reopen at 3–5% remains possible without repeat closure. Recovery (two ≥7%
      samples or cable power) re-arms interruption. Restart/reboot after a successful
      low-power checkpoint returns to the list; reopening restores the anchor.
- [x] At ≤2%, list, menu, chooser, opening, reader overlays, and busy work stop
      and checkpoint to the list before requesting normal firmware shutdown.
      No command loop occurs; USB/charging/full,
      unknown/conflicting, stale, or recovered readings suppress/cancel requests.
- [ ] Exercise repeated threshold/failure cases with injected fixture data using
      isolated state. A failed save says SAVE FAILED, retains captured data, and
      X retries; A/B dismisses after a result. The five-second deadline reports
      SAVE INCOMPLETE rather than claiming success. Host tests cover delayed IO.
- [x] After a production critical-battery shutdown, verify the list session, last
      completed anchor, repeat and mixer state after reboot. The normal shutdown
      mechanism is already confirmed; record any additional physical threshold
      observations separately, without treating synthetic checks as device results.
- [x] Popup rows are Back → Font size → Music Repeat → Exit. Left/Right on Repeat
      clamps ALL → ONE → STOP; A is consumed. Setting persists across books/restarts,
      stays selected, works during reader work, and does not restart the current PCM.
- [ ] ALL continues shuffled cycles; ONE repeats the current successful track;
      STOP ends after its final tail. Long A skips once in every mode and is inert
      while stopped. Paused skip is silent. Bad/removed tracks recover/exhaust
      without a ONE retry loop. Brief A after STOP starts a fresh shuffle session.
- [x] Books group Reading → Unread → Read alphabetically. `<1%` is Reading;
      unknown/0% is Unread; exactly 100% is Read. A finished book revisited at 40%
      moves to Reading and keeps its checkmark. A one-page book starts in Read
      without a checkmark until a forward-turn attempt. Reordering is silent and
      preserves selection/viewport, including duplicate copies and renamed books.
- [x] Read/listen for 15–30 minutes with polling, physical-volume keys, all
      overlays, and both global quit-chord orders. No extra chirps/skips, stuck
      inputs, lost positions, audio disruption, or growing memory are observed.

## Logs and feedback

Device state is `/media/sdcard/epubreader/`. Copy `reader.log`/`stderr.log` before relaunch;
they are replaced each run. POWER SAMPLE/TRANSITION/REQUEST/RESULT and POWER SAVE
FAILED/INCOMPLETE supplement the existing PAGE/INDEX/MUSIC/MIXER/AUDIO records.
Report artifact SHA256, firmware/kernel, probe output and RG350 test comparisons,
screenshots, threshold/event order, saved-state results, and any actual shutdown
permission/exit result. Mark acceptance only from the user's device observations.
