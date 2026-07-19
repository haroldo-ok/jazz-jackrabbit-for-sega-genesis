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

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).parent))
from import_jj1_shareware import (SKEY, TRANSPARENT, decode_anims, decode_blocks,
                                  decode_mainchar, decode_sprite_set)
from build_sgdk_jj1_level_data import runtime_records

JJ1_ENEMY_SLOT_TILES = 48   # keep in sync with inc/jazz_gfx.h


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



# --- event -> animation -> sprite resolution -------------------------------
#
# The original never hard-codes sprite numbers.  Each event record names an
# animation (byte 6 is the right-facing one, byte 5 the left-facing), each
# animation record lists the sprite index of every frame, and those indices
# address SPRITES.<world>.  Following that chain is the only way to get the
# right art for an event: sprite indices differ per world, so the old approach
# of hard-coding "spring = sprite 123" was guesswork that broke on any other
# level set.
E_RIGHTANIM = 6
E_LEFTANIM = 5
MAX_FRAMES = 4          # frames kept per event; JJ1 anims are short loops
MAX_PLAYER_FRAMES = 6   # frames kept per Jazz animation state


def event_frames(eventset: bytes, anims: list[dict], event_id: int, facing: str = "R") -> list[dict]:
    """Frames (sprite index + offsets) for one facing of an event's animation.

    JJ1 stores a separate sprite set for each direction (E_RIGHTANIM at byte 6,
    E_LEFTANIM at byte 5) and the engine picks by travel direction rather than
    hardware-flipping - the "right" set may itself be drawn facing left.  So we
    extract both and let the ROM choose, instead of flipping one.
    """
    if not (0 < event_id < 128):
        return []
    # eventSet[i] is loaded from buffer[i * 32] and looked up as
    # eventSet[grid.event]: record i IS event i (no off-by-one).
    record = eventset[event_id * 32:(event_id + 1) * 32]
    if len(record) < 32:
        return []
    primary, secondary = (E_RIGHTANIM, E_LEFTANIM) if facing == "R" else (E_LEFTANIM, E_RIGHTANIM)
    anim_id = record[primary] or record[secondary]
    if not anim_id or anim_id >= len(anims):
        return []
    return anims[anim_id]["frames"][:MAX_FRAMES]


# The Genesis caps one hardware sprite at 4x4 tiles (32x32 px), but JJ1 enemies
# are bigger than that - the Turtle Goon is 68x45, about twice Jazz's size.  So
# a frame is stored as a grid of up to 2x2 "chunks", each chunk being at most
# 4x4 tiles, and the ROM draws one hardware sprite per chunk.  Tiles inside a
# chunk are column-major, which is the order the VDP fetches them in.
MAX_CELL_W = 64      # 8 tiles
MAX_CELL_H = 48      # 6 tiles


def sprite_to_image(sprite, source_palette, w, h):
    """Place an original sprite in a w x h cell: centred, standing on its feet."""
    width, height, pixels = sprite
    raw = Image.frombytes("P", (width, height), pixels)
    raw.putpalette([c for rgb in source_palette for c in rgb])
    cell = Image.new("P", (w, h), SKEY)
    cell.putpalette([c for rgb in source_palette for c in rgb])
    ox = (w - width) // 2
    oy = h - height
    cell.paste(raw, (ox, oy))       # PIL clips negative offsets for us
    mask = []
    for y in range(h):
        for x in range(w):
            sx, sy = x - ox, y - oy
            if 0 <= sx < width and 0 <= sy < height:
                mask.append(pixels[sy * width + sx] == SKEY)
            else:
                mask.append(True)
    return cell.convert("RGB"), mask


def cell_size(frames) -> tuple[int, int]:
    """Cell big enough for every frame of one event, in whole tiles."""
    w = max(f[0] for f in frames)
    h = max(f[1] for f in frames)
    w = min(((w + 7) // 8) * 8, MAX_CELL_W)
    h = min(((h + 7) // 8) * 8, MAX_CELL_H)
    return max(w, 8), max(h, 8)


def encode_sprite_tiles(indices: list[int], w: int, h: int) -> list[int]:
    """Emit a cell as up to 2x2 chunks of at most 4x4 tiles, column-major inside
    each chunk, so every chunk can be handed to one hardware sprite directly."""
    tiles_w, tiles_h = w // 8, h // 8
    words: list[int] = []
    for cy in range(0, tiles_h, 4):
        for cx in range(0, tiles_w, 4):
            for tx in range(cx, min(cx + 4, tiles_w)):
                for ty in range(cy, min(cy + 4, tiles_h)):
                    block = [indices[(ty * 8 + y) * w + tx * 8 + x]
                             for y in range(8) for x in range(8)]
                    words.extend(encode_tile(block))
    return words


def main() -> None:
    parser = argparse.ArgumentParser()
    # Must match inc/jazz_gfx.h.
    global JJ1_ENEMY_SLOT_TILES
    parser.add_argument("--input", type=Path, required=True, help="installed JJ1 directory")
    parser.add_argument("--output", type=Path, required=True, help="generated .inc path")
    parser.add_argument("--level", default="LEVEL0.000",
                        help="level whose event/animation records name the sprites")
    parser.add_argument("--world", default="000",
                        help="BLOCKS/SPRITES suffix for that level's world")
    parser.add_argument("--name", default="jj1_level0",
                        help="symbol prefix; each level has its own animation "
                             "table, so sprites are resolved per level")
    parser.add_argument("--shared", action="store_true",
                        help="also emit the symbols only needed once (jazz_world_tiles)")
    parser.add_argument("--preview", type=Path,
                        help="write a labelled contact sheet of the extracted "
                             "enemy frames, to check against a reference sheet")
    args = parser.parse_args()

    tiles, source_palette, _sky = decode_blocks(args.input / f"BLOCKS.{args.world}")
    sprite_file = args.input / f"SPRITES.{args.world}"
    # loadSprites() in the original "loads all the sprites, not just those in
    # fileName": it walks MAINCHAR.000 and SPRITES.<world> in parallel, the
    # world file overriding.  decode_mainchar does exactly that merge, and the
    # merged list is the bank the animation records index into.  Reading only
    # SPRITES.<world> yields a mostly-empty bank.
    sprites = decode_mainchar(args.input / "MAINCHAR.000", sprite_file)
    bank = sprites
    _grid, _masks, eventset, _sx, _sy, anim_bytes, player_anims, level_anims = runtime_records(args.input / args.level)
    anims = decode_anims(anim_bytes)
    # The first eight MAINCHAR records are consecutive original Jazz walk
    # frames in the Episode 1 shareware data. Keep all of them resident so the
    # 68000 can animate without a cartridge/VRAM upload every frame.
    # Jazz's animation states come from the level's MT_P_ANIMS block, resolved
    # through the same animation table as everything else, instead of assuming
    # the first eight sprites are the whole character.  Map the port's states
    # to the original PA_ indices (right-facing; the ROM flips for left).
    PA_RWALK, PA_RJUMP, PA_RSHOOT, PA_RCROUCH, PA_RFALL = 1, 3, 7, 9, 11
    PA_RSTAND, PA_LOOKUP, PA_RRUN, PA_RSTOP, PA_RHURT, PA_RSPRING = 19, 24, 29, 35, 13, 36
    PA_RBOARD = 17
    player_state_anims = [
        PA_RSTAND, PA_RWALK, PA_RRUN, PA_RJUMP, PA_RFALL, PA_RSHOOT,
        PA_RCROUCH, PA_LOOKUP, PA_RSTOP, PA_RHURT, PA_RSPRING, PA_RBOARD,
    ]

    def frames_for_player_anim(pa_index: int) -> list:
        anim_id = player_anims[pa_index] if pa_index < len(player_anims) else 0
        out = []
        if anim_id and anim_id < len(anims):
            for frame in anims[anim_id]["frames"][:MAX_PLAYER_FRAMES]:
                sprite = bank[frame["sprite"]] if frame["sprite"] < len(bank) else None
                if sprite is not None:
                    out.append(sprite)
        if not out:                       # fall back to the standing frame
            out = [sprites[0]] if sprites and sprites[0] else []
        return out

    player_state_frames = [frames_for_player_anim(pa) for pa in player_state_anims]

    # Level animations (JJ1LANIMS) hold art that is not tied to any event: the
    # bird companion, the airboard and the shields.  LA_RBIRD/LA_LBIRD are 7/6
    # and LA_1SHIELD/LA_4SHIELD are 10/3.
    LA_4SHIELD, LA_LBIRD, LA_RBIRD, LA_1SHIELD = 3, 6, 7, 10

    def frames_for_level_anim(index):
        anim_id = level_anims[index] if index < len(level_anims) else 0
        out = []
        if anim_id and anim_id < len(anims):
            for frame in anims[anim_id]["frames"][:MAX_FRAMES]:
                sprite = bank[frame["sprite"]] if frame["sprite"] < len(bank) else None
                if sprite is not None:
                    out.append(sprite)
        return out

    bird_frames = frames_for_level_anim(LA_RBIRD) or frames_for_level_anim(LA_LBIRD)
    bird_left_frames = frames_for_level_anim(LA_LBIRD)
    shield_frames = frames_for_level_anim(LA_1SHIELD) or frames_for_level_anim(LA_4SHIELD)
    blank = (8, 8, bytes([SKEY]) * 64)
    if not bird_frames: bird_frames = [blank]
    if not bird_left_frames: bird_left_frames = bird_frames
    if not shield_frames: shield_frames = [blank]

    # Resolve every event that appears in this level through its animation,
    # rather than guessing sprite numbers.  JJ1 keeps separate art for each
    # travel direction and picks by direction (never hardware-flipping), so we
    # resolve both facings.  When a level only ever draws one facing, or the two
    # are the same sprites, the second is dropped to avoid doubling the data.
    used = sorted({b for b in _grid[1::2] for b in (b & 0x7F,) if b})

    def resolve(event_id, facing):
        out = []
        for frame in event_frames(eventset, anims, event_id, facing):
            sprite = bank[frame["sprite"]] if frame["sprite"] < len(bank) else None
            if sprite is not None:
                out.append((frame["sprite"], sprite))
        return out

    event_sprites: dict[int, list] = {}       # right-facing frames
    event_sprites_left: dict[int, list] = {}   # left-facing frames, if distinct
    for event_id in used:
        right = resolve(event_id, "R")
        left = resolve(event_id, "L")
        if not right and not left:
            continue
        if not right:
            right = left
        event_sprites[event_id] = [s for _, s in right]
        # Only keep a distinct left set when the sprite ids actually differ.
        if left and [i for i, _ in left] != [i for i, _ in right]:
            event_sprites_left[event_id] = [s for _, s in left]
    if not event_sprites:
        raise SystemExit(f"no event animations resolved from {args.level}")

    # Springs are just events too: take them from the same chain.
    spring_ids = [i for i in event_sprites if eventset[i * 32 + 10] == 29]
    spring_frames = [event_sprites[i][0] for i in spring_ids[:3]]
    while len(spring_frames) < 3 and spring_frames:
        spring_frames.append(spring_frames[-1])
    if not spring_frames:
        # Not every level has springs (the secret level has none).  Emit blank
        # frames so the per-level symbol still exists for the stage table.
        blank = (8, 8, bytes([SKEY]) * 64)
        spring_frames = [blank, blank, blank]

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

    # One cell per player state, sized to that state's frames, exactly like the
    # enemy cells (Jazz's frames are not all 32x32 either: swim is 48 wide).
    player_state_images: list[list] = []
    player_state_masks: list[list] = []
    player_state_cells: list[tuple[int, int]] = []
    for frames in player_state_frames:
        cw, ch = cell_size(frames) if frames else (32, 32)
        player_state_cells.append((cw, ch))
        images, masks = [], []
        for sprite in frames:
            image, mask = sprite_to_image(sprite, source_palette, cw, ch)
            images.append(image)
            masks.append(mask)
        player_state_images.append(images)
        player_state_masks.append(masks)
    flat_player = [im for images in player_state_images for im in images]

    # Springs vary in size between worlds, so centre them in the cell rather
    # than cropping to an assumed 32x16.
    spring_images: list[Image.Image] = []
    spring_masks: list[list[bool]] = []
    for spring_frame in spring_frames:
        image, mask = sprite_to_image(spring_frame, source_palette, 32, 16)
        spring_images.append(image)
        spring_masks.append(mask)

    # A cell per event, sized to that event's frames (JJ1 enemies are not all
    # 32x32: the Turtle Goon is 68x45).
    enemy_images: dict[int, list] = {}
    enemy_masks: dict[int, list] = {}
    enemy_cells: dict[int, tuple[int, int]] = {}
    enemy_images_left: dict[int, list] = {}
    enemy_masks_left: dict[int, list] = {}
    for event_id, frames in event_sprites.items():
        left_frames = event_sprites_left.get(event_id, [])
        # Both facings share one cell size, so nothing shifts when direction
        # flips mid-walk.
        cw, ch = cell_size(frames + left_frames)
        enemy_cells[event_id] = (cw, ch)
        images, masks = [], []
        for sprite in frames:
            image, mask = sprite_to_image(sprite, source_palette, cw, ch)
            images.append(image)
            masks.append(mask)
        enemy_images[event_id] = images
        enemy_masks[event_id] = masks
        if left_frames:
            limages, lmasks = [], []
            for sprite in left_frames:
                image, mask = sprite_to_image(sprite, source_palette, cw, ch)
                limages.append(image)
                lmasks.append(mask)
            enemy_images_left[event_id] = limages
            enemy_masks_left[event_id] = lmasks

    flat_enemy = ([im for images in enemy_images.values() for im in images] +
                  [im for images in enemy_images_left.values() for im in images])
    # One shared 15-colour line: the Genesis has four palette lines and the
    # font, level and sky already take three, so Jazz and every enemy must fit
    # the same line.  Choosing the palette across all of them at once is what
    # keeps the enemies from coming out miscoloured.
    palette = choose_palette(terrain_images + flat_player + spring_images + flat_enemy)
    world_words: list[int] = []
    for image, mask in zip(terrain_images, terrain_masks):
        world_words.extend(encode_tile(quantize_pixels(image, palette, mask)))

    player_words: list[int] = []
    player_descriptors: list[tuple[int, int, int, int]] = []
    for images, masks, (cw, ch) in zip(player_state_images, player_state_masks, player_state_cells):
        base = len(player_words) // 8
        for image, mask in zip(images, masks):
            player_words.extend(encode_sprite_tiles(quantize_pixels(image, palette, mask), cw, ch))
        player_descriptors.append((base, len(images), cw // 8, ch // 8))
    # Player states carry no separate left art (the ROM flips Jazz), so the
    # fifth Jj1EventSprite field just mirrors the base.

    spring_words: list[int] = []
    for spring_image, spring_mask in zip(spring_images, spring_masks):
        spring_indices = quantize_pixels(spring_image, palette, spring_mask)
        for tx in range(4):
            for ty in range(2):
                block = [spring_indices[(ty * 8 + y) * 32 + tx * 8 + x] for y in range(8) for x in range(8)]
                spring_words.extend(encode_tile(block))

    enemy_words: list[int] = []
    descriptors: dict[int, tuple[int, int, int, int, int]] = {}
    for event_id, images in enemy_images.items():
        cw, ch = enemy_cells[event_id]
        base = len(enemy_words) // 8      # 8 u32 words per tile
        for image, mask in zip(images, enemy_masks[event_id]):
            enemy_words.extend(encode_sprite_tiles(quantize_pixels(image, palette, mask), cw, ch))
        # A distinct left-facing set follows immediately, if any; otherwise the
        # left offset equals the right (the ROM then hardware-flips nothing and
        # simply reuses the one set, matching a symmetric or single-facing enemy).
        left_base = base
        if event_id in enemy_images_left:
            left_base = len(enemy_words) // 8
            for image, mask in zip(enemy_images_left[event_id], enemy_masks_left[event_id]):
                enemy_words.extend(encode_sprite_tiles(quantize_pixels(image, palette, mask), cw, ch))
        descriptors[event_id] = (base, len(images), cw // 8, ch // 8, left_base)

    # Bird (both facings) and shield share the event sprite bank.
    extra = []
    for label, frames in (("bird", bird_frames), ("bird_left", bird_left_frames),
                          ("shield", shield_frames)):
        cw, ch = cell_size(frames)
        base = len(enemy_words) // 8
        for sprite in frames:
            image, mask = sprite_to_image(sprite, source_palette, cw, ch)
            enemy_words.extend(encode_sprite_tiles(quantize_pixels(image, palette, mask), cw, ch))
        extra.append((label, base, len(frames), cw // 8, ch // 8))

    per_frame = {e: (d[2] * d[3]) for e, d in descriptors.items()}
    biggest = max(per_frame.values())
    if biggest > JJ1_ENEMY_SLOT_TILES:
        raise SystemExit(
            f"an event frame needs {biggest} tiles but a VRAM slot holds "
            f"{JJ1_ENEMY_SLOT_TILES}; raise JJ1_ENEMY_SLOT_TILES in inc/jazz_gfx.h")

    if args.preview:
        cols = 8
        rows = (sum(len(v) for v in enemy_images.values()) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * 40, max(rows, 1) * 44), (40, 40, 90))
        draw = ImageDraw.Draw(sheet)
        n = 0
        for event_id, images in sorted(enemy_images.items()):
            for image in images:
                x, y = (n % cols) * 40 + 4, (n // cols) * 44 + 12
                sheet.paste(image, (x, y))
                draw.text(((n % cols) * 40 + 4, (n // cols) * 44), f"ev{event_id}", fill=(255, 255, 0))
                n += 1
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(args.preview)
        print(f"Wrote {args.preview}: compare against a reference sheet to confirm the mapping")

    n = args.name
    output = [
        "/* Generated from a user-supplied JJ1 shareware installation. */",
        "/* Do not regenerate this file without the corresponding legal game data. */",
    ]
    if args.shared:
        output += [emit_u32_array("jazz_world_tiles", world_words), ""]
    output += [
        emit_u32_array(f"{n}_player_tiles", player_words), "",
        "/* [state] = { first tile, frames, tiles wide, tiles high }, one per",
        "   JJ1_PLAYER_* state, resolved from the level's MT_P_ANIMS block. */",
        f"const Jj1EventSprite {n}_player_states[JJ1_PLAYER_STATES] = {{",
    ]
    for state, (base, frames, tw, th) in enumerate(player_descriptors):
        output.append(f"    [{state}] = {{ {base}, {frames}, {tw}, {th}, {base} }},")
    output.append("};")
    output += [
        emit_u32_array(f"{n}_spring_tiles", spring_words), "",
        f"const u16 {n}_pal1[16] = {{",
        "    RGB3_3_3_TO_VDPCOLOR(0,0,0),",
    ]
    output.extend(f"    0x{rgb333(color):04X}," for color in palette)
    output.append("};")
    output += [
        "",
        "/* Original enemy/event sprites, resolved event -> animation -> sprite",
        "   index -> sprite bank.  Each level has its own animation table, so",
        "   these are resolved per level, not per world. */",
        "#define JJ1_HAVE_ORIGINAL_ENEMY_SPRITES 1",
        emit_u32_array(f"{n}_event_sprite_tiles", enemy_words),
        "",
        "/* [event id] = { first tile, frames, tiles wide, tiles high, left tile }.",
        "   JJ1 stores separate art per travel direction; `left` is the tile base",
        "   of the left-facing set (equal to the first tile when the enemy has no",
        "   distinct left art).  The ROM picks by direction and never H-flips. */",
        f"const Jj1EventSprite {n}_event_sprites[128] = {{",
    ]
    for event_id, (base, frames, tw, th, left_base) in sorted(descriptors.items()):
        output.append(f"    [{event_id}] = {{ {base}, {frames}, {tw}, {th}, {left_base} }},")
    output.append("};")
    output += ["",
               "/* Bird companion, its left-facing set, and the shield orb, taken",
               "   from the level animation table rather than any event. */"]
    for label, base, frames, tw, th in extra:
        output.append(f"const Jj1EventSprite {n}_{label}_sprite = "
                      f"{{ {base}, {frames}, {tw}, {th}, {base} }};")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(output) + "\n")
    print(f"Generated {args.output}: terrain, 8 Jazz frames, springs, and "
          f"{len(descriptors)} event sprite sets ({len(enemy_words) // 8} tiles)")


if __name__ == "__main__":
    main()
