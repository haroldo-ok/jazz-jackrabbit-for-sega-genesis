#!/usr/bin/env python3
"""Convert the original episode-selection screen for the Genesis.

Reads MENU.000 (after the main-menu pictures: the difficulty palette and
screen, the episode palette, then one 134x110 picture per episode and one for
the bonus levels) and FONTMN2.0FN, and lays the screen out the way the game
does (positions and colour mapping as in OpenJazz's gamemenu.cpp; OpenJazz:
Copyright 2005-2017 AJ Thomson, 2015-2024 Carsten Teibes; GPL-2.0-or-later,
this converter is distributed under the same terms):

* "EPISODES" at the top right, the list on the left (tag + title per row,
  grey ramp), the picture of the selected episode on the right;
* the selected row is drawn black-on-white; episodes the cartridge does not
  carry are drawn in the red ramp.

The 320x200 screen is placed 2 pixels right and 10 down in the Genesis'
320x224, which puts every row's highlight bar on whole 8x8 tiles.

Output: packs/menu/episode_menu.inc (gitignored: it is made from the full
game's MENU.000).  The program includes it when present
(src/jj1_episode_menu.c).

Usage: build_episode_menu.py --input /path/to/JAZZ
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from build_planets import font as load_font  # noqa: E402
from import_jj1_shareware import Reader, load_palette  # noqa: E402

OX, OY = 2, 10
W, H = 320, 224
TAGS = ["1", "2", "3", "4", "5", "6", "a", "b", "c", "x", "z"]
TITLES = ["turtle terror", "ballistic bunny", "rabbits revenge", "gene machine",
          "the chase is on", "the final clash", "outta dis world", "turtle soup",
          "wild wabbit", "holiday hare", "bonus levels"]
ROWS = len(TAGS)
PIC_COL, PIC_ROW, PIC_W, PIC_H = 22, 6, 17, 15      # tile rectangle of the picture


def glyph_index(ch):
    o = ord(ch)
    if 48 <= o < 58: return o + 5
    if 65 <= o < 91: return o - 38
    if 97 <= o < 123: return o - 96
    return None


def draw_string(canvas, face, text, x, y, mapping):
    """OpenJazz Font::showString: glyph width + 2 per character."""
    space, height, glyphs = face
    for ch in text:
        g = glyph_index(ch)
        if g is None or g >= len(glyphs) or not glyphs[g][0]:
            x += space
            continue
        w, h, pix = glyphs[g]
        for yy in range(h):
            for xx in range(w):
                v = pix[yy * w + xx]
                if v and 0 <= x + xx < W and 0 <= y + yy < H:
                    canvas[y + yy, x + xx] = mapping(v)
        x += w + 2


def ramp(start, step):
    """Font::mapPalette(240, 8, start, step*8): colour 240+k -> start+step*k."""
    return lambda v: start + step * (v - 240) if 240 <= v < 248 else v


NORMAL, HIGHLIGHT, MISSING = ramp(9, 10), ramp(79, -10), ramp(94, -2)


def read_menu(path: Path):
    r = Reader(path.read_bytes())
    for _ in range(6):                    # normal and December main menus
        r.skip_rle()
    load_palette(r)
    r.rle(320 * 200)                      # difficulty screen
    palette = load_palette(r)
    pictures = []
    while r.pos < len(r.data):
        pictures.append(bytes(r.rle(134 * 110)))
    return palette, pictures


def vdp(rgb):
    r, g, b = (min(7, (c + 16) // 36) for c in rgb)
    return (b << 9) | (g << 5) | (r << 1)


def tiles_from(canvas, x0, y0, cols, rows, lut):
    """4bpp tiles of a canvas region; lut maps canvas values to 0-15."""
    out = []
    for ty in range(rows):
        for tx in range(cols):
            t = bytearray()
            for y in range(8):
                row = canvas[y0 + ty * 8 + y, x0 + tx * 8:x0 + tx * 8 + 8]
                for x in range(0, 8, 2):
                    t.append((lut[row[x]] << 4) | lut[row[x + 1]])
            out.append(bytes(t))
    return out


def text_screens(face):
    """The list and title in the three colourings."""
    def canvas_for(mapping_of_row):
        c = np.zeros((H, W), dtype=np.int32)
        draw_string(c, face, "EPISODES", 160 + 48 + OX, 8 + OY, NORMAL)
        for row in range(ROWS):
            mapping = mapping_of_row(row)
            y = 100 + (row << 4) - 92 + OY
            if mapping is HIGHLIGHT:
                c[100 + (row << 4) - 94 + OY:100 + (row << 4) - 94 + OY + 15, 6 + OX:6 + OX + 160] = 79
            draw_string(c, face, TAGS[row], 8 - (2 if row < 6 else 0) + OX, y, mapping)
            draw_string(c, face, TITLES[row], 8 + 16 + OX, y, mapping)
        return c
    return (canvas_for(lambda r: NORMAL), canvas_for(lambda r: HIGHLIGHT),
            canvas_for(lambda r: MISSING))


def picture_tiles(pixels, palette):
    """A 134x110 picture on its tile grid, in two 15-colour palettes (each
    tile uses the one that fits it better); colour 0 is black."""
    from PIL import Image
    img = np.zeros((PIC_H * 8, PIC_W * 8, 3), dtype=np.int32)
    src = np.array([palette[v] for v in pixels], dtype=np.int32).reshape(110, 134, 3)
    dx = (176 + OX) - PIC_COL * 8
    dy = (45 + OY) - PIC_ROW * 8
    img[dy:dy + 110, dx:dx + 134] = src
    tiles = [img[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8].reshape(64, 3)
             for ty in range(PIC_H) for tx in range(PIC_W)]
    # two groups of tiles by k-means on their mean colour
    means = np.array([t.mean(axis=0) for t in tiles])
    centres = means[[int(np.argmin(means.sum(1))), int(np.argmax(means.std(1) + means.sum(1)))]].astype(float)
    for _ in range(12):
        group = np.argmin(((means[:, None, :] - centres[None]) ** 2).sum(2), axis=1)
        for k in range(2):
            if np.any(group == k):
                centres[k] = means[group == k].mean(0)

    def quantise(pixels_rgb):
        im = Image.fromarray(pixels_rgb.reshape(1, -1, 3).astype(np.uint8), "RGB")
        q = im.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
        pal = q.getpalette()[:45]
        cols = [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
        cols = [tuple((min(7, (c + 16) // 36) * 255 + 3) // 7 for c in col) for col in cols]
        return [(0, 0, 0)] + cols + [(0, 0, 0)] * (15 - len(cols))

    pals = []
    for k in range(2):
        chosen = [t for t, g in zip(tiles, group) if g == k] or [tiles[0]]
        pals.append(np.array(quantise(np.concatenate(chosen)), dtype=np.int32))
    data, attrs = [], []
    for t in tiles:
        best = None
        for k, pal in enumerate(pals):
            d = ((t[:, None, :] - pal[None]) ** 2).sum(2)
            idx = d.argmin(1)
            err = d.min(1).sum()
            if best is None or err < best[0]:
                best = (err, k, idx)
        _, k, idx = best
        b = bytearray()
        for i in range(0, 64, 2):
            b.append((int(idx[i]) << 4) | int(idx[i + 1]))
        data.append(bytes(b))
        attrs.append(k)
    return data, attrs, [[vdp(c) for c in p] for p in pals]


def c_bytes(name, blob):
    rows = [", ".join("0x%02X" % b for b in blob[i:i + 16]) for i in range(0, len(blob), 16)]
    return f"static const u8 {name}[{len(blob)}] __attribute__((aligned(2))) = {{\n    " + ",\n    ".join(rows) + "\n};"


def packed(name, blob, work: Path):
    """aPLib (SGDK's aplib_unpack) of a blob, as a C array."""
    import os, subprocess
    raw, ap = work / (name + ".raw"), work / (name + ".ap")
    raw.write_bytes(blob)
    gdk = Path(os.environ.get("GDK", "/opt/sgdk"))
    subprocess.run(["java", "-jar", str(gdk / "bin" / "apj.jar"), "p", str(raw), str(ap)],
                   check=True, capture_output=True)
    return c_bytes(name, ap.read_bytes()), len(ap.read_bytes())


def c_words(name, words):
    rows = [", ".join("0x%04X" % w for w in words[i:i + 12]) for i in range(0, len(words), 12)]
    return f"static const u16 {name}[{len(words)}] = {{\n    " + ",\n    ".join(rows) + "\n};"


def convert(data: Path, out: Path):
    palette, pictures = read_menu(data / "MENU.000")
    face = load_font(data / "FONTMN2.0FN")
    normal, high, missing = text_screens(face)
    # PAL1: black, the grey ramp 19..79, the red ramp 80..94
    text_cols = [0] + list(range(19, 80, 10)) + list(range(80, 95, 2))
    lut = np.zeros(256, dtype=np.int32)
    for i, c in enumerate(text_cols):
        lut[c] = i
    lut[9] = 0                                   # palette 9 is black too
    pal1 = [vdp(palette[c]) for c in text_cols]
    # whole screen: unique tiles + map; then 20x2-tile row strips
    base = tiles_from(normal, 0, 0, 40, 28, lut)
    uniq, index = {}, []
    for t in base:
        index.append(uniq.setdefault(t, len(uniq)))
    base_tiles = b"".join(sorted(uniq, key=uniq.get))
    strips = bytearray()
    for canvas in (high, missing):
        for row in range(ROWS):
            for t in tiles_from(canvas, 8, 16 + 16 * row, 20, 2, lut):
                strips += t
    work = out.parent / "work"
    work.mkdir(parents=True, exist_ok=True)
    total = 0
    base_c, n = packed("epmenu_base_tiles", base_tiles, work); total += n
    strip_c = []
    for k in range(2 * ROWS):
        c, n = packed(f"epmenu_strip{k}", bytes(strips[k * 1280:(k + 1) * 1280]), work)
        strip_c.append(c); total += n
    lines = ["/* Generated by tools/build_episode_menu.py from MENU.000 and FONTMN2.0FN; do not edit.",
             " * The tile data is aPLib-packed (unpacked into RAM, then sent to VRAM). */",
             f"#define EPMENU_ROWS {ROWS}",
             f"#define EPMENU_PICTURES {len(pictures)}",
             f"#define EPMENU_BASE_TILES {len(uniq)}",
             f"#define EPMENU_PIC_COL {PIC_COL}", f"#define EPMENU_PIC_ROW {PIC_ROW}",
             f"#define EPMENU_PIC_W {PIC_W}", f"#define EPMENU_PIC_H {PIC_H}",
             c_words("epmenu_text_pal", pal1),
             base_c,
             c_words("epmenu_base_map", index)] + strip_c + [
             "/* strips: 0..ROWS-1 highlighted, ROWS..2*ROWS-1 not on this cartridge */",
             "static const u8 *const epmenu_strip[] = { " + ", ".join(f"epmenu_strip{k}" for k in range(2 * ROWS)) + " };"]
    pics = []
    for i, px in enumerate(pictures):
        data_, attrs, pals = picture_tiles(px, palette)
        c, n = packed(f"epmenu_pic{i}", b"".join(data_), work)
        lines.append(c); total += n
        lines.append(c_words(f"epmenu_pic{i}_pal", pals[0] + pals[1]))
        lines.append(f"static const u8 epmenu_pic{i}_attr[{len(attrs)}] = {{ " + ", ".join(map(str, attrs)) + " };")
        pics.append(i)
    lines.append("static const u8 *const epmenu_pic[] = { " + ", ".join(f"epmenu_pic{i}" for i in pics) + " };")
    lines.append("static const u16 *const epmenu_pic_pal[] = { " + ", ".join(f"epmenu_pic{i}_pal" for i in pics) + " };")
    lines.append("static const u8 *const epmenu_pic_attr[] = { " + ", ".join(f"epmenu_pic{i}_attr" for i in pics) + " };")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    total += len(index) * 2 + len(pictures) * (PIC_W * PIC_H + 64)
    print(f"{out}: {len(pictures)} pictures, {len(uniq)} screen tiles, {total // 1024} KB")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=ROOT / "packs" / "menu" / "episode_menu.inc")
    a = ap.parse_args()
    convert(a.input, a.out)
