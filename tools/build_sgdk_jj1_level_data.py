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


def runtime_records(level_path: Path) -> tuple[bytes, bytes, int, int]:
    """Mirror JJ1Level::load record skips through mask/start-coordinate data."""
    r = Reader(level_path.read_bytes())
    r.seek(39)
    grid = r.rle(256 * 64 * 2)
    r.skip_rle()                # transparency reference table
    masks = r.rle((60 * 4 + 16) * 8)
    r.rle(16 * 512)             # movement paths
    r.rle(127 * 32)             # event definitions
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
    return grid, masks, start_x, start_y


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", type=Path, required=True)
    ap.add_argument("--level", default="LEVEL0.000")
    ap.add_argument("--name", default="jj1_level0")
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()

    grid, metadata = parse_level(args.input / args.level)
    runtime_grid, masks, start_x, start_y = runtime_records(args.input / args.level)
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
    print(f"Generated {args.output}: {len(blocks)} blocks, {len(words)//8} VDP tiles, masks, start {start_x},{start_y}")


if __name__ == "__main__":
    main()
