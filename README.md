# Jazz Jackrabbit — Genesis / Mega Drive SGDK prototype

A **working SGDK gameplay prototype** inspired by *Jazz Jackrabbit* and structured as a 68000 Genesis project. It produces a bootable `JazzJackrabbitGenesis.bin` ROM with a title screen, three short side-scrolling stages, run/jump/fire controls, enemies, pickups, HUD, health/lives, pause, exits, and an episode-complete state.

> **Current scope / legal status:** this is not yet a binary-compatible or asset-complete port of the original PC shareware release. The current build contains a **locally converted JJ1 pack** derived from the supplied shareware installation: eight original `MAINCHAR.000` Jazz animation frames, selected `BLOCKS.000` terrain patterns, three original `LEVEL0–2.000` / `BLOCKS.000` 10×7 background chunks, and original `SOUNDS.000` gun/jump/pickup effects. It does **not** include the raw installation/archive. OpenJazz is GPL-2.0-or-later and original game data remains subject to its own rights.
>
> The remaining major gap is data-driven play: entity/event and three-stage progression rules are still bespoke. The asset catalog importer decodes every installed block set, sprite set, map, UI background/panel, font, and sound clip. The runtime now streams all three shareware `LEVEL0–2.000` 256×64 block grids, original 8×8 block collision masks, and original start coordinates through the same 88-block Genesis VRAM cache. A faithful port still requires JJ1 event tables, item/enemy animation mapping, music conversion, bosses, save data, and cutscene flow.

The runtime now renders the JJ1 maps on Plane A, uses the original block masks for 4×4-pixel collision probes, handles event 122 as a one-way platform, follows 4px ramp steps, and draws a JJ1-style gradient on Plane B. The original address-error shown in the supplied screenshot was corrected by:

- linking SGDK's 68000-safe `libgcc.a` instead of a Linux-target `libgcc` that emitted 68020 `BSR.L` instructions;
- disabling GCC's store-merging/vector store optimizations for target code;
- preventing byte tile-map cells from being folded into unaligned word stores;
- removing an unaligned stack-string initialization.

The final ROM is **`JazzJackrabbitGenesis.bin`**.

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
