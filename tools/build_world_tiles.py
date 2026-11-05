#!/usr/bin/env python3
"""Emit one world's block tile bank.

Every level in a world shares its terrain, so the 240 blocks of BLOCKS.<world>
are converted once into Genesis patterns and referenced by each level of that
world.  The shareware worlds' banks were checked into the repository, which is
why only those episodes could be built; this produces the same file for any
world, so an episode spanning worlds 003-005 (or any other set) can be built
from its own data.

A block is 32x32 pixels, which is 16 patterns of 8x8, giving
240 * 16 * 8 = 30720 words per bank.  The bank carries its own 16-colour line,
independent of the sprite palette, since terrain and sprites occupy different
palette lines on the Genesis.

Usage:
    python3 tools/build_world_tiles.py --input DIR --world 003 \\
        --output src/jj1_world003_tiles.inc
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_jj1_shareware import decode_blocks  # noqa: E402
from import_jj1_shareware import TRANSPARENT  # noqa: E402
from build_sgdk_jj1_visuals import (  # noqa: E402
    encode_tile,
    quantize_pixels,
    rgb333,
)
from collections import Counter
import jj1_cycle  # noqa: E402

BLOCKS_PER_WORLD = 240
BLOCK_PX = 32
TILE_PX = 8
TILES_PER_BLOCK = (BLOCK_PX // TILE_PX) ** 2


def block_images(tiles, source_palette, limit=None):
    """Blocks as sixteen 8x8 images each, in the order the VDP reads them.

    `limit` caps how many blocks are converted.  The bank always holds 240
    slots because the ROM indexes it at a fixed stride, but a world can define
    far fewer - several define 120 - and the empty tail must not be handed to
    the palette chooser.  Letting it through means half the pixels are blank,
    median cut spends the 15 colours on shades of nothing, and the world comes
    out looking washed out.
    """
    images, masks = [], []
    for index in range(limit if limit is not None else BLOCKS_PER_WORLD):
        raw = tiles[index] if index < len(tiles) else bytes(BLOCK_PX * BLOCK_PX)
        full = Image.frombytes("P", (BLOCK_PX, BLOCK_PX), raw)
        full.putpalette([c for rgb in source_palette for c in rgb])
        # Row-major within the block: the map draws these as a 4x4 patch.
        for ty in range(BLOCK_PX // TILE_PX):
            for tx in range(BLOCK_PX // TILE_PX):
                ox, oy = tx * TILE_PX, ty * TILE_PX
                images.append(full.crop((ox, oy, ox + TILE_PX, oy + TILE_PX)))
                # Terrain is opaque; nothing is keyed out of a block.
                masks.append([False] * (TILE_PX * TILE_PX))
    return images, masks


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", type=Path, required=True)
    ap.add_argument("--world", required=True, help="BLOCKS suffix, e.g. 003")
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()

    blocks_file = args.input / f"BLOCKS.{args.world}"
    if not blocks_file.exists():
        print(f"missing {blocks_file}", file=sys.stderr)
        return 1

    tiles, source_palette, _sky = decode_blocks(blocks_file)
    defined = min(len(tiles), BLOCKS_PER_WORLD)

    # Reserve palette slots for the ranges the engine rotates, then spend the
    # remaining colours on everything else.
    cycles = jj1_cycle.plan(tiles, defined, source_palette)
    reserved = sum(c["k"] for c in cycles)
    static_n = 15 - reserved
    cyc_idx = {i for c in cycles
               for i in range(jj1_cycle.RANGES[c["n"]][0],
                              jj1_cycle.RANGES[c["n"]][0] + jj1_cycle.RANGES[c["n"]][1])}
    pix = []
    for block in tiles[:defined]:
        pix.extend(source_palette[p] for p in block
                   if p not in cyc_idx and p != TRANSPARENT)
    src = Image.new("RGB", (max(1, len(pix)), 1))
    src.putdata(pix or [(0, 0, 0)])
    q = src.quantize(colors=static_n, method=Image.Quantize.MEDIANCUT)
    raw = q.getpalette()[:static_n * 3]
    static = [tuple(raw[i:i + 3]) for i in range(0, static_n * 3, 3)]
    palette = [(0, 0, 0)] + static          # slot 0 + static colours (1..static_n)
    slot = 1 + static_n
    for c in cycles:
        first, count, _ = jj1_cycle.RANGES[c["n"]]
        c["slot"] = slot
        use = Counter()
        for block in tiles[:defined]:
            use.update(block)
        cols = [tuple(int(round(v)) for v in col)
                for col in jj1_cycle.group_colours(c["n"], c["k"], c["off"], use, source_palette)]
        c["colors"] = cols
        palette.extend(cols)
        slot += c["k"]
    assert len(palette) == 16, len(palette)

    words = []
    lookup = {}
    sp = [tuple(c) for c in source_palette]
    for blk in range(BLOCKS_PER_WORLD):
        raw = tiles[blk] if blk < len(tiles) else bytes(BLOCK_PX * BLOCK_PX)
        for ty in range(4):
            for tx in range(4):
                idxs = []
                for y in range(8):
                    for x in range(8):
                        p = raw[(ty * 8 + y) * BLOCK_PX + tx * 8 + x]
                        key = p
                        if key not in lookup:
                            hit = 0 if p == TRANSPARENT else None   # sky shows through
                            for c in (cycles if hit is None else []):
                                g = jj1_cycle.group_of(p, c)
                                if g is not None:
                                    hit = c["slot"] + g
                            if hit is None:
                                col = sp[p]
                                hit = 1 + min(range(static_n),
                                              key=lambda n: sum((col[k] - static[n][k]) ** 2 for k in range(3)))
                            lookup[key] = hit
                        idxs.append(lookup[key])
                words.extend(encode_tile(idxs))

    expected = BLOCKS_PER_WORLD * TILES_PER_BLOCK * 8
    if len(words) != expected:
        print(f"expected {expected} words, produced {len(words)}", file=sys.stderr)
        return 1

    name = f"jj1_world{args.world}"
    out = [
        "/* Generated from user-supplied JJ1 BLOCKS data. */",
        f"/* BLOCKS.{args.world}: {BLOCKS_PER_WORLD} x {TILES_PER_BLOCK} Genesis patterns,"
        " shared by every level",
        "   in this world. */",
        f"const u32 {name}_block_tiles[{expected}] = {{",
    ]
    for i in range(0, len(words), 4):
        row = ", ".join(f"0x{w:08X}" for w in words[i:i + 4])
        out.append(f"    {row},")
    out += ["};", "", f"const u16 {name}_palette[16] = {{",
            "    RGB3_3_3_TO_VDPCOLOR(0,0,0),"]
    for colour in palette[1:16]:
        out.append(f"    0x{rgb333(colour):04X},")
    out.append("};")
    out += ["", f"#define JJ1_WORLD{args.world}_CYCLES {len(cycles)}",
            f"const Jj1Cycle {name}_cycles[{max(1, len(cycles))}] = {{"]
    for c in cycles:
        first, count, speed = jj1_cycle.RANGES[c["n"]]
        cols = ", ".join(f"0x{rgb333(col):04X}" for col in c["colors"])
        cols += ", 0" * (4 - c["k"])
        out.append(f"    {{ {c['slot']}, {c['k']}, {1 << c['n']}, "
                   f"{jj1_cycle.step_frames(c)}, {1 if speed > 0 else -1}, {{ {cols} }} }},")
    if not cycles:
        out.append("    { 0, 0, 0, 1, 1, { 0, 0, 0, 0 } },")
    out += ["};"]

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(out) + "\n")
    print(f"wrote {args.output}: {expected} words for world {args.world} "
          f"({defined} blocks defined)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
