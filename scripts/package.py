#!/usr/bin/env python3
"""Stage, audit, and deterministically package the pinned MIPS reader."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EPOCH = 1663804800
# uClibc-ng 1.0.38 merges pthread/libm/libdl into libc in the pinned SDK.
FIRMWARE = {"libc.so.0", "ld-uClibc.so.1"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, default=ROOT / "build/rg280v")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--diagnostic", action="store_true", help="Package the original platform diagnostic instead")
    args = parser.parse_args()
    version = re.search(r"project\(epubreader VERSION ([\d.]+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    package_name = f"epubreader-diagnostic-m1-{version}" if args.diagnostic else f"epubreader-m4-{version}"
    build, output = args.build.resolve(), (args.output or ROOT / "dist" / (package_name + ".opk")).resolve()
    # Only generated, project-local staging/output directories may be modified.
    build.relative_to(ROOT); output.relative_to(ROOT)
    sdk = ROOT / "third_party/sdk/gcw0-toolchain"
    sysroot = sdk / "mipsel-gcw0-linux-uclibc/sysroot"
    readelf = sdk / "bin/mipsel-linux-readelf"
    strip = sdk / "bin/mipsel-linux-strip"
    binary = build / ("epubreader-diagnostic" if args.diagnostic else "epubreader")
    header = subprocess.check_output([str(readelf), "-h", str(binary)], text=True)
    if not all(s in header for s in ("ELF32", "little endian", "mips32r2", "o32")):
        raise SystemExit("Refusing to package a non-MIPS32r2/o32 little-endian executable")
    interpreter = subprocess.check_output([str(readelf), "-l", str(binary)], text=True)
    if "/lib/ld-uClibc.so.0" not in interpreter:
        raise SystemExit("Unexpected ELF interpreter")
    version = subprocess.check_output(["mksquashfs", "-version"], text=True)
    if "version 4.6.1" not in version:
        raise SystemExit("Reproducible packaging requires squashfs-tools 4.6.1")
    stage = build / "opk-stage"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    lib = stage / "lib"; lib.mkdir()
    shutil.copyfile(binary, stage / binary.name)
    subprocess.run([str(strip), "--strip-unneeded", str(stage / binary.name)], check=True)
    records = {}
    firmware = set()
    pending = [stage / binary.name]
    def needed(path):
        return re.findall(r"NEEDED.*\[(.*?)\]", subprocess.check_output([str(readelf), "-d", str(path)], text=True))
    while pending:
        current = pending.pop()
        for name in needed(current):
            if name in FIRMWARE:
                firmware.add(name); continue
            if name in records:
                continue
            sources = [p / name for p in (sysroot / "usr/lib", sysroot / "lib") if (p / name).exists()]
            if not sources:
                raise SystemExit(f"Unresolved target library: {name}")
            source = sources[0].resolve()
            source.relative_to(sysroot)
            target = lib / name
            shutil.copyfile(source, target)
            subprocess.run([str(strip), "--strip-unneeded", str(target)], check=True)
            records[name] = {"sdk_file": str(source.relative_to(sysroot)), "sha256": digest(target), "needed": needed(target)}
            pending.append(target)
    (stage / "runtime-libraries.json").write_text(json.dumps({"schema": 1, "firmware_provided": sorted(firmware), "bundled": records}, indent=2, sort_keys=True) + "\n")
    # SDL dynamically loads ALSA by SONAME even when absent from DT_NEEDED;
    # libasound is an explicit application dependency, so the closure contains it.
    # Package the original fixtures explicitly. Personal EPUBs placed beside
    # them for local testing are not distributable application resources.
    shutil.copytree(ROOT / "assets", stage / "assets", ignore=shutil.ignore_patterns("books"))
    (stage / "assets/books").mkdir()
    for name in ("prose-epub2.epub", "prose-epub3.epub"):
        shutil.copyfile(ROOT / "assets/books" / name, stage / "assets/books" / name)
    notices = ROOT / "third_party/notices"
    notice_lock = json.loads((notices / "sources.json").read_text())
    for name, spec in notice_lock.items():
        if digest(notices / name) != spec["sha256"]:
            raise SystemExit(f"License notice hash mismatch: {name}")
    shutil.copytree(notices, stage / "licenses")
    desktop = "epubreader-diagnostic.gcw0.desktop" if args.diagnostic else "epubreader.gcw0.desktop"
    launcher = "launch.sh" if args.diagnostic else "launch-reader.sh"
    checklist = "DEVICE-TEST-0.1.1.md" if args.diagnostic else "DEVICE-TEST-M4.md"
    for source, target in (("LICENSE.md", "LICENSE.md"), ("third_party/README.md", "licenses/DEPENDENCIES.md"), ("third_party/manifest.json", "dependencies.json"), ("docs/" + checklist, "DEVICE-TEST.txt"), ("packaging/" + desktop, desktop), ("packaging/" + launcher, "launch.sh"), ("packaging/epubreader.png", "epubreader.png")):
        shutil.copyfile(ROOT / source, stage / target)
    if not args.diagnostic:
        shutil.copyfile(ROOT / "docs/CONTROLS.md", stage / "CONTROLS.txt")
    for path in sorted(stage.rglob("*")) + [stage]:
        executable = path.is_dir() or path.name in ("launch.sh", binary.name)
        path.chmod(0o755 if executable else 0o644)
        os.utime(path, (EPOCH, EPOCH))
    output.parent.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, "LC_ALL": "C"}
    # 4.6.1 rejects SOURCE_DATE_EPOCH together with explicit timestamp flags.
    env.pop("SOURCE_DATE_EPOCH", None)
    subprocess.run(["mksquashfs", str(stage), str(output), "-noappend", "-comp", "gzip", "-b", "131072", "-all-root", "-no-xattrs", "-no-exports", "-processors", "1", "-mkfs-time", str(EPOCH), "-all-time", str(EPOCH)], check=True, env=env)
    checksum = digest(output)
    Path(str(output) + ".sha256").write_text(f"{checksum}  {output.name}\n")
    print(f"OPK ready: {output}\nSHA256 {checksum}\nBundled {len(records)} target libraries; firmware provides {sorted(firmware)}")


if __name__ == "__main__":
    main()
