#!/usr/bin/env python3
"""Refresh vendored upstream notices and record their URL and content digest.

Not part of the normal build: checked-in notices make packaging offline.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    "GPL-2.0.txt": "https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-11.1.0/COPYING",
    "GPL-3.0.txt": "https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-11.1.0/COPYING3",
    "GCC-Runtime-Exception.txt": "https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-11.1.0/COPYING.RUNTIME",
    "SDL-COPYING.txt": "https://raw.githubusercontent.com/OpenDingux/SDL/97557d6bd/COPYING",
    "SDL_ttf-COPYING.txt": "https://raw.githubusercontent.com/libsdl-org/SDL_ttf/70b2940cc75e92aab02a67d2f827caf2836a2c74/COPYING",
    "alsa-lib-COPYING.txt": "https://raw.githubusercontent.com/alsa-project/alsa-lib/v1.2.5.1/COPYING",
    "FreeType-FTL.txt": "https://raw.githubusercontent.com/freetype/freetype/VER-2-10-4/docs/FTL.TXT",
    "libiconv-COPYING.LIB.txt": "https://raw.githubusercontent.com/arthenica/libiconv/v1.15/COPYING.LIB",
    "eudev-COPYING.txt": "https://raw.githubusercontent.com/eudev-project/eudev/v3.2.10/COPYING",
    "tslib-COPYING.txt": "https://raw.githubusercontent.com/libts/tslib/1.22/COPYING",
    "libpng-LICENSE.txt": "https://raw.githubusercontent.com/pnggroup/libpng/v1.6.37/LICENSE",
    "zlib-LICENSE.txt": "https://raw.githubusercontent.com/madler/zlib/v1.2.11/README",
}


def main():
    directory = ROOT / "third_party/notices"
    directory.mkdir(parents=True, exist_ok=True)
    records = {}
    for name, url in SOURCES.items():
        subprocess.run(["curl", "-fL", "--retry", "2", "-o", str(directory / name), url], check=True)
        records[name] = {"url": url, "sha256": hashlib.sha256((directory / name).read_bytes()).hexdigest()}
    for name, url, member in (
        ("mpg123-COPYING.txt", "https://sources.buildroot.net/mpg123/mpg123-1.25.15.tar.bz2", "mpg123-1.25.15/COPYING"),
        ("libdrm-MIT.txt", "https://dri.freedesktop.org/libdrm/libdrm-2.4.107.tar.xz", "libdrm-2.4.107/xf86drm.c"),
    ):
        archive = ROOT / "third_party/downloads" / url.rsplit("/", 1)[-1]
        archive.parent.mkdir(parents=True, exist_ok=True)
        if not archive.exists():
            subprocess.run(["curl", "-fL", "--retry", "2", "-o", str(archive), url], check=True)
        with tarfile.open(archive) as tar:
            data = tar.extractfile(member).read()
        if name == "libdrm-MIT.txt":
            comments = re.findall(rb"/\*.*?\*/", data, re.DOTALL)
            data = b"\n\n".join(c for c in comments if b"Permission is hereby granted" in c) + b"\n"
            if b"Copyright" not in data:
                raise SystemExit("libdrm license extraction failed")
        (directory / name).write_bytes(data)
        records[name] = {"url": url, "member": member, "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(), "sha256": hashlib.sha256(data).hexdigest()}
    (directory / "sources.json").write_text(json.dumps(records, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
