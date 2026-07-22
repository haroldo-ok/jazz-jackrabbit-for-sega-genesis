#!/usr/bin/env python3
"""Generate complete JJ1 block/map/mask data for Genesis runtime streaming.

Each output contains all 240 32x32 BLOCKS patterns as 16 sequential Genesis
8x8 tiles, the 256x64 block map, 8x8 collision masks, and the original level
start coordinate. The runtime keeps 88 blocks in VRAM and reads the masks for
actual foreground collision rather than a decorative background.
"""
from __future__ import annotations
import argparse
import sys
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from import_jj1_shareware import TRANSPARENT, Reader, decode_blocks, parse_level


def md(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    return ((r * 7 // 255) << 1) | ((g * 7 // 255) << 5) | ((b * 7 // 255) << 9)


def make_palette(tiles: list[bytes], source_palette: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
    pixels = [source_palette[p] for tile in tiles for p in tile if p != TRANSPARENT]
    source = Image.new("RGB", (len(pixels), 1)); source.putdata(pixels)
    q = source.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
    raw = q.getpalette()[:45]
    return [tuple(raw[i:i+3]) for i in range(0, 45, 3)]


def nearest(rgb: tuple[int, int, int], palette: list[tuple[int, int, int]]) -> int:
    return 1 + min(range(15), key=lambda i: sum((rgb[c] - palette[i][c]) ** 2 for c in range(3)))


def encode_block(raw: bytes, source_palette: list[tuple[int, int, int]], palette: list[tuple[int, int, int]]) -> list[int]:
    indices = [0 if p == TRANSPARENT else nearest(source_palette[p], palette) for p in raw]
    out: list[int] = []
    for ty in range(4):
        for tx in range(4):
            for y in range(8):
                word = 0
                for x in range(8):
                    word = (word << 4) | indices[(ty * 8 + y) * 32 + tx * 8 + x]
                out.append(word)
    return out


def array(name: str, ctype: str, values: list[int], per_line: int, fmt: str) -> str:
    lines = [f"const {ctype} {name}[{len(values)}] = {{"]
    for start in range(0, len(values), per_line):
        lines.append("    " + ", ".join(fmt.format(v) for v in values[start:start+per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def runtime_records(level_path: Path):
    """Mirror JJ1Level::load record skips through mask/start-coordinate data.

    Returns (grid, masks, eventset, start_x, start_y, anims, player_anims),
    where player_anims is the 38 animation indices the level assigns to Jazz's
    animation states (PA_LWALK..PA_LSPRING), read from the MT_P_ANIMS block.
    """
    r = Reader(level_path.read_bytes())
    r.seek(39)
    grid = r.rle(256 * 64 * 2)
    r.skip_rle()                # transparency reference table
    masks = r.rle((60 * 4 + 16) * 8)
    r.rle(16 * 512)             # movement paths
    eventset = r.rle(127 * 32)  # event definitions
    r.skip_rle()                # event names
    anims = r.rle(128 * 64)     # animation definitions
    r.skip_rle()                # animation names
    r.skip((16 * (8 + 1)) + 9)  # level block names / compression info
    r.skip(32 * 2)              # sound rates
    for _ in range(32):
        length = r.byte()
        if length > 8:
            raise ValueError(f"invalid level sound name length {length}")
        r.skip(8)               # terminated fields consume exactly max size
    r.skip(13)                  # music filename field
    r.skip(13)                  # level start scene field
    r.skip(13)                  # end scene field
    r.skip(39)                  # editor tileset names
    start_x = r.u16()
    start_y = r.u16() + 1
    # After the start coordinates: JJ1Level::load reads a couple of bytes,
    # jump height, water level, anim speed, then the player animation set.
    r.skip(2)                   # l, w bytes
    r.skip(2)                   # jump height (short)
    r.skip(2)                   # padding seek(2)
    r.skip(2)                   # water level target (short)
    r.skip(1)                   # anim speed (char)
    r.skip(2)                   # padding seek(2)
    JJ1PANIMS = 38
    player_anim_block = r.rle(JJ1PANIMS * 2)
    player_anims = [player_anim_block[i << 1] for i in range(JJ1PANIMS)]

    # Level animations (JJ1LANIMS = 11) hold the bird, airboard and shield art.
    # They sit past the bullet table and a variable-length palette-effect block,
    # so rather than model that block we scan for the run of 11 bytes whose
    # LA_UNKNOWN9 entry is 31 - the value JJ1Level::load itself expects there.
    level_anims = [0] * 11
    bullets = bytes(32 * 20)
    try:
        r.skip(4)                      # JJ1MANIMS misc animation bytes
        bullets = r.rle(32 * 20)       # bullet definitions
        r.skip_rle()                   # bullet names
        tail = r.data[r.pos:r.pos + 4096]
        for off in range(len(tail) - 11):
            block = tail[off:off + 11]
            if block[8] == 31 and all(b < 128 for b in block):
                level_anims = list(block)
                break
    except Exception:
        pass                           # leave zeros: the caller falls back

    return (grid, masks, eventset, start_x, start_y, anims, player_anims,
            level_anims, bullets)


# Genesis runtime classes (keep in sync with inc/jj1_events.h).
CLASS_NONE, CLASS_ITEM, CLASS_ENEMY_WALK, CLASS_ENEMY_FLY, CLASS_HAZARD, \
    CLASS_SPRING, CLASS_ONEWAY, CLASS_END, CLASS_DESTRUCT, CLASS_TUBE, \
    CLASS_BRIDGE, CLASS_UNBOARD, CLASS_BOSS = range(13)
ITEM_SCORE, ITEM_HEALTH, ITEM_LIFE, ITEM_FASTFEET, ITEM_AMMO, \
    ITEM_INVINCIBLE, ITEM_SHIELD, ITEM_HIGHJUMP, ITEM_BIRD, ITEM_AIRBOARD = range(10)


def classify_event(record: bytes, has_anim: bool = True) -> tuple[int, int, int, int]:
    """Map one original 32-byte event record to (class, param, points/25, strength).

    The modifier values below are the ones the reference GPL engine acts on in
    JJ1LevelPlayer::touchEvent / setEvent; `movement` 21 is the destructible
    block.  Reading these straight from the level file replaces the spatial
    guesswork the Genesis tables used to rely on.
    """
    movement = record[4]
    magnitude = record[8] if record[8] < 128 else record[8] - 256
    strength = record[9]
    modifier = record[10]
    points = record[11]

    # Bridges are a behaviour, not a modifier: JJ1LevelFrame creates a
    # JJ1Bridge for movement 28.  They carry modifier 7 ("harmless on touch"),
    # so the modifier-7 rule below used to swallow them and the whole span
    # vanished - invisible and not walkable.  Classify them first.
    # The episode guardian (movement 41) is the boss fight.  It carries
    # modifier 8, which would otherwise classify as an end trigger, so it has
    # to be recognised by behaviour first; strength is its hit points.
    if movement == 41:
        return CLASS_BOSS, 0, min(points * 10 // 25, 255), min(strength, 255)

    if movement == 28:
        # JJ1Bridge spans multiA pieces spaced pieceSize*4 px apart, with the
        # deck multiB px below the cell top.  Drawing only the event's own cell
        # is why just the first log appeared.  param = piece count,
        # strength = piece spacing in pixels (both clamped to a byte).
        pieces = record[22]
        piece_size = record[24] if record[24] < 128 else record[24] - 256
        spacing = max(1, piece_size) * 4
        return CLASS_BRIDGE, min(pieces, 255), min(record[23], 255), min(spacing, 255)

    # Destructible scenery is selected by behaviour, not by modifier: the engine
    # counts hits and swaps the block once they reach `strength`.  Modifier 7
    # marks the same events as harmless to touch.
    if movement == 21:
        return CLASS_DESTRUCT, 0, min(points * 10 // 25, 255), max(1, min(strength, 255))

    # Sucker tubes (Tubelectric).  The engine (touchEvent, movement 37/38) has
    # two forms, selected by multiB (byte 23):
    #   multiB == 0            -> horizontal repel: push at magnitude * F40 >> 6,
    #                             which is magnitude * 171 in the port's 8.8.
    #   multiB != 0, multiA>0  -> vertical *launch*: lift the player to
    #                             targetY = gridY - multiA*3 tiles, capped at
    #                             multiA * F20 upward, with a magnitude-signed
    #                             horizontal drift.  This is what makes the
    #                             Tubelectric shafts fire the player upward.
    # We keep both in CLASS_TUBE: `param` carries the signed horizontal
    # magnitude, and `strength` carries the vertical launch height in tiles
    # (0 for a plain horizontal tube).
    if movement in (37, 38):
        multi_a = record[22]
        multi_b = record[23]
        if multi_b and multi_a > 0:
            return CLASS_TUBE, magnitude & 0xFF, 0, min(multi_a, 255)
        if not multi_b:
            return CLASS_TUBE, magnitude & 0xFF, 0, 0
        # multiB set with multiA <= 0 is a downward repel; treat as horizontal
        # drift only (the shareware levels don't use it for launches).
        return CLASS_TUBE, magnitude & 0xFF, 0, 0

    if modifier == 7:
        return CLASS_NONE, 0, 0, 0

    if modifier == 38:                   # "Airboard, etc. off" zone
        return CLASS_UNBOARD, 0, 0, 0
    if modifier == 6:
        return CLASS_ONEWAY, 0, 0, 0
    if modifier in (8, 27, 41):          # boss / end of level / bonus level
        return CLASS_END, 0, 0, 0
    if modifier == 29:                   # upward spring: rises |magnitude| * 21 px
        mag = -magnitude if magnitude < 0 else magnitude
        return CLASS_SPRING, min(mag, 255), 0, 0
    if modifier == 0 and strength:       # anything that hurts and can be shot
        flying = movement in (6, 7, 25)
        return (CLASS_ENEMY_FLY if flying else CLASS_ENEMY_WALK), 0, 0, min(strength, 255)

    # Pickups.  Which effect each modifier grants is taken from takeEvent();
    # note that touchEvent only *takes* an event in its default case when
    # strength is 0, so a pickup with strength has to be shot open instead of
    # walked into.  We carry the record's strength through so the runtime can
    # make that distinction.
    item = {
        1: ITEM_INVINCIBLE,  # temporary invincibility
        2: ITEM_HEALTH,      # health
        3: ITEM_HEALTH,      # full health
        4: ITEM_LIFE,        # extra life
        5: ITEM_HIGHJUMP,    # high-jump feet
        9: ITEM_SCORE,       # sand timer
        10: ITEM_SCORE,      # checkpoint
        11: ITEM_SCORE,      # generic item
        12: ITEM_AMMO,       # rapid fire
        15: ITEM_AMMO, 16: ITEM_AMMO, 17: ITEM_AMMO, 18: ITEM_AMMO,
        19: ITEM_AMMO, 20: ITEM_AMMO, 39: ITEM_AMMO, 40: ITEM_AMMO,
        26: ITEM_FASTFEET,   # fast feet box
        33: ITEM_SHIELD,     # 1-hit shield
        36: ITEM_SHIELD,     # 4-hit shield
        34: ITEM_BIRD,       # bird companion
        35: ITEM_AIRBOARD,   # airboard
        37: ITEM_SCORE,      # diamond
    }.get(modifier)
    if item is not None:
        hits = min(strength, 255)        # 0 = grab on touch, else shoot open
        # Ammo pickups also select a weapon.  OpenJazz's setEvent maps each
        # modifier to addAmmo(weaponType, count), and createBullet fires
        # bulletSet[weaponType + 1]; the resulting behaviours (read from the
        # bullet table) are: 0 -> fast straight (toaster), 1 -> fast angled up,
        # 2 -> gravity bouncer, 3 -> special.  Encode the shot type in the
        # param's high nibble so collecting a crate switches Jazz's weapon.
        if item == ITEM_AMMO:
            weapon = {15: 0, 18: 0, 16: 1, 19: 1, 17: 2, 20: 2, 39: 3, 40: 3,
                      12: 0}.get(modifier, 0)
            shot = weapon + 1            # 0 is the default blaster
            return CLASS_ITEM, (ITEM_AMMO | (shot << 4)), min(points * 10 // 25, 255), hits
        # A shield's capacity rides in the param high nibble (1 or 4 hits).
        if item == ITEM_SHIELD:
            capacity = 4 if modifier == 36 else 1
            return CLASS_ITEM, (ITEM_SHIELD | (capacity << 4)), min(points * 10 // 25, 255), hits
        return CLASS_ITEM, item, min(points * 10 // 25, 255), hits
    if points:
        return CLASS_ITEM, ITEM_SCORE, min(points * 10 // 25, 255), 0

    # Only something with an animation can be seen or touched.  Event 123, for
    # example, has a behaviour but no sprite, no strength and no points, and it
    # blankets hundreds of cells: it is an invisible region marker, and calling
    # it a hazard would carpet the level in damage.
    if has_anim and modifier == 0 and movement and movement < 37:
        return CLASS_HAZARD, 0, 0, 0
    return CLASS_NONE, 0, 0, 0


def eventset_include(name: str, eventset: bytes, anims: bytes) -> str:
    lines = [
        "/* Ground-truth event set generated from the original level file. */",
        f"static const Jj1EventInfo {name}_eventset[128] = {{",
    ]
    # JJ1Level::load fills eventSet[i] from buffer[i * ELENGTH] and then looks
    # events up as eventSet[grid.event]: record i IS event i.  The old code
    # emitted record i as event i+1, which shifted every property by one slot.
    entries: dict[int, tuple[int, int, int, int]] = {}
    for i in range(1, 127):
        record = eventset[i * 32:(i + 1) * 32]
        anim_id = record[6] or record[5]
        has_anim = bool(anim_id) and anim_id < 128 and anims[(anim_id << 6) + 6] > 0
        entries[i] = classify_event(record, has_anim)

    # The engine hard-codes two event ids at tile level rather than in the
    # records (JJ1Level::checkMaskUp / checkSpikes), so their records are blank
    # and they must be filled in here.
    entries[122] = (CLASS_ONEWAY, 0, 0, 0)
    entries[126] = (CLASS_HAZARD, 0, 0, 0)

    for i in sorted(entries):
        klass, param, points, strength = entries[i]
        if klass or param or points or strength:
            lines.append(f"    [{i}] = {{ {klass}, {param}, {points}, {strength} }},")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", type=Path, required=True)
    ap.add_argument("--level", default="LEVEL0.000")
    ap.add_argument("--name", default="jj1_level0")
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--eventset", type=Path,
                    help="also write a ground-truth jj1_levelN_eventset.inc")
    ap.add_argument("--world-tiles", type=Path,
                    help="write this level's world tileset (BLOCKS.<world>) to its "
                         "own include; several levels share one world, so the "
                         "120KB tile bank is emitted once per world, not per level")
    args = ap.parse_args()

    grid, metadata = parse_level(args.input / args.level)
    runtime_grid, masks, eventset, start_x, start_y, _anims, _panims, _lanims, _bul = runtime_records(args.input / args.level)
    if grid != runtime_grid:
        raise SystemExit("grid decode mismatch")
    ext = str(metadata["blocks_extension"])
    blocks_path = args.input / f"BLOCKS.{ext}"
    if not blocks_path.exists(): blocks_path = args.input / "BLOCKS.000"
    blocks, source_palette, _ = decode_blocks(blocks_path)
    palette = make_palette(blocks, source_palette)
    words = [word for block in blocks for word in encode_block(block, source_palette, palette)]
    map_data = [grid[(y + x * 64) * 2] for y in range(64) for x in range(256)]
    event_data = [grid[(y + x * 64) * 2 + 1] & 0x7F for y in range(64) for x in range(256)]
    # The level map, events and masks are per level; the tile bank and its
    # palette belong to the world and are shared by every level in it.
    code = [
        "/* Generated from user-supplied JJ1 LEVEL/BLOCKS data. */",
        f"/* {args.level}: 256x64 block map, event grid, 8x8 masks.",
        f"   Tiles come from the shared BLOCKS.{ext} world bank. */",
        array(f"{args.name}_blocks", "u8", map_data, 16, "0x{:02X}"), "",
        array(f"{args.name}_events", "u8", event_data, 16, "0x{:02X}"), "",
        array(f"{args.name}_masks", "u8", list(masks), 16, "0x{:02X}"), "",
        f"const u16 {args.name}_start_x = {start_x};",
        f"const u16 {args.name}_start_y = {start_y};",
    ]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(code) + "\n")

    if args.world_tiles:
        world = [
            "/* Generated from user-supplied JJ1 BLOCKS data. */",
            f"/* BLOCKS.{ext}: 240 x 16 Genesis patterns, shared by every level",
            "   in this world. */",
            array(f"jj1_world{ext}_block_tiles", "u32", words, 4, "0x{:08X}"), "",
            f"const u16 jj1_world{ext}_palette[16] = {{",
            "    RGB3_3_3_TO_VDPCOLOR(0,0,0),",
        ]
        world.extend(f"    0x{md(color):04X}," for color in palette)
        world.append("};")
        args.world_tiles.parent.mkdir(parents=True, exist_ok=True)
        args.world_tiles.write_text("\n".join(world) + "\n")
        print(f"Generated {args.world_tiles}: BLOCKS.{ext}, {len(words)//8} VDP tiles")
    if args.eventset:
        args.eventset.parent.mkdir(parents=True, exist_ok=True)
        args.eventset.write_text(eventset_include(args.name, eventset, _anims) + "\n")
        print(f"Generated {args.eventset} from the original event records")
    print(f"Generated {args.output}: world {ext}, map/events/masks, start {start_x},{start_y}")


if __name__ == "__main__":
    main()
