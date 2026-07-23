#!/usr/bin/env python3
"""Derive each episode's level order from the game data.

The port used to carry a hardcoded list of eight shareware levels.  Every level
file names the level that follows it, so the running order is in the data and
does not need to be guessed: walk that chain from each level nothing else
points at and you get one run per episode.

Doing this also corrected the shareware list.  It had eight entries, but the
episode's chain is seven - LEVEL2.000 is a secret level that rejoins the run
partway through rather than a stage in its own right.

Usage:
    python3 tools/discover_episodes.py --input /path/to/game/files
    python3 tools/discover_episodes.py --input DIR --episode 2 --format make
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_sgdk_jj1_level_data import runtime_records  # noqa: E402
from import_jj1_shareware import parse_level  # noqa: E402

LEVEL_RE = re.compile(r"LEVEL(\d)\.(\d{3})$")
# A level whose next-level fields hold this is the end of its run.
END_MARKER = 99
# Every full episode is three worlds of two levels plus a guardian stage.
EPISODE_LENGTH = 7


def read_links(directory: Path) -> dict:
    """Map each level file to the level it leads to."""
    links = {}
    for path in sorted(directory.iterdir()):
        if not LEVEL_RE.match(path.name):
            continue
        try:
            record = runtime_records(path)
        except Exception:
            continue  # not a level we can decode; skip rather than abort
        links[path.name] = (record[9], record[10])
    return links


def walk(start: str, links: dict) -> list:
    """Follow the chain from `start`, stopping at the end marker or a loop."""
    seen, current, order = set(), start, []
    while current in links and current not in seen:
        seen.add(current)
        order.append(current)
        level, world = links[current]
        if level == END_MARKER or world == END_MARKER:
            break
        current = f"LEVEL{level}.{world:03d}"
    return order


def episodes(directory: Path) -> list:
    """Every full-length run, in world order."""
    links = read_links(directory)
    pointed_at = set()
    for level, world in links.values():
        if level != END_MARKER and world != END_MARKER:
            pointed_at.add(f"LEVEL{level}.{world:03d}")
    runs = []
    for name in links:
        if name in pointed_at:
            continue  # not a starting point: something leads here
        # An episode opens on its first level.  Secret levels (LEVEL2.xxx) and
        # alternate entries also start chains nothing points at, and some are
        # exactly episode length, so they would otherwise be counted as extra
        # episodes - duplicating a real one under a different number.
        if not name.startswith("LEVEL0."):
            continue
        order = walk(name, links)
        if len(order) == EPISODE_LENGTH:
            runs.append(order)
    # Order by the world the run opens in, so episode numbering is stable.
    runs.sort(key=lambda run: int(LEVEL_RE.match(run[0]).group(2)))
    return runs


def describe(directory: Path, run: list) -> list:
    """Pair each level with the tile world and sprite bank it needs.

    These differ: a guardian stage borrows another world's tiles while shipping
    its own sprites, so both have to be resolved per level rather than assumed.
    """
    out = []
    for name in run:
        meta = parse_level(directory / name)[1]
        out.append({
            "level": name,
            "blocks": meta["blocks_extension"],
            "sprites": f"{meta['world']:03d}",
        })
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", type=Path, required=True,
                    help="directory holding the LEVEL*.* files")
    ap.add_argument("--episode", type=int, default=None,
                    help="report just this episode (1-based)")
    ap.add_argument("--format", choices=("text", "json", "make"), default="text")
    args = ap.parse_args()

    if not args.input.is_dir():
        print(f"not a directory: {args.input}", file=sys.stderr)
        return 2

    runs = episodes(args.input)
    if not runs:
        print(f"no complete episodes found in {args.input}", file=sys.stderr)
        return 1

    if args.episode is not None:
        if not 1 <= args.episode <= len(runs):
            print(f"episode {args.episode} not present; found {len(runs)}",
                  file=sys.stderr)
            return 1
        runs = [runs[args.episode - 1]]
        first = args.episode
    else:
        first = 1

    if args.format == "json":
        print(json.dumps([{"episode": first + i, "levels": describe(args.input, r)}
                          for i, r in enumerate(runs)], indent=2))
    elif args.format == "make":
        for i, run in enumerate(runs):
            entries = describe(args.input, run)
            print(f"EPISODE_{first + i}_LEVELS = " + " ".join(e["level"] for e in entries))
            print(f"EPISODE_{first + i}_BLOCKS = " + " ".join(e["blocks"] for e in entries))
            print(f"EPISODE_{first + i}_SPRITES = " + " ".join(e["sprites"] for e in entries))
    else:
        print(f"{len(runs)} episode(s) in {args.input}\n")
        for i, run in enumerate(runs):
            entries = describe(args.input, run)
            print(f"episode {first + i}: {len(entries)} levels")
            for stage, entry in enumerate(entries):
                note = "" if entry["blocks"] == entry["sprites"] else "  (own sprite bank)"
                print(f"   stage {stage}: {entry['level']}"
                      f"  tiles {entry['blocks']}  sprites {entry['sprites']}{note}")
            print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
