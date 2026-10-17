# M5.5 manual shutdown test build — verification

## Accepted device result and retirement

The user confirmed:

> Yep the shutdown worked perfectly!

Evidence is in `tmp/shutdown-test/reader.log` and `stderr.log`:

- The actual OPK executable `/opk/epubreader --assets /opk/assets` ran as PID 441,
  UID/effective UID 1000, GID/effective GID 100, with the expected Adam groups.
- A completed page (11/708) was restored before returning to the list. Its serialized
  anchor matches the captured critical checkpoint.
- The checkpoint succeeded on attempt 1 at boottime 102304 ms, after its pending
  observation at 101987 ms (317 ms between observations, not a pure IO benchmark).
- `/bin/sync` and `/sbin/poweroff` each exited 0; the final result was `submitted`.
- Stderr contains the launch header only, with no error. The user's actual power-down
  confirmation establishes successful shutdown beyond a submitted request.

This confirms the normal mechanism at ordinary battery level, not a physical
2% discharge experiment or a new battery calibration. **Production 0.5.7 promotes
the same guarded adapter to critical-battery handling and removes all temporary
manual-shortcut/build code.** The artifact and commands below are historical
records of the completed experiment; the retired build option is no longer part
of the current source. Current delivery: [VERIFICATION-M5.5.md](VERIFICATION-M5.5.md).

## Artifact

```text
dist/epubreader-m5.5-0.5.6-shutdown-test.opk
Version: 0.5.6-test
5,439,488 bytes (5.19 MiB including SquashFS padding)
SHA256: 1b52ebcfeae18649d46bc8ba4aeffa8f14a919830237918076563128e081577c
```

This is a distinct evaluation build of the M5.5 reader requested by the user.
**Y on the unobstructed Books List** invokes an explicit manual shutdown test.
Normal shutdown was accepted by the user as recorded above. This historical
artifact itself did not enable automatic 2% system shutdown.

The test follows the existing critical state path: retain the last completed
bookmark (including a pending Back-to-list checkpoint), capture selection/config,
stop music and worker jobs, acknowledge atomic book → list session → config writes,
then submit normal `/bin/sync` and `/sbin/poweroff`. A five-second preparation
deadline/one automatic retry is retained; unlike low-battery urgency, save failure
or deadline expiry **aborts a manual test**, with no command submitted.

The separate manual backend is installed by the compile-time test option. Its
guard requires an explicit active test. Synthetic power events still use the
unavailable automatic backend. Real spawning additionally requires a device
build without smoke/fixture input. Host tests use recording/fake-process ports.

`reader.log` and `stderr.log` append across test-app launches. Logs include launch
credentials/groups, boottime/PID, build/arguments/paths/backend gating, input
trigger, work cancellation, mixer/config/book checkpoint data, save-ticket
results/attempts/errors, command argv, spawn/exit/timeout results, and request
outcome. Command stderr is inherited by the normal app stderr file.

## Checks

| Check | Result |
|---|---|
| Host Debug test flavor | **10/10 passed**, including all nine baseline checks and `shutdown-test-logging` |
| ASan/UBSan test flavor | **10/10 passed**, `UBSAN_OPTIONS=halt_on_error=1` |
| Device test build | Passed with the pinned GCW0 GCC 11.1.0 / 2022-09-22 SDK; dependency/source hashes verified |
| Independent reproduction | Fresh `build/repro-shutdown-test` passed; byte-identical OPKs and `cmp` passed |
| Package/checksum/audits | 57 files / 8 directories, exact extraction/staging comparison; test flavor/manual/launcher/probe and checksum passed |
| Target ABI/runtime closure | ELF32 little-endian MIPS32r2/o32 hard float, 14 SDK libraries, firmware loader/libc only, no libatomic |
| Assets/notices/documentation | Original-asset allowlist, runtime and notice hashes, local doc links, shell syntax and `git diff --check` passed |
| Test logging evidence | Two synthetic host launches retained both stdout/stderr histories, process credentials/groups and real-backend-disabled markers |
| Real firmware shutdown | Confirmed by the user's successful power-down report and the save/command logs above |

Focused controller coverage includes inactive Y in a normal controller and all
reader/menu/chooser/opening/busy contexts, active list/indexing Y, held/repeated Y
and held A, list/anchor/config acknowledgement before command, pending Back write,
test vs automatic backend isolation, global quit priority, save failure/deadline
abort, one attempt per launch, backend submitted/failed/timeout outcomes and a
fresh-store reboot readback. The log test runs two isolated synthetic host smoke
launches and verifies retained stdout/stderr evidence and real-backend gating.

## Historical build commands (retired source variant)

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEPUBREADER_SHUTDOWN_TEST=ON
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
cmake -S . -B build/sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEPUBREADER_SANITIZE=ON -DEPUBREADER_SHUTDOWN_TEST=ON
cmake --build build/sanitize --parallel 4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitize --output-on-failure
SHUTDOWN_TEST=ON bash scripts/build-opk.sh
export OD_SDK="$PWD/third_party/sdk/gcw0-toolchain"
export SOURCE_DATE_EPOCH=1663804800
cmake -S . -B build/repro-shutdown-test -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rg280v.cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DEPUBREADER_SHUTDOWN_TEST=ON
cmake --build build/repro-shutdown-test --parallel 4
python3 scripts/package.py --build build/repro-shutdown-test --output dist/repro-shutdown-test.opk
cmp dist/epubreader-m5.5-0.5.6-shutdown-test.opk dist/repro-shutdown-test.opk
```

See the [button-only device procedure and log collection](DEVICE-TEST-SHUTDOWN-M5.5.md).
