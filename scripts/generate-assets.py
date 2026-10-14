#!/usr/bin/env python3
"""Regenerate original diagnostic icon and quiet stereo MP3 specimen."""
import math
from pathlib import Path
import struct
import subprocess
import wave
import zlib

ROOT = Path(__file__).resolve().parents[1]


def main():
    audio = ROOT / "assets/audio"
    audio.mkdir(parents=True, exist_ok=True)
    tmp = ROOT / "tmp"
    tmp.mkdir(exist_ok=True)
    path = tmp / "test-tone.wav"
    rate, seconds = 44100, 12
    with wave.open(str(path), "wb") as wav:
        wav.setparams((2, 2, rate, 0, "NONE", "not compressed"))
        data = bytearray()
        for i in range(rate * seconds):
            t = i / rate
            fade = min(1, t / 0.15, (seconds - t) / 0.15)
            gate = min(1, (t % 1.5) / 0.08, (1.5 - t % 1.5) / 0.12)
            note = [261.6256, 329.6276, 391.9954, 523.2511][int(t / 1.5) % 4]
            left = 2600 * fade * gate * math.sin(2 * math.pi * note * t)
            right = 2600 * fade * gate * math.sin(2 * math.pi * note * 1.5 * t)
            data += struct.pack("<hh", int(left), int(right))
        wav.writeframes(data)
    subprocess.run(["ffmpeg", "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-i", str(path), "-map_metadata", "-1", "-codec:a", "libmp3lame", "-b:a", "128k", "-write_xing", "0", "-id3v2_version", "0", str(audio / "test-tone.mp3")], check=True)
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    pixels = bytearray()
    for y in range(32):
        pixels.append(0)
        for x in range(32):
            c = (16, 20, 25)
            if 5 <= x <= 26 and 5 <= y <= 26: c = (146, 182, 176)
            if x in (15, 16) and 7 <= y <= 24: c = (16, 20, 25)
            if x in range(7, 14) or x in range(18, 25):
                if y in (10, 14, 18, 22): c = (48, 73, 73)
            pixels += bytes(c)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 32, 32, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(pixels, 9)) + chunk(b"IEND", b"")
    (ROOT / "packaging/epubreader.png").write_bytes(png)


if __name__ == "__main__":
    main()
