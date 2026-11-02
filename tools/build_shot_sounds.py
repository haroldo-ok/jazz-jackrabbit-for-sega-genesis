#!/usr/bin/env python3
"""Extract shareware weapon clips at the original level sound-slot rates.

Usage: python3 tools/build_shot_sounds.py --input /path/to/jj1 --output res/jj1
The WAVs retain original source PCM. SGDK rescomp converts XGM WAVs to 14 kHz.
"""
import argparse
import struct
import wave
from pathlib import Path

SHOTS = (("MACHGUN1", 13000, "fire_blaster.wav"),
         ("FLAMER", 10000, "fire_toaster.wav"),
         ("PHOTON", 10000, "fire_upward.wav"),
         ("ROCKET2", 13000, "fire_bouncer.wav"))


def read_archive(path):
    data = path.read_bytes()
    if not data.startswith(b"sf") or data[3] != 0x1a:
        raise ValueError("not a JJ1 sounds archive")
    directory = struct.unpack_from("<I", data, len(data) - 4)[0]
    result = {}
    for pos in range(directory, len(data) - 17, 18):
        name = data[pos:pos + 12].split(b"\0")[0].decode("ascii").strip()
        off, length = struct.unpack_from("<IH", data, pos + 12)
        if off + length > directory:
            raise ValueError(f"invalid sound {name}")
        result[name] = data[off:off + length]
    return result


def generate(data_dir, output):
    clips = read_archive(data_dir / "SOUNDS.000")
    output.mkdir(parents=True, exist_ok=True)
    for name, rate, filename in SHOTS:
        with wave.open(str(output / filename), "wb") as w:
            w.setnchannels(1); w.setsampwidth(1); w.setframerate(rate)
            w.writeframes(bytes((v + 128) & 255 for v in clips[name]))

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--output", type=Path, default=Path("res/jj1"))
    args = p.parse_args()
    generate(args.input, args.output)
