#!/usr/bin/env python3
"""Convert a JJ1 .0FN font into an SGDK 96-glyph (ASCII 32..127) tileset PNG.

Glyphs are placed in 8x8 cells in ASCII order, using OpenJazz's ASCII->glyph
map.  The font's 16..31 colour ramp becomes palette indices 1..15 so the
text palette (PAL0) can shade it.  Usage:
    build_sgdk_font.py --input FONT2.0FN --output res/jj1_font.png
"""
import argparse, sys
from pathlib import Path
from PIL import Image
sys.path.insert(0, str(Path(__file__).parent))
import import_jj1_shareware as imp


def ascii_map():
    m = {33: 107, 34: 116, 36: 63, 39: 115, 40: 111, 41: 112, 43: 105, 44: 101,
         45: 104, 46: 102, 47: 108, 58: 114, 59: 113, 61: 106, 63: 103}
    for c in range(48, 58): m[c] = c + 5
    # Glyph slots 1-26 are the large capitals, 27-52 the small letters.  The
    # ROM's strings are uppercase, so both cases use the large capitals.
    for c in range(65, 91): m[c] = c - 64
    for c in range(97, 123): m[c] = c - 96
    return m


def load_glyphs(path):
    r = imp.Reader(Path(path).read_bytes())
    assert r.block(18) == b"Digital Dimensions" and r.byte() == 0x1A
    r.byte(); r.byte()
    glyphs, i = {}, 0
    while r.pos < len(r.data):
        size = r.u16()
        if size > 4:
            p = r.rle(size)
            w, h = p[0] | p[1] << 8, p[2] | p[3] << 8
            if w and h and 4 + w * h <= len(p):
                glyphs[i] = (w, h, p[4:4 + w * h])
        i += 1
    return glyphs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    a = ap.parse_args()
    glyphs, amap = load_glyphs(a.input), ascii_map()
    img = Image.new("P", (8, 8 * 96), 0)
    # The original glyphs are drawn with a dithered 16..31 shading ramp, which
    # turns to noise at 8x8.  Every inked pixel becomes one solid bright colour
    # (index 15, white) so the text stays readable; index 0 is the backdrop.
    pal = [0, 0, 40] + [255, 255, 255] * 0
    # index 1 is an opaque dark backdrop for every glyph cell so the status bar
    # stays readable over any level background; index 15 is the ink.
    pal = [0, 0, 40, 0, 0, 40] + [200, 200, 200] * 13 + [255, 255, 255]
    pal += [0, 0, 0] * (256 - 16)
    img.putpalette(pal)
    for yy in range(8 * 96):
        for xx in range(8):
            img.putpixel((xx, yy), 1)
    for code in range(32, 128):
        g = glyphs.get(amap.get(code, -1))
        if not g: continue
        w, h, px = g
        rows = [list(px[y * w:(y + 1) * w]) for y in range(h)]
        # Glyphs taller than the 8x8 cell (descenders, brackets, '?') lose
        # their least-inked interior rows instead of being cropped.
        while len(rows) > 8:
            ink = [(sum(1 for v in rows[y] if v), y) for y in range(1, len(rows) - 1)]
            rows.pop(min(ink)[1])
        if w > 8:
            print(f"warning: glyph {code} is {w} wide, cropped", file=sys.stderr)
        for y in range(len(rows)):
            for x in range(min(w, 8)):
                v = rows[y][x]
                if v:
                    img.putpixel((min(7, x + (8 - min(w, 8)) // 2), (code - 32) * 8 + y), 15)
    img.save(a.output)
    print(f"wrote {a.output}")


main()
