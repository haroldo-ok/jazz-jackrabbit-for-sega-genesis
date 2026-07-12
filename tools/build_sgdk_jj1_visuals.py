#!/usr/bin/env python3
"""Generate compact Genesis 4bpp visuals from a user-supplied JJ1 install.

The source material is never downloaded by this tool. It uses the decoder in
import_jj1_shareware.py and emits a C include containing:
  * 7 original-art 8x8 terrain/detail patterns,
  * the first original 32x32 Jazz animation frame as 16 Genesis tiles,
  * a shared 15-colour Genesis palette.

This is intentionally a first VRAM-budgeted visual pack, not a substitute for
the full streaming tile cache needed to display all 240 32x32 JJ1 block tiles.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from import_jj1_shareware import TRANSPARENT, decode_blocks, decode_mainchar


def rgb333(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    return ((r * 7 // 255) << 1) | ((g * 7 // 255) << 5) | ((b * 7 // 255) << 9)


def choose_palette(images: list[Image.Image]) -> list[tuple[int, int, int]]:
    pixels: list[tuple[int, int, int]] = []
    for im in images:
        pixels.extend(im.convert("RGB").getdata())
    source = Image.new("RGB", (len(pixels), 1))
    source.putdata(pixels)
    # Median cut returns stable colour choices and works without external tools.
    q = source.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
    raw = q.getpalette()[:45]
    return [tuple(raw[i:i + 3]) for i in range(0, 45, 3)]


def quantize_pixels(im: Image.Image, palette: list[tuple[int, int, int]], transparent_mask: list[bool]) -> list[int]:
    data = list(im.convert("RGB").getdata())
    out: list[int] = []
    for color, transparent in zip(data, transparent_mask):
        if transparent:
            out.append(0)
            continue
        best = min(range(len(palette)), key=lambda n: sum((color[c] - palette[n][c]) ** 2 for c in range(3)))
        out.append(best + 1)
    return out


def encode_tile(indices: list[int]) -> list[int]:
    if len(indices) != 64:
        raise ValueError("Genesis tile must be 8x8")
    words: list[int] = []
    for y in range(8):
        word = 0
        for x in range(8):
            word = (word << 4) | indices[y * 8 + x]
        words.append(word)
    return words


def emit_u32_array(name: str, words: list[int]) -> str:
    lines = [f"const u32 {name}[{len(words)}] = {{"]
    for start in range(0, len(words), 4):
        lines.append("    " + ", ".join(f"0x{value:08X}" for value in words[start:start + 4]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True, help="installed JJ1 directory")
    parser.add_argument("--output", type=Path, required=True, help="generated .inc path")
    args = parser.parse_args()

    tiles, source_palette, _sky = decode_blocks(args.input / "BLOCKS.000")
    sprites = decode_mainchar(args.input / "MAINCHAR.000", args.input / "SPRITES.000")
    # The first eight MAINCHAR records are consecutive original Jazz walk
    # frames in the Episode 1 shareware data. Keep all of them resident so the
    # 68000 can animate without a cartridge/VRAM upload every frame.
    player_frames = sprites[:8]
    if any(frame is None for frame in player_frames):
        raise SystemExit("MAINCHAR.000 initial Jazz animation frames are unavailable")
    # Spring event animations 33/34/36 point to red/green/blue frames.
    spring_frames = [sprites[123], sprites[125], sprites[127]]
    if any(frame is None for frame in spring_frames):
        raise SystemExit("JJ1 spring sprites are unavailable")

    # Each item is an original 32x32 block and a representative 8x8 region.
    # The order maps to empty/dirt/grass/platform/exit/star/hill in the game.
    selections = [(6, 0, 0), (50, 8, 8), (15, 8, 8), (17, 8, 8), (42, 8, 8), (6, 8, 8), (34, 8, 8)]
    terrain_images: list[Image.Image] = []
    terrain_masks: list[list[bool]] = []
    for tile_id, ox, oy in selections:
        raw = tiles[tile_id]
        full = Image.frombytes("P", (32, 32), raw)
        full.putpalette([component for rgb in source_palette for component in rgb])
        terrain_images.append(full.crop((ox, oy, ox + 8, oy + 8)))
        terrain_masks.append([raw[(oy + y) * 32 + ox + x] == TRANSPARENT for y in range(8) for x in range(8)])

    player_images: list[Image.Image] = []
    player_masks: list[list[bool]] = []
    for frame in player_frames:
        assert frame is not None
        width, height, player_pixels = frame
        if width < 32 or height < 32:
            raise SystemExit(f"unexpected Jazz sprite size {width}x{height}")
        raw_player = Image.frombytes("P", (width, height), player_pixels)
        raw_player.putpalette([component for rgb in source_palette for component in rgb])
        player_images.append(raw_player.crop((0, 0, 32, 32)))
        player_masks.append([player_pixels[y * width + x] == 254 for y in range(32) for x in range(32)])

    spring_images: list[Image.Image] = []
    spring_masks: list[list[bool]] = []
    for spring_frame in spring_frames:
        assert spring_frame is not None
        spring_w, spring_h, spring_pixels = spring_frame
        raw_spring = Image.frombytes("P", (spring_w, spring_h), spring_pixels)
        raw_spring.putpalette([component for rgb in source_palette for component in rgb])
        spring_images.append(raw_spring.crop((0, 0, 32, 16)))
        spring_masks.append([spring_pixels[y * spring_w + x] == 254 for y in range(16) for x in range(32)])

    palette = choose_palette(terrain_images + player_images + spring_images)
    world_words: list[int] = []
    for image, mask in zip(terrain_images, terrain_masks):
        world_words.extend(encode_tile(quantize_pixels(image, palette, mask)))

    player_words: list[int] = []
    for player_image, player_mask in zip(player_images, player_masks):
        player_indices = quantize_pixels(player_image, palette, player_mask)
        # Genesis VDP multi-tile sprites fetch tiles down each column before
        # advancing to the next column (not normal image row-major ordering).
        for tx in range(4):
            for ty in range(4):
                block = [player_indices[(ty * 8 + y) * 32 + tx * 8 + x] for y in range(8) for x in range(8)]
                player_words.extend(encode_tile(block))

    spring_words: list[int] = []
    for spring_image, spring_mask in zip(spring_images, spring_masks):
        spring_indices = quantize_pixels(spring_image, palette, spring_mask)
        for tx in range(4):
            for ty in range(2):
                block = [spring_indices[(ty * 8 + y) * 32 + tx * 8 + x] for y in range(8) for x in range(8)]
                spring_words.extend(encode_tile(block))

    output = [
        "/* Generated from a user-supplied JJ1 shareware installation. */",
        "/* Do not regenerate this file without the corresponding legal game data. */",
        emit_u32_array("jazz_world_tiles", world_words), "",
        emit_u32_array("jazz_player_tiles", player_words), "",
        emit_u32_array("jazz_spring_tiles", spring_words), "",
        "const u16 jazz_pal1[16] = {",
        "    RGB3_3_3_TO_VDPCOLOR(0,0,0),",
    ]
    output.extend(f"    0x{rgb333(color):04X}," for color in palette)
    output.append("};")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(output) + "\n")
    print(f"Generated {args.output}: terrain, 8 Jazz frames, and three original spring variants")


if __name__ == "__main__":
    main()
