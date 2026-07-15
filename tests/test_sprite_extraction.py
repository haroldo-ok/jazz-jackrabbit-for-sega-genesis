#!/usr/bin/env python3
"""Verify the event -> animation -> sprite chain used to extract enemy art.

The game data is not redistributable, so this test builds byte buffers in the
documented original formats and checks the decoders recover exactly what was
put in.  That covers the part that used to be guesswork: the old importer
hard-coded sprite numbers (e.g. "spring = sprite 123"), which is only ever
right by accident.  The real mapping is:

    event record byte 6  -> animation id
    animation record     -> byte 6 = frame count, bytes 7.. = sprite indices
    sprite index         -> image in SPRITES.<world>
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

from import_jj1_shareware import TRANSPARENT, decode_anims, decode_sprite_set
from build_sgdk_jj1_visuals import encode_sprite_tiles, event_frames

failures = []


def check(condition, what):
    if condition:
        print(f"PASS: {what}")
    else:
        print(f"FAIL: {what}")
        failures.append(what)


def scramble(data: bytes) -> bytes:
    """Inverse of the decoder's descramble(): the original stores each sprite
    as four interleaved quarters."""
    quarter = len(data) // 4
    out = bytearray(len(data))
    for n in range(len(data)):
        out[(n >> 2) + ((n & 3) * quarter)] = data[n]
    return bytes(out)


def make_sprite(width: int, height: int, fill: int) -> bytes:
    """One unmasked sprite record: w/4, h, pad, mask_offset=0, next_offset."""
    pixels = bytes([fill]) * (width * height)
    header = struct.pack("<HHHHH", width // 4, height, 0, 0, 0)
    return header + scramble(pixels)


def make_sprite_bank(specs) -> bytes:
    count = len(specs)
    out = struct.pack("<H", count) + b"\x00\x00" * count
    for width, height, fill in specs:
        out += make_sprite(width, height, fill)
    return out


def make_eventset(mapping: dict[int, int]) -> bytes:
    """127 x 32-byte records; byte 6 names the right-facing animation.

    JJ1Level::load fills eventSet[i] from buffer[i * 32] and then looks events
    up as eventSet[grid.event], so record i IS event i.  Writing event N at
    (N-1) * 32 - as the importer used to - shifts every property by one slot.
    """
    records = bytearray(128 * 32)
    for event_id, anim_id in mapping.items():
        records[event_id * 32 + 6] = anim_id
    return bytes(records)


def make_anims(mapping: dict[int, list[int]]) -> bytes:
    """128 x 64-byte records; byte 6 = frame count, bytes 7.. = sprite indices."""
    records = bytearray(128 * 64)
    for anim_id, sprite_ids in mapping.items():
        base = anim_id << 6
        records[base + 6] = len(sprite_ids)
        for n, sprite_id in enumerate(sprite_ids):
            records[base + 7 + n] = sprite_id
            records[base + 26 + n] = 0    # x offset
            records[base + 45 + n] = 0    # y offset
    return bytes(records)


def main() -> int:
    # A bank of three distinguishable sprites.
    bank_bytes = make_sprite_bank([(8, 8, 3), (8, 8, 5), (8, 8, 9)])
    bank = decode_sprite_set(Path("/dev/stdin")) if False else None

    tmp = Path("/tmp/_sprites_test.bin")
    tmp.write_bytes(bank_bytes)
    bank = decode_sprite_set(tmp)
    check(len(bank) == 3, "the sprite bank decodes every slot")
    check(all(s is not None for s in bank), "no sprite slot is lost")
    check(bank[1][0] == 8 and bank[1][1] == 8, "sprite dimensions survive the round trip")
    check(set(bank[2][2]) == {9}, "sprite pixels survive descrambling")

    # Event 17 -> animation 5 -> sprites 2, 0, 1.
    eventset = make_eventset({17: 5})
    anims = decode_anims(make_anims({5: [2, 0, 1]}))
    frames = event_frames(eventset, anims, 17)
    check([f["sprite"] for f in frames] == [2, 0, 1],
          "an event resolves to its animation's sprite indices, in order")

    # An event with no animation must resolve to nothing rather than to sprite 0,
    # which is what silently produced wrong art before.
    check(event_frames(make_eventset({}), anims, 17) == [],
          "an event with no animation resolves to no frames, not to sprite 0")

    # Tile encoding must follow the VDP's column-major sprite order.
    indices = [0] * (32 * 32)
    indices[8 * 32 + 0] = 7     # row 8, col 0 -> tile column 0, tile row 1
    words = encode_sprite_tiles(indices, 32, 32)
    check(len(words) == 16 * 8, "a 32x32 frame encodes to 16 tiles")
    # Column-major: tile (col 0, row 1) is the second tile emitted.
    check(words[8] == 0x70000000,
          "sprite tiles are emitted down each column, as the VDP fetches them")

    if failures:
        print(f"{len(failures)} failure(s)")
        return 1
    print("PASS: sprite extraction chain (event -> anim -> sprite) verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
