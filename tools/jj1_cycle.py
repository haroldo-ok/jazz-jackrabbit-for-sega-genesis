"""Palette colour cycling for JJ1 levels, reduced to fit a Genesis palette line.

OpenJazz (jj1levelload.cpp) animates a handful of fixed ranges of the 256-colour
palette with RotatePaletteEffect: waterfalls, lava, electric arcs, lamps.  Which
ranges turn depends on the level's background-effect byte.  The Genesis has 15
usable colours per terrain line, so a range cannot rotate colour for colour.
Instead every used range is cut into K groups, each group becomes one reserved
palette slot, and the runtime rotates those K slots; the visible speed matches
the original because K/N of the colours turn at N/K times the step length.
"""
from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_jj1_shareware import Reader  # noqa: E402

# (first index, colours, signed colours-per-second, level-type exclusions)
# speeds are RotatePaletteEffect's F16/F32 (16 / 32 colours per second); the
# sign follows the engine (positive speed walks the colours towards lower
# indices, so the picture appears to flow towards higher ones).
RANGES = [
    (112, 4, 32),
    (116, 8, 16),
    (124, 3, 16),
    (132, 8, 16),
    (160, 32, -16),
    (192, 32, -32),
    (224, 16, 16),
]
PE_SKY, PE_2D, PE_1D = 2, 8, 9

SLOT_BUDGET = 7          # of the 15 terrain colours
MIN_SHARE = 0.004        # a range under 0.4 % of the world's pixels stays static


def level_mask(bg_type: int) -> int:
    """Bit n set means RANGES[n] rotates in a level with this background byte."""
    m = 0b0000111
    if bg_type not in (PE_1D, PE_2D):
        m |= 1 << 3
    if bg_type not in (PE_SKY, PE_2D):
        m |= 1 << 4
    if bg_type != PE_SKY:
        m |= (1 << 5) | (1 << 6)
    return m


def level_bg_type(path: Path) -> int:
    """The background palette-effect byte that follows the bullet-name block."""
    r = Reader(path.read_bytes())
    r.seek(39)
    r.skip_rle()
    r.skip_rle()
    r.rle((60 * 4 + 16) * 8)
    r.rle(16 * 512)
    r.rle(127 * 32)
    r.skip_rle()
    r.rle(128 * 64)
    r.skip_rle()
    r.skip((16 * (8 + 1)) + 9)
    r.skip(32 * 2)
    for _ in range(32):
        r.byte()
        r.skip(8)
    r.skip(13 * 3)
    r.skip(39)
    r.u16(); r.u16()
    r.skip(1 + 1 + 2 + 2 + 2 + 1 + 2)
    r.rle(38 * 2)
    r.skip(4)
    r.rle(32 * 20)
    r.skip_rle()
    return r.byte()


def group_colours(n: int, k: int, off: int, use: Counter, palette):
    """Average colour of each of the k groups, weighted by how much the
    world's tiles use each entry (equal weights when none of it is used)."""
    first, count, _ = RANGES[n]
    c = {"n": n, "k": k, "off": off}
    groups = [[] for _ in range(k)]
    for i in range(first, first + count):
        groups[group_of(i, c)].append(i)
    out = []
    for g in groups:
        w = [use[i] for i in g]
        tot = sum(w)
        if tot:
            out.append(tuple(sum(use[i] * palette[i][ch] for i in g) / tot for ch in range(3)))
        else:
            out.append(tuple(sum(palette[i][ch] for i in g) / len(g) for ch in range(3)))
    return out


def activity(n: int, k: int, off: int, use: Counter, palette) -> float:
    """Colour change (summed over used pixels) when the ring advances one step.

    A pixel shows its group's average colour and one step moves it to the next
    group's, so groups that average to near-identical colours read as frozen.
    """
    first, count, _ = RANGES[n]
    colours = group_colours(n, k, off, use, palette)
    c = {"n": n, "k": k, "off": off}
    total = 0.0
    for i in range(first, first + count):
        g = group_of(i, c)
        a, b = colours[g], colours[(g + 1) % k]
        total += use[i] * sum(abs(a[x] - b[x]) for x in range(3)) / 3
    return total


def best_offset(n: int, k: int, use: Counter, palette) -> int:
    """Where to start cutting the range into groups so the ring moves most."""
    count = RANGES[n][1]
    return max(range(max(1, count // k)),
               key=lambda off: activity(n, k, off, use, palette))


MIN_PER_PIXEL = 20.0     # colour change per ring step (0-255) a range's own pixels must show


def plan(tiles: list[bytes], defined: int, palette) -> list[dict]:
    """Choose which ranges get reserved slots and how many (K) each.

    A range qualifies when it is used enough and, measured with the averaged
    colours its groups would really show, one ring step visibly changes its
    pixels (a range that is mostly one colour only wastes slots).  Every
    qualifying range gets two groups, most important first; leftover slots go
    to the range whose steps are subtlest, because that is the one that looks
    frozen with only two groups.
    """
    use = Counter()
    for block in tiles[:defined]:
        use.update(block)
    total = sum(use.values()) or 1
    cands = []
    for n, (first, count, speed) in enumerate(RANGES):
        px = sum(use[i] for i in range(first, first + count))
        if px / total < MIN_SHARE:
            continue
        step = activity(n, 2, best_offset(n, 2, use, palette), use, palette) / px      # per pixel of the range
        # Greys and blacks flicker rather than flow: weight by how colourful
        # the range's pixels are.
        colourful = sum(use[i] * (max(palette[i]) - min(palette[i])) / 255
                        for i in range(first, first + count)) / px
        if step >= MIN_PER_PIXEL and colourful >= 0.25:
            cands.append({"n": n, "px": px, "step": step, "k": 0,
                          "weight": px * colourful})
    cands.sort(key=lambda c: -c["weight"])
    left = SLOT_BUDGET
    chosen = []
    for c in cands:
        if left >= 2:
            c["k"] = 2
            left -= 2
            chosen.append(c)
    while left and chosen:
        grow = [c for c in chosen if c["k"] < min(4, RANGES[c["n"]][1])]
        if not grow:
            break
        min(grow, key=lambda c: c["step"])["k"] += 1
        left -= 1
    chosen.sort(key=lambda c: c["n"])
    return [{"n": c["n"], "px": c["px"], "k": c["k"],
             "off": best_offset(c["n"], c["k"], use, palette)} for c in chosen]


def group_of(index: int, c: dict) -> int | None:
    first, count, _ = RANGES[c["n"]]
    if first <= index < first + count:
        return ((index - first + c.get("off", 0)) % count) * c["k"] // count
    return None


def step_frames(c: dict) -> int:
    """Frames (60 Hz) the group ring waits before moving one slot."""
    _, count, speed = RANGES[c["n"]]
    return max(1, round(60 * count / (abs(speed) * c["k"])))
