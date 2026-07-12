#!/usr/bin/env python3
"""Convert one 10x7 JJ1 map chunk to a Genesis 320x224 background plane.

A JJ1 logical block is 32x32. A 10x7 block chunk is exactly 320x224, or
40x28 Genesis hardware tiles. Every output tile is kept in ROM so the plane
can be drawn with a single sequential tile-map fill. This is the offline form
of the chunk cache used by the next streaming renderer stage.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from import_jj1_shareware import TRANSPARENT, decode_blocks, parse_level

CHUNK_BLOCK_W, CHUNK_BLOCK_H = 10, 7
SCREEN_W, SCREEN_H = 320, 224


def md_palette(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    return ((r * 7 // 255) << 1) | ((g * 7 // 255) << 5) | ((b * 7 // 255) << 9)


def compose(tiles: list[bytes], palette: list[tuple[int, int, int]], grid: bytes, x0: int, y0: int) -> tuple[Image.Image, list[bool]]:
    image = Image.new("P", (SCREEN_W, SCREEN_H), TRANSPARENT)
    image.putpalette([c for rgb in palette for c in rgb])
    transparent: list[bool] = []
    for by in range(CHUNK_BLOCK_H):
        for bx in range(CHUNK_BLOCK_W):
            ti = grid[((y0 + by) + (x0 + bx) * 64) * 2]
            raw = tiles[ti] if ti < len(tiles) else bytes([TRANSPARENT]) * 1024
            block = Image.frombytes("P", (32, 32), raw)
            block.putpalette([c for rgb in palette for c in rgb])
            image.paste(block, (bx * 32, by * 32))
    for index in image.getdata():
        transparent.append(index == TRANSPARENT)
    return image, transparent


def quantize(image: Image.Image, transparency: list[bool]) -> tuple[list[int], list[tuple[int, int, int]]]:
    opaque = [rgb for rgb, transparent in zip(image.convert("RGB").getdata(), transparency) if not transparent]
    source = Image.new("RGB", (len(opaque), 1)); source.putdata(opaque)
    q = source.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
    raw = q.getpalette()[:45]
    palette = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    # Sparse rooms may use fewer than 15 distinct colours; pad the hardware
    # palette so the nearest-colour loop always has a full 4bpp bank.
    while len(palette) < 15:
        palette.append(palette[-1] if palette else (0, 0, 0))
    pixels: list[int] = []
    for rgb, transparent in zip(image.convert("RGB").getdata(), transparency):
        if transparent:
            pixels.append(0)
        else:
            pixels.append(1 + min(range(15), key=lambda n: sum((rgb[c] - palette[n][c]) ** 2 for c in range(3))))
    return pixels, palette


def words_for_screen(indices: list[int]) -> list[int]:
    words: list[int] = []
    for tile_y in range(28):
        for tile_x in range(40):
            for y in range(8):
                word = 0
                for x in range(8):
                    word = (word << 4) | indices[(tile_y * 8 + y) * SCREEN_W + tile_x * 8 + x]
                words.append(word)
    return words


def emit_array(name: str, data: list[int]) -> str:
    lines = [f"const u32 {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 4):
        lines.append("    " + ", ".join(f"0x{v:08X}" for v in data[i:i + 4]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", type=Path, required=True)
    ap.add_argument("--level", default="LEVEL0.000")
    ap.add_argument("--x", type=int, default=40, help="JJ1 block X (0..246)")
    ap.add_argument("--y", type=int, default=0, help="JJ1 block Y (0..57)")
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--name", default="jj1_level0_screen", help="C symbol prefix for tile/palette arrays")
    ap.add_argument("--preview", type=Path)
    args = ap.parse_args()
    if not (0 <= args.x <= 256 - CHUNK_BLOCK_W and 0 <= args.y <= 64 - CHUNK_BLOCK_H):
        raise SystemExit("chunk is outside the 256x64 level")
    grid, metadata = parse_level(args.input / args.level)
    ext = str(metadata["blocks_extension"])
    block_path = args.input / f"BLOCKS.{ext}"
    if not block_path.exists():
        block_path = args.input / "BLOCKS.000"
    tiles, palette, _ = decode_blocks(block_path)
    image, mask = compose(tiles, palette, grid, args.x, args.y)
    indices, colors = quantize(image, mask)
    words = words_for_screen(indices)
    code = [
        "/* Generated from a user-supplied JJ1 LEVEL/BLOCKS chunk. */",
        f"/* {args.level}, block origin {args.x},{args.y}; 40x28 Genesis tiles. */",
        emit_array(f"{args.name}_tiles", words), "",
        f"const u16 {args.name}_palette[16] = {{",
        "    RGB3_3_3_TO_VDPCOLOR(0,0,0),",
    ]
    code.extend(f"    0x{md_palette(c):04X}," for c in colors)
    code.append("};")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(code) + "\n")
    if args.preview:
        out = Image.new("P", (SCREEN_W, SCREEN_H)); out.putpalette([0, 0, 0] + [v for c in colors for v in c] + [0] * (768 - 48)); out.putdata(indices)
        args.preview.parent.mkdir(parents=True, exist_ok=True); out.save(args.preview)
    print(f"Generated {args.output} from {args.level} @ {args.x},{args.y} ({len(words)//8} VDP tiles)")


if __name__ == "__main__":
    main()
