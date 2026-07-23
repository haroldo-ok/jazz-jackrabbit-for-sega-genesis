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
from build_sgdk_jj1_visuals import (  # noqa: E402
    choose_palette,
    encode_tile,
    quantize_pixels,
    rgb333,
)

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

    # Choose the colour line from the blocks this world actually defines.
    real_images, _ = block_images(tiles, source_palette, defined)
    palette = choose_palette(real_images)

    # Then encode the full bank, empty tail included, so the stride is fixed.
    images, masks = block_images(tiles, source_palette)
    words = []
    for image, mask in zip(images, masks):
        words.extend(encode_tile(quantize_pixels(image, palette, mask)))

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

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(out) + "\n")
    print(f"wrote {args.output}: {expected} words for world {args.world} "
          f"({defined} blocks defined)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
