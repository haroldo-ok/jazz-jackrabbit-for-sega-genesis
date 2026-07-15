#!/usr/bin/env python3
"""Import user-supplied Jazz Jackrabbit 1 shareware files.

This is a host-side *extractor*, not a redistribution mechanism. It implements
only the documented JJ1 packed image/map/sound formats needed by the Genesis
conversion pipeline. It writes inspectable PNG/WAV/JSON intermediates outside
the source tree by default.

Usage:
  python3 tools/import_jj1_shareware.py --input /path/to/JAZZ --output /tmp/jj1-out
"""
from __future__ import annotations

import argparse
import json
import struct
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from PIL import Image

TILE_W = TILE_H = 32
TILES_PER_SET = 60
TILE_SETS = 4
LEVEL_W, LEVEL_H = 256, 64
TRANSPARENT = 127


class DecodeError(RuntimeError):
    pass


@dataclass
class Reader:
    data: bytes
    pos: int = 0

    def seek(self, offset: int) -> None:
        if not 0 <= offset <= len(self.data):
            raise DecodeError(f"seek outside input: {offset:#x}")
        self.pos = offset

    def skip(self, count: int) -> None:
        self.seek(self.pos + count)

    def byte(self) -> int:
        if self.pos >= len(self.data):
            raise DecodeError("unexpected end of input")
        value = self.data[self.pos]
        self.pos += 1
        return value

    def u16(self) -> int:
        if self.pos + 2 > len(self.data):
            raise DecodeError("unexpected end reading u16")
        value = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return value

    def u32(self) -> int:
        if self.pos + 4 > len(self.data):
            raise DecodeError("unexpected end reading u32")
        value = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return value

    def block(self, size: int) -> bytes:
        if self.pos + size > len(self.data):
            raise DecodeError(f"unexpected end reading block ({size})")
        value = self.data[self.pos:self.pos + size]
        self.pos += size
        return value

    def rle(self, output_size: int) -> bytes:
        """JJ1 RLE, matching OpenJazz File::loadRLE()."""
        packed_size = self.u16()
        start = self.pos
        end = start + packed_size
        if end > len(self.data):
            raise DecodeError("RLE block extends beyond file")
        out = bytearray(output_size)
        written = 0
        while written < output_size and self.pos < end:
            code = self.byte()
            amount = code & 0x7F
            if code & 0x80:
                value = self.byte()
                if written + amount >= output_size:
                    break
                out[written:written + amount] = bytes([value]) * amount
                written += amount
            elif amount:
                if written + amount >= output_size:
                    break
                out[written:written + amount] = self.block(amount)
                written += amount
            else:
                out[written] = self.byte()
                written += 1
                break
        # The original container provides the next logical record position.
        self.seek(end)
        return bytes(out)

    def skip_rle(self) -> None:
        self.skip(self.u16())


def load_palette(r: Reader) -> list[tuple[int, int, int]]:
    raw = r.rle(256 * 3)
    # JJ1 palette channels are 6-bit values.
    return [tuple(((raw[n * 3 + c] << 2) | (raw[n * 3 + c] >> 4)) for c in range(3)) for n in range(256)]


def indexed_image(pixels: bytes, width: int, height: int, palette: list[tuple[int, int, int]]) -> Image.Image:
    image = Image.frombytes("P", (width, height), pixels)
    flat = [component for rgb in palette for component in rgb]
    flat.extend([0] * (768 - len(flat)))
    image.putpalette(flat)
    return image


def decode_blocks(path: Path) -> tuple[list[bytes], list[tuple[int, int, int]], list[tuple[int, int, int]]]:
    r = Reader(path.read_bytes())
    palette = load_palette(r)
    sky_palette = load_palette(r)
    r.skip_rle()  # alternating sky palette
    tiles: list[bytes] = []
    for _set_index in range(TILE_SETS):
        marker = r.block(2)
        if marker == b"ok":
            for _ in range(TILES_PER_SET):
                tiles.append(r.rle(TILE_W * TILE_H))
        elif marker != b"  ":
            raise DecodeError(f"unknown BLOCKS marker {marker!r}")
    return tiles, palette, sky_palette


def save_tile_sheet(tiles: list[bytes], palette: list[tuple[int, int, int]], target: Path) -> None:
    cols = 15
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new("P", (cols * TILE_W, rows * TILE_H), TRANSPARENT)
    flat = [component for rgb in palette for component in rgb]
    sheet.putpalette(flat)
    for index, tile in enumerate(tiles):
        sheet.paste(indexed_image(tile, TILE_W, TILE_H, palette), ((index % cols) * TILE_W, (index // cols) * TILE_H))
    sheet.save(target)


SKEY = 254  # sprite colour key (JJ1Level::loadSprite); NOT the tile TRANSPARENT


def descramble(data: bytes) -> bytes:
    """File::loadPixels(length): gather each pixel from the interleaved quarters."""
    length = len(data)
    if length % 4:
        raise DecodeError("sprite length is not divisible by 4")
    quarter = length // 4
    return bytes(data[(n >> 2) + ((n & 3) * quarter)] for n in range(length))


def load_sprite(r: Reader) -> Optional[tuple[int, int, bytes]]:
    """Decode one sprite exactly as JJ1Level::loadSprite + File::loadPixels do.

    The masked path is easy to get subtly wrong, and getting it wrong desyncs
    the reader so every later sprite decodes as noise:

      1. read one mask bit per pixel, four packed into the low end of a byte;
      2. SCATTER the mask through the interleave permutation;
      3. read a pixel byte for each set mask bit, in that scrambled order,
         skipping any byte equal to the colour key;
      4. GATHER the pixel buffer back into image order.

    Steps 2 and 4 use inverse permutations - doing the same one twice, or
    skipping step 4, is what produced garbage.
    """
    if r.pos >= len(r.data):
        return None
    width = r.u16() * 4
    height = r.u16()
    r.skip(2)
    mask_offset = r.u16()
    next_offset = r.u16() * 4
    if not width:
        return None

    if mask_offset:
        height += 1
        r.skip(mask_offset)
        end = next_offset + r.pos + ((width // 4) * height)
        length = width * height
        quarter = length >> 2

        mask_bits = bytearray(length)
        packed = 0
        for n in range(length):
            if not (n & 3):
                packed = r.byte()
            mask_bits[n] = (packed >> (n & 3)) & 1

        scrambled_mask = bytearray(length)
        for n in range(length):
            scrambled_mask[(n >> 2) + ((n & 3) * quarter)] = mask_bits[n]

        scrambled_pixels = bytearray([SKEY]) * length
        for n in range(length):
            if scrambled_mask[n]:
                value = r.byte()
                while value == SKEY:
                    value = r.byte()
                scrambled_pixels[n] = value

        pixels = bytes(scrambled_pixels[(n >> 2) + ((n & 3) * quarter)] for n in range(length))
        r.seek(end)
        return width, height, pixels

    raw = r.block(width * height)
    return width, height, descramble(raw)


def decode_sprite_set(sprite_path: Path) -> list[Optional[tuple[int, int, bytes]]]:
    """Decode SPRITES.<world>, the sprite bank the level's animations index into.

    Same container as MAINCHAR.000: a u16 count, then `count` anchor words, then
    the sprite images laid out sequentially.  A leading 0xFF marks an empty slot,
    which still consumes an index (so indices stay aligned with the animation
    records that reference them).
    """
    r = Reader(sprite_path.read_bytes())
    count = r.u16()
    if count > 512:
        raise DecodeError(f"invalid sprite count: {count}")
    r.skip(count * 2)  # anchor offsets; images remain sequential
    result: list[Optional[tuple[int, int, bytes]]] = []
    for _ in range(count):
        if r.pos >= len(r.data):
            result.append(None)
            continue
        if r.data[r.pos] == 0xFF:
            r.skip(2)
            result.append(None)
            continue
        result.append(load_sprite(r))
    return result


def decode_anims(anims: bytes) -> list[dict]:
    """Decode the level's 128 x 64-byte animation records.

    Mirrors JJ1Level::load: byte 6 is the frame count, bytes 7.. are the sprite
    indices for each frame, and bytes 26.. / 45.. are the per-frame x/y offsets.
    """
    out: list[dict] = []
    for i in range(128):
        base = i << 6
        record = anims[base:base + 64]
        frames = record[6]
        if frames > 19:      # 19 frame slots exist between offsets 7 and 26
            frames = 19
        out.append({
            "frames": [
                {
                    "sprite": record[7 + n],
                    "x": record[26 + n] - 256 if record[26 + n] > 127 else record[26 + n],
                    "y": record[45 + n] - 256 if record[45 + n] > 127 else record[45 + n],
                }
                for n in range(frames)
            ],
        })
    return out


def decode_mainchar(main_path: Path, sprite_path: Path) -> list[Optional[tuple[int, int, bytes]]]:
    spec = Reader(sprite_path.read_bytes())
    count = spec.u16()
    if count > 256:
        raise DecodeError(f"invalid sprite count: {count}")
    spec.skip(count * 2)  # sprite anchor offsets; images remain sequential
    main = Reader(main_path.read_bytes(), 2)
    result: list[Optional[tuple[int, int, bytes]]] = []
    for _ in range(count):
        item: Optional[tuple[int, int, bytes]] = None
        if main.pos < len(main.data):
            if main.data[main.pos] == 0xFF:
                main.skip(2)
            else:
                item = load_sprite(main)
        if spec.pos < len(spec.data):
            if spec.data[spec.pos] == 0xFF:
                spec.skip(2)
            else:
                # The world-specific image overrides the MAINCHAR frame.
                item = load_sprite(spec)
        result.append(item)
    return result


def save_sprite_sheet(sprites: list[Optional[tuple[int, int, bytes]]], palette: list[tuple[int, int, int]], target: Path) -> None:
    # 64x64 cells keep the sheet readable while preserving original pixels.
    cell_w, cell_h, cols = 64, 64, 16
    rows = (len(sprites) + cols - 1) // cols
    sheet = Image.new("P", (cols * cell_w, rows * cell_h), TRANSPARENT)
    flat = [component for rgb in palette for component in rgb]
    sheet.putpalette(flat)
    for index, sprite in enumerate(sprites):
        if sprite is None:
            continue
        width, height, pixels = sprite
        if width > cell_w or height > cell_h:
            # Preserve large frames as their own images; thumbnail is clipped only in the sheet.
            indexed_image(pixels, width, height, palette).save(target.parent / f"sprite_{index:03d}.png")
            continue
        sheet.paste(indexed_image(pixels, width, height, palette), ((index % cols) * cell_w, (index // cols) * cell_h))
    sheet.save(target)


def parse_level(path: Path) -> tuple[bytes, dict[str, int | str]]:
    """Decode grid and metadata using the same seek pattern as OpenJazz JJ1."""
    data = path.read_bytes()
    grid_reader = Reader(data)
    if grid_reader.block(2) != b"DD" or grid_reader.byte() != 0x1A:
        raise DecodeError(f"{path.name}: invalid level header")
    grid_reader.seek(39)
    grid = grid_reader.rle(LEVEL_W * LEVEL_H * 2)

    # The level loader later walks the packed records to find the true
    # level/world and the BLOCKS extension. This is deliberately a second
    # reader so grid extraction remains independent of optional records.
    r = Reader(data)
    r.seek(39)
    for _ in range(8):
        r.skip_rle()
    r.skip(598)
    r.skip_rle()
    r.skip(4)
    r.skip_rle(); r.skip_rle()
    r.skip(25)
    r.skip_rle()
    r.skip(3)
    level_number = r.byte() ^ 210
    world_number = r.byte() ^ 4
    enemies_easy = r.u16(); enemies_hard = r.u16(); enemies_turbo = r.u16()
    items = r.u16()
    extension_len = r.byte()
    if extension_len > 3:
        raise DecodeError(f"{path.name}: invalid BLOCKS extension length {extension_len}")
    extension = r.block(extension_len).decode("ascii", "replace")
    r.skip(3 - extension_len)
    if extension == "999":
        extension = f"{world_number:03d}"
    return grid, {
        "level": level_number, "world": world_number, "blocks_extension": extension,
        "enemies_easy": enemies_easy, "enemies_hard": enemies_hard,
        "enemies_turbo": enemies_turbo, "items": items,
    }


def decode_menu(path: Path) -> tuple[Image.Image, Image.Image]:
    r = Reader(path.read_bytes())
    # The normal (non-December) MENU.000 theme is the first palette + two 320x200 planes.
    palette = load_palette(r)
    background = indexed_image(r.rle(320 * 200), 320, 200, palette)
    highlight = indexed_image(r.rle(320 * 200), 320, 200, palette)
    return background, highlight


def decode_panel_preview(path: Path, palette: list[tuple[int, int, int]]) -> Image.Image:
    r = Reader(path.read_bytes())
    data = r.rle(46272)
    # The first 320x144 rows cover the panel/embedded font atlas. The small
    # remaining tail is metadata / auxiliary packed panel graphics.
    return indexed_image(data[:320 * 144], 320, 144, palette)


def decode_font(path: Path, palette: list[tuple[int, int, int]], target: Path) -> int:
    r = Reader(path.read_bytes())
    if r.block(18) != b"Digital Dimensions" or r.byte() != 0x1A:
        raise DecodeError(f"{path.name}: invalid font header")
    r.skip(2)  # space width, line height
    glyphs: list[tuple[int, Image.Image]] = []
    index = 0
    while r.pos < len(r.data):
        size = r.u16()
        if size > 4:
            packed = r.rle(size)
            width = packed[0] | (packed[1] << 8)
            height = packed[2] | (packed[3] << 8)
            if width and height and 4 + width * height <= len(packed):
                glyphs.append((index, indexed_image(packed[4:4 + width * height], width, height, palette)))
        index += 1
    if not glyphs:
        return 0
    cols, cell_w, cell_h = 16, 16, 24
    rows = (len(glyphs) + cols - 1) // cols
    atlas = Image.new("P", (cols * cell_w, rows * cell_h), 0)
    atlas.putpalette([component for rgb in palette for component in rgb])
    for index, image in glyphs:
        if image.width <= cell_w and image.height <= cell_h:
            atlas.paste(image, ((index % cols) * cell_w, (index // cols) * cell_h))
    atlas.save(target)
    return len(glyphs)


def save_level_preview(grid: bytes, tiles: list[bytes], palette: list[tuple[int, int, int]], target: Path) -> None:
    # Build the original 8192x2048 raster and then make a practical 1024x256 preview.
    preview = Image.new("P", (LEVEL_W, LEVEL_H), TRANSPARENT)
    flat = [component for rgb in palette for component in rgb]
    preview.putpalette(flat)
    colors: list[int] = []
    for tile in tiles:
        # representative non-transparent colour for one logical grid cell
        colors.append(next((p for p in tile if p != TRANSPARENT), TRANSPARENT))
    for x in range(LEVEL_W):
        for y in range(LEVEL_H):
            tile_index = grid[(y + x * LEVEL_H) * 2]
            preview.putpixel((x, y), colors[tile_index] if tile_index < len(colors) else TRANSPARENT)
    preview.resize((LEVEL_W * 4, LEVEL_H * 4), Image.Resampling.NEAREST).save(target)


def export_sounds(path: Path, target: Path) -> list[dict[str, int | str]]:
    data = path.read_bytes()
    r = Reader(data)
    if r.block(3)[:2] != b"sf" or r.byte() != 0x1A:
        raise DecodeError("SOUNDS.000: invalid sound header")
    header_offset = struct.unpack_from("<I", data, len(data) - 4)[0]
    if header_offset >= len(data):
        raise DecodeError("SOUNDS.000: invalid directory offset")
    count = (len(data) - header_offset) // 18
    manifest: list[dict[str, int | str]] = []
    for index in range(count):
        pos = header_offset + index * 18
        raw_name = data[pos:pos + 12].split(b"\0", 1)[0]
        name = raw_name.decode("ascii", "replace").strip() or f"sound_{index:02d}"
        offset, length = struct.unpack_from("<IH", data, pos + 12)
        clip = data[offset:offset + length]
        if len(clip) != length:
            continue
        # JJ1 clips are signed 8-bit PCM. The per-level sample rates are read
        # separately; 11025 Hz is OpenJazz's default fallback for inspection.
        filename = f"{index:02d}_{''.join(c if c.isalnum() else '_' for c in name)}.wav"
        with wave.open(str(target / filename), "wb") as wav:
            wav.setnchannels(1); wav.setsampwidth(1); wav.setframerate(11025)
            wav.writeframes(bytes((value + 128) & 0xFF for value in clip))
        manifest.append({"index": index, "name": name, "length": length, "file": filename})
    return manifest


def safe_name(path: Path) -> str:
    return path.name.lower().replace(".", "_")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True, help="directory containing installed JJ1 game files")
    parser.add_argument("--output", type=Path, required=True, help="output directory; use a local generated-assets path")
    args = parser.parse_args()
    source = args.input
    target = args.output
    target.mkdir(parents=True, exist_ok=True)
    for dirname in ("blocks", "sprites", "maps", "sounds", "ui", "fonts"):
        (target / dirname).mkdir(exist_ok=True)

    mainchar = source / "MAINCHAR.000"
    sounds = source / "SOUNDS.000"
    if not mainchar.exists() or not sounds.exists():
        raise SystemExit("Expected MAINCHAR.000 and SOUNDS.000 in --input")

    # Convert every available world block set/background palette.
    block_sets: dict[str, tuple[list[bytes], list[tuple[int, int, int]], list[tuple[int, int, int]]]] = {}
    for block_path in sorted(source.glob("BLOCKS.*")):
        suffix = block_path.suffix[1:]
        try:
            block_sets[suffix] = decode_blocks(block_path)
            tiles, palette, _sky = block_sets[suffix]
            save_tile_sheet(tiles, palette, target / "blocks" / f"{safe_name(block_path)}_tiles.png")
        except DecodeError as exc:
            print(f"warning: {block_path.name}: {exc}")
    if not block_sets:
        raise SystemExit("No decodable BLOCKS.* files found")
    default_tiles, default_palette, default_sky = block_sets.get("000", next(iter(block_sets.values())))

    # Convert every world sprite table against its matching BLOCKS palette.
    sprite_sets: list[dict[str, int | str]] = []
    for sprite_path in sorted(source.glob("SPRITES.*")):
        suffix = sprite_path.suffix[1:]
        palette = block_sets.get(suffix, (default_tiles, default_palette, default_sky))[1]
        try:
            decoded = decode_mainchar(mainchar, sprite_path)
            sheet_name = f"{safe_name(sprite_path)}_sheet.png"
            save_sprite_sheet(decoded, palette, target / "sprites" / sheet_name)
            sprite_sets.append({"file": sprite_path.name, "sprite_slots": len(decoded), "sheet": f"sprites/{sheet_name}"})
        except DecodeError as exc:
            print(f"warning: {sprite_path.name}: {exc}")

    # Convert every JJ1 256x64 map (both shareware and bundled auxiliary worlds).
    levels: list[dict[str, int | str]] = []
    for level_path in sorted(source.glob("LEVEL*.*")):
        try:
            grid, metadata = parse_level(level_path)
            extension = str(metadata["blocks_extension"])
            tiles, palette, _sky = block_sets.get(extension, (default_tiles, default_palette, default_sky))
            preview_name = f"{safe_name(level_path)}_map.png"
            save_level_preview(grid, tiles, palette, target / "maps" / preview_name)
            levels.append({"file": level_path.name, **metadata, "preview": f"maps/{preview_name}"})
        except DecodeError as exc:
            print(f"warning: {level_path.name}: {exc}")

    # Menu background/highlight and panel are independently useful Genesis UI art.
    ui_assets: list[str] = []
    menu = source / "MENU.000"
    if menu.exists():
        try:
            normal, highlight = decode_menu(menu)
            normal.save(target / "ui" / "menu_background.png")
            highlight.save(target / "ui" / "menu_highlight.png")
            ui_assets.extend(["ui/menu_background.png", "ui/menu_highlight.png"])
        except DecodeError as exc:
            print(f"warning: MENU.000: {exc}")
    panel = source / "PANEL.000"
    if panel.exists():
        try:
            decode_panel_preview(panel, default_palette).save(target / "ui" / "panel_preview.png")
            ui_assets.append("ui/panel_preview.png")
        except DecodeError as exc:
            print(f"warning: PANEL.000: {exc}")

    font_manifest: list[dict[str, int | str]] = []
    for font_path in sorted(source.glob("*.0FN")):
        atlas_name = f"{safe_name(font_path)}_atlas.png"
        try:
            glyphs = decode_font(font_path, default_palette, target / "fonts" / atlas_name)
            font_manifest.append({"file": font_path.name, "glyphs": glyphs, "atlas": f"fonts/{atlas_name}"})
        except DecodeError as exc:
            print(f"warning: {font_path.name}: {exc}")

    sound_manifest = export_sounds(sounds, target / "sounds")
    manifest = {
        "source": str(source),
        "block_sets": [{"extension": ext, "tile_count": len(value[0])} for ext, value in block_sets.items()],
        "sprite_sets": sprite_sets,
        "levels": levels,
        "sounds": sound_manifest,
        "ui_assets": ui_assets,
        "fonts": font_manifest,
    }
    (target / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print(f"Imported {len(block_sets)} block sets, {len(sprite_sets)} sprite sets, {len(levels)} maps, {len(sound_manifest)} sound clips to {target}")


if __name__ == "__main__":
    main()
