# EPUB Reader 0.5.8 — blank-page navigation fix

## Delivery

Version **0.5.8 is device-accepted**, resolving the runtime issue reported
in `BRAINSTORM.md`: page turns repeatedly returned blank page 3 in the supplied
EPUB, while a slider jump bypassed the stall. The
[pre-fix reproduction](RUNTIME-ISSUE-PAGE-TURNS.md) was captured before changing
the implementation.

```text
dist/epubreader-m5.5-0.5.8.opk
dist/epubreader-m5.5-0.5.8.opk.sha256
Size: 5,439,488 bytes (5.19 MiB)
SHA256: d4473f6aeab797af1be3dc4757e253fc5cc8b14a00b8d3affab57b3182036fad
```

Copy the OPK to `/media/sdcard/apps`, replacing the previous version. The user
verified this build on the physical RG280V and accepted the fix. The package includes the
[device checklist](DEVICE-TEST-M5.5.md#058-page-turn-regression--focused-evaluation).

## Device acceptance

**Resolved and accepted on 2026-10-17**, following the user's physical-device
verification of the delivered 0.5.8 OPK:

> Verified on physical device, good job! mark this as resolved and accepted.

This closes the blank-page/page-3 navigation issue in `BRAINSTORM.md`. The
acceptance record supplements the host, sanitizer and package evidence below.
The artifact hash above identifies the tested delivery; its bundled checklist
predates this repository acceptance update.

## Implementation

`ReaderEngine::prefetch()` restores the exact page index after rendering its
neighbors, including on a rendering exception. It leaves the authoritative text
anchor unchanged. The removed bookmark restoration could map an empty page's
fallback XPointer to the previous page, making the next forward turn return to
the same blank page indefinitely.

Legitimate blank pages still appear once and can be traversed in either direction.
Text anchors continue to govern persisted positions and font re-pagination.
This is a navigation-only change: layout/cache identity, JSON schemas, reader
styles, and the pinned rendering dependencies retain their existing versions.

## Regression evidence

- The supplied `tmp/stuck-on-page3.epub` originally stalled at **page 3 / 1422**.
  The fixed worker advances through pages **1–13**, including blank pages 3 and 5,
  returns backward to page 1, then advances again with warm neighbor caches.
- Changing themes on blank page 3 retains the exact page, bookmark and page count.
- The 10% slider jump still renders page 146; six backward turns reach page 140.
- New original `tests/fixtures/blank-pages.epub` reproduces the same mechanism:
  an oversized title block ending in an empty element creates blank page 2 whose
  fallback bookmark maps to page 1. **It failed on 0.5.7 before the fix**, then
  passed on 0.5.8. The test asserts that the off-page blank bookmark remains
  present, preventing a fixture change from silently removing coverage.
- `scripts/generate-epub-fixtures.py` generates the new fixture deterministically.
  The four previously checked-in EPUB fixtures retained their exact SHA256 values.
- The default CTest suite has 12 checks. Enabling `EPUBREADER_PAGE_TURN_BOOK` adds
  the thirteenth, using the supplied local EPUB in place.

| Check | Result |
|---|---|
| Host Debug CTest, supplied EPUB enabled | **13/13 passed** |
| ASan/UBSan CTest, `UBSAN_OPTIONS=halt_on_error=1`, supplied EPUB enabled | **13/13 passed** |
| Physical RG280V verification | **User verified and accepted 0.5.8; runtime issue resolved** |
| Existing prose continuity, images, font-anchor preservation, resume/cache and worker tests | Passed in both suites |
| Pinned MIPS cross-build and packaging | Passed; GCW0 2022-09-22 / GCC 11.1.0, verified dependency archives/source extraction |
| Independent clean target build / byte-identical OPK | Passed; fresh `build/repro-m5.5-0.5.8`, matching SHA256 and `cmp` |
| Full extraction, ELF/runtime closure, notices and packaged manuals | Passed; 56 files / 8 directories match staging, ELF32 LE MIPS32r2/o32 hard float, 14 SDK libraries, correct 0.5.8 manuals/version |
| Package checksum, normalized modes/timestamps, original-asset allowlist | Passed; no personal EPUBs, test fixture or GitHub-page screenshots in the OPK |
| Native screenshot dimensions, regression traces, documentation links, whitespace | Passed |

Commands follow [BUILD.md](BUILD.md). The Debug and sanitizer traces are under
`build/host/page-turns-reported/trace.txt` and
`build/sanitize/page-turns-reported/trace.txt`; the original-fixture traces are
in each build's `page-turns-blank/trace.txt`. Each trace records expected/actual
page numbers, content pixel counts, cache hits, and authoritative/page anchors.
Page and popup BMPs accompany the traces.

The independently built artifact is `dist/repro-m5.5-0.5.8.opk`. The extracted
production package is under `tmp/opk-audit-0.5.8/`; `tmp/audit-0.5.8.py` checks
file hashes against staging, executable/library architecture and runtime closure,
notices, manuals, file modes/timestamps, original assets, screenshot dimensions,
passing regression traces, local documentation links and OPK identity. It also
creates `build/host/page-turns-reported/comparison-fixed.png`, showing the title,
blank page 3, the successfully reached page-4 popup, and rendered later text.

Core delivery commands:

```sh
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
cmake --build build/sanitize --parallel 4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitize --output-on-failure
bash scripts/build-opk.sh
# Independent clean-build commands are in BUILD.md.
cmp dist/epubreader-m5.5-0.5.8.opk dist/repro-m5.5-0.5.8.opk
unsquashfs -d tmp/opk-audit-0.5.8 dist/epubreader-m5.5-0.5.8.opk
python3 tmp/audit-0.5.8.py
git diff --check
```

`sha256sum -c epubreader-m5.5-0.5.8.opk.sha256` passed from `dist/`.
