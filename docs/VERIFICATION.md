# Milestone 1 host verification — 2026-10-14

## Current feedback build — 0.1.1

```text
dist/epubreader-diagnostic-m1-0.1.1.opk
Approximately 3.6 MiB
SHA256: f405e2e6d4384c94a6ec63701d9b525efa4db7d1c9e420aaed3c7cbfd7232b94
```

- Host Debug build and CTest: **2/2 passed**, including warm2 state roundtrip,
  six-theme smoke rendering, and two-native-row geometry expectations.
- Smoke output: **36 native BMP specimens** (six themes × five screens, plus
  two font paths × three sizes). Warm2, light, and amber previews were inspected
  in aspect-correct geometry; 5×14 footer text fits above the two-row indicator.
- Two independent clean target builds (`build/rg280v-0.1.1`,
  `build/repro-0.1.1`) produced **byte-identical OPKs**.
- Target ELF/shared-library audits, checksum verification, and complete SquashFS
  extraction passed. The OPK includes the updated visual follow-up manual and
  the same 13 bundled target libraries.
- `git diff --check` passed.

Original Milestone 1 device acceptance is complete. The new palette/indicator
appearance can be checked with [DEVICE-TEST-0.1.1.md](DEVICE-TEST-0.1.1.md).

## Original accepted build — 0.1.0

### Delivered artifact

```text
dist/epubreader-diagnostic-m1-0.1.0.opk
3,780,608 bytes (3.61 MiB including SquashFS padding)
SHA256: ccb11a5df1b14449d0ba0f8e8a48141d2116979d0e117792c3caf1ab08fb997b
```

### Completed checks

| Check | Result |
|---|---|
| Host Debug build | Passed; GCC 15.2.1, SDL 1.2 compatibility library |
| `ctest --test-dir build/host --output-on-failure` | 2/2 passed |
| ASan/UBSan build and `ctest --test-dir build/sanitize --output-on-failure` | 2/2 passed |
| Two fresh target builds: `build/rg280v-final`, `build/repro-final` | Passed; pinned GCC 11.1.0 |
| `cmp dist/epubreader-diagnostic-m1-0.1.0.opk dist/repro.opk` | Passed; byte-identical OPKs |
| `sha256sum -c epubreader-diagnostic-m1-0.1.0.opk.sha256` in `dist/` | Passed |
| Target ELF audit | ELF32 little-endian, MIPS32r2, o32, hard float, uClibc loader |
| Shared-library closure | 13 bundled target libraries; only loader/libc supplied by firmware |
| SquashFS inspection and full extraction | Passed; gzip, 128 KiB blocks, 43 files; executable launcher/binary, bundled fonts/tone/icon/notices |
| Shell syntax and `git diff --check` | Passed |

Behavioral checks cover key-repeat timing and wraparound, short/long A arbitration,
transition suppression, quit-chord order, native-row/column transforms, atomic
state recovery and schema validation, real MP3 callback consumption, bounded PCM,
pause/resume, repeated stop/restart generation handling, missing-track recovery,
and playback through the full 12-second specimen without dropping its tail.

Smoke checks render all five screens in all five palettes, plus both font paths
at all three sizes (31 native BMPs), and exercise chirp/MP3 audio with dummy SDL
drivers. Display and font specimens were also inspected in aspect-correct host
previews. Actual ALSA restoration and firmware volume-button behavior require
the device; host access was observation-only.

## Device-dependent results

**On-device acceptance passed for version 0.1.0.** The user checked all items in
[DEVICE-TEST.md](DEVICE-TEST.md): launch, geometry, fonts, input mappings, audio,
per-press volume adjustment, persistence, speaker/headphone output, and
suspend/resume. Reported observations:

- Reader path: 640×480 horizontally reduced with the 2:1 box filter.
- UI bitmap font: 5×14 native pixels, correct aspect ratio and more readable.
- Playback volume: `PCM,0`, raw L/R 16/16, range 0–31, switches -1/-1.
- Other observed controls: `HEADPHONES,0`, `LINE IN BYPASS,0`; physical volume
  buttons affect PCM only, as desired.
- Output: 44100 Hz / 2 channels / S16, zero underruns.
- No separate mute-switch integration is needed for the reported setup.

Version 0.1.1 implements the resulting visual changes (two-row progress bar,
warm2, warmer amber). Its [focused follow-up](DEVICE-TEST-0.1.1.md) concerns those
adjustments; the original accepted checklist remains intact.

QEMU 9.2.4 execution was attempted but failed before application startup on an
Ingenic indexed-load instruction in the SDK loader (`lxw`). This is recorded as
an emulator limitation, not a passing target-runtime test. Adam v2.1's published
Buildroot base and this SDK both use uClibc-ng 1.0.38; actual platform compatibility
has now been confirmed by the user's RG280V test.
