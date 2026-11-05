#!/usr/bin/env python3
"""Build the in-game status panel tiles from a user-supplied JJ1 PANEL.000.

The original panel is a 320x32 metal strip.  The Genesis port keeps its
look in two tile rows: the plate texture, the yellow panel digits, and the
lives and planet icons are cut from PANEL.000 and quantised to one 16-colour
line; the hearts are drawn here, since the original's energy bar is a purple
meter that does not map onto tiles.

Output: res/jj1_hud.png, a 64x32 sheet of 8x8 tiles in the fixed order below
(inc/jazz_hud.h names the same indices), and its palette.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_jj1_shareware import Reader, load_palette  # noqa: E402

# Tile order (keep in sync with inc/jazz_hud.h)
HUD_PLATE, HUD_BLACK, HUD_DIGIT0 = 0, 1, 2          # digits take 2..11
HUD_HEART_FULL, HUD_HEART_EMPTY = 12, 13
HUD_LIVES = 14                                      # 2x2 icon: 14 15 / 16 17
HUD_PLANET = 18                                     # 2x2 icon: 18 19 / 20 21
HUD_TILES = 22

# Palette line: index 1 black and 15 white because the 8x8 text font draws
# its background with 1 and its ink with 15 on the same line.
TARGET = [
    (0, 0, 0),          # 0 (unused)
    (0, 0, 0),          # 1 black
    (32, 32, 32),       # 2 plate shadow
    (73, 73, 73),       # 3
    (113, 113, 113),    # 4
    (170, 170, 170),    # 5 plate highlight
    (110, 110, 0),      # 6 digit shade
    (190, 190, 0),      # 7
    (255, 255, 0),      # 8 digit bright
    (255, 56, 0),       # 9 heart
    (203, 20, 0),       # 10
    (117, 0, 0),        # 11
    (0, 170, 0),        # 12 icon green
    (0, 80, 0),         # 13
    (93, 0, 255),       # 14 icon purple
    (255, 255, 255),    # 15 white
]
YELLOW = {75, 76, 77, 78, 79}


def nearest(rgb):
    best, best_d = 1, 1 << 30
    for i in range(1, 16):
        t = TARGET[i]
        d = (rgb[0] - t[0]) ** 2 * 3 + (rgb[1] - t[1]) ** 2 * 4 + (rgb[2] - t[2]) ** 2 * 2
        if d < best_d:
            best, best_d = i, d
    return best


HEART = [
    "..XX.XX.",
    ".XXXXXXX",
    "XXHXXXXX",
    "XXXXXXXX",
    ".XXXXXXX",
    "..XXXXX.",
    "...XXX..",
    "....X...",
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", type=Path, required=True)
    ap.add_argument("--output", type=Path, default=Path("res/jj1_hud.png"))
    args = ap.parse_args()

    pal = load_palette(Reader((args.input / "BLOCKS.000").read_bytes()))
    data = Reader((args.input / "PANEL.000").read_bytes()).rle(46272)

    def px(x, y):
        return data[y * 320 + x]

    def conv(index):
        return nearest(pal[index])

    sheet = Image.new("P", (64, 32), 1)
    flat = [c for rgb in TARGET for c in rgb] + [0] * (768 - 48)
    sheet.putpalette(flat)

    def put_tile(n, pixels):
        ox, oy = (n % 8) * 8, (n // 8) * 8
        for y in range(8):
            for x in range(8):
                sheet.putpixel((ox + x, oy + y), pixels[y][x])

    # plate: a plain stretch of the original brushed metal
    put_tile(HUD_PLATE, [[conv(px(200 + x, 2 + y)) for x in range(8)] for y in range(8)])
    put_tile(HUD_BLACK, [[1] * 8 for _ in range(8)])

    # digits: 5x7 pixel glyphs in the panel's yellow, on black.  (The yellow
    # digit strip in PANEL.000 is a shaded colour ramp, not a legible bitmap
    # at 8x8, so the glyph shapes are drawn here and shaded with its ramp.)
    GLYPHS = [
        "01110 10001 10011 10101 11001 10001 01110",
        "00100 01100 00100 00100 00100 00100 01110",
        "01110 10001 00001 00110 01000 10000 11111",
        "11110 00001 00001 01110 00001 00001 11110",
        "00010 00110 01010 10010 11111 00010 00010",
        "11111 10000 11110 00001 00001 10001 01110",
        "00110 01000 10000 11110 10001 10001 01110",
        "11111 00001 00010 00100 01000 01000 01000",
        "01110 10001 10001 01110 10001 10001 01110",
        "01110 10001 10001 01111 00001 00010 01100",
    ]
    for d in range(10):
        tile = [[1] * 8 for _ in range(8)]
        for y, row in enumerate(GLYPHS[d].split()):
            for x, c in enumerate(row):
                if c == "1":
                    tile[y][x + 1] = 8 if y < 3 else 7
        put_tile(HUD_DIGIT0 + d, tile)

    # hearts
    full = [[1] * 8 for _ in range(8)]
    empty = [[1] * 8 for _ in range(8)]
    for y, row in enumerate(HEART):
        for x, c in enumerate(row):
            if c == "X":
                full[y][x] = 10
                empty[y][x] = 3
            elif c == "H":
                full[y][x] = 9
                empty[y][x] = 4
    put_tile(HUD_HEART_FULL, full)
    put_tile(HUD_HEART_EMPTY, empty)

    # icons: 16x16 stretches of the panel
    for base, (ix, iy) in ((HUD_LIVES, (88, 16)), (HUD_PLANET, (150, 16))):
        for t in range(4):
            tx, ty = (t % 2) * 8, (t // 2) * 8
            put_tile(base + t, [[conv(px(ix + tx + x, iy + ty + y)) for x in range(8)]
                                for y in range(8)])

    args.output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.output)
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
