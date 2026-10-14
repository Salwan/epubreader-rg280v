#!/usr/bin/env python3
"""Fetch checksum-pinned SDK, fonts, and reader sources locally."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / "third_party/manifest.json").read_text())


def fetch(spec):
    downloads = ROOT / "third_party/downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / spec["archive"]
    # Reuse a previously downloaded, checksum-verified archive.
    local = ROOT / "tmp" / spec["archive"]
    if not archive.exists() and local.exists():
        shutil.copyfile(local, archive)
    if not archive.exists():
        subprocess.run(["curl", "-fL", "--retry", "3", "-o", str(archive) + ".part", spec["url"]], check=True)
        Path(str(archive) + ".part").rename(archive)
    actual = hashlib.sha256(archive.read_bytes()).hexdigest()
    if actual != spec["sha256"]:
        raise SystemExit(f"Checksum mismatch: {archive}: {actual}")
    print(f"Verified {archive.name}: {actual}")
    return archive


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fonts-only", action="store_true")
    parser.add_argument("--host", action="store_true", help="Fonts and reader sources, without the device SDK")
    args = parser.parse_args()
    if not args.fonts_only and not args.host:
        archive = fetch(MANIFEST["sdk"])
        sdk_parent = ROOT / "third_party/sdk"
        sdk_parent.mkdir(parents=True, exist_ok=True)
        sdk = sdk_parent / "gcw0-toolchain"
        if not sdk.exists():
            subprocess.run(["tar", "-xf", str(archive), "-C", str(sdk_parent)], check=True)
        subprocess.run([str(sdk / "relocate-sdk.sh")], check=True)
        subprocess.run([str(sdk / "bin/mipsel-linux-g++"), "--version"], check=True)
    archive = fetch(MANIFEST["fonts"])
    fonts = ROOT / "assets/fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        for face in MANIFEST["fonts"]["faces"]:
            data = tar.extractfile(f"dejavu-fonts-ttf-2.37/ttf/{face}").read()
            (fonts / face).write_bytes(data)
        license_data = tar.extractfile("dejavu-fonts-ttf-2.37/LICENSE").read()
        (fonts / "LICENSE-DejaVu.txt").write_bytes(license_data)
    if not args.fonts_only:
        for name, spec in MANIFEST["sources"].items():
            archive = fetch(spec)
            destination = ROOT / "third_party/src" / name
            extract = not destination.exists()
            if extract:
                destination.mkdir(parents=True)
            with tarfile.open(archive) as tar:
                for member in tar.getmembers():
                    relative = Path(member.name).relative_to(spec["prefix"])
                    if ".." in relative.parts or member.issym() or member.islnk():
                        raise SystemExit(f"Unsafe source archive member: {member.name}")
                    if extract:
                        member.name = str(relative)
                        tar.extract(member, destination, filter="data")
                    elif member.isfile():
                        target = destination / relative
                        if not target.is_file() or target.read_bytes() != tar.extractfile(member).read():
                            raise SystemExit(f"Pinned source differs or extraction is incomplete: {target}. Restore/re-extract it before building.")
            print(f"Verified extracted {name} sources")


if __name__ == "__main__":
    main()
