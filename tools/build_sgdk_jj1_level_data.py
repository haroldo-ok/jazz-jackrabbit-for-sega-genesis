#!/usr/bin/env python3
"""Generate complete JJ1 block/map/mask data for Genesis runtime streaming.

Each output contains all 240 32x32 BLOCKS patterns as 16 sequential Genesis
8x8 tiles, the 256x64 block map, 8x8 collision masks, and the original level
start coordinate. The runtime keeps 88 blocks in VRAM and reads the masks for
actual foreground collision rather than a decorative background.
"""
from __future__ import annotations
import argparse
import sys
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from import_jj1_shareware import TRANSPARENT, Reader, decode_blocks, parse_level


def md(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    return ((r * 7 // 255) << 1) | ((g * 7 // 255) << 5) | ((b * 7 // 255) << 9)


def make_palette(tiles: list[bytes], source_palette: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
    pixels = [source_palette[p] for tile in tiles for p in tile if p != TRANSPARENT]
    source = Image.new("RGB", (len(pixels), 1)); source.putdata(pixels)
    q = source.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
    raw = q.getpalette()[:45]
    return [tuple(raw[i:i+3]) for i in range(0, 45, 3)]


def nearest(rgb: tuple[int, int, int], palette: list[tuple[int, int, int]]) -> int:
    return 1 + min(range(15), key=lambda i: sum((rgb[c] - palette[i][c]) ** 2 for c in range(3)))


def encode_block(raw: bytes, source_palette: list[tuple[int, int, int]], palette: list[tuple[int, int, int]]) -> list[int]:
    indices = [0 if p == TRANSPARENT else nearest(source_palette[p], palette) for p in raw]
    out: list[int] = []
    for ty in range(4):
        for tx in range(4):
            for y in range(8):
                word = 0
                for x in range(8):
                    word = (word << 4) | indices[(ty * 8 + y) * 32 + tx * 8 + x]
                out.append(word)
    return out


def array(name: str, ctype: str, values: list[int], per_line: int, fmt: str) -> str:
    lines = [f"const {ctype} {name}[{len(values)}] = {{"]
    for start in range(0, len(values), per_line):
        lines.append("    " + ", ".join(fmt.format(v) for v in values[start:start+per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def runtime_records(level_path: Path) -> tuple[bytes, bytes, bytes, int, int]:
    """Mirror JJ1Level::load record skips through mask/start-coordinate data."""
    r = Reader(level_path.read_bytes())
    r.seek(39)
    grid = r.rle(256 * 64 * 2)
    r.skip_rle()                # transparency reference table
    masks = r.rle((60 * 4 + 16) * 8)
    r.rle(16 * 512)             # movement paths
    eventset = r.rle(127 * 32)  # event definitions
    r.skip_rle()                # event names
    r.rle(128 * 64)             # animation definitions
    r.skip_rle()                # animation names
    r.skip((16 * (8 + 1)) + 9)  # level block names / compression info
    r.skip(32 * 2)              # sound rates
    for _ in range(32):
        length = r.byte()
        if length > 8:
            raise ValueError(f"invalid level sound name length {length}")
        r.skip(8)               # terminated fields consume exactly max size
    r.skip(13)                  # music filename field
    r.skip(13)                  # level start scene field
    r.skip(13)                  # end scene field
    r.skip(39)                  # editor tileset names
    start_x = r.u16()
    start_y = r.u16() + 1
    return grid, masks, eventset, start_x, start_y


# Genesis runtime classes (keep in sync with inc/jj1_events.h).
CLASS_NONE, CLASS_ITEM, CLASS_ENEMY_WALK, CLASS_ENEMY_FLY, CLASS_HAZARD, \
    CLASS_SPRING, CLASS_ONEWAY, CLASS_END = range(8)
ITEM_SCORE, ITEM_HEALTH, ITEM_LIFE, ITEM_FASTFEET, ITEM_AMMO = range(5)


def classify_event(record: bytes) -> tuple[int, int, int, int]:
    """Map one original 32-byte event record to a (class, param, points/25,
    strength) tuple, using the same rules the reference GPL engine applies:
    modifier 0 with strength is an enemy, a scoring record is an item,
    modifier 6 is a one-way platform, 27/8/41 end the level, 29 is an upward
    spring, and hurting records that cannot be shot are static hazards."""
    movement = record[4]
    magnitude = record[8] if record[8] < 128 else record[8] - 256
    strength = record[9]
    modifier = record[10]
    points = record[11]
    if modifier == 6:
        return CLASS_ONEWAY, 0, 0, 0
    if modifier in (8, 27, 41):
        return CLASS_END, 0, 0, 0
    if modifier == 29:
        # spring strength bucketed from launch magnitude
        mag = -magnitude if magnitude < 0 else magnitude
        return CLASS_SPRING, 0 if mag <= 16 else (1 if mag <= 24 else 2), 0, 0
    if modifier == 0 and strength:
        flying = movement in (6, 7, 25)
        return (CLASS_ENEMY_FLY if flying else CLASS_ENEMY_WALK), 0, 0, min(strength, 255)
    if modifier == 0 and not strength and points == 0 and movement in (37, 38):
        return CLASS_NONE, 0, 0, 0
    if points:
        param = ITEM_SCORE
        if modifier in (2, 3):
            param = ITEM_HEALTH
        elif modifier == 4:
            param = ITEM_LIFE
        elif modifier == 5:
            param = ITEM_FASTFEET
        elif modifier in (10, 11, 12):
            param = ITEM_AMMO
        return CLASS_ITEM, param, min((points * 10) // 25, 255), 0
    if modifier == 0 and movement and movement < 37:
        return CLASS_HAZARD, 0, 0, 0
    return CLASS_NONE, 0, 0, 0


def eventset_include(name: str, eventset: bytes) -> str:
    lines = [
        "/* Ground-truth event set generated from the original level file. */",
        f"static const Jj1EventInfo {name}_eventset[128] = {{",
    ]
    for i in range(127):
        klass, param, points, strength = classify_event(eventset[i * 32:(i + 1) * 32])
        if klass or param or points or strength:
            lines.append(f"    [{i + 1}] = {{ {klass}, {param}, {points}, {strength} }},")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", type=Path, required=True)
    ap.add_argument("--level", default="LEVEL0.000")
    ap.add_argument("--name", default="jj1_level0")
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--eventset", type=Path,
                    help="also write a ground-truth jj1_levelN_eventset.inc")
    args = ap.parse_args()

    grid, metadata = parse_level(args.input / args.level)
    runtime_grid, masks, eventset, start_x, start_y = runtime_records(args.input / args.level)
    if grid != runtime_grid:
        raise SystemExit("grid decode mismatch")
    ext = str(metadata["blocks_extension"])
    blocks_path = args.input / f"BLOCKS.{ext}"
    if not blocks_path.exists(): blocks_path = args.input / "BLOCKS.000"
    blocks, source_palette, _ = decode_blocks(blocks_path)
    palette = make_palette(blocks, source_palette)
    words = [word for block in blocks for word in encode_block(block, source_palette, palette)]
    map_data = [grid[(y + x * 64) * 2] for y in range(64) for x in range(256)]
    event_data = [grid[(y + x * 64) * 2 + 1] & 0x7F for y in range(64) for x in range(256)]
    code = [
        "/* Generated from user-supplied JJ1 LEVEL/BLOCKS data. */",
        f"/* {args.level}, BLOCKS.{ext}; 240 x 16 Genesis patterns, 256x64 block map, 8x8 masks. */",
        array(f"{args.name}_block_tiles", "u32", words, 4, "0x{:08X}"), "",
        array(f"{args.name}_blocks", "u8", map_data, 16, "0x{:02X}"), "",
        array(f"{args.name}_events", "u8", event_data, 16, "0x{:02X}"), "",
        array(f"{args.name}_masks", "u8", list(masks), 16, "0x{:02X}"), "",
        f"const u16 {args.name}_start_x = {start_x};",
        f"const u16 {args.name}_start_y = {start_y};", "",
        f"const u16 {args.name}_palette[16] = {{",
        "    RGB3_3_3_TO_VDPCOLOR(0,0,0),",
    ]
    code.extend(f"    0x{md(color):04X}," for color in palette)
    code.append("};")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(code) + "\n")
    if args.eventset:
        args.eventset.parent.mkdir(parents=True, exist_ok=True)
        args.eventset.write_text(eventset_include(args.name, eventset) + "\n")
        print(f"Generated {args.eventset} from the original event records")
    print(f"Generated {args.output}: {len(blocks)} blocks, {len(words)//8} VDP tiles, masks, start {start_x},{start_y}")


if __name__ == "__main__":
    main()
