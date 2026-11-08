#!/usr/bin/env python3
"""Convert the user-supplied JJ1 music to XGM using all four PCM channels.

Every Jazz Jackrabbit module (PSM) has four channels; the XGM driver mixes
four PCM channels next to five FM channels and the PSG.  Rendering the whole
modules as PCM needs 2.3-3.3 MB of samples (XGM plays PCM at a fixed 14 kHz
with no pitch or volume control, so every pitch/volume is a separate
rendering), far more than the ROM has room for.  So the music keeps its FM/PSG
arrangement for the melodic parts (res/music/<track>.vgm, the existing
conversion) and moves every percussion and one-shot instrument to PCM, played
from the module's own samples:

* each module channel gets its own PCM voice, so hits that overlap in the
  original (kick under a cymbal, drums on two channels) no longer cut each
  other off as they did on the single DAC stream;
* hi-hats, shakers and cymbals become their real samples instead of PSG noise.

Notes of the PCM instruments are grouped by sample, pitch, pitch slide and
volume into at most 63 renderings (the XGM sample table) and rendered
band-limited at 14 kHz.  The FM/PSG part is the old VGM with its PCM stream and
PSG noise removed, converted by xgmtool; the PCM commands are then added to
its frames.  The XGM is written directly because xgmtool keeps only one PCM
start per frame when reading a VGM (a real VGM has a single DAC).

The busiest drum channel plays on PCM channel 1, which the game's sound
effects never use; effects borrow channels 2-4 (priority >= 8 in
jj1_sfx_start) and the drum voice returns with its next hit.

Usage: build_music.py --input <dir with the .PSM files> [--music res/music]
"""
from __future__ import annotations

import argparse
import collections
import math
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jj1_psm  # noqa: E402

RATE = 14000                 # XGM v1 PCM rate
FPS = 60                     # XGM v1 frame rate (NTSC)
MAX_SAMPLES = 63             # XGM sample id table
TRACKS = {                   # output name -> module
    "menu": "MENUSNG.PSM",
    "bonus": "BONUS.PSM",
    "endlevel": "ENDLEVEL.PSM",
    "diamondus": "SONG0.PSM",
    "tubelectric": "SONG3.PSM",
    "medivo": "SONG17.PSM",
}
# Instruments played as PCM: everything the old conversion had to put on its
# single PCM stream or on PSG noise (drums, cymbals, hats, shakers, one-shot
# hits), plus the menu's kick/hat sample that had been approximated on FM.
PCM_INSTRUMENTS = {
    "menu": {"BDDRM.ST", "gmbdwhh.st", "gmsnare.st", "CLCCHH.ST", "dryfctom.st", "VSRIDE.NEW"},
    "bonus": {"drum-ben.st", "FOGSD01.NEW", "CLCCHH.ST"},
    "endlevel": {"BDDRM.ST", "disthit.st", "FLTROPEN.ST"},
    "diamondus": {"bassdrum.st", "janetsna.st", "SIMMSTOM.ST", "HiHatRev.st"},
    "tubelectric": {"BDDRM.ST", "shaker.st", "SNAREVER.ST", "REALCYMC.ST"},
    "medivo": {"bassd2.st", "SNAREV2.st", "OPCCHH.ST", "CLCCHH.ST", "ohme.st", "Lorchit2.st"},
}
# XGM channel for each drum channel rank (0 = busiest); the effects use
# channel 3 (jump) the most, then 2 (shots), then 4 (pickups).
RANK_TO_CHANNEL = [0, 1, 3, 2]


def semis(f, f0):
    return 12.0 * math.log2(f / f0) if f > 0 and f0 > 0 else 0.0


def trim(seq):
    seq = list(seq)
    while len(seq) > 1 and seq[-1] == seq[-2]:
        seq.pop()
    return tuple(seq)


class Group:
    """Notes sharing one rendering."""

    def __init__(self, key, insts):
        self.key, self.insts = key, insts

    def vol0(self):
        return [i["env"][0][1] if i["env"] else 0 for i in self.insts]

    def error(self):
        v = self.vol0()
        med = sorted(v)[len(v) // 2]
        return sum(abs(x - med) * (i["end"] - i["start"]) for x, i in zip(v, self.insts))

    def split(self):
        """Two groups by first-tick volume, at the cut that helps most."""
        v = sorted(set(self.vol0()))
        best = None
        for cut in v[1:]:
            a = Group(self.key, [i for i in self.insts if i["env"][0][1] < cut])
            b = Group(self.key, [i for i in self.insts if i["env"][0][1] >= cut])
            e = a.error() + b.error()
            if best is None or e < best[0]:
                best = (e, a, b)
        return best

    def representative(self):
        """Envelope of the loudest-typical note, as long as the longest one."""
        v = self.vol0()
        med = sorted(v)[len(v) // 2]
        rep = min(self.insts, key=lambda i: (abs(i["env"][0][1] - med), -(i["end"] - i["start"])))
        length = max(i["end"] - i["start"] for i in self.insts)
        return rep, length


def plan_groups(notes):
    """Group notes into at most MAX_SAMPLES renderings."""
    for n in notes:
        f0 = n["env"][0][0] if n["env"] else 0
        n["pitchsig"] = trim(round(semis(f, f0) * 8) for f, _ in n["env"]) if f0 else (0,)
    by = collections.defaultdict(list)
    for n in notes:
        by[(n["smp"], n["note"], n["pitchsig"])].append(n)
    # too many slide shapes: play the rarest ones at a steady pitch
    while len(by) > MAX_SAMPLES:
        slides = [k for k in by if len(k[2]) > 1]
        if not slides:
            raise SystemExit("more than %d distinct sample/pitch pairs" % MAX_SAMPLES)
        k = min(slides, key=lambda k: len(by[k]))
        steady = (k[0], k[1], (0,))
        for n in by.pop(k):
            n["env"] = [(n["env"][0][0], v) for _, v in n["env"]]
            by[steady].append(n)
    groups = [Group(k, v) for k, v in by.items()]
    # spare ids: separate the volume levels that are furthest from their group
    while len(groups) < MAX_SAMPLES:
        cand = [(g.error() - s[0], g, s) for g in groups for s in [g.split()] if s]
        if not cand:
            break
        gain, g, (_, a, b) = max(cand, key=lambda c: c[0])
        if gain <= 0:
            break
        groups.remove(g)
        groups += [a, b]
    return groups


def source_array(smp, needed):
    """Sample data, with the loop unrolled far enough for `needed` frames."""
    d = np.asarray(smp["data"], dtype=np.float64)
    if smp["loop"] and smp["le"] > smp["ls"] + 1:
        body, loop = d[:smp["le"]], d[smp["ls"]:smp["le"]]
        reps = int(max(0, needed - len(body)) // len(loop)) + 2
        d = np.concatenate([body] + [loop] * reps)
    return d


LOOP_CAP = 4.0               # seconds a looped (sustained) sample may ring
FADE = 0.25                  # fade-out when a sustain is cut at LOOP_CAP


def render(rep, length, smp, tick_len, loop_cap=None):
    """Band-limited render of one note at RATE for `length` seconds, cut
    where it has gone silent; sustained notes stop at the cap with a fade."""
    env = rep["env"]
    if loop_cap is None:
        loop_cap = LOOP_CAP
    if smp["loop"] and length > loop_cap:
        length = loop_cap
    if not env or env[0][0] <= 0:
        return np.zeros(0)
    nout = int(math.ceil(length * RATE))
    tau = np.arange(nout) / RATE
    k = np.minimum((tau / tick_len).astype(np.int64), len(env) - 1)
    freqs = np.array([f for f, _ in env]); vols = np.array([v for _, v in env]) / 64.0
    start = np.concatenate([[0.0], np.cumsum(freqs * tick_len)])[:len(env)]
    pos = start[k] + freqs[k] * (tau - k * tick_len)
    src = source_array(smp, pos[-1] + 16 if nout else 0)
    if not smp["loop"]:
        keep = pos < len(src) - 1
        pos, k = pos[keep], k[keep]
    fc = min(1.0, RATE / env[0][0]) * 0.9          # anti-alias cutoff
    taps = 12
    base = np.floor(pos).astype(np.int64)
    frac = pos - base
    out = np.zeros(len(pos))
    for j in range(-taps + 1, taps + 1):
        idx = base + j
        x = frac - j
        w = np.sinc(fc * x) * fc * (0.5 + 0.5 * np.cos(np.pi * np.clip(x / taps, -1, 1)))
        valid = (idx >= 0) & (idx < len(src))
        out += np.where(valid, src[np.clip(idx, 0, len(src) - 1)], 0.0) * w
    out = out * vols[k]
    if smp["loop"] and len(out) >= int(loop_cap * RATE) - 1:
        nf = int(FADE * RATE)
        out[-nf:] *= np.linspace(1.0, 0.0, nf)
    loud = np.nonzero(np.abs(out) > 0.75)[0]        # below 1 LSB at gain >= 0.5
    return out[:loud[-1] + 1] if len(loud) else out[:0]


def strip_vgm(vgm: bytes) -> bytes:
    """The VGM without its PCM streams and with the PSG noise silenced."""
    data_off = struct.unpack_from("<I", vgm, 0x34)[0] + 0x34
    loop_rel = struct.unpack_from("<I", vgm, 0x1C)[0]
    loop_abs = loop_rel + 0x1C if loop_rel else None
    out = bytearray(vgm[:data_off])
    out += bytes([0x50, 0xFF])                 # noise channel silent
    new_loop = None
    i = data_off
    latched = 0
    sizes = {0x4F: 2, 0x50: 2, 0x52: 3, 0x53: 3, 0x61: 3, 0x62: 1, 0x63: 1, 0xE0: 5,
             0x90: 5, 0x91: 5, 0x92: 6, 0x93: 11, 0x94: 2, 0x95: 5}
    while i < len(vgm):
        if loop_abs is not None and i == loop_abs:
            new_loop = len(out)
        c = vgm[i]
        if c == 0x66:
            out.append(0x66)
            break
        if c == 0x67:
            # data blocks stay: xgmtool crashes on a VGM without them (the
            # samples are not referenced any more and are not used)
            n = 7 + (struct.unpack_from("<I", vgm, i + 3)[0] & 0x7FFFFFFF)
            out += vgm[i:i + n]
            i += n
            continue
        if 0x70 <= c <= 0x8F:
            # 0x8n: DAC write from the data block + wait n: keep the wait
            out.append(c if c < 0x80 else (0x70 + (c & 15) - 1 if c & 15 else None) or 0)
            if c >= 0x80 and not (c & 15):
                out.pop()
            i += 1
            continue
        n = sizes.get(c)
        if n is None:
            raise SystemExit("unexpected VGM command %02X" % c)
        if 0x90 <= c <= 0x95:
            i += n
            continue
        if c == 0x50:
            v = vgm[i + 1]
            if v & 0x80:
                latched = (v >> 5) & 3
                if latched == 3 and (v & 0x10):
                    # noise volume -> silent (dropping the writes crashes xgmtool)
                    out += bytes([0x50, 0xFF])
                    i += n
                    continue
        out += vgm[i:i + n]
        i += n
    struct.pack_into("<I", out, 0x04, len(out) - 0x04)
    struct.pack_into("<I", out, 0x1C, (new_loop - 0x1C) if new_loop is not None else 0)
    return bytes(out)


XGM_SIZES = {0x00: 0, 0x10: None, 0x20: None, 0x30: None, 0x40: None, 0x50: 1, 0x7E: 3, 0x7F: 0}


def xgm_frames(xgm: bytes):
    """Frames of an XGM's music data (commands without the frame end), the
    loop target as (frame, byte within frame) and whether it loops."""
    start = (struct.unpack_from("<H", xgm, 0x100)[0] << 8) + 0x104
    length = struct.unpack_from("<I", xgm, start)[0]
    music = xgm[start + 4:start + 4 + length]
    frames, cur, offsets = [], bytearray(), {}
    loop = None
    i = 0
    while i < len(music):
        c = music[i]
        offsets[i] = (len(frames), len(cur))
        kind = c & 0xF0 if c < 0x7E else c
        if c == 0x00:
            frames.append(bytes(cur)); cur = bytearray(); i += 1; continue
        if c == 0x7E:
            loop = music[i + 1] | (music[i + 2] << 8) | (music[i + 3] << 16); i += 4; continue
        if c == 0x7F:
            break
        if kind == 0x10 or kind == 0x40:
            n = (c & 15) + 1
        elif kind in (0x20, 0x30):
            n = 2 * ((c & 15) + 1)
        elif kind == 0x50:
            n = 1
        else:
            raise SystemExit("unexpected XGM command %02X" % c)
        cur += music[i:i + 1 + n]
        i += 1 + n
    open_end = bool(cur)            # xgmtool loops from inside the last frame
    if cur:
        frames.append(bytes(cur))
    target = offsets.get(loop) if loop is not None else None
    return frames, target, open_end


def fm_part(vgm_path: Path, xgmtool: Path, work: Path):
    stripped = work / (vgm_path.stem + "_fm.vgm")
    stripped.write_bytes(strip_vgm(vgm_path.read_bytes()))
    out = work / (vgm_path.stem + "_fm.xgm")
    subprocess.run([str(xgmtool), stripped.name, out.name, "-s"], cwd=work, check=True,
                   stdout=subprocess.DEVNULL)
    return xgm_frames(out.read_bytes())


def convert(name: str, path: Path, vgm_path: Path, out_path: Path, xgmtool: Path, work: Path):
    m, song, notes, total, loop_time, tick_len = jj1_psm.play(str(path))
    wanted = PCM_INSTRUMENTS[name]
    notes = [n for n in notes if n["env"] and n["env"][0][0] > 0 and m.samples[n["smp"]]["name"] in wanted]
    # drum channel -> XGM channel by number of hits
    hits = collections.Counter(n["ch"] for n in notes)
    rank = [c for c, _ in hits.most_common()]
    chan_of = {c: RANK_TO_CHANNEL[r] for r, c in enumerate(rank)}

    groups = plan_groups(notes)
    renders = []
    for g in groups:
        rep, length = g.representative()
        renders.append(render(rep, length, m.samples[g.key[0]], tick_len))
        for n in g.insts:
            n["gid"] = len(renders) - 1

    # gain: the four drum voices summed, rare peaks may saturate; never
    # louder than a single voice at full scale
    total_n = int(math.ceil(total * RATE)) + 1
    mix = np.zeros(total_n)
    single = 0.0
    for n in notes:
        r = renders[n["gid"]]
        a = int(round(n["start"] * RATE)); b = min(int(round(n["end"] * RATE)), a + len(r), total_n)
        if b > a:
            mix[a:b] += r[:b - a]
            single = max(single, float(np.abs(r[:b - a]).max()))
    peak = np.percentile(np.abs(mix[mix != 0]), 99.9) if np.any(mix) else 1.0
    gain = min(127.0 / peak, 127.0 / max(single, 1e-9))

    blobs = []
    for r in renders:
        q = np.clip(np.round(r * gain), -128, 127).astype(np.int8).tobytes()
        q += b"\0" * (-len(q) % 256)
        blobs.append(q if q else b"\0" * 256)

    # PCM commands per frame: play, and stop when the module channel goes
    # quiet (or plays a melodic note) before the rendering has ended
    pcm = collections.defaultdict(dict)             # frame -> {channel: id}
    starts = collections.defaultdict(set)
    for n in notes:
        starts[chan_of[n["ch"]]].add(int(round(n["start"] * FPS)))
    for n in sorted(notes, key=lambda n: n["start"]):
        pcm[int(round(n["start"] * FPS))][chan_of[n["ch"]]] = n["gid"] + 1
    for n in notes:
        ch = chan_of[n["ch"]]
        stopf = int(round(n["end"] * FPS))
        if n["end"] < n["start"] + len(renders[n["gid"]]) / RATE - 0.02 and stopf not in starts[ch]:
            pcm[stopf].setdefault(ch, 0)

    frames, target, open_end = fm_part(vgm_path, xgmtool, work)
    music = bytearray()
    loop_offset = None
    for f, cmds in enumerate(frames):
        if target is not None and target[0] == f:
            loop_offset = len(music) if target[1] == 0 else None
        for ch, sid in sorted(pcm.get(f, {}).items()):
            music += bytes([0x50 | ((3 - ch) << 2) | ch, sid])
        if target is not None and target[0] == f and target[1]:
            loop_offset = len(music) + target[1]
        music += cmds
        if not (open_end and f == len(frames) - 1):
            music.append(0x00)
    if loop_offset is not None:
        music += bytes([0x7E, loop_offset & 0xFF, (loop_offset >> 8) & 0xFF, loop_offset >> 16])
    music.append(0x7F)

    data = bytearray(b"XGM ")
    offset = 0
    for blob in blobs:
        data += struct.pack("<HH", offset >> 8, len(blob) >> 8)
        offset += len(blob)
    for _ in range(len(blobs), 0x3F):
        data += b"\xff\xff\x00\x00"
    data += struct.pack("<H", offset >> 8) + bytes([1, 0])
    for blob in blobs:
        data += blob
    data += struct.pack("<I", len(music)) + music
    out_path.write_bytes(bytes(data))
    late = sum(1 for f in pcm if f >= len(frames))
    return dict(samples=len(blobs), pcm=offset, size=len(data), gain=gain, seconds=total,
                frames=len(frames), late=late, loops=loop_offset is not None,
                channels={c: chan_of[c] + 1 for c in rank})


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    root = Path(__file__).resolve().parent.parent
    ap.add_argument("--input", type=Path, required=True)
    ap.add_argument("--music", type=Path, default=root / "res" / "music")
    ap.add_argument("--xgmtool", type=Path,
                    default=Path(os.environ.get("GDK", "/opt/sgdk")) / "bin" / "xgmtool")
    args = ap.parse_args()
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, mod in TRACKS.items():
            src = args.input / mod
            if not src.exists():
                print(f"skip {name}: {src} missing")
                continue
            info = convert(name, src, args.music / f"{name}.vgm", args.music / f"{name}.xgm",
                           args.xgmtool, Path(tmp))
            total += info["size"]
            print(f"{name:12s} {info['seconds']:6.1f}s {info['frames']:5d} frames  "
                  f"{info['samples']:2d} renderings  {info['size'] // 1024:4d} KB  gain {info['gain']:.2f}  "
                  f"drum ch -> PCM ch {info['channels']}  loop {info['loops']}  late {info['late']}")
    print(f"total {total // 1024} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
