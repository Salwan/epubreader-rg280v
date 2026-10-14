# Milestone 2 verification — 2026-10-14

## Feedback build — 0.2.1

```text
dist/epubreader-m2-0.2.1.opk
5,181,440 bytes (4.94 MiB including SquashFS padding)
SHA256: f2e1d929d98115dd75053194ed1b67e57e4734ff51c6f4f07fcec8dfe7b80c82
```

- Amber foreground/accent/muted colors and warm2 paper/popup backgrounds are
  warmer to compensate for the device's reported cool bias.
- The popup now contains Reopen book, **Font size**, and Exit application. Font
  size uses Left/Right only when highlighted, clamps at small/large, and retains
  both the menu selection and authoritative text anchor. Y no longer adjusts it.
  The library design displays the confirmed preset read-only.
- Host Debug and ASan/UBSan suites: **4/4 passed** each. The final reader checks
  were rerun after the popup contrast adjustment and passed in both builds.
  Tests cover overlay/page-turn isolation, font endpoints, busy-reader exit,
  A/repeat consumption across transitions, and six menu-driven font changes at
  a mid-book text location on both EPUB specimens.
- Smoke uses the production popup dispatch path and produces **13 native BMPs**:
  three font pages, three highlighted font rows, six themes, and the warm2 popup.
  Amber, warm2, and dark/warm2 menu previews were inspected in aspect-correct
  host geometry.
- Target build, dependency/notice checksum checks, ELF/loader/library closure
  audits, OPK checksum verification, full extraction (52 files), shell syntax,
  and `git diff --check` passed.
- Independent target trees `build/rg280v` and `build/repro-m2` produced identical
  packages; `cmp dist/epubreader-m2-0.2.1.opk dist/repro-m2-0.2.1.opk` passed.

Version `0.2.0` device acceptance is complete for the checks the user performed.
The new palettes and popup interaction have a short
[focused follow-up](DEVICE-TEST-M2-0.2.1.md). Malformed-book and prolonged-session
device checks remain deferred to ordinary-use feedback.

## Original reader slice — 0.2.0

### Delivered artifact

```text
dist/epubreader-m2-0.2.0.opk
5,177,344 bytes (4.94 MiB including SquashFS padding)
SHA256: e50c95aa3086b4a5e43d7f38080c01f82dc6a4d0f06b78883723c51fb1b72187
```

**Version 0.2.0 was accepted on RG280V / Adam v2.1.** All checks the user
performed passed, including rendering, font/anchor changes, cache reuse,
restart/reboot restoration, responsiveness, and suspend/resume. Malformed-book
and prolonged-session device checks are intentionally deferred to normal-use
usability feedback. The annotated results and warmer-palette note are preserved
in [DEVICE-TEST-M2.md](DEVICE-TEST-M2.md); the feedback build's focused follow-up
is [DEVICE-TEST-M2-0.2.1.md](DEVICE-TEST-M2-0.2.1.md).

### Completed host/build checks

| Check | Result |
|---|---|
| Host Debug build | Passed, GCC 15.2.1 |
| Host CTest suite | 4/4 passed: reader core/smoke and diagnostic core/smoke |
| ASan/UBSan build and suite (`UBSAN_OPTIONS=halt_on_error=1`) | 4/4 passed; final parser-error recovery additions also passed in both reader checks |
| MIPS target build | Passed with pinned GCC 11.1.0 / OpenDingux 2022-09-22 SDK |
| Independent build/package in `build/repro-m2` | Passed; byte-identical to the deliverable with `cmp` |
| SDK/font/engine/hash source checksum verification | Passed, including extracted reader-source comparison |
| Target ELF audit | ELF32 little-endian, MIPS32r2, o32, hard float, uClibc loader |
| Shared-library closure | 14 bundled target libraries; firmware supplies only loader/libc |
| SquashFS inspection/extraction | Passed, gzip / 128 KiB blocks, 52 files, fixed root ownership/timestamps/modes |
| Shell syntax and `git diff --check` | Passed |

The reader and xxHash are statically linked. The packager audits target shared
dependencies and verifies notice hashes before including fonts, reader CSS,
EPUB specimens, launcher/icon, dependency manifest, and the device checklist.
Both the existing incremental target tree and independently configured target
tree were rebuilt with the final sources before comparing packages.
Fixture regeneration was also checked byte-for-byte using the EPUB checksums.

### Behavioral coverage

The original EPUB 2 and EPUB 3 specimens each paginate to 82 medium-size pages
on this host. The checks cover:

- OPF discovery through `META-INF/container.xml`, title/author metadata, and
  spine order despite reversed manifest entries and nested resource paths.
- All 288 sentence markers and 300 long-paragraph word markers appearing once
  and in order across successive half-open text ranges; rendered pages are
  nonblank. Engine text extraction is bounded by serialized page anchors because
  its geometric `getPageText()` range can include the next page's first line.
- Actual green PNG and blue JPEG pixels in rendered page images, including
  portable RGB JPEG decoding used for both host and SDK libjpeg builds.
- Beginning/end and one-page clamping; neighboring page-image cache hits.
- Six repeated font changes preserving the original text anchor, returning to
  the original medium page, and distinct cache keys/counts for other sizes.
- Theme-only redraw preserving layout key, page count, and anchor; warm2 uses
  the intended dark foreground and does not recolor illustrations.
- Atomic position roundtrip, reopen, renamed content identity, changed files,
  and restoration in a fresh reader process.
- Missing/non-ZIP books, a malformed EPUB package without a spine, invalid
  supplied/stored XPointer bookmarks, and recovery by opening a valid book.
  Failed parser-generated error documents cannot be navigated/saved as books.
- Document cache hits after reopening, plus rebuilding interrupted cache files
  without losing the independently persisted text position.
- Bounded nonblocking IPC and worker stop during an active request, with a
  500 ms host assertion around termination/reaping. The child also terminates
  if its UI parent dies.

Reader smoke output consists of 10 native BMPs under
`build/host/reader-screenshots/`: three font presets, six palettes, and the
information overlay. Dark/medium and warm2 aspect-correct previews were inspected
visually. The existing 36 platform diagnostic images and input/audio/state tests
also pass.

An additional locally supplied real-world EPUB passed the reader smoke check
with an empty engine error log: 197 small, 266 medium, and 328 large pages.
The measured first medium-size open was 176 ms on this host. Its local outputs
are under `tmp/real-book-state/` and `tmp/real-book-screenshots/`. Packaging lists
the two original prose specimens explicitly, so local personal books do not
become OPK resources.

### Device acceptance

The user confirmed physical text quality, margins, page-turn/loading
responsiveness, exit during a large-book load, suspend/resume, reboot restoration,
and representative personal EPUBs on version `0.2.0`. The remaining focused
follow-up concerns warmer amber/warm2 and the new popup font controls in `0.2.1`.
The first milestone's accepted platform results remain in [DEVICE-TEST.md](DEVICE-TEST.md).
