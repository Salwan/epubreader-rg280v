# Milestone 5.5 verification — production 0.5.7

## Delivery status

**Version `0.5.7` is the production M5.5 build with confirmed normal shutdown.**
The accepted baseline for earlier milestones remains M5 `0.5.0`. The battery observer, pure threshold
policy, acknowledged low-power transitions, repeat setting/EOF behavior, grouped
library, and native UI are implemented. Source/profile notes and a read-only
device probe are included in the OPK.

The 0.5.6 follow-up adds **Better File System Integration** to M5.5: device
config/bookmarks/session/cache/logs use `/media/sdcard/epubreader`; fresh content
defaults are `/media/sdcard/books` and `/media/sdcard/music`. Saved folder choices
override defaults. Existing installations use the checklist's one-time state copy;
host defaults and explicit CLI/smoke isolation are retained.

**Normal firmware shutdown is confirmed by the user and enabled for critical battery in 0.5.7.** The user's
unplugged/connected reports confirm the installed `jz-battery/usb-charger` supply
profile, 3.4–4.2 V design bounds, and USB `online=0/1` signal. Full-state comparison
and percentage calibration remain pending; voltage percentages are marked as
estimates. The supplied device logs show
the actual OPK running as `od`, successful save acknowledgement on attempt 1, and
`/bin/sync` plus `/sbin/poweroff` exiting 0. The user confirmed actual power-down.
Production critical-battery handling uses that same guarded adapter. Temporary
manual test code, Y shortcut, build option/launcher and flavor manifest are removed.
See [power capability notes](POWER-PROFILE-M5.5.md) and
[device evaluation](DEVICE-TEST-M5.5.md), and
[the accepted shutdown experiment](VERIFICATION-SHUTDOWN-M5.5.md). This confirms
the mechanism; it does not invent a completed physical 2% discharge experiment.

## Artifact and build evidence

```text
dist/epubreader-m5.5-0.5.7.opk
5,439,488 bytes (5.19 MiB including SquashFS padding)
SHA256: cfcba183aa5db7c992ce04b4408c4010075c0d755efb3a433a87d086dac547b3
```

The adjacent `.sha256` verifies this artifact. The independently configured target
tree produces a byte-identical OPK. Packaged manual/profile/controls match source
and reflect the confirmed shutdown result; this verification report is maintained
in the repository.

| Check | Result |
|---|---|
| Host Debug CTest | **9/9 passed** for production 0.5.7 |
| ASan/UBSan CTest (`UBSAN_OPTIONS=halt_on_error=1`) | **9/9 passed** for production 0.5.7 |
| Pinned MIPS cross-build | Passed; GCW0 2022-09-22 SDK / GCC 11.1.0; dependency and source archive hashes verified |
| Independent `build/repro-m5.5-0.5.7` / `dist/repro-m5.5-0.5.7.opk` | Fresh target tree passed; OPKs byte-identical, `cmp` passed |
| ELF/runtime closure and full extraction | ELF32 LE MIPS32r2/o32 hard float; 14 SDK libraries, firmware loader/libc only; 56 files / 8 directories match staging exactly |
| Manuals/launcher/probe, assets/notices, integrity/documentation | Current production docs/probe match source; original-asset allowlist, runtime/notice hashes, checksum, doc links and shell/whitespace checks passed |
| Native rendering | 69 application, 44 reader and 36 diagnostic native BMP dimensions/counts passed |
| Device profile and normal shutdown | Supply/profile/USB states observed; normal OPK-context shutdown confirmed by the user and accepted logs |
| Runtime isolation and removed shortcut | Y inert across nine contexts, verified-profile final guard and host/smoke/fixture isolation pass; executable has no test markers/shortcut and package has no test launcher/flavor manifest |

## Behavioral evidence

- **SD-card integration:** device/host path selection; build-local SD-style fresh
  books/music defaults; critical book/session/config and cache persistence; custom
  folder overrides, theme, repeat, anchor and completion survive reopening with a
  different firmware HOME. Device upgrade/setup steps cover the one-time old-state
  copy and content-folder selection.
- **Power:** known/alternate supply pairs, native capacity preference and invalid
  recovery, microvolt validation, design bounds, below-minimum physical voltage,
  median-three filtering and gap/cable resets, USB-only vs actual status, Full/
  Not charging/conflicts, missing/ambiguous supplies, unknown/stale data.
  A regression fixture reproduces the user's 3.966 V / USB-offline and 4.122 V /
  USB-connected reports with absent capacity/status. It checks the estimated-70%
  on-battery and estimated-90% USB POWER labels, reset of battery-only median
  history, and confirmed external power without claiming a charging state.
  The focused `power-core` check passed in Debug and ASan/UBSan after adding this
  observed-device fixture; no real shutdown command was executed.
- **Policy:** 15/14/5/2/0, startup below threshold, 16→1, distinct-sample rather than
  UI-tick confirmation, noise, exact 7%/4% episode re-arm, explicit reopen, unknown
  suppression, suspend invalidation and 64-bit boottime across SDL wrap.
- **Observer/backend:** nonblocking mailbox while a fixture read is held; refresh
  discards a pre-suspend in-flight result. Host/smoke/fixture backends stay unavailable.
  The device runtime enables the confirmed Adam backend, with fresh ≤2% on-battery,
  error-free and exact supply-profile checks repeated before each command. The
  normal adapter uses an injected fake process to check `/bin/sync` followed by
  `/sbin/poweroff`, fixed argv, guarded cancellation between commands, permission/
  exit/timeout reporting. Synthetic tests never execute a system command.
- **Storage:** explicit success/failure tickets with specific book/config failure,
  bounded retry, retained captured data, critical-before-catalog priority behind
  active IO, no session past a failed book, and fresh-store anchor/list/repeat
  roundtrip. Existing atomic replacement, revision and migration checks remain.
- **Controller:** every screen/overlay plus opening/font/jump/index work; music
  stops before continuation, unfinished worker/slider results cannot replace the
  completed anchor, held A cannot dismiss/open, dismissal/reopen does not reclose.
  5%→2% promotion preserves captured data and final config. Delayed IO proves a
  nonblocking five-second best-effort deadline. Failed writes use one automatic
  retry and explicit X recovery. Charger, unknown, stale, and >2% cancel an
  unsubmitted request; global quit preserves the poweroff reason. Recording
  backends verify acknowledged state before one request per episode.
  A cancelled unsubmitted request can be confirmed again; an actual attempt stays
  latched. Delayed failure after the deadline cannot begin an out-of-budget retry.
  Save failures and the shutdown-capability explanation have separate UI fields.
  Production checks retain the pending Back-to-list bookmark case, command outcomes,
  current raw mixer/repeat config and fresh-store state, and verify Y is inert in
  all nine existing screen/work contexts. `reader-smoke` now runs two isolated
  launches with synthetic critical sysfs data and verifies real shutdown is disabled,
  logs reset for the current launch, and test-build controls/markers are absent.
- **Repeat:** real MP3 tail drain and ALL/ONE/STOP transitions, initial shuffle,
  ONE cursor preservation, all-mode silent paused manual skip, stopped holds,
  current-mode decisions on pending EOF, both manual-skip/EOF orders, stale
  generation/end-book stop, invalid ONE exhaustion, default/roundtrip/invalid
  preference recovery, and responsive busy-reader menu/config persistence.
- **Library/UI:** raw progress group boundaries, unknown fallback, `<1%`, mixed
  1,000-entry contiguous groups, sticky finished-but-revisited, one-page completion,
  deterministic ties/selection/rename/duplicates/viewport, and silent reordering.
  Existing M5 native specimens remain; 24 added six-theme warning-frame, popup,
  slider and low-power-modal BMPs validate native edges and progress rows.

Commands follow [BUILD.md](BUILD.md). Core delivery commands:

```sh
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
cmake --build build/sanitize --parallel 4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitize --output-on-failure
export OD_SDK="$PWD/third_party/sdk/gcw0-toolchain"
export SOURCE_DATE_EPOCH=1663804800
bash scripts/build-opk.sh
# Independent pinned-toolchain configuration as in BUILD.md:
cmake --build build/repro-m5.5-0.5.7 --parallel 4
python3 scripts/package.py --build build/repro-m5.5-0.5.7 --output dist/repro-m5.5-0.5.7.opk
cmp dist/epubreader-m5.5-0.5.7.opk dist/repro-m5.5-0.5.7.opk
unsquashfs -d tmp/opk-audit-m5.5 dist/epubreader-m5.5-0.5.7.opk
diff -r build/rg280v/opk-stage tmp/opk-audit-m5.5
sh -n scripts/probe-power.sh
python3 tmp/audit-m5.5.py
git diff --check
```

Existing schemas stay at 1; only optional `musicRepeat` is added to config.
Book/session schemas and sticky-completion semantics are unchanged. Runtime
dependencies and original distributable asset allowlist are unchanged. Device
accuracy, suspend/audio continuity, sustained reading and further physical threshold
observations remain user-performed scenarios; normal shutdown itself is confirmed.

Checksum verification was run from `dist/` with
`sha256sum -c epubreader-m5.5-0.5.7.opk.sha256`. The project-local audit verifies
all extracted files, packaged documents/probe, ELF/library closure and hashes,
original assets/notices, native screenshot dimensions/counts, and local doc links.
It also generates aspect-correct six-theme previews under `tmp/m5.5-themes.png`
and `tmp/m5.5-modals.png`. The pinned SDK and squashfs-tools 4.6.1 remain inputs
to reproducibility. No dependency or distributable asset additions were needed.
