#!/usr/bin/env python3
"""Generate deterministic, original EPUB 2/3 prose fixtures (no network/tools)."""
from pathlib import Path
import html
import struct
import zlib
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def png():
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    pixels = b"".join(b"\0" + b"".join(bytes((50, 140, 110) if 12 < x < 84 and 12 < y < 84 else (235, 218, 185)) for x in range(96)) for y in range(96))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 96, 96, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(pixels, 9)) + chunk(b"IEND", b"")


def generate(destination, version, short=False):
    title = "One-page specimen" if short else f"EPUB {version} Prose Specimen"
    chapters = []
    for chapter in range(1 if short else 3):
        body = f"<h1>Chapter {chapter + 1}</h1>"
        if chapter == 0:
            body += '<p>“Café,” she said—naïve, but hopeful… <em>Italic emphasis</em>, <strong>bold emphasis</strong>, and <strong><em>both together</em></strong>.</p>'
            if not short:
                body += '<p><img src="../../images/square.png" alt="A green square" /></p><p><img src="../../images/blue-square.jpg" alt="A blue JPEG square" /></p><blockquote><p>A quiet blockquote, with a useful line break.<br />Second line.</p></blockquote><ul><li>First list item</li><li>Second list item</li></ul>'
        if not short:
            for paragraph in range(24):
                body += f'<p id="p{paragraph}">' + " ".join(f'S{chapter * 96 + paragraph * 4 + sentence:04d} The traveller watched the river from the old stone bridge, and wondered where the winding path would lead next.' for sentence in range(4)) + '</p>'
            body += '<p>' + " ".join(f'W{chapter * 100 + word:04d}' for word in range(100)) + '</p>'
        body += f'<p>ENDCHAPTER{chapter + 1}</p>'
        chapters.append('<?xml version="1.0" encoding="UTF-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>' + html.escape(title) + '</title><link rel="stylesheet" href="../../styles/book.css" /></head><body>' + body + '</body></html>')
    items = [f'<item id="c{i}" href="../text/nested/chapter-{i}.xhtml" media-type="application/xhtml+xml"/>' for i in reversed(range(len(chapters)))]
    nav = '<nav xmlns:epub="http://www.idpf.org/2007/ops" epub:type="toc"><ol>' + ''.join(f'<li><a href="text/nested/chapter-{i}.xhtml">Chapter {i+1}</a></li>' for i in range(len(chapters))) + '</ol></nav>'
    items += ['<item id="css" href="../styles/book.css" media-type="text/css"/>', '<item id="img" href="../images/square.png" media-type="image/png"/>', '<item id="jpeg" href="../images/blue-square.jpg" media-type="image/jpeg"/>']
    if version == 3:
        items += ['<item id="nav" href="../nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>']
    else:
        items += ['<item id="ncx" href="../toc.ncx" media-type="application/x-dtbncx+xml"/>']
    modified = '<meta property="dcterms:modified">2022-09-22T00:00:00Z</meta>' if version == 3 else '<meta name="generator" content="RG280V Reader Project"/>'
    toc = ' toc="ncx"' if version == 2 else ''
    opf = f'''<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="{version}.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">original-rg280v-{version}-{'short' if short else 'prose'}</dc:identifier><dc:title>{title}</dc:title><dc:creator>RG280V Reader Project</dc:creator><dc:language>en</dc:language>{modified}</metadata><manifest>{''.join(items)}</manifest><spine{toc}>{''.join(f'<itemref idref="c{i}"/>' for i in range(len(chapters)))}</spine></package>'''
    files = {
        "mimetype": b"application/epub+zip",
        "META-INF/container.xml": b'<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="Books/package/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
        "Books/package/content.opf": opf.encode(),
        "Books/styles/book.css": b'body { font-size: 8pt; color: red; background: white; } p { font-family: sans-serif; }',
        "Books/images/square.png": png(),
        "Books/images/blue-square.jpg": (ROOT / "tests/fixtures/blue-square.jpg").read_bytes(),
    }
    for i, chapter in enumerate(chapters):
        files[f"Books/text/nested/chapter-{i}.xhtml"] = chapter.encode()
    if version == 3:
        files["Books/nav.xhtml"] = ('<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Contents</title></head><body>' + nav + '</body></html>').encode()
    else:
        points = ''.join(f'<navPoint id="c{i}" playOrder="{i+1}"><navLabel><text>Chapter {i+1}</text></navLabel><content src="text/nested/chapter-{i}.xhtml"/></navPoint>' for i in range(len(chapters)))
        files["Books/toc.ncx"] = (f'<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head><meta name="dtb:uid" content="original-rg280v-2-prose"/></head><docTitle><text>{title}</text></docTitle><navMap>{points}</navMap></ncx>').encode()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w") as archive:
        for name, data in files.items():
            info = zipfile.ZipInfo(name, (2022, 9, 22, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED if name == "mimetype" else zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data, compresslevel=9)


if __name__ == "__main__":
    generate(ROOT / "assets/books/prose-epub2.epub", 2)
    generate(ROOT / "assets/books/prose-epub3.epub", 3)
    generate(ROOT / "tests/fixtures/one-page.epub", 3, short=True)
    # Valid EPUB container with an invalid package: exercises parser failure
    # after format detection, rather than only rejection of a non-ZIP file.
    with zipfile.ZipFile(ROOT / "tests/fixtures/one-page.epub") as source:
        with zipfile.ZipFile(ROOT / "tests/fixtures/no-spine.epub", "w") as broken:
            for info in source.infolist():
                data = source.read(info.filename)
                if info.filename == "Books/package/content.opf":
                    data = data.replace(b'<spine><itemref idref="c0"/></spine>', b'<spine/>')
                broken.writestr(info, data, compresslevel=9)
