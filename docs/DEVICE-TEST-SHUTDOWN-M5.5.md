# M5.5 shutdown test — EPUB Reader 0.5.6-test

**Historical procedure — experiment completed successfully.** The user confirmed
that shutdown worked perfectly and supplied `tmp/shutdown-test/reader.log` and
`stderr.log`. See [the accepted evidence](VERIFICATION-SHUTDOWN-M5.5.md).
Production **0.5.7 removes the Y shortcut, test launcher and build option** and
uses the confirmed normal mechanism for critical-battery shutdown. The following
describes the retired artifact as it was tested, not current production controls.

## Install and shortcut

Copy **`dist/epubreader-m5.5-0.5.6-shutdown-test.opk`** to
`/media/sdcard/apps`. Launch **EPUB Reader (Shutdown Test)** from Adam.
The reader header identifies this build as **0.5.6-test**. It uses the existing
state/content settings under `/media/sdcard/epubreader`.

**Press Y once on the unobstructed Books List to save and request normal system
shutdown.** This is the hidden test shortcut; it is not added to the footer or
menus. Close the library menu first. Y does nothing in the reader, opening screen,
folder chooser, or overlays. It also works while the list is indexing.

This manual action works at ordinary battery levels and does not require draining
to 2%. It intentionally does not use battery-percentage/USB suppression; for the
initial power-down test, disconnect USB and release the physical power button.
Automatic low-battery handling still uses the documented unavailable backend.

## Button-only device test

1. Open a book and move several pages. Optionally change theme/font/repeat and
   adjust the physical volume so persistence can be checked as well.
2. Use **X → Back to book list**. Keep the book selected, close any menu, then
   **press Y once**. The app stops background work and shows `SHUTDOWN TEST` /
   `SAVING`, then `POSITION SAVED` after the critical write acknowledgement.
3. The app invokes normal firmware **`/bin/sync`**, followed by
   **`/sbin/poweroff`**, with no arguments, shell, or forced-poweroff flags. Observe
   whether the device actually powers down, remains on, or reboots.
4. After powering up, launch **EPUB Reader (Shutdown Test)** again. It should
   return to the Books List. Reopen the book and check the same completed page,
   checkmark, settings and volume. Music starts stopped as usual.
5. Use **Commander** to copy the logs listed below to the development machine.
   Report whether it powered off, and whether state survived. No keyboard or
   terminal entry is needed for this procedure.

One Y shutdown test is allowed per application launch. Repeated/held Y cannot
resubmit the command. On failure, the app remains stopped in the list with the
result; Select+Start still exits. Save failure or the five-second save deadline
**aborts this manual test without submitting shutdown**. X can retry a failed
save, but does not resubmit the shutdown command; another shutdown test requires
exiting and relaunching the test app.

## Logs to send

Copy these with Commander:

- **`/media/sdcard/epubreader/reader.log`**
- **`/media/sdcard/epubreader/stderr.log`**

This test build **appends** to both logs across launches, retaining the shutdown
attempt when you relaunch after reboot. Each launch adds its PID, boottime, UID /
effective UID, GID / effective GID, supplementary groups, version, arguments, paths,
and whether the real test backend was enabled. Collect the logs before launching
the ordinary reader, whose existing behavior replaces them.

The reader log includes:

- `SHUTDOWN TEST BUILD`, `SHUTDOWN TEST GROUPS`, `SHUTDOWN TEST BACKEND`
- `SHUTDOWN TEST TRIGGER Y`, including boottime and busy-work state
- `SHUTDOWN TEST MIXER`, plus captured raw volume/repeat/theme/font
- `POWER SAMPLE`, including supply profile, raw microvolts, percentage and USB state
- `POWER CHECKPOINT`, including ticket, last completed anchor and list selection
- `POWER SAVE`, with pending/succeeded/failed, attempts, boottime and specific errors
- `POWER REQUEST reason=shutdown-test save=POSITION SAVED`
- `POWER COMMAND`, exact executable/argv, exit results, and final `POWER RESULT`

The stderr log captures storage/decoder errors, spawn errors, timeout details, and
the firmware command's own stderr (for example, a BusyBox permission error).
The log may end at `/sbin/poweroff` if firmware terminates the reader during
shutdown. A zero exit/result of `submitted` only means the request was submitted;
actual power-down and post-reboot state are established by your observations.

## Build isolation

The shortcut is installed only when built with
`-DEPUBREADER_SHUTDOWN_TEST=ON`. Its backend is separate from automatic power
effects. Host builds, `--smoke-test`, and `--power-fixture` never enable real
shutdown. Host/controller checks inject recording backends; process adapter checks
inject fake processes. The normal build has no Y shortcut.

Build and host evidence: [VERIFICATION-SHUTDOWN-M5.5.md](VERIFICATION-SHUTDOWN-M5.5.md).
General controls: [CONTROLS.md](CONTROLS.md).
Observed hardware/profile: [POWER-PROFILE-M5.5.md](POWER-PROFILE-M5.5.md).
