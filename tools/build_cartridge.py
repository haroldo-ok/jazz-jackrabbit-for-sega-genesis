#!/usr/bin/env python3
"""Build the cartridge: every episode in 5 MB.

(This was the "lite" cartridge.  The 31 MB version that switched whole
episodes in and out with the SSF2 mapper was dropped because most emulators
could not run it; see README.md.)

The packs are made from the episodes' linked packs (out/packs/ep<N>/game.elf,
built by `build_packs.py packs`):

* tile graphics become a pool of distinct 8x8 tiles plus a compact index per
  logical tile.  Exact repeats cost nothing; on top of that
  - enemies' walk/fly cycles reuse every second frame (`--halve-frames`;
    items and Jazz keep every frame), and
  - each world's tile pool is capped at a share of its distinct tiles
    (`--world-keep`; enemies and items `--sprite-keep`, Jazz `--player-keep`),
    the rarest tiles being replaced by their nearest kept neighbour in colour
    (transparent and palette-cycling colours never mix with others);
* the 256x64 block map and event grid of each level are aPLib-compressed;
  the program unpacks them into RAM when the stage starts;
* the music keeps its FM/PSG arrangement and PCM drums cut down to fit a
  per-track budget (`--music-budget`), in ROM banks 7-9;
* planet approach screens and episode endings are text screens, and every
  episode plays episode 1's three bonus levels.

Layout: program from $000000, then the packs up to $380000, then the music
banks; a track's bank is mapped at $380000 (SSF2 register $A130FF) when it
starts.  The emulators that cannot do that still run everything else; only
the tracks in banks 8-9 would play wrongly.

Usage:  build_cartridge.py packs --input /path/to/JAZZ   (the music needs the modules)
        build_cartridge.py rom --program out/release/rom.bin
"""
from __future__ import annotations

import argparse
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_packs as bp  # noqa: E402

ROM_SIZE = 5 * 1024 * 1024
BANK = 0x80000
DATA_END = 0x380000          # program and level packs: banks 0-6, never moved
MUSIC_BANKS = range(7, ROM_SIZE // BANK)   # mapped one at a time at $380000
OUT = ROOT / "out" / "lite"
MUSIC_CACHE = ROOT / "packs" / "music"   # survives `make clean`


def tool(name):
    return os.environ.get("PREFIX", "m68k-elf-") + name


# ---------------------------------------------------------------- elf access
class Elf:
    def __init__(self, elf: Path):
        self.blob = Path(str(elf)[:-4] + ".bin").read_bytes()
        out = subprocess.run([tool("nm"), "-n", "-S", str(elf)], capture_output=True, text=True).stdout
        self.syms = {}
        self.org = None
        plain = subprocess.run([tool("nm"), "-n", str(elf)], capture_output=True, text=True).stdout
        # absolute symbols (the music addresses given with --defsym) are not in the image
        addrs = [int(l.split()[0], 16) for l in plain.splitlines()
                 if l.strip() and len(l.split()) == 3 and l.split()[1] not in "aA"]
        self.org = min(addrs)
        for line in out.splitlines():
            p = line.split()
            if len(p) == 4:
                self.syms[p[3]] = (int(p[0], 16), int(p[1], 16))

    def get(self, name):
        a, size = self.syms[name]
        return self.blob[a - self.org:a - self.org + size]


# ------------------------------------------------------------ source parsing
SPRITE_RE = re.compile(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}")


def sprite_records(text: str, level: int):
    """(tile, frames, w, h, tileLeft) of everything drawn from the level's
    sprite and player tiles: name -> list of records."""
    out = {"sprite": [], "player": []}
    m = re.search(rf"jj1_level{level}_player_states\[[^\]]*\] = \{{(.*?)\n\}};", text, re.S)
    if m:
        out["player"] = [tuple(map(int, r)) for r in SPRITE_RE.findall(m.group(1))]
    # Only enemies' walk/fly cycles are halved.  Items pick their frame by
    # what they hold (an ammo monitor and a 1-up monitor are frames of one
    # set), the player picks frames by state and speed (rising, falling),
    # and the one-off sprites (bird, board, boss parts) are few: those keep
    # every frame.
    klass = {int(i): int(k) for i, k in re.findall(
        rf"(?s)jj1_level{level}_eventset\[128\].*?(?=\n}};)", text) and re.findall(
        r"\[(\d+)\] = \{ (\d+),", re.search(rf"(?s)jj1_level{level}_eventset\[128\] = \{{(.*?)\n\}};", text).group(1))}
    m = re.search(rf"jj1_level{level}_event_sprites\[128\] = \{{(.*?)\n\}};", text, re.S)
    if m:
        for ident, rec in re.findall(r"\[(\d+)\] = (\{[^}]*\})", m.group(1)):
            r = SPRITE_RE.findall(rec)
            if r and klass.get(int(ident)) in (2, 3) and int(r[0][1]) >= 4:
                out["sprite"].append(tuple(map(int, r[0])))
    return out


def vdp_to_rgb(word):
    return ((word >> 1) & 7, (word >> 5) & 7, (word >> 9) & 7)


def parse_palette(body: str):
    cols = []
    for item in body.split(",\n"):
        item = item.strip().rstrip(",")
        if not item:
            continue
        if item.startswith("RGB3_3_3"):
            r, g, b = map(int, re.findall(r"\d+", item)[-3:])
            cols.append((r, g, b))
        else:
            cols.append(vdp_to_rgb(int(item, 16)))
    return np.array((cols + [(0, 0, 0)] * 16)[:16], dtype=np.int32)


def level_palette(text: str, level: int):
    m = re.search(rf"jj1_level{level}_pal1\[16\] = \{{(.*?)\}};", text, re.S)
    return parse_palette(m.group(1))


def world_palette(text: str, world: str):
    m = re.search(rf"jj1_world{world}_palette\[16\] = \{{(.*?)\}};", text, re.S)
    cols = []
    for item in m.group(1).split(",\n"):
        item = item.strip().rstrip(",")
        if not item:
            continue
        if item.startswith("RGB3_3_3"):
            r, g, b = map(int, re.findall(r"\d+", item)[-3:])
            cols.append((r, g, b))
        else:
            cols.append(vdp_to_rgb(int(item, 16)))
    cyc = set()
    for c in re.finditer(r"\{ (\d+), (\d+), \d+, \d+, -?\d+, \{", text[m.end():m.end() + 4000]):
        slot, count = int(c.group(1)), int(c.group(2))
        cyc.update(range(slot, slot + count))
    return np.array(cols[:16], dtype=np.int32), cyc


# ------------------------------------------------------------------- pooling
def tiles_of(data: bytes):
    return [data[i:i + 32] for i in range(0, len(data), 32)]


def unpack_tile(t: bytes):
    a = np.frombuffer(t, dtype=np.uint8)
    return np.stack([a >> 4, a & 15], axis=1).reshape(64)


def pool(tiles, alias=None):
    """Distinct tiles and an index per logical tile.  `alias[i] = j` makes
    logical tile i show tile j (frame halving)."""
    alias = alias or {}
    seen, pool_, index = {}, [], []
    for i, t in enumerate(tiles):
        if i in alias:
            t = tiles[alias[i]]
        if t not in seen:
            seen[t] = len(pool_)
            pool_.append(t)
        index.append(seen[t])
    return pool_, index


def cap_pool(pool_, index, keep_n, palette, cycling):
    """Keep the keep_n most used tiles; send the others to the closest kept."""
    if keep_n >= len(pool_):
        return pool_, index
    use = np.bincount(np.array(index), minlength=len(pool_))
    order = np.argsort(-use, kind="stable")
    kept = sorted(order[:keep_n].tolist())
    gone = order[keep_n:]
    px = np.stack([unpack_tile(t) for t in pool_])           # n x 64 colour indices
    rgb = palette[px]                                        # n x 64 x 3
    cyc = np.zeros(16, dtype=bool)
    cyc[list(cycling)] = True
    k_rgb = rgb[kept].astype(np.int32)
    k_px = px[kept]
    remap = {}
    for g in gone:
        d = ((k_rgb - rgb[g]) ** 2).sum(axis=2)              # keep x 64
        mismatch = (k_px != px[g]) & (cyc[k_px] | cyc[px[g]])
        d = d + mismatch * 1000
        remap[g] = kept[int(np.argmin(d.sum(axis=1)))]
    new_pos = {old: i for i, old in enumerate(kept)}
    new_index = [new_pos[remap.get(i, i)] for i in index]
    return [pool_[i] for i in kept], new_index


def pack_index(idx):
    """Compact index: per group of 16 logical tiles a u16 base, per tile a u8
    offset from it (255 = look the tile up in a sorted exception list of
    (logical, tile) pairs).  About 60 % of the plain u16 table.  Layout:
    u16 groups, u16 exceptions, u16 base[groups], u8 offset[groups*16],
    then the (u16, u16) exceptions."""
    n = len(idx)
    groups = (n + 15) // 16
    padded = list(idx) + [idx[-1] if idx else 0] * (groups * 16 - n)
    bases, offsets, exc = [], bytearray(), []
    for g in range(groups):
        grp = padded[g * 16:g * 16 + 16]
        s_ = sorted(grp)
        base = max(s_, key=lambda b: sum(1 for x in s_ if b <= x < b + 255))
        bases.append(base)
        for k, x in enumerate(grp):
            if base <= x < base + 255:
                offsets.append(x - base)
            else:
                offsets.append(255)
                exc.append((g * 16 + k, x))
    blob = struct.pack(">HH", groups, len(exc)) + struct.pack(f">{groups}H", *bases) + bytes(offsets)
    for logical, tile in exc:
        blob += struct.pack(">HH", logical, tile)
    return blob


def halving_alias(records, total_tiles):
    alias = {}
    for tile, frames, w, h, left in records:
        n = w * h
        if frames < 2 or n == 0:
            continue
        for base in {tile, left}:
            for f in range(1, frames, 2):
                for k in range(n):
                    src, dst = base + (f - 1) * n + k, base + f * n + k
                    if dst < total_tiles:
                        alias[dst] = src
    return alias


def aplib(data: bytes, work: Path, name: str) -> bytes:
    raw = work / (name + ".raw")
    packed = work / (name + ".ap")
    raw.write_bytes(data)
    if not packed.exists() or packed.stat().st_mtime < raw.stat().st_mtime:
        subprocess.run(["java", "-jar", str(Path(os.environ.get("GDK", "/opt/sgdk")) / "bin" / "apj.jar"),
                        "p", str(raw), str(packed)], check=True, capture_output=True)
    return packed.read_bytes()


# -------------------------------------------------------------------- music
def lite_music(track: str, work: Path, data: Path, budget: int) -> Path:
    """The track's FM/PSG arrangement plus PCM drums cut down to fit
    `budget` bytes (build_music.convert), compiled for the driver."""
    import build_music
    gdk = Path(os.environ.get("GDK", "/opt/sgdk"))
    work.mkdir(parents=True, exist_ok=True)
    vgm = ROOT / "res" / "music" / (track + ".vgm")
    xgm = work / (track + ".xgm")
    xgc = work / (track + ".xgc")
    stamp = work / (track + ".budget")
    if not (xgc.exists() and stamp.exists() and stamp.read_text() == str(budget)
            and xgc.stat().st_mtime > vgm.stat().st_mtime):
        if data is None:
            raise SystemExit(f"{track}: the music has to be converted once from the game's "
                             "modules: give --input /path/to/JAZZ (make JAZZ_DATA=...)")
        module = data / build_music.TRACKS[track]
        size = budget
        while True:
            info = build_music.convert(track, module, vgm, xgm, gdk / "bin" / "xgmtool", work, budget=size)
            subprocess.run([str(gdk / "bin" / "xgmtool"), str(xgm), str(xgc), "-s"], check=True, capture_output=True)
            if xgc.stat().st_size <= budget or size < 8192:
                break
            size -= xgc.stat().st_size - budget + 256
        stamp.write_text(str(budget))
        print(f"  {track:12s} {xgc.stat().st_size // 1024:3d} KB  drums cut at {info.get('cap', 0) * 1000:.0f} ms, "
              f"{info.get('ids', 0)} renderings")
    return xgc


def build_music_banks(tracks, data: Path, budget: int):
    """Pack every track into the music banks; returns {track: encoded
    address} (bank number in the top byte, address at $380000 below)."""
    files = {t: lite_music(t, MUSIC_CACHE, data, budget) for t in tracks}
    banks = [bytearray() for _ in MUSIC_BANKS]
    where = {}
    for t in sorted(files, key=lambda t: -files[t].stat().st_size):
        blob = files[t].read_bytes()
        for k, b in enumerate(banks):
            start = len(b) + (-len(b) % 256)
            if start + len(blob) <= BANK:
                b.extend(b"\xff" * (start - len(b)))
                b.extend(blob)
                where[t] = (MUSIC_BANKS[k] << 24) | (DATA_END + start)
                break
        else:
            raise SystemExit(f"music does not fit {len(banks)} banks; lower --music-budget")
    image = bytearray()
    for b in banks:
        image += b + b"\xff" * (BANK - len(b))
    (OUT / "lite_music.bin").write_bytes(image)
    used = sum(len(b) for b in banks)
    print(f"music: {len(files)} tracks, {used / 1048576:.2f} MB in {len(banks)} banks")
    return where


def build_episode(d: Path, m: dict, args, stats):
    ep = d.name
    work = OUT / ep
    work.mkdir(parents=True, exist_ok=True)
    elf = Elf(ROOT / "out" / "packs" / ep / "game.elf")
    text = "\n".join(p.read_text() for p in sorted(d.glob("jj1_*.inc")))
    inc = ["/* Generated by tools/build_cartridge.py; do not edit. */"]
    asm = []

    def emit(label, data, align=2):
        f = work / (label + ".bin")
        f.write_bytes(data)
        asm.append(f'    .section .packbin,"a"\n    .balign {align}\n    .global {label}\n{label}:\n    .incbin "{f}"\n')
        inc.append(f"extern const u8 {label}[];")
        stats[label.split("_")[1] if False else "x"] = 0

    def add(kind, n):
        stats[kind] = stats.get(kind, 0) + n

    worlds = sorted(set(re.findall(r"jj1_world(\w+?)_block_tiles\b", text)))
    for w in worlds:
        data = elf.get(f"jj1_world{w}_block_tiles")
        palette, cycling = world_palette(text, w)
        p, idx = pool(tiles_of(data))
        # colour 0 lets the sky through: it never mixes with a drawn colour
        p, idx = cap_pool(p, idx, max(64, int(len(p) * args.world_keep)), palette, cycling | {0})
        emit(f"lite_world{w}_pool", b"".join(p))
        blob = pack_index(idx)
        emit(f"lite_world{w}_index", blob)
        add("world tiles", len(p) * 32); add("indices", len(blob))
    for n in range(m["stages"]):
        recs = sprite_records(text, n)
        for kind, sym in (("sprite", f"jj1_level{n}_event_sprite_tiles"),
                          ("player", f"jj1_level{n}_player_tiles")):
            data = elf.get(sym)
            tiles = tiles_of(data)
            alias = halving_alias(recs[kind], len(tiles)) if (args.halve_frames and kind == "sprite") else {}
            p, idx = pool(tiles, alias)
            keep = args.sprite_keep if kind == "sprite" else args.player_keep
            # colour 0 is transparent: never merged with a visible colour
            p, idx = cap_pool(p, idx, max(32, int(len(p) * keep)), level_palette(text, n), {0})
            emit(f"lite_level{n}_{kind}_pool", b"".join(p))
            blob = pack_index(idx)
            emit(f"lite_level{n}_{kind}_index", blob)
            add(kind + " tiles", len(p) * 32); add("indices", len(blob))
        for kind, sym in (("map", f"jj1_level{n}_blocks"), ("events", f"jj1_level{n}_events")):
            packed = aplib(elf.get(sym), work, f"level{n}_{kind}")
            emit(f"lite_level{n}_{kind}", packed)
            add("maps+events (aPLib)", len(packed))
    # the stage music lives in the music banks (cmd_packs); the pack only
    # holds its encoded address, given to the linker with --defsym
    # the level records: same start positions, packed map and event grid
    levels = re.search(r"#define PACK_LEVELS \{ \\\n(.*?)\n\}", (d / "pack_game.inc").read_text(), re.S).group(1)
    levels = re.sub(r"jj1_level(\d+)_blocks", r"lite_level\1_map", levels)
    levels = re.sub(r"jj1_level(\d+)_events", r"lite_level\1_events", levels)
    inc.append("#define LITE_LEVELS { \\\n" + levels + "\n}")
    (work / "lite_bins.s").write_text("".join(asm))
    (work / "lite_pack.inc").write_text("\n".join(inc) + "\n")
    return work


def link_episode(d: Path, m: dict, work: Path, org: int, music: dict):
    gdk = Path(os.environ.get("GDK", "/opt/sgdk"))
    flags = ["-m68000", "-O1", "-fno-builtin", "-fdata-sections", "-fms-extensions",
             "-DSGDK_GCC", "-DJAZZ_JJ1_RUNTIME", "-w",
             f"-I{work}", f"-I{d}", f"-I{ROOT / 'inc'}", f"-I{ROOT / 'res'}",
             f"-isystem{gdk / 'inc'}", f"-isystem{gdk / 'res'}"]
    obj = work / "lite.o"
    bp.run([tool("gcc"), *flags, "-c", ROOT / "pack" / "lite_pack.c", "-o", obj])
    bobj = work / "lite_bins.o"
    bp.run([tool("gcc"), "-m68000", "-c", work / "lite_bins.s", "-o", bobj])
    elf = work / "lite.elf"
    tracks = [f"--defsym=pack_music_{i}=0x{music[t]:08X}" for i, t in enumerate(m["music"])]
    bp.run([tool("ld"), "-T", ROOT / "pack" / "pack.ld", f"--defsym=PACK_ORG=0x{org:X}", *tracks,
            "--gc-sections", "-o", elf, obj, bobj, "-Map", work / "lite.map"])
    undefined = bp.run([tool("nm"), "-u", elf]).strip()
    if undefined:
        raise SystemExit(f"{d.name} lite pack refers outside itself:\n{undefined}")
    image = work / "lite.bin"
    bp.run([tool("objcopy"), "-O", "binary", elf, image])
    blob = image.read_bytes()
    return blob + b"\xff" * (-len(blob) % 256)


def cmd_packs(args):
    OUT.mkdir(parents=True, exist_ok=True)
    # the episodes `build_packs.py packs` linked last time
    linked = (ROOT / "out" / "packs" / "stamp").read_text().split()
    found = [(d, m) for d, m in bp.manifests() if d.name in linked]
    tracks = ["menu", "bonus", "endlevel"]
    for _, m in found:
        tracks += [t for t in m["music"] if t not in tracks]
    music = build_music_banks(tracks, args.input, args.music_budget)
    # the program's three tracks: addresses for its jj1_music.h
    header = ["/* Generated by tools/build_cartridge.py; do not edit. */",
              "/* Lite cartridge: the music is in banks 7-9; jj1_music_start maps the",
              " * track's bank (top byte) at $380000 before playing it. */"]
    for t in ("menu", "bonus", "endlevel"):
        header.append(f"#define jj1_music_{t} ((const u8 *)0x{music[t]:08X}UL)")
    (OUT / "jj1_music.h").write_text("\n".join(header) + "\n")
    stats = {}
    works = [(d, m, build_episode(d, m, args, stats)) for d, m in found]
    # First link anywhere to learn the sizes, then for real just below the music.
    sizes = [len(link_episode(d, m, w, 0x200000, music)) for d, m, w in works]
    total = sum(sizes)
    base = (DATA_END - total) & ~0xFF
    blob = bytearray()
    entries = []
    for (d, m, w), size in zip(works, sizes):
        org = base + len(blob)
        image = link_episode(d, m, w, org, music)
        assert len(image) == size
        blob += image
        entries.append((org, m))
    (OUT / "lite_packs.bin").write_bytes(blob)
    lines = ["/* Generated by tools/build_cartridge.py packs; do not edit. */",
             f"#define JJ1_LITE_PACK_BASE 0x{base:06X}UL",
             "const Jj1PackEntry jj1_pack_dir[] = {"]
    for org, m in entries:
        lines.append(f'    {{ {m["stages"]}, {m["episode"]}, 0x{org:06X}UL, '
                     f'"{m["title"]}", "{m["worlds"]}" }},')
    lines += ["};", f"const u8 jj1_pack_count = {len(entries)};"]
    (OUT / "lite_packdir.inc").write_text("\n".join(lines) + "\n")
    for k, v in sorted(stats.items(), key=lambda x: -x[1]):
        if k != "x":
            print(f"  {k:22s} {v / 1048576:5.2f} MB")
    print(f"lite packs {total / 1048576:.2f} MB from 0x{base:06X}; the program may use {base / 1048576:.2f} MB")


def cmd_rom(args):
    prog = (ROOT / args.program).read_bytes()
    # SGDK pads the program to a 128 KB multiple; the packs may use the
    # padding.  Whatever is cut is written back with the same byte below.
    pad = prog[-1]
    prog = prog.rstrip(bytes([pad]))
    prog += bytes([pad]) * (len(prog) & 1)
    blob = (OUT / "lite_packs.bin").read_bytes()
    text = (OUT / "lite_packdir.inc").read_text()
    want = int(re.search(r"JJ1_LITE_PACK_BASE 0x([0-9A-F]+)", text).group(1), 16)
    if len(prog) > want:
        raise SystemExit(f"the lite program is {len(prog)} bytes; the packs start at 0x{want:06X} "
                         f"({(len(prog) - want) / 1024:.0f} KB too many)")
    rom = bytearray(prog) + bytes([pad]) * (want - len(prog)) + blob
    rom += b"\xff" * (DATA_END - len(rom))
    rom += (OUT / "lite_music.bin").read_bytes()
    assert len(rom) == ROM_SIZE, len(rom)
    struct.pack_into(">I", rom, 0x1A4, ROM_SIZE - 1)
    body = bytes(rom[0x200:])
    total = sum(struct.unpack(">%dH" % (len(body) // 2), body)) & 0xFFFF
    struct.pack_into(">H", rom, 0x18E, total)
    out = ROOT / args.output
    out.write_bytes(rom)
    (ROOT / "out" / "rom.bin").write_bytes(rom)      # where the tests look
    print(f"{out.name}: {len(rom)} bytes; program {len(prog) / 1048576:.2f} MB, "
          f"free {(want - len(prog)) / 1024:.0f} KB, packs {len(blob) / 1048576:.2f} MB")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("packs")
    p.add_argument("--input", type=Path, default=None,
                   help="the game's JAZZ directory (needed once, to convert the music)")
    p.add_argument("--music-budget", type=int, default=53 * 1024,
                   help="largest compiled track, bytes")
    p.add_argument("--world-keep", type=float, default=0.33)
    p.add_argument("--sprite-keep", type=float, default=0.65)
    p.add_argument("--player-keep", type=float, default=1.0)
    p.add_argument("--halve-frames", action="store_true", default=True)
    p.add_argument("--no-halve-frames", dest="halve_frames", action="store_false")
    p.set_defaults(fn=cmd_packs)
    r = sub.add_parser("rom")
    r.add_argument("--program", default="out/release/rom.bin")
    r.add_argument("--output", default="JazzJackrabbitGenesis.bin")
    r.set_defaults(fn=cmd_rom)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
