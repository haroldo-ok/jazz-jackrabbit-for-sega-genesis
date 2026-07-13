# Jazz Jackrabbit — Genesis / Mega Drive SGDK port (work in progress)

An SGDK port of *Jazz Jackrabbit* for the Sega Genesis / Mega Drive. It builds a bootable `JazzJackrabbitGenesis.bin` ROM that plays the three converted shareware Diamondus levels with the original maps, collision masks, event grids, start positions, and a physics model matched to the original game (via the GPL OpenJazz reference engine).

> **Current scope / legal status:** the repository ships with a **locally converted JJ1 pack** derived from a user-supplied shareware installation: `MAINCHAR.000` Jazz animation frames, `BLOCKS.000` terrain patterns, all three `LEVEL0–2.000` 256×64 block grids with their 8×8 collision masks, event layers, and start records, plus `SOUNDS.000` gun/jump/pickup effects. It does **not** include the raw installation or archive. OpenJazz is GPL-2.0-or-later and original game data remains subject to its own rights. Additional episodes/levels require the corresponding original level files (see *Regenerating data* below).

## What is implemented

- **Physics matched to the original model** in 8.8 fixed-point per frame: two-tier walk (2.50 px/f) / run (5.08 px/f) acceleration with distinct stop and turn decelerations; an 84 px target-height variable jump with a running-speed bonus, cut short by releasing the button or bonking a ceiling; gravity only while rising and the original's immediate terminal fall speed; 4 px mask ramp following.
- **Data-driven entities from the original event grids**: items (score/health/extra-life), walking and flying enemies, static hazards, springs (button-independent relaunch of the same jump-ascent rule), one-way platforms, and end-of-level signs are all resolved through per-level event tables in `src/jj1_eventset.c`. A taken-bitmap makes items collect once and killed enemies stay dead; an 8-slot pool activates enemies near the player, scanning one grid column per frame to bound 68000 cost.
- **Original rendering path**: streaming 88-block VRAM cache over the 256×64 maps on Plane A, sky gradient on Plane B, HUD, camera, springs and uncollected items drawn straight from the event grid.
- **Three-stage episode flow** with exits, transitions, lives/health, pause, game over, and completion.

Platform-neutral logic lives in `src/game_core.c` + `src/jj1_runtime.c` and compiles unchanged on the host for testing and inside the ROM.

## Regenerating data from a shareware installation

The converter reads an installed shareware copy and emits the compiled-in includes, including ground-truth event tables that automatically override the provisional ones:

```bash
python3 tools/build_sgdk_jj1_level_data.py \
  --input /path/to/jazz-install --level LEVEL0.000 --name jj1_level0 \
  --output src/jj1_level0_data.inc --eventset src/jj1_level0_eventset.inc
```

Repeat per level. `src/jj1_eventset.c` picks up `jj1_levelN_eventset.inc` files automatically when present.

## Controls

| Genesis controller | Action |
|---|---|
| D-pad Left / Right | Run |
| A | Fire |
| B or C | Jump |
| Start | Start game / pause / resume |

## Build

### Official SGDK

Set `GDK` to the SGDK root and invoke the project makefile:

```bash
make GDK=/path/to/sgdk release
# ROM: out/rom.bin
cp out/rom.bin JazzJackrabbitGenesis.bin
```

### Linux validation setup used here

The checked build used SGDK 2.11 with a native Linux `m68k-linux-gnu-` cross compiler:

```bash
export GDK=/path/to/sgdk
export PREFIX=m68k-linux-gnu-
export PATH="$GDK/bin:$PATH"      # native sjasm/bintos helpers
make GDK="$GDK" PREFIX="$PREFIX" release
```

The project `Makefile` intentionally links `$(GDK)/lib/libgcc.a`: the distribution's `m68k-linux-gnu` `libgcc` may contain instructions unavailable on a stock 68000. Rebuild SGDK's `libmd.a` with the same compiler when using a nonstandard native toolchain.

## Tests

### Host simulation tests

These compile the pure game-core model outside SGDK and cover stage construction, motion, jumping/landing, pause, projectile/enemy collision, pickup collection, damage invulnerability, stage transitions, and complete episode progression:

```bash
./tests/run_host_tests.sh
```

### BlastEm point-to-point integration test

This launches the release ROM under a virtual X display, injects **Start + Right**, captures a frame, and asserts that level terrain, player, pickup, HUD path, and gameplay palette pixels are present. A saved image is produced at `tests/artifacts/blastem_gameplay.png`.

```bash
GDK=/path/to/sgdk PREFIX=m68k-linux-gnu- ./tests/run_emulator_test.sh
```

Requirements: `BlastEm`, `Xvfb`, `xdotool`, ImageMagick `import`, Python Pillow, SGDK, and an m68k cross toolchain. Run both suites with:

```bash
GDK=/path/to/sgdk PREFIX=m68k-linux-gnu- ./tests/run_all.sh
```

## Importing the supplied shareware installation

The importer expects the **installed** JJ1 files, not the outer `1jazz13.zip` self-extractor. For example, after installing the supplied shareware package into `~/jazz/JAZZ`:

```bash
python3 tools/import_jj1_shareware.py \
  --input ~/jazz/JAZZ --output /tmp/jj1-export
python3 tools/build_sgdk_jj1_visuals.py \
  --input ~/jazz/JAZZ --output src/jj1_visuals.inc
python3 tools/build_sgdk_jj1_level_data.py \
  --input ~/jazz/JAZZ --level LEVEL0.000 --name jj1_level0 \
  --output src/jj1_level0_data.inc
python3 tools/build_sgdk_jj1_level_screen.py \
  --input ~/jazz/JAZZ --level LEVEL0.000 --x 40 --y 0 \
  --name jj1_level0_screen --output src/jj1_level0_screen.inc
```

The catalog command converts **every available** `BLOCKS.*` set, `SPRITES.*` set, `LEVEL*.*` map, `MENU.000` background/highlight, `PANEL.000` preview, `.0FN` font atlas, and the `SOUNDS.000` clip catalog into PNG/WAV/JSON intermediates. The visual script creates the resident eight-frame Jazz pack. The level-data script creates all 240 block patterns plus a complete 256×64 map for the runtime cache; run it with a distinct symbol name/output for each shareware world. The level-screen script creates inspectable 40×28 map chunks. Do not commit a complete original installation or redistributed raw archive.

## Project layout

- `src/game_core.c`, `inc/jazz_game.h` — platform-neutral deterministic gameplay / collision simulation.
- `src/main.c` — SGDK VDP, controller, audio beep, plane, sprite, HUD, and state integration.
- `src/jj1_visuals.inc` — generated 4bpp terrain/detail patterns plus eight original 32×32 Jazz animation frames.
- `src/jj1_level[0-2]_data.inc` — all converted `BLOCKS.000` blocks and full row-major 256×64 shareware maps used by the 88-block runtime VRAM cache.
- `src/jj1_level[0-2]_screen.inc` — offline 40×28-tile chunk outputs used to validate the map converter; the runtime uses the complete data includes.
- `src/gfx.c` — visual integration plus remaining placeholder entity graphics.
- `res/jj1/`, `res/jj1_sounds.res` — locally converted original JJ1 PCM gun/jump/pickup effects.
- `tools/import_jj1_shareware.py` — `BLOCKS.000`, `MAINCHAR.000`, `SPRITES.000`, `LEVEL*.000`, and `SOUNDS.000` decoder/exporter.
- `tools/build_sgdk_jj1_visuals.py` — quantizes selected source art to the 16-colour Genesis pack and writes `src/jj1_visuals.inc`.
- `tools/build_sgdk_jj1_level_screen.py` — composes a real 10×7 JJ1 block-map region into a 40×28 Genesis foreground tile chunk.
- `tools/build_sgdk_jj1_level_data.py` — quantizes all blocks plus an entire 256×64 JJ1 map for the streamed Genesis block cache.
- `src/boot/rom_head.c` — Genesis ROM header.
- `tests/test_game_core.c` — host point-to-point assertions.
- `tests/run_emulator_test.sh` — emulator P2P capture/assertion.

## Roadmap toward a faithful OpenJazz-based port

1. Build a host-side importer for legally supplied JJ1 data (`LEVEL*.###`, `MAINCHAR.000`, `PANEL.000`, palette and sound files).
2. Convert level event streams into Genesis-friendly chunk/tile/object data and stream 64×32 plane columns.
3. Port selected GPL-compatible OpenJazz JJ1 rules in fixed-point C, preserving original event identifiers and episode flow.
4. Convert graphics to 16-colour palette banks and music/SFX to XGM/XGM2/PCM within VRAM, sprite-per-line, and Z80 budgets.
5. Add save RAM, all shareware worlds, guardian/boss behavior, bonus levels, ending flow, regression recordings, and hardware tests.

## References

- Original shareware installer: <https://www.classicdosgames.com/files/games/epic/1jazz13.zip>
- OpenJazz source port (GPL-2.0-or-later): <https://github.com/AlisterT/openjazz>
- SGDK: <https://github.com/Stephane-D/SGDK>

## Testing

```bash
tests/run_all.sh          # everything below
tests/run_host_tests.sh   # host point-to-point simulation tests
GDK=/path/to/sgdk tests/run_headless_emulator_test.sh
```

- The **host suite** compiles the exact game core, JJ1 runtime, and converted level data with a native compiler and asserts point-to-point behaviour: spawn settling on the original masks of all three levels, walk/run speed tiers, the 84 px variable jump, immediate terminal fall, item collection with the taken bitmap, enemy activation/kill/no-respawn, contact damage and invulnerability, spring launch heights, end-sign exits, and full episode completion.
- The **headless emulator suite** needs no display: it builds the Genesis Plus GX libretro core, runs a `-DJAZZ_AUTOTEST` ROM whose 68000-side assertions report through an SRAM marker, then scripts a title → START → run/jump playthrough of the release ROM and asserts framebuffer pixels (title text, Jazz sprite, sky, converted terrain, advancing animation, camera scrolling, live HUD). Screenshots land in `tests/artifacts/`.
- `tests/run_emulator_test.sh` remains as an optional X-based BlastEm variant.

See `TEST_REPORT.md` for the latest results and remaining gaps (original sprite/animation mapping, music, bosses, warps, and additional worlds pending their level files).
