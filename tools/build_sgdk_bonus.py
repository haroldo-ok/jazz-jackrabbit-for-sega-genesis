#!/usr/bin/env python3
"""Convert JJ1 bonus levels for the Genesis.

Reads BONUSMAP.00n, BONUSY.000 (floor tiles/background) and BONUS.000 (item
sprites) from an installed game and writes:
  res/bonus/tiles.bin      60 floor tiles, 32x32 bytes of palette index 1..15
  res/bonus/tiles_hi.bin   the same, each byte shifted left by 4
  res/bonus/masks.bin      60 x 64 collision cells (4x4 px each)
  res/bonus/grid<n>.bin    256x256 tile map per level
  res/bonus/events<n>.bin  256x256 event map per level
  res/bonus/bg.bin         512x20 background strip as 4bpp tiles (64x3)
  src/jj1_bonus_data.inc   palette, per-level metadata, item sprites, sine table
Usage: build_sgdk_bonus.py --input <install dir> [--out-res res/bonus] [--out-inc src/jj1_bonus_data.inc]
"""
import argparse, math, sys
from pathlib import Path
from PIL import Image
sys.path.insert(0, str(Path(__file__).parent))
import bonus_parse as bp

ITEM_SPRITES = {1: 46, 2: 47, 3: 48, 4: 49}   # event id -> BONUS.000 sprite (clock, gem, glove, exit)
SIZES = [(4, 32), (3, 24), (2, 16), (1, 8)]    # tiles per side, pixels


def snap(c):
    return tuple(min(255, round(v / 255 * 7) * 255 // 7) for v in c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--out-res", default="res/bonus")
    ap.add_argument("--out-inc", default="src/jj1_bonus_data.inc")
    a = ap.parse_args()
    root = Path(a.input)
    outres = Path(a.out_res); outres.mkdir(parents=True, exist_ok=True)
    bg, pal, tiles = bp.parse_tileset(root / "BONUSY.000")
    sprites = bp.parse_sprites(root / "BONUS.000")
    levels = [bp.parse_level(root / f"BONUSMAP.00{n}") for n in range(3)]

    # ---- collision masks from the original index values (before quantising)
    masks = bytearray(60 * 64)
    for t in range(60):
        for y in range(32):
            for x in range(32):
                if (tiles[t * 1024 + y * 32 + x] & 240) == 192:
                    masks[t * 64 + ((y << 1) & 56) + ((x >> 2) & 7)] = 1
    # ---- one 15-colour palette for floor, background and item sprites
    samples = []
    for v in tiles: samples.append(snap(pal[v]))
    for v in range(512 * 20): samples.append(snap(pal[bg[(v // 512) * 832 + v % 512]]))
    for sid in ITEM_SPRITES.values():
        w, h, px, m = sprites[sid]
        for j in range(w * h):
            if m[j] and px[j]: samples.append(snap(pal[px[j]]))   # value 0 is the colour key
    side = 512
    img = Image.new("RGB", (side, (len(samples) + side - 1) // side))
    img.putdata(samples + [samples[0]] * (side * img.height - len(samples)))
    q = img.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    qp = q.getpalette()[:45]
    cols = [tuple(qp[i * 3:i * 3 + 3]) for i in range(15)]
    palette = [(0, 0, 0)] + cols

    def nearest(c):
        c = snap(c)
        return 1 + min(range(15), key=lambda i: sum((cols[i][k] - c[k]) ** 2 for k in range(3)))

    cache = {}
    def idx(orig):
        if orig not in cache: cache[orig] = nearest(pal[orig])
        return cache[orig]

    (outres / "tiles.bin").write_bytes(bytes(idx(v) for v in tiles))
    # The same texels shifted into the high nibble: the floor renderer reads
    # the left sample of each pair from here (src/floor_asm.s).
    (outres / "tiles_hi.bin").write_bytes(bytes(idx(v) << 4 for v in tiles))
    (outres / "masks.bin").write_bytes(bytes(masks))
    for n, L in enumerate(levels):
        (outres / f"grid{n}.bin").write_bytes(bytes(min(59, t) for t in L["tiles"]))
        (outres / f"events{n}.bin").write_bytes(bytes(L["events"]))

    # ---- background strip, 64x3 tiles of 4bpp
    strip = [[idx(bg[y * 832 + x]) if y < 20 else 1 for x in range(512)] for y in range(24)]
    bgtiles = bytearray()
    for ty in range(3):
        for tx in range(64):
            for y in range(8):
                row = strip[ty * 8 + y][tx * 8:tx * 8 + 8]
                for k in range(0, 8, 2):
                    bgtiles.append((row[k] << 4) | row[k + 1])
    (outres / "bg.bin").write_bytes(bytes(bgtiles))

    # ---- item sprites, scaled to 4 sizes, VDP column-major 4bpp tiles
    def sprite_tiles(sid, tiles_n, px_size):
        w, h, px, m = sprites[sid]
        im = Image.new("P", (w, h), 0)
        data = [idx(px[j]) if (m[j] and px[j]) else 0 for j in range(w * h)]
        im.putdata(data)
        scale = min(px_size / w, px_size / h)
        nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
        im = im.resize((nw, nh), Image.NEAREST)
        canvas = Image.new("P", (px_size, px_size), 0)
        canvas.paste(im, ((px_size - nw) // 2, (px_size - nh) // 2))
        c = list(canvas.getdata())
        out = []
        for tx in range(tiles_n):
            for ty in range(tiles_n):
                for y in range(8):
                    word = 0
                    for x in range(8):
                        word = (word << 4) | c[(ty * 8 + y) * px_size + tx * 8 + x]
                    out.append(word)
        return out

    lines = ["/* Generated by tools/build_sgdk_bonus.py - do not edit. */"]
    lines.append("static const u16 jj1_bonus_palette[16] = {")
    lines.append("    " + ", ".join("0x%03X" % (((b >> 5) << 9) | ((g >> 5) << 5) | ((r >> 5) << 1)) for r, g, b in palette))
    lines.append("};")
    # The sky is the original's gradient (palette entries 128-171, black at the
    # top to light blue at the horizon) on its own palette line, so blue items
    # stay visible against it.
    sky = [(0, 0, 0)] + [snap(pal[128 + round(k * 43 / 14)]) for k in range(15)]
    lines.append("static const u16 jj1_bonus_sky_palette[16] = {")
    lines.append("    " + ", ".join("0x%03X" % (((b >> 5) << 9) | ((g >> 5) << 5) | ((r >> 5) << 1)) for r, g, b in sky))
    lines.append("};")
    for ev, sid in ITEM_SPRITES.items():
        for n, (t, p) in enumerate(SIZES):
            words = sprite_tiles(sid, t, p)
            lines.append(f"static const u32 jj1_bonus_item{ev}_s{n}[{len(words)}] = {{")
            lines.append("    " + ", ".join("0x%08X" % w for w in words))
            lines.append("};")
    lines.append("static const struct { u16 seconds, items; u8 startX, startY; u8 type[16], passable[16], used[16], sound[16]; } jj1_bonus_meta[3] = {")
    for L in levels:
        e = L["evt"]
        j = lambda k: "{" + ",".join(str(x[k]) for x in e) + "}"
        lines.append(f"    {{ {L['seconds']}, {L['items']}, {L['px']}, {L['py']}, {j('type')}, {j('passable')}, {j('used')}, {j('sound')} }},")
    lines.append("};")
    sine = [round(math.sin(i * 2 * math.pi / 1024) * 1024) for i in range(1024)]
    lines.append("static const s16 jj1_bonus_sin[1024] = {")
    for i in range(0, 1024, 16):
        lines.append("    " + ", ".join(str(v) for v in sine[i:i + 16]) + ",")
    lines.append("};")
    Path(a.out_inc).write_text("\n".join(lines) + "\n")
    print("palette", palette)
    print("sound names", [L["names"][:4] for L in levels][0])


main()
