"""Parser for JJ1 bonus levels (BONUSMAP.00n + tileset BONUSY.000 + BONUS.000)."""
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import import_jj1_shareware as imp


def tstr(r, maxsize):
    n = r.byte()
    s = r.block(n) if n else b""
    if maxsize > n:
        r.skip(maxsize - n)
    return s.decode("latin1")


def parse_tileset(path):
    r = imp.Reader(Path(path).read_bytes())
    background = r.rle(832 * 20)
    pal6 = r.rle(768)
    pal = [(pal6[i*3] << 2 | pal6[i*3] >> 4, pal6[i*3+1] << 2 | pal6[i*3+1] >> 4,
            pal6[i*3+2] << 2 | pal6[i*3+2] >> 4) for i in range(256)]
    tiles = r.rle(1024 * 60)
    return background, pal, tiles


def parse_level(path):
    r = imp.Reader(Path(path).read_bytes())
    r.skip(90)
    tileset = tstr(r, 8)
    r.skip(22)
    music = tstr(r, 12)
    anims = r.block(32 << 6)
    r.skip(32 * 16)
    tiles = r.rle(256 * 256)
    evprops = r.rle(16 * 16)
    events = r.rle(256 * 256)
    rates = [r.u16() for _ in range(16)]
    names = [tstr(r, 8) for _ in range(16)]
    marker = r.u16()
    seconds = r.u16()
    items = r.u16()
    px, py = r.u16(), r.u16()
    evt = []
    for c in range(16):
        b = evprops[c * 16:(c + 1) * 16]
        evt.append(dict(anim=b[5], type=b[7], passable=b[8], used=b[9], sound=b[13]))
    return dict(tileset=tileset, music=music, anims=anims, tiles=tiles, events=events,
                evt=evt, rates=rates, names=names, marker=marker, seconds=seconds,
                items=items, px=px, py=py)


if __name__ == "__main__":
    root = Path(sys.argv[1])
    for n in range(3):
        L = parse_level(root / f"BONUSMAP.00{n}")
        ev = [e for e in L["events"] if e]
        print(n, L["tileset"], L["music"], "marker %04x" % L["marker"], "sec", L["seconds"],
              "items", L["items"], "start", L["px"], L["py"],
              "tiles used", len(set(L["tiles"])), "events", len(ev), sorted(set(ev)))
        print("  evt", [(i, e["type"], e["passable"], e["used"], e["anim"]) for i, e in enumerate(L["evt"]) if e["used"]])
        print("  sounds", L["names"][:6])


def parse_sprites(path):
    """BONUS.000 sprites -> list of (width, height, pixels, mask) (index 0 = transparent when masked)."""
    data = Path(path).read_bytes()
    r = imp.Reader(data)
    r.skip(2)
    count = min(r.u16(), 256)
    out = []
    for _ in range(count):
        w, h = r.u16(), r.u16()
        plen, mlen = r.u16(), r.u16()
        if plen != 0xFFFF:
            w <<= 2
            length = w * h
            end = r.pos + (plen << 2) + mlen
            # mask: 4 pixels per byte, scrambled the same way as the pixels
            maskbytes = r.block((length + 3) // 4)
            mask = [(maskbytes[i >> 2] >> (i & 3)) & 1 for i in range(length)]
            sm = [0] * length
            for i in range(length):
                sm[(i >> 2) + (i & 3) * (length >> 2)] = mask[i]
            px = [0] * length
            for i in range(length):
                if sm[i]:
                    # OpenJazz File::loadPixels skips zero padding bytes: zero
                    # is the transparent key and can never be an opaque pixel.
                    value = 0
                    while value == 0:
                        if r.pos >= end:
                            raise ValueError("unterminated masked BONUS sprite")
                        value = r.byte()
                    px[i] = value
            pixels = bytes(px[(i >> 2) + (i & 3) * (length >> 2)] for i in range(length))
            mask_ = bytes(mask)
            out.append((w, h, pixels, mask_))
            r.seek(end)
        elif w:
            raw = r.block(w * h)
            out.append((w, h, raw, bytes([1]) * (w * h)))
        else:
            out.append((0, 0, b"", b""))
    return out
