# Milestone 5.5 — Power awareness and reading controls

**Status:** production `0.5.8`, following device-accepted M5
`0.5.0` and before M6 hardening. See [verification](VERIFICATION-M5.5.md) and
[device checklist](DEVICE-TEST-M5.5.md). User probes confirm unplugged/connected
supply paths/units/design bounds and the USB `online=0/1` signal. The user confirmed
normal reader-context shutdown; logs show save success followed by both firmware
commands exiting 0. Version 0.5.7 enables the confirmed backend for critical battery
handling and retires the temporary Y shortcut/build machinery. Percentage/full-state
comparison remains an evaluation item, not an invented charge-state measurement.
See [power capability notes](POWER-PROFILE-M5.5.md) and
[the accepted shutdown record](VERIFICATION-SHUTDOWN-M5.5.md).

**Target version/artifact:** `0.5.8`, `dist/epubreader-m5.5-0.5.8.opk`, checksum,
updated controls, host verification, and an RG280V / Adam v2.1 device checklist.

The 0.5.8 maintenance follow-up fixes the reported blank-page navigation loop
with exact-page prefetch restoration and an original regression fixture.
**The user verified the fix on the physical RG280V and accepted 0.5.8; this runtime
issue is resolved.** See [0.5.8 verification and acceptance](VERIFICATION-0.5.8.md).

## 1. Outcome and baseline

Implement the five requests in [BRAINSTORM.md](../BRAINSTORM.md#feature-requests):

1. Battery percentage and charging/power information in the reader popup, a static
   red low-battery frame below 15%, and checkpoint/return-to-list at 5%.
2. App-wide normal system shutdown at 2% on battery, if Adam permits it.
3. A **Music Repeat** popup row with **ALL**, **ONE**, and **STOP**.
4. Books List ordering by **Reading → Unread → Read**, alphabetical within groups.
5. **Better File System Integration:** device state/config/cache under
   `/media/sdcard/epubreader`, default books at `/media/sdcard/books`, and default
   music at `/media/sdcard/music`, keeping user data on Adam's second SD card.

The accepted baseline is [M5 device acceptance](DEVICE-TEST-M5.md) and
[M5 verification](VERIFICATION-M5.md). The application uses C++17/SDL 1.2,
a replaceable CRengine child, serialized atomic JSON storage, and the M5 music
facade with generation-tagged terminal events. That M5 baseline has no battery
observer, low-power policy, system-poweroff adapter, or repeat preference.

The behaviors below are implementation decisions for M5.5. Existing M5 controls
and build documentation now describe the `0.5.8` production build; historical M5
acceptance/verification records retain the accepted `0.5.0` baseline.

## 2. Hardware findings and first implementation step

The user identified Adam's bundled **RG350 test** app as an on-device reference
with battery and charging indicators. Adam v2.1's repository includes
`data/apps/rg350test.opk`; compare against that installed version during testing.

### Findings from upstream source

| Source | Finding | Implementation consequence |
|---|---|---|
| RG350 test, `src/main.cpp`, `get_batterylevel()` / `is_batterycharging()` | Reads `battery/voltage_now` and `usb/online` under `/sys/class/power_supply/`. Uses a voltage-derived percentage, 3.31–4.20 V constants, USB compensation, and a ten-sample average. | Battery information is feasible. These are reference formulas/older paths, not established calibration for the user's installed Adam build. |
| OpenDingux `jz-5.15`, `ingenic-battery.c` | Registers `jz-battery` with voltage/design-voltage/health properties; the inspected driver does not expose `capacity` or charging `status`. | Support voltage-based estimates; do not assume `BAT0/capacity` exists. |
| RG280V device tree and included GCW0 base | Battery monitor uses the ADC and a USB supply; the base describes 3.4–4.2 V design bounds. | Discover the actual supply paths and design bounds; distinguish kernel bounds from RG350 test's constants. |
| SimpleMenu `logic.c`, `quit()` | Saves frontend state and invokes `sync && poweroff`. Its OpenDingux hardware code reads `jz-battery` voltage and `usb-charger/online`. | A normal software shutdown path exists in the frontend; verify the reader's launch permissions and exact command on Adam. |
| OpenDingux `pwswd`, poweroff backend | Invokes `/sbin/poweroff`; physical power-button release matters to that backend. | Prefer Adam's normal poweroff executable, with fixed arguments, after app checkpoints. Source support is not yet a successful reader-initiated device shutdown. |

References and inspected revisions are listed in section 10. Current upstream
sources and the installed prebuilt apps need not be identical.

### Device discovery deliverable

Start with a read-only `scripts/probe-power.sh` and an early M5.5 diagnostic build.
Record supply names/types, available attributes and units, voltage/design bounds,
native capacity if present, USB online/status changes, process UID/GID, and the
resolved poweroff executable/permissions. Observe unplugged, connected, and full
states, comparing with **RG350 test**. The probe itself does not invoke shutdown.

Select and document the actual Adam power profile from these observations.
Verify one normal reader-context shutdown during device testing, including saved
state after reboot. If the reader cannot access a supported shutdown mechanism,
record that concrete limitation and deliver checkpoint/list fallback with a clear
status. The requested shutdown feature is conditional on this capability; the
other four requests proceed independently.

## 3. Battery observation and threshold policy

### 3.1 Snapshot and source selection

Add `src/platform/power.*` for read-only observation and a small injectable power
snapshot: optional percent, percentage source (native/estimated), voltage,
external-power state, charging status when available, sample time, and an error.

- Discover battery/USB supplies by type and the verified Adam profile. Candidate
  paths include `jz-battery`/`usb-charger` and the older `battery`/`usb` names.
  Resolve a consistent battery/supply pair rather than mixing unrelated nodes.
- Prefer valid native `capacity` (0–100). Otherwise calculate a bounded estimate
  from valid voltage and the verified design/calibration endpoints, using wide
  arithmetic. Label an estimate with `~`, e.g. `BATTERY: ~64% / USB POWER`.
  Do not copy RG350 test's charging compensation or voltage constants without
  matching the installed device behavior.
- Track **external power** separately from **Charging / Full / Not charging /
  Unknown**. USB `online=1` establishes power connection, not measured charge
  current. If that is the only available signal, display `USB POWER`, rather than
  claiming a full/charging state the hardware does not report.
- Missing/malformed values, unsupported units, inconsistent bounds, and read
  errors produce Unknown, never an invented 0%. Native values outside 0–100 are
  invalid. A physically valid voltage below the calibrated minimum may estimate
  0%; that is distinct from a failed read.
- Poll about every **2 seconds**, independently of popup visibility, library
  indexing, and reader work. Publish bounded snapshots from an observer worker;
  sysfs/ADC reads must not stall SDL input or the audio callback. Update the UI
  only when relevant fields change. Host preview/tests use injected data and an
  unavailable poweroff backend by default.
- Smooth voltage estimates with a short rolling median of three valid samples.
  Do not blend samples across external-power changes or suspend. Treat samples
  older than **6 seconds** as stale and refresh immediately after a suspend gap.
  Show unknown/stale status until fresh data arrives.

Only fresh, valid readings with confirmed battery operation may cause automatic
actions. Any known external power, Charging, or Full state suppresses them;
unknown/conflicting power state also prevents automatic closure/shutdown.

### 3.2 Threshold contract

| Condition | Effect |
|---|---|
| Valid on-battery percentage **<15%** | Static red two-native-pixel frame around the displayed book page, including popup/slider views. |
| On battery at **≤5%**, confirmed by two successive fresh samples | Once per low-battery episode, checkpoint the last completed page, stop music, leave the reader/opening screen, and show a Books List notification. |
| On battery at **≤2%**, confirmed by two successive fresh samples | From every screen, prepare durable state and request normal firmware poweroff if supported. This takes precedence over the 5% notification. |
| External power / charging / full | Remove the warning and suppress automatic actions. |
| Unknown, invalid, or stale reading | Show an unknown/stale popup field; do not act on old percentages. |

Two-sample confirmation prevents one noisy voltage estimate from closing a book
or powering off. Count distinct observer samples, not repeated UI ticks reading
the same snapshot. Test startup already below a threshold and jumps such as 16% →
1%; do not require observing an exact 5% or 2% sample. The frame follows the valid
displayed percentage; an already visible warning may remain during a brief stale
interval, but stale data cannot initiate a new automatic action.

Latch the 5% action when a reader/opening interruption starts. Dismissing its
notification and explicitly reopening a book is allowed while still at 3–5%;
subsequent polls must not immediately close it again. Re-arm after two readings
at ≥7% or confirmed external power. Unknown data never re-arms the episode.
An app restart creates a new episode; a resumed low-battery book may be returned
to the list once again. The 2% action remains app-wide even after a 5% override.

Latch an actual poweroff attempt once per critical episode to avoid command
loops. Re-arm after two readings at ≥4% or external power. Recheck fresh percent
and power state immediately before invoking it; cancel an unsubmitted request if
power is connected, status becomes unknown, or the critical condition clears.

## 4. Low-power transitions and durable shutdown

### 4.1 Five-percent return to list

Factor a controller-owned low-power transition from the existing Back path:

1. Capture the last successfully received `page`/`currentBook_`, selected entry,
   and pending settings before clearing anything. An uncommitted slider target
   and unfinished page/font/jump result are not bookmarks.
2. Stop/invalidate music, close overlays, invalidate/cancel reader/index work,
   and consume held A/repeat through the production transition helper. Initial
   opening has no new bookmark to save; retain the previous durable records.
3. Queue the captured book checkpoint followed by the list session and current
   config, including the latest mixer observation. Give these critical writes
   precedence over rebuildable catalog/cache work.
4. Show the cached Books List with a compact modal notification, e.g.
   `LOW BATTERY (5%) / READING INTERRUPTED / POSITION SAVED`. Show `SAVING` until
   acknowledgement, and `SAVE FAILED` if applicable. Defer background rescanning
   until the critical transition settles. A/B dismisses the notification;
   dismissal must not also open the selected book. Global save/exit stays usable.
   Display the observed percentage, not a hard-coded 5% when the level skipped it.
5. Preserve the selected book/viewport and sticky completion. After successful
   checkpoint, a restart returns to the list and reopening restores its anchor.
   Keep failed checkpoint data retryable and report the error without claiming
   a successful save.

### 4.2 Storage acknowledgement is required

`Store::idle()` currently checks pending/active work but **does not include failed
writes**. `flush()` waits for the queue to drain and is not proof that a checkpoint
succeeded. Do not use either as the low-power success signal.

Add a narrow asynchronous critical-checkpoint ticket/result API to `storage/store.*`.
It must identify successful book → session → config persistence or the specific
failure, preserve existing revision/order rules, and support bounded retry. Hold
new reader opens until the transition's result is known; retain captured data
through failure. The existing atomic replacement mechanism and ordinary debounce
remain the write foundation.

### 4.3 Two-percent shutdown

Introduce an exit reason/state distinguishing **Quit application** from
**Low-battery poweroff**. Evaluate power events before ordinary reader/music work
and before accepting new screen actions; do not let a busy-worker early return
or overlay suppress the policy. Global quit cannot downgrade an already latched
poweroff request to an ordinary exit.

- Stop music and obsolete work immediately, capture the current displayed anchor
  and mixer/config, and checkpoint a list session even from a chooser/menu or
  unfinished opening. If 5% handling is already in progress, promote it rather
  than losing its captured checkpoint or starting conflicting writes.
- Await the critical ticket asynchronously, with an initial **5-second deadline**
  and one retry within that budget. Do not block the input thread with an
  unbounded flush/join or wait for audio EOF, full indexing, or a fresh pagination.
- If writes cannot complete by the deadline, retain the last atomic durable files,
  report/log the incomplete save, and proceed with best-effort **normal firmware
  shutdown** while the system still has power. Do not label incomplete data saved.
  This deadline and retry policy must be exercised with delayed/failing storage.
- Recheck fresh battery/power state, then invoke the verified executable with
  fixed argv using `posix_spawn`/an equivalent bounded adapter. Follow Adam's normal
  sync/poweroff sequence, with no forced-poweroff flags or direct reboot syscall.
  Separate the pure policy and a fake/recording backend from the device adapter;
  synthetic power tests must never reach a real shutdown command.
- Observe spawn errors/exit status and bound the response wait. Command success
  means a request was submitted, not proof the hardware powered down. On failure
  or unsupported shutdown, remain in the saved/stopped list state with an
  explanation and working global exit. Do not repeatedly execute the command.

Automatic poweroff is enabled only for the verified device profile/backend.
If normal shutdown is unavailable from the OPK context, document the reason and
the 2% checkpoint/list fallback in the M5.5 delivery and device checklist.

## 5. Music Repeat menu and EOF behavior

Add a small `RepeatMode { All, One, Stop }`, with **ALL** as the default.

- Popup order: **Back to book list → Font size → Music Repeat → Exit application**.
  Up/Down selects; Left/Right adjusts `ALL → ONE → STOP`, clamped at the ends.
  A on the setting is consumed, matching Font size. Keep the menu open and row
  selected; a repeat change neither starts music nor changes its current PCM,
  position, generation, or shuffle order.
- Repeat adjustment remains responsive during page/font/jump work because it
  does not need CRengine. Font adjustment retains its current busy guard.
- Persist an app-wide `musicRepeat` stable string (`all`, `one`, `stop`) in
  `Config`, using the existing atomic config-save path. Missing values default
  to ALL for M5/M2 imports. An invalid repeat value recovers that preference to
  ALL with a notice, retaining other valid settings. An optional schema-1 field
  is sufficient; book/session schemas do not change.
- Remember the setting across books/restarts, while keeping the accepted rule
  that every new/resumed book starts with music stopped. Expose the mode in the
  read-only music snapshot/menu; use the same enum in controls, service, and tests.

| Event | ALL | ONE | STOP |
|---|---|---|---|
| Matching natural Finished after PCM drains | Next eligible shuffled track; replenish the cycle as M5 does | Restart the current eligible track from the beginning without advancing the shuffle cursor | Stop, clear buffered/transient playback state, and do not continue automatically |
| Manual long A while playing/paused | Next shuffled track | Next shuffled track | Next shuffled track |
| Manual long A while stopped | No action | No action | No action |
| Bad/removed track | Existing bounded exclusion/recovery | Exclude and advance; do not repeat a failed track forever | Existing bounded exclusion/recovery; STOP applies to successful natural EOF |

Paused skip remains silent in all modes. After STOP reaches EOF, brief A uses the
normal Stopped-start behavior: a fresh shuffle session, with the selected repeat
mode still active. ONE starts with a shuffled selection when initially stopped;
its manual skips continue the retained permutation. Repeat decisions use the
current mode when a matching Finished event is consumed. Preserve generation
filtering, final-tail drain, pause intent, and low-power/end-book invalidation;
test mode changes and manual skip around pending EOF in both event orders.

## 6. Books List grouping

Implement the request's **position-based** ordering with one shared group-rank
function, using stored progress in **0–10000 units**, not rounded display text:

| Rank | Group | Rule |
|---|---|---|
| 0 | Reading | `0 < progress < 10000` |
| 1 | Unread | `progress == 0`; unknown `-1` also belongs here until refreshed |
| 2 | Read | `progress == 10000` |

Sort by `(group rank, folded title, filename, full path)`. Keep entries contiguous
without adding selectable section-heading rows, retaining the 17-book viewport,
existing paging/repeat behavior, and two-native-column scrollbar. Show `<1%` for
known progress between 0 and 1% so an in-progress book does not misleadingly show
`0%`. Unknown progress retains the existing unread/0% fallback.

Grouping is independent of sticky `finished`: a previously finished book revisited
at 40% moves to Reading and retains its checkmark; an opened one-page book at
100% sorts into Read but does not gain a checkmark until the existing forward-turn
completion rule fires. Do not rewrite completion flags to implement sorting.

Preserve selection by path/identity and viewport through progress/title changes,
incremental indexing, duplicate copies, renames, restart, and reader/list return.
Automatic regrouping must not chirp. No book-state migration or new progress
measurement is needed; imported unknown progress refreshes on a successful open.

### 6.1 Better File System Integration — 0.5.6 follow-up

On RG280V / Adam v2.1 the second card is mounted at `/media/sdcard`. Use
`/media/sdcard/epubreader` for all production config, sessions, book records,
cache, and logs; fresh content defaults are `/media/sdcard/books` and
`/media/sdcard/music`. The OPK stays in `/media/sdcard/apps`.

Saved Books/Music folder choices take precedence over fresh defaults, and explicit
`--state-dir`, `--books-dir`, and `--music-dir` overrides remain usable. Host preview
keeps home-relative defaults; smoke state remains isolated under its supplied root.

For an existing installation, exit from the Books List and copy the old
`$HOME/.epubreader` directory to `/media/sdcard/epubreader` before first launch.
The device checklist gives the copy step and folder-change procedure. Copying
preserves schemas/bookmarks/preferences; copied config retains its saved folders.
Content files can be placed on the second card and those folders selected in-app.
This path change does not automatically relocate existing files.

Verify a fresh SD-style state root, persisted custom folder overrides and critical
checkpoints/cache, and reopening the same state after the firmware HOME changes.
Use build-local fixture paths for all host checks.

## 7. Integration and native layout

| Files | Planned work |
|---|---|
| `src/platform/power.*`, `scripts/probe-power.sh` | Verified sysfs profile, bounded observer snapshots, device capability diagnostics, and isolated normal-poweroff adapter. |
| `src/platform/paths.hpp`, `src/app/reader_main.cpp`, `src/storage/store.*` | Device SD-card state/content defaults; saved config and explicit overrides take precedence; retain host/smoke isolation. |
| `src/app/power_policy.*` | SDL-independent threshold confirmation, freshness, episode latches, and typed power effects with injected time/samples. |
| `src/app/controller.*` | Low-power preparation/list modal, captured checkpoint ownership, transition consumption, power priority, and exit reason. |
| `src/storage/store.*` | Critical checkpoint acknowledgement/priority and optional repeat config preference; keep ordinary atomic writes/retries. |
| `src/app/reader_main.cpp` | Observer/backend lifetime, pre-dispatch power pumping, suspend invalidation/refresh, final mixer capture, and shutdown coordination. |
| `src/audio/music.*` | Repeat setter/snapshot and Finished dispatch; distinguish repeat-current, manual-next, failure recovery, and explicit stop. |
| `src/ui/reader_controls.*` | Four-row popup and repeat effect before reader-busy filtering. |
| `src/ui/{reader_slice,books_list}.*` | Battery field, repeat row, low-power modal, native warning frame, and sub-1% list text. |
| `src/library/catalog.*` | Shared progress group rank and stable selection-preserving sort. |
| `tests/*`, `CMakeLists.txt` | Power-policy/observer/backend fake coverage and extensions to existing music, controller, storage, library, and smoke checks. |

A starting reader layout keeps the existing music row at y=138 and adds battery
at y=158, expanding the header to end before y=180. Place the four popup rows
at y=194/222/250/278 and help below y=310, ending before the existing y=404 status
panel. Verify measured 5×14-native text bounds in all six themes.

Draw the static red frame after reader overlays, using columns 0–1/318–319 and
rows 0–1/476–477. Draw the progress bar last at rows **478–479**. This preserves
its two native rows; the warning deliberately covers the page's outer edge
without changing CRengine layout or bookmarks. Test the warning exception to
M4's normal row-477 invariance. Use an explicit red warning color in every theme.

## 8. Ordered implementation and verification

| Step | Work | Completion evidence |
|---|---|---|
| 1. Device power profile | Probe and compare installed RG350 test; identify voltage/capacity, external power, and normal shutdown permissions. | Source/profile notes and early device observations; shutdown capability recorded explicitly. |
| 1a. Normal shutdown capability | Completed the temporary button-operated experiment and recorded the user's successful device shutdown plus command/save logs; remove the experiment from production. | `0.5.6-test` accepted for normal shutdown; `0.5.7` has the confirmed guarded automatic backend and no manual shortcut/test flavor. |
| 2. Observer and pure policy | Implement snapshots, estimated/native percentage, freshness, smoothing, thresholds, and latches. | Fixture-sysfs and injected-time boundary/noise/suspend tests. |
| 3. Durable transitions | Add critical write tickets, 5% list notification, 2% preparation/poweroff adapter, and main-loop priority. | Delayed/failed writes, stale worker results, skipped thresholds, and fake shutdown ordering pass. |
| 4. Repeat behavior | Add enum/config/menu and generation-safe Finished handling. | Model and real-decoder ALL/ONE/STOP, manual-skip, pause, and EOF-race checks pass. |
| 5. Grouping and composition | Add rank/sort, sub-1% display, battery/menu/frame/modal layout. | Selection/reorder and all-theme native-pixel/screenshot checks pass. |
| 5.5. Better File System Integration | Use Adam's `/media/sdcard` state/content layout; document the one-time state copy and folder selection. | Fresh SD-style state, custom-folder persistence, cache/checkpoint, and changed-HOME checks pass; device upgrade checklist ready. |
| 6. M5.5 delivery | Set `0.5.7`, update launcher/CLI/log/package names/manual selection, complete verification, and reproduce the OPK. | Artifact/checksum, host/sanitizer checks, package audits, and confirmed shutdown record ready. |

### Focused host coverage

- **Power parsing:** real-profile and alternate supply names, native capacity,
  voltage estimates/bounds/units, missing/malformed reads, USB online without
  charging status, Full/Not charging, conflicting inputs, and unknown/stale data.
- **Policy:** 15/14/5/2/0 boundaries, startup already low, 16 → 1 jumps, two-sample
  confirmation, noise around thresholds, external-power changes, stale gaps,
  suspend/time wrap, low-episode re-arm, and explicit reopen after the 5% notice.
- **Controller/storage:** every screen/overlay and opening/font/jump/index work;
  stop music before continuation; retain last completed anchor; no preview save;
  success acknowledgement versus drained-but-failed queue; failed/delayed saves,
  critical priority/deadline, 5% → 2% promotion, modal held-A consumption, and
  successful reboot/resume state from a fresh store.
- **Shutdown:** recording backend verifies save-before-command, one attempt per
  episode, fixed argv, unsupported/permission/exit/timeout outcomes, charger
  insertion before submission, and normal quit versus poweroff reason. Automated
  tests and synthetic samples cannot call the real device backend.
- **Repeat:** all three EOF outcomes with real short MP3s, ONE cursor preservation,
  all-mode manual skip, silent paused skip, stopped holds, invalid-track exhaustion,
  mode changes while paused/busy/EOF-pending, stale generations, and power stop.
  Cover default/roundtrip/invalid repeat config and coexistence with volume saves.
- **Library/rendering:** all rank boundaries, raw-vs-rounded percentage, unknown
  migration state, finished-but-revisited and one-page edge cases, deterministic
  large mixed libraries, rename/duplicate/selection/viewport preservation, no
  automatic chirp, six-theme popup/modal/frame, and exact native indicator bounds.
- **File system:** device vs host defaults, isolated SD-style config/session/book/
  cache writes, preserved saved folder choices, and state reuse with changed HOME.

Retain the eight accepted M5 CTest checks and add a focused `power-core` check.
Use build-local fixtures/state and project `tmp/`; keep device readings and
poweroff execution behind injected interfaces. Run Debug and ASan/UBSan workflows
from [BUILD.md](BUILD.md), followed by the pinned MIPS cross-build. Reproduce in
`build/repro-m5.5-0.5.7`, package to `dist/repro-m5.5-0.5.7.opk`, and compare it against
`dist/epubreader-m5.5-0.5.7.opk`. Audit ELF/library closure, complete extraction,
manuals/controls, notice hashes, original-asset allowlist, and whitespace/links.

## 9. Delivery and device acceptance

Update README, build/controls docs, and roadmap with actual M5.5 implementation
status. Add `docs/VERIFICATION-M5.5.md` and `docs/DEVICE-TEST-M5.5.md`, packaged as
the M5.5 manual, documenting source/profile/calibration, poweroff support or its
specific fallback, repeat persistence, and grouping/completion distinction.

The device checklist must cover:

1. Upgrade from M5 with all settings/bookmarks/session/music behavior retained;
   missing repeat preference starts at ALL.
2. Battery percentage/source and cable status versus **RG350 test**, including
   connected/full/unknown cases, estimate stability, and suspend/resume refresh.
3. Static two-native-pixel warning below 15% in all reader overlays/themes,
   preserving the progress bar and reading location.
4. A single 5% interruption saves the last displayed anchor, stops music, returns
   to the selected list entry, and shows its notification; dismiss/reopen remains
   possible without repeated closure. Reboot returns to the list with that anchor.
5. At 2%, normal shutdown from reader/list/chooser and busy work, or the documented
   unsupported fallback. Verify charger suppression and state after reboot.
   Use injected thresholds/recording mode for repeated cases and a controlled
   device shutdown test rather than relying only on draining the battery.
6. Music Repeat row/default/persistence; ALL cycles, ONE repeats the current
   track, STOP ends after it; long A still skips once and paused skip stays silent.
7. Reading/Unread/Read ordering, alphabetical ties, partial percentages, unknown
   books, sticky checkmarks, one-page books, and selection after regrouping.
8. A sustained reading/music session with power polling, physical-volume keys,
   all overlays, and both global exit-chord orders.
9. Fresh SD-card defaults, one-time state copy on upgrade, custom-folder settings,
   SD-card bookmarks/config/cache/logs after restart and firmware HOME replacement.

M5.5 is ready for evaluation when host/build checks and its OPK/checksum/manual
are available. Send the project-required Gotify notification with project,
milestone/version, and agent name then. Record device acceptance only from the
user's results; carry the accepted M5.5 baseline into M6 regression/hardening.

## 10. Research references

- [Adam v2.1 bundled RG350 test](https://github.com/eduardofilo/RG350_adam_image/blob/v2.1/data/apps/rg350test.opk)
  and [Adam v2.1 build/base firmware](https://github.com/eduardofilo/RG350_adam_image/blob/v2.1/build.sh).
- [RG350 test battery/USB implementation](https://github.com/RafaVico/rg350_test/blob/bf734c2d1fb5c450c37f1dc60ada283535b83729/src/main.cpp#L488-L563),
  revision `bf734c2d1fb5c450c37f1dc60ada283535b83729`.
- [OpenDingux RG280V device tree](https://github.com/OpenDingux/linux/blob/jz-5.15/arch/mips/boot/dts/ingenic/rg280v.dts),
  [GCW0 base](https://github.com/OpenDingux/linux/blob/jz-5.15/arch/mips/boot/dts/ingenic/bits/gcw0-base.dtsi),
  and [Ingenic battery driver](https://github.com/OpenDingux/linux/blob/jz-5.15/drivers/power/supply/ingenic-battery.c).
- [SimpleMenu shutdown implementation](https://github.com/fgl82/simplemenu/blob/e70a861b1f05fd83cd1b19be53be11c0a0ecd510/simplemenu/src/logic/logic.c#L204-L232)
  and [OpenDingux battery implementation](https://github.com/fgl82/simplemenu/blob/e70a861b1f05fd83cd1b19be53be11c0a0ecd510/simplemenu/src/logic/system_logic_od.c),
  inspected revision `e70a861b1f05fd83cd1b19be53be11c0a0ecd510`.
- [OpenDingux poweroff backend](https://github.com/pcercuei/pwswd/blob/master/backend/poweroff/poweroff.c).

The branch-based kernel/daemon references were inspected during planning; record
the actual installed profile and exact source revisions used during implementation.
