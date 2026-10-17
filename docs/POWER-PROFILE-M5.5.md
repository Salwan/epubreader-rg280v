# M5.5 power profile — 0.5.7 production build

## Capability status

**Installed supply profile and USB connection signal: confirmed by the user's unplugged and connected probes.**
**Percentage calibration/RG350 test comparison and full-state observations: pending.**
**Reader-context normal shutdown: confirmed by the user; enabled for critical-battery handling in 0.5.7.**
The temporary **0.5.6-test** experiment successfully shut down the device. Its
shortcut/build code is retired; [the accepted record](VERIFICATION-SHUTDOWN-M5.5.md)
preserves the actual result and logs.
The user supplied `tmp/power-probe-19700101-071205-420.txt` and
`tmp/power-probe-19700101-072002-536.txt`; their observed supply paths/units/design
bounds and USB signal match the implemented profile. The supplied
`tmp/shutdown-test/reader.log` and `stderr.log`, together with the user's successful
power-down confirmation, establish the normal OPK-context mechanism. Percentage
accuracy remains a labelled voltage estimate rather than a calibrated gauge.

## Observed installed-device profile

Sources: the user's `power-probe-19700101-071205-420.txt` (unplugged) and
`power-probe-19700101-072002-536.txt` (USB connected) reports. Their timestamps
are 1970-01-01 because the device wall clock was unset; this is not an observation
date. Battery freshness uses CLOCK_BOOTTIME rather than that wall clock.

| Observation | Reported value |
|---|---|
| Kernel | `5.15.0-rc6-opendingux`, build `#2`, 2022-02-13 |
| Probe user/group | `uid=1000(od)`, `gid=100(users)`; supplementary groups include `admin` and `pm` |
| Probe launch context | Commander, confirmed by the user; the handheld has no keyboard for terminal entry |
| Battery | `/sys/class/power_supply/jz-battery`, type `Battery` |
| Battery device | `/sys/devices/platform/pmu/power_supply/jz-battery` |
| Voltage | Unplugged: `3966000` µV = 3.966 V; connected: `4122000` µV = 4.122 V |
| Design bounds | `3400000`–`4200000` µV = 3.4–4.2 V |
| Native capacity / charging status | Neither attribute is present in either battery listing |
| USB supply | `/sys/class/power_supply/usb-charger`, type `USB` |
| USB device | `/sys/devices/platform/connector/power_supply/usb-charger` |
| External power | Unplugged: `online=0`; connected: `online=1` |
| Normal poweroff executable | `/sbin/poweroff` resolves to `/usr/bin/busybox` |
| Normal sync executable | `/bin/sync` resolves to `/usr/bin/busybox` |
| BusyBox permissions | root-owned, mode `4755` (`-rwsr-xr-x`), executable by the probe user |

This selects the existing `jz-battery/usb-charger` voltage-estimate profile without
changing endpoints or adding compensation. Expected labels for the reported values:

| Sample | Kernel-design estimate | Expected popup label |
|---|---|---|
| Unplugged, 3.966 V | `floor((3966000 - 3400000) * 100 / 800000) = 70` | `BATTERY: ~70% / ON BATTERY` |
| Connected, 4.122 V | `floor((4122000 - 3400000) * 100 / 800000) = 90` | `BATTERY: ~90% / USB POWER` |

Live estimates additionally use the rolling median, reset on the cable-state
change. These observations confirm readable inputs and both USB signal states,
not percentage accuracy or the battery's true remaining runtime. The voltage
difference while connected does not establish a 20-point gain in stored charge;
charge/load-dependent voltage can affect the estimate. No RG350 test percentage
comparison accompanies these reports yet.

The cable-state signal is now observed both offline and connected. Full-state
and percentage-comparison observations are still pending.
Without a `status` attribute, even a connected/full physical battery must use
`USB POWER`, not an inferred Charging/Full label. The user confirmed that the
discovery probes were launched through Commander. The later reader OPK experiment
separately verified shutdown in the application's own launch context, not merely
through executable permissions.

## Confirmed reader-context shutdown

The user reported that shutdown worked perfectly. In the test OPK's log, PID 441
ran `/opk/epubreader --assets /opk/assets` as UID/effective UID 1000 and
GID/effective GID 100. Supplementary groups matched the earlier probe, including
1006 (`admin`) and 1009 (`pm`). The save ticket succeeded on attempt 1, with the
captured anchor matching the last completed page. `/bin/sync` and `/sbin/poweroff`
each exited 0 and the adapter reported `submitted`. The accompanying stderr has
no errors. Actual power-down is established by the user's confirmation.

Version 0.5.7 uses that same fixed-command adapter for the two-sample ≤2% policy.
Only native device/MIPS builds with real sysfs observation enable it. Before
each command, the latest observer snapshot must be fresh, valid, on battery,
≤2%, and use the confirmed `jz-battery/usb-charger` profile. Other profiles retain
checkpoint/list behavior with a command/capability explanation. Host, smoke and
explicit fixture runs never enable the real backend. No manual test shortcut or
test build option remains.

## Implemented observation profile

- Read-only `/sys/class/power_supply` discovery by `type`, limited to 64 supplies.
  Prefer the coherent alternative pairs `jz-battery`/`usb-charger` and
  `battery`/`usb`; never cross-pair those names. A generic single-battery,
  single-input tree is also supported. Ambiguous batteries are unavailable;
  multiple inputs cannot establish battery operation.
- Prefer valid `capacity` in 0–100. Otherwise use `voltage_now` and the supply's
  own `voltage_min_design`/`voltage_max_design`, all in Linux ABI **microvolts**.
  No RG350 test constants, charge-voltage compensation, or guessed fallback
  endpoints are used. A voltage estimate is marked `~` and is a linear
  kernel-design estimate, **not a measured state-of-charge calibration**.
- Wide-integer estimate: clamp `(median_voltage - min) * 100 / (max - min)` to
  0–100. Median uses the last three valid estimates (available startup samples
  until three arrive). Reset on profile, cable, or charge-state change, invalid
  data, suspend refresh, or a gap over six seconds.
- `online=1` means external power, not necessarily charging. Only supply `status`
  can claim Charging/Full/Not charging. Contradictory inputs suppress actions.
  Malformed/unreadable values never become an invented 0%.
- A worker samples about every two seconds and publishes one bounded mailbox;
  the SDL/audio callback paths do not read sysfs. CLOCK_BOOTTIME freshness includes
  suspend time and avoids SDL tick wrap. A >1.5-second main-loop gap invalidates
  confirmation/mailbox and requests an immediate refreshed sample.
- `reader.log` records sequence, discovered profile, percentage/power label, and
  observation errors. The popup deliberately reports unknown/stale rather than
  treating an old percentage as current.

## Read-only device discovery

The OPK includes `probe-power.sh` and this file as `POWER-PROFILE.txt`. Use the
updated file-reporting script through **Commander**, as the user did for the
first report; device testing must work with the handheld's buttons.

1. Locate the updated `probe-power.sh` in Commander and launch it using the same
   action as for the first report. No arguments or typed commands are needed.
2. Connect USB, then launch the same script again for a connected-state report.
3. When fully charged, launch it once more and keep that report too.
4. Copy the report files from `/media/sdcard/epubreader` and provide the matching
   RG350 test percentage/cable observations.

The script saves both diagnostic output and read errors to a timestamped
`/media/sdcard/epubreader/power-probe-YYYYMMDD-HHMMSS-PID.txt` file by default,
creates the report directory if needed, and prints the saved path. Each launch
uses a new report filename. It lists supply names,
resolved paths, types, readable attributes, voltage/design bounds, native capacity,
USB/status, kernel, UID/GID, and candidate sync/poweroff permissions. The probe
never calls sync/poweroff or changes hardware; its only writes are the report and
its directory. Copy the reports alongside the installed **RG350 test**
percentage/cable readings.
Executable permission alone does not prove shutdown authorization.

Commander establishes the probe's execution context; the accepted OPK experiment
establishes shutdown from the reader. If another installed kernel
omits design bounds and native capacity, the evaluation build reports unknown;
provide probe output before adding a justified calibration profile.

## Shutdown adapter and isolation

`NormalPoweroff` is disabled by default for host/injected use; the production
device runtime explicitly enables the confirmed backend. Its guarded adapter uses fixed
argv **`/bin/sync`**, then **`/sbin/poweroff`**, via `posix_spawn`, with a two-second
response budget per command. It uses no shell, forced flags, or reboot syscall.
The guard is checked again before each command. Spawn, nonzero exit, timeout,
and unsupported outcomes are reported; exit zero means **submitted**, not proof
of hardware powerdown. Application policy permits only one attempt per critical
episode. A fresh ≤2% confirmed-battery recheck follows acknowledged saving, or
the five-second best-effort deadline, before submission.

Host policy/controller tests inject snapshots and recording backends. Adapter
tests inject a fake process interface, including both fixed argv calls; they
cannot invoke real system commands. `--power-fixture` cannot enable shutdown.
The production build has no user-facing shutdown-enable switch or Y shortcut.
It uses the existing policy confirmation/latches and five-second save budget.
Process failures/timeouts are logged and leave the stopped list available, while
the last atomic durable files remain intact.

## Source evidence (not installed-device evidence)

Inspected during implementation on 2026-10-16:

- OpenDingux Linux `jz-5.15` resolved to
  [`7bf6a7125db1ca1ed2aeca42896c3e5d621e6ee9`](https://github.com/OpenDingux/linux/tree/7bf6a7125db1ca1ed2aeca42896c3e5d621e6ee9).
  Its `drivers/power/supply/ingenic-battery.c` exposes `jz-battery` voltage,
  min/max design voltage and health, not capacity/status. The GCW0 base declares
  3,400,000–4,200,000 µV design bounds. Runtime attributes remain authoritative.
- pwswd `master` resolved to
  [`67d8d65107e548673896c27d3b4bc08efa8fa218`](https://github.com/pcercuei/pwswd/tree/67d8d65107e548673896c27d3b4bc08efa8fa218).
  Its poweroff backend executes `/sbin/poweroff` after physical power-button
  release; no reader-context permission conclusion follows from that source.
- The pinned RG350 test and SimpleMenu source findings, and Adam v2.1 bundled
  app reference, are recorded in [the M5.5 plan](PLAN-M5.5.md#10-research-references).
  Their formulas/paths were reference material, not copied calibration.
