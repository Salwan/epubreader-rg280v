#!/usr/bin/env python3
"""Generate original short MP3s for sample conversion and shuffle integration."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
for name, rate, channels, frequency in (
    ("short-stereo", 44100, 2, 330),
    ("short-mono", 22050, 1, 440),
    ("short-48k", 48000, 2, 550),
):
    subprocess.run([
        "ffmpeg", "-nostdin", "-y", "-hide_banner", "-loglevel", "error",
        "-f", "lavfi", "-i", f"sine=frequency={frequency}:sample_rate={rate}:duration=0.30",
        "-af", "afade=t=in:d=0.02,afade=t=out:st=0.25:d=0.05,volume=0.2",
        "-ac", str(channels), "-map_metadata", "-1", "-codec:a", "libmp3lame",
        "-b:a", "64k", "-write_xing", "0", "-id3v2_version", "0",
        str(ROOT / "tests/fixtures" / (name + ".mp3")),
    ], check=True)
