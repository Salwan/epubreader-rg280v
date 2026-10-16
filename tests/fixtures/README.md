# Original EPUB specimens

`python3 scripts/generate-epub-fixtures.py` reproducibly creates:

- `assets/books/prose-epub2.epub`: EPUB 2 with NCX navigation.
- `assets/books/prose-epub3.epub`: EPUB 3 with an XHTML navigation document.
- `tests/fixtures/one-page.epub`: a short EPUB 3 boundary specimen.
- `tests/fixtures/no-spine.epub`: a valid ZIP/container with a malformed package
  for post-detection error recovery.

The two longer books contain three spine chapters, deliberately reversed
manifest order, an OPF below `Books/package/`, nested XHTML paths, relative PNG
and JPEG resources, English UTF-8 punctuation, all four font styles, lists,
blockquotes, an explicit break, and paragraphs that cross page boundaries.
`S0000`–`S0287` and `W0000`–`W0299` identify text for automated continuity checks.
Everything is original project material under the application license.

The PNG is generated using Python's standard library. `blue-square.jpg` is a
checked-in original 96×96 blue image; fixture generation reuses its exact bytes.
Its SHA256 is
`862361efcf0d90f0687bed60cd9bb413fb169fbb864287a0dd7a3316ff36a91b`.
It was created with:

```sh
ffmpeg -f lavfi -i color=c=0x2850b4:s=96x96 -frames:v 1 -q:v 2 tests/fixtures/blue-square.jpg
```

The test suite also creates missing/malformed books, changed/renamed copies,
invalid bookmarks, and interrupted document caches in its isolated build tree.

## Original music fixtures

`python3 scripts/generate-audio-fixtures.py` (ffmpeg/libmp3lame) creates three
quiet 0.30-second sine/fade MP3s: `short-stereo.mp3` (44100 Hz stereo),
`short-mono.mp3` (22050 Hz mono), and `short-48k.mp3` (48000 Hz stereo).
They are original GPL-2.0-or-later project material. Checked-in bytes avoid a
test/build dependency on the encoder; a different ffmpeg version may change them.
They exercise real output conversion, EOF drain, and automatic shuffle cycles.
The OPK does not include these test fixtures.
