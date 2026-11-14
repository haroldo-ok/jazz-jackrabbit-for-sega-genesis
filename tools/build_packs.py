#!/usr/bin/env python3
"""Convert the episodes and link their packs.

  build_packs.py generate --input DIR   convert episodes 2-9 and Holiday Hare
                                        from a full game installation into
                                        packs/ep<N>/ (episode 1, the
                                        shareware, is checked in), and the
                                        episode-selection screen
  build_packs.py packs                  compile and link each episode's pack
                                        (out/packs/ep<N>/game.elf)

The linked packs are the input of tools/build_cartridge.py, which turns them into
the cartridge's compact packs (tile pools, packed maps) and assembles the
5 MB ROM.  `make` runs both.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
PACKS = ROOT / "packs"
sys.path.insert(0, str(TOOLS))


# Module -> track name (the names of the FM arrangements in res/music).
SONGS = {
    "song0.psm": "diamondus", "song3.psm": "tubelectric", "song17.psm": "medivo",
    "song7.psm": "letni", "song2.psm": "technoir", "song11.psm": "orbitus",
    "song8.psm": "fanolint", "song14.psm": "scraparap", "song16.psm": "megairbase",
    "song4.psm": "turtemple", "song5.psm": "nippius", "song6.psm": "jungrock",
    "song9.psm": "marbelara", "song1.psm": "sluggion", "song13.psm": "dreempipes",
    "song12.psm": "pezrock", "song10.psm": "chrysilis", "song15.psm": "battleships",
    "boss.psm": "boss",
    "songcd0.psm": "exoticus", "songcd1.psm": "industrius", "songcd2.psm": "muckamok",
    "songcd3.psm": "raneforus", "songcd4.psm": "stonar", "songcd5.psm": "deckstar",
    "songcd6.psm": "pezrock", "songcd7.psm": "deserto", "songcd8.psm": "lagunicus",
    "xm2.psm": "xmas_1", "xm3.psm": "xmas_2",
}
TITLES = {1: "TURTLE TERROR", 2: "BALLISTIC BUNNY", 3: "RABBITS REVENGE",
          4: "GENE MACHINE", 5: "THE CHASE IS ON", 6: "THE FINAL CLASH",
          7: "EPISODE A", 8: "EPISODE B", 9: "EPISODE C", 10: "HOLIDAY HARE"}
HOLIDAY_RUN = ["LEVEL0.050", "LEVEL0.051", "LEVEL0.052"]


def bonus_number(world: int) -> int:
    """BONUSMAP index of a world: the thirty maps follow the worlds in order."""
    if world < 18: return world
    if 30 <= world <= 38: return world - 12
    if 50 <= world <= 52: return world - 23
    return -1


# --------------------------------------------------------------------------
# generate: game data -> packs/ep<N>/
# --------------------------------------------------------------------------
def level_strings(path: Path):
    from import_jj1_shareware import Reader
    r = Reader(path.read_bytes()); r.seek(39)
    for _ in range(8): r.skip_rle()
    r.skip(16 * 9 + 9 + 32 * 2 + 32 * 9)
    out = []
    for _ in range(3):
        n = r.byte()
        out.append(r.block(min(n, 12)).decode("ascii", "replace").lower()); r.skip(12 - min(n, 12))
    return out      # music, next-level scene, end scene


def generate_bonus(data: Path, out: Path, numbers):
    import bonus_parse as bp
    metas = []
    for slot, number in enumerate(numbers):
        level = bp.parse_level(data / f"BONUSMAP.{number:03d}")
        (out / f"bonus_grid{slot}.bin").write_bytes(bytes(min(59, t) for t in level["tiles"]))
        (out / f"bonus_events{slot}.bin").write_bytes(bytes(level["events"]))
        e = level["evt"]
        j = lambda k: "{" + ",".join(str(x[k]) for x in e) + "}"
        metas.append(f"    {{ {level['seconds']}, {level['items']}, {level['px']}, {level['py']}, "
                     f"{j('type')}, {j('passable')}, {j('used')}, {j('sound')} }},")
    return metas


def start_position(inc: Path, index: int):
    text = inc.read_text()
    x = re.search(rf"jj1_level{index}_start_x = (\d+);", text)
    y = re.search(rf"jj1_level{index}_start_y = (\d+);", text)
    return int(x.group(1)), int(y.group(1))


def generate_episode(data: Path, episode: int, run):
    """Everything packs/ep<N>/ needs.  Episode 1 keeps its checked-in levels."""
    from discover_episodes import describe
    import build_episode
    out = PACKS / f"ep{episode}"
    out.mkdir(parents=True, exist_ok=True)
    stages = describe(data, run)
    if episode != 1:
        for stale in out.glob("jj1_world*_tiles.inc"):
            stale.unlink()
        build_episode.generate(data, episode, out, levels=run)

    strings = [level_strings(data / s["level"]) for s in stages]
    # --- music: one track per distinct module
    tracks = []
    stage_track = []
    for music, _, _ in strings:
        name = SONGS.get(music, "diamondus")
        if name not in tracks: tracks.append(name)
        stage_track.append(tracks.index(name))
    # --- planets: by the level file's own world
    planet_exts = []
    for s in stages:
        ext = s["level"].rsplit(".", 1)[-1]
        if ext not in planet_exts and (data / ("PLANET." + ext)).exists():
            planet_exts.append(ext)
    planet_index = {e: i for i, e in enumerate(planet_exts)}
    # --- bonus maps: the three worlds of the episode, in order; a guardian
    #     stage uses the last one
    numbers = []
    stage_bonus = []
    for s in stages:
        n = bonus_number(int(s["sprites"]))
        if n < 0 or not (data / f"BONUSMAP.{n:03d}").exists():
            stage_bonus.append(max(0, len(numbers) - 1)); continue
        if n not in numbers and len(numbers) < 3: numbers.append(n)
        stage_bonus.append(numbers.index(n) if n in numbers else len(numbers) - 1)
    while len(numbers) < 3: numbers.append(numbers[-1] if numbers else 0)
    if episode == 1:          # every episode plays episode 1's bonus levels
        generate_bonus(data, out, numbers)
    # --- glue
    levels = []
    for i in range(len(stages)):
        x, y = start_position(out / f"jj1_level{i}_data.inc", i)
        levels.append(f"    {{ jj1_level{i}_blocks, jj1_level{i}_masks, jj1_level{i}_events, {x}, {y} }},")
    game = ["/* Generated by tools/build_packs.py; do not edit. */",
            f"#define PACK_EPISODE {episode}", f"#define PACK_STAGES {len(stages)}"]
    for i in range(len(tracks)):
        game.append(f"extern const u8 pack_music_{i}[];")
    game.append("#define PACK_STAGE_BONUS { " + ", ".join(str(b) for b in stage_bonus) + " }")
    game.append("#define PACK_STAGE_PLANET { " + ", ".join(
        str(planet_index.get(s["level"].rsplit(".", 1)[-1], 255)) for s in stages) + " }")
    game.append("#define PACK_LEVELS { \\\n" + " \\\n".join(levels) + " \\\n}")
    game.append("#define PACK_STAGE_MUSIC { " + ", ".join(f"pack_music_{t}" for t in stage_track) + " }")
    (out / "pack_game.inc").write_text("\n".join(game) + "\n")

    from build_planets import planet
    worlds = []
    for e in planet_exts:
        name = planet(data / ("PLANET." + e))[1].upper()
        if name not in worlds and name != "GUARDIAN": worlds.append(name)
    manifest = dict(episode=episode, title=TITLES.get(episode, f"EPISODE {episode}"),
                    worlds=" ".join(worlds), stages=len(stages),
                    levels=[s["level"] for s in stages], music=tracks,
                    planets=planet_exts, bonus=numbers)
    (out / "episode.json").write_text(json.dumps(manifest, indent=1) + "\n")
    print(f"episode {episode}: {manifest['title']}: {manifest['worlds']}; music {tracks}")
    return manifest


def cmd_generate(args):
    from discover_episodes import episodes
    data = args.input
    runs = episodes(data)
    wanted = args.episodes or list(range(1, len(runs) + 1)) + ([10] if (data / HOLIDAY_RUN[0]).exists() else [])
    for episode in wanted:
        run = HOLIDAY_RUN if episode == 10 else runs[episode - 1]
        generate_episode(data, episode, run)
    # the original episode-selection screen (pictures from the full game's MENU.000)
    import build_episode_menu
    try:
        build_episode_menu.convert(data, PACKS / "menu" / "episode_menu.inc")
    except Exception as e:                      # the shareware MENU.000 has no such pictures
        print(f"episode menu not converted ({e})")


# --------------------------------------------------------------------------
# packs: compile and link
# --------------------------------------------------------------------------
def tool(name):
    return os.environ.get("PREFIX", "m68k-elf-") + name


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    if r.returncode:
        sys.stderr.write(" ".join(str(c) for c in cmd) + "\n" + r.stdout + r.stderr)
        raise SystemExit(1)
    return r.stdout


def manifests():
    out = []
    for d in sorted(PACKS.glob("ep*"), key=lambda p: int(p.name[2:])):
        m = d / "episode.json"
        if m.exists():
            out.append((d, json.loads(m.read_text())))
    return out


def build_pack(d: Path, m, work: Path, gdk: Path):
    """Compile one episode's pack.  The music is not linked here:
    tools/build_cartridge.py gives each track's address."""
    work.mkdir(parents=True, exist_ok=True)
    flags = ["-m68000", "-O1", "-fno-builtin", "-fdata-sections", "-fms-extensions",
             "-DSGDK_GCC", "-DJAZZ_JJ1_RUNTIME", "-w",
             f"-I{d}", f"-I{ROOT / 'inc'}", f"-I{ROOT / 'res'}", f"-I{ROOT / 'out'}",
             f"-isystem{gdk / 'inc'}", f"-isystem{gdk / 'res'}"]
    obj = work / "game.o"
    run([tool("gcc"), *flags, "-c", ROOT / "pack" / "game_pack.c", "-o", obj])
    elf = work / "game.elf"
    music = [f"--defsym=pack_music_{i}=0" for i in range(len(m["music"]))]
    run([tool("ld"), "-T", ROOT / "pack" / "pack.ld", "--defsym=PACK_ORG=0x200000", *music,
         "--gc-sections", "-o", elf, obj, "-Map", work / "game.map"])
    undefined = run([tool("nm"), "-u", elf]).strip()
    if undefined:
        raise SystemExit(f"{d.name} pack refers outside itself:\n{undefined}")
    run([tool("objcopy"), "-O", "binary", elf, work / "game.bin"])
    return (work / "game.bin").stat().st_size


def cmd_packs(args):
    gdk = Path(os.environ.get("GDK", "/opt/sgdk"))
    found = manifests()
    if args.episodes:
        found = [(d, m) for d, m in found if m["episode"] in args.episodes]
    if not found:
        raise SystemExit("no packs: packs/ep1 is missing")
    total = 0
    for d, m in found:
        total += build_pack(d, m, ROOT / "out" / "packs" / d.name, gdk)
    (ROOT / "out" / "packs" / "stamp").write_text(" ".join(d.name for d, _ in found) + "\n")
    print(f"{len(found)} episodes linked ({total // 1024} KB before compaction)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate"); g.add_argument("--input", type=Path, required=True)
    g.add_argument("--episodes", type=int, nargs="*")
    g.set_defaults(fn=cmd_generate)
    p = sub.add_parser("packs"); p.add_argument("--episodes", type=int, nargs="*",
                                               help="only these episodes (default: all in packs/)")
    p.set_defaults(fn=cmd_packs)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
