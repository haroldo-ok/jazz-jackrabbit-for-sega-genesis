# Jazz Jackrabbit — Genesis / Mega Drive SGDK port (work in progress)

An SGDK port of *Jazz Jackrabbit* for the Sega Genesis / Mega Drive. It builds a bootable `JazzJackrabbitGenesis.bin` ROM with seven converted episode stages across Diamondus, Tubelectric and Medivo, using original maps, collision masks, event grids and start positions. It remains a work in progress; see `PORTING_STATUS.md`.

> **Current scope / legal status:** the repository ships with a **locally converted JJ1 pack** derived from a user-supplied shareware installation: `MAINCHAR.000` Jazz animation frames, `BLOCKS.000` terrain patterns, the available episode 1 level grids with their 8×8 collision masks, event layers and start records, plus converted scene, menu and sound assets. It does **not** include the raw installation or archive. OpenJazz is GPL-2.0-or-later and original game data remains subject to its own rights. Additional episodes/levels require the corresponding original level files (see *Regenerating data* below).

## What is implemented

- **Physics matched to the original model** in 8.8 fixed-point per frame: two-tier walk (2.50 px/f) / run (5.08 px/f) acceleration with distinct stop and turn decelerations; an 84 px target-height variable jump with a running-speed bonus, cut short by releasing the button or bonking a ceiling; gravity only while rising and the original's immediate terminal fall speed; 4 px mask ramp following.
- **Data-driven entities from the original event grids**: items (score/health/extra-life), walking and flying enemies, static hazards, springs (button-independent relaunch of the same jump-ascent rule), one-way platforms, and end-of-level signs are all resolved through per-level event tables in `src/jj1_eventset.c`. A taken-bitmap makes items collect once and killed enemies stay dead; an 8-slot pool activates enemies near the player, scanning one grid column per frame to bound 68000 cost.
- **Original rendering path**: streaming 64-block VRAM cache over the 256×64 maps on Plane A, sky gradient on Plane B, HUD, camera, springs and uncollected items drawn straight from the event grid.
- **Episode flow** with exits, transitions, lives/health, pause, game over, and completion.
- **Original-art presentation**: startup animation and credit pages, title, six highlighted menu choices, level-clear animation, and bonus-win animation. Screen palettes are reduced for Genesis hardware. All six menu actions work, including original instruction and order pages, four SRAM save slots, and Genesis-specific setup options.

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

#### Weapon sound regeneration

```sh
python3 tools/build_shot_sounds.py --input /path/to/shareware --output res/jj1
```

The weapon clips use the original shareware bullet sound slots and their recorded rates. SGDK converts them to the XGM driver’s fixed playback rate. The fourth special shot has no assigned firing sound in these levels.

### Planet approaches and episode ending

```sh
python3 tools/build_planets.py --input /path/to/shareware
python3 tools/build_episode_ending.py --input /path/to/shareware
```

These generate the original shareware planet/ending tile assets and scene data.
Planet scaling is sampled at 10 Hz; the original ENDE1 text pages wait for a
button and its animation plays at 14 fps. See `PORTING_STATUS.md` for remaining
scene, menu and gameplay limitations.

## Main menu and saves

Use UP/DOWN and START in the main menu. **Load Game** offers four slots. To
save, press START during a level to pause, then C; select a slot with UP/DOWN
and A or START. B/C cancels. Saving overwrites the chosen slot; it preserves
the current port's game state, including position and collected items. Loading
resumes that state in the level. Saves and settings require persistent cartridge
SRAM or an emulator that saves its `.srm` data. They are not DOS/OpenJazz saves
and may not be compatible with future versions of this work-in-progress port.

**Instructions** and **Order Info** contain the original shareware pages.
A/START/RIGHT/DOWN advances, LEFT/UP goes back, and B/C exits. Original DOS
key hints are retained as artwork; the bottom line gives Genesis controls.
Order details are historical and may no longer be valid.

**Setup Options** toggles music and effects and selects among A-fire/B-jump,
B-fire/A-jump, and C-fire/B-jump layouts (the remaining button cycles weapons).
Settings persist in SRAM. Bonus stages use the configured fire button to run
and either other button to jump. The main-menu, viewer and pause controls stay fixed.

To regenerate the original information pages:

```sh
python3 tools/build_menu_pages.py --input /path/to/shareware
```

## Bonus levels

Finish a stage while holding the **bonus diamond** and that world's bonus level starts: D-pad up/down walk, left/right turn, **A** run, **B/C** jump (or the configured fire button to run and either other button to jump). Collect all the gems before the clock runs out (hourglasses add a minute; the exit sign gives up). Winning grants an extra life. Jazz uses the original BONUS.000 player frames and animation lists from BONUSMAP.00n. They are also in the stage-select list below as BONUS 1-3.

### Stage select

On the title screen **or main menu**, enter **UP, UP, DOWN, DOWN, LEFT, RIGHT, LEFT, RIGHT, B, A** (release between repeated buttons). Once unlocked, select a stage or BONUS 1–3 with **UP/DOWN** on the title or **LEFT/RIGHT** on the menu. Press **START** to open the menu, then **START** on New Game. The unlock lasts until reset.

### Official SGDK (2.x)

The project now uses SGDK 2.x's `makefile.gen` (`src/rom_header.c` is the cartridge header). Set `GDK` to the SGDK root and invoke the project makefile:

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
export PATH="$GDK/bin:$PATH"      # native sjasm, bintos and xgmtool helpers
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

## Bonus levels

```bash
python3 tools/build_sgdk_bonus.py --input ~/jazz/JAZZ    # writes res/bonus/*.bin and src/jj1_bonus_data.inc
python3 tools/build_bonus_player.py --input ~/jazz/JAZZ # writes src/jj1_bonus_player.inc
```

## Startup, title, menu and clear scenes

```bash
python3 tools/build_sgdk_scenes.py --input ~/jazz/JAZZ
```

This produces `res/scenes/*.bin` and `src/jj1_scene_data.inc` from the original `.0SC`, `MENU.000`, and `FONTMN2.0FN` files. The STARTUP `BLOCK3` sound is exported by `tools/import_jj1_shareware.py` from `SOUNDS.000` and copied to `res/jj1/BLOCK3.wav`. The converted End Level track is from the FM soundtrack converter linked in `THIRD_PARTY.md`.

## Font

```bash
python3 tools/build_sgdk_font.py --input ~/jazz/JAZZ/FONT2.0FN --output res/jj1_font.png
```

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

## Latest additions

- **Palette cycling** (`tools/jj1_cycle.py`): the original's seven rotating colour ranges (water, lava, sky shimmer) are gated by each level's background type, packed into up to seven rotating palette slots, and stepped at the original rates at runtime (`update_palette_cycles` in `src/main.c`). An on-target self-test checks the rotation.
- **Cycling plan**: ranges qualify by how colourful and how visibly animated their pixels are (greys and blacks are skipped); groups are cut at the offset that makes the ring move most and coloured by pixel usage, so Medivo's lava (red/yellow/orange) and rain both visibly move.
- **Event sounds**: the sound index stored in each event record (item taken, enemy killed, block broken) selects one of the 22 SOUNDS.000 clips via `jj1_event_sfx()` in `inc/jj1_sfx.h`; clips that already had an id are reused, 11 more are registered.
- **Status panel** (`tools/build_sgdk_panel.py`): metal plate, score/gem/lives boxes with yellow digits, lives and gem icons from PANEL.000, and hearts for health. The hearts and digit glyphs are redrawn, not copied, because the originals do not map onto 8x8 tiles. The tiles sit in the unused lowercase font slots.
- **Performance**: queued DMA for per-frame uploads, per-stage event/class caches, persistent item tile slots, HUD rebuild only on change, off-screen enemy culling. Profiling builds use `-DJAZZ_PROFILE` (main game: per-section times, missed frames) and `-DBONUS_PROF` (bonus level).

### Optimisation pass (bonus levels 10 -> 30 fps, main levels far fewer dropped frames)

Every change keeps the output identical; each is checked as noted.

- **Bonus floor as tilemaps** (`src/bonus_view.c`): two horizontally adjacent 4x4 samples form the top half of one of 256 preloaded "pair" tiles. Plane B shows the even sample rows; plane A, scrolled 4 lines down, shows the odd ones. The floor costs 800 map words per frame (two DMAs of 1280 bytes) instead of 12.8 KB of tile pixels, which used to overflow vblank and halt the CPU. The sky and strip now turn with per-tile horizontal scroll so the floor rows stay put.
- **Bonus floor inner loop** (`src/floor_asm.s`): positions only keep their place inside the current floor tile, with guard bits so the carry flag fires exactly at a tile crossing; the tile lookup happens only then. Rows are walked in whichever direction makes both steps positive (four variants), the left sample of each pair reads a pre-shifted texel copy (`res/bonus/tiles_hi.bin`), and one call renders all 20 rows. Checked: `-DBONUS_SELFTEST` compares it word for word with the C reference (`render_floor_c`) at all 1024 angles and six positions; fixed-step builds (`-DBONUS_FIXSTEP=n -DBONUS_STOPAT=n`) match the original renderer's frames pixel for pixel.
- **Bonus items**: an assembly scan lists the few non-empty event cells; the projection then uses exact 16x16 multiplies, a cheap exact off-screen test before the division, an exact small-quotient division and exact size-class thresholds. The removed-item check has a per-row filter.
- **Exact 16-bit arithmetic**: GCC calls libgcc's `__mulsi3`/`__modsi3`/`__divsi3` for many products and remainders the 68000 can do with one `muls`/`mulu`/`divu`; the hot ones (floor rows, item projection, sprite animation frames) now use small inline-asm helpers.
- **Collision probes** (`src/game_core.c`, `src/jj1_runtime.c`): the game probes several points per pixel moved; each probe was two cross-file calls. It now reads the runtime's cached tables inline (`Jj1Fast`), bridge scans only run on rows that hold a bridge, rectangle tests walk the 4 px cells once, and grid scans use the class table instead of two calls per cell. Checked: a host trace of 7 stages x 6000 frames of random input (player, enemies, bullets, score, destroyed blocks) is identical before and after.
- **Map streaming** (`src/main.c`): the block cache has a reverse map and a per-window visibility stamp instead of a 64-slot search plus an 88-cell scan per eviction candidate, and map rows are written with one VDP address per four cells (auto-increment set explicitly). Checked: `-DJAZZ_MAPCHECK -DJAZZ_AUTOPILOT` reads plane A back from VRAM after every camera update; stages 0-6 show no wrong block.

Measured under Genesis Plus GX: bonus levels went from 6 vblanks per frame (10 fps) to 2 (30 fps). On the main levels, with identical autopilot input, missed frames over the same run dropped from roughly 900 to 169 on stage 0 and from about 600 to 149 on stage 3; stages 5 and 6 now miss none. Some heavy frames on stages 0 and 3 still miss a vblank.

### Music: four PCM voices

`tools/build_music.py --input <dir with the .PSM files>` re-converts the six tracks (menu, bonus, end of level, Diamondus, Tubelectric, Medivo) to `res/music/*.xgm`, which `res/jj1_music.res` now uses (`tools/build_episode.py` picks `.xgm` over `.vgm` when present).

- Every original module has four channels. Each module channel's percussion and one-shot hits (kicks, snares, toms, hats, shakers, cymbals, rides, orchestra and distortion hits) now play on their own XGM PCM channel, from the module's own samples, rendered band-limited at 14 kHz per pitch and volume. Hits that overlap in the original no longer cut each other off as they did on the single DAC stream, and hats, shakers and cymbals are real samples instead of PSG noise.
- The melodic parts keep the existing FM/PSG arrangement (`res/music/*.vgm`, with its PCM stream removed and PSG noise silenced).
- Playing all four channels entirely as PCM was measured at 2.3-3.3 MB of pre-rendered samples. XGM has no PCM pitch or volume control, and the ROM has no room for that much, so the melodic parts stay on FM.
- The XGM files are written directly, because xgmtool keeps only one PCM start per frame when reading a VGM.
- Sound effects now play at priority 8 and above (`jj1_sfx_start`), so they always take their channel over a drum voice, and the drum voice comes back with its next hit. Channel 1, which effects never use, carries each song's busiest drum channel.

Test-only build flags: `-DJAZZ_AUTOPILOT` (input from the play iteration, so builds of different speed play the same game), `-DJAZZ_FIXED_PACE=n` (each loop iteration takes n frames), `-DJAZZ_MAPCHECK`, `-DBONUS_SELFTEST`.

New files: `src/floor_asm.s`, `tools/jj1_cycle.py`, `tools/build_sgdk_panel.py`, `res/jj1_hud.*`, `res/bonus/tiles_hi.bin`. The ROM is 4 MB (4,135,520 bytes used; header end 0x3FFFFF).

The remaining original fonts (FONTBIG, FONTINY, FONTMN1, FONTMN2) are still not converted.

## Roadmap toward a faithful OpenJazz-based port

1. Build a host-side importer for legally supplied JJ1 data (`LEVEL*.###`, `MAINCHAR.000`, `PANEL.000`, palette and sound files).
2. Convert level event streams into Genesis-friendly chunk/tile/object data and stream 64×32 plane columns.
3. Port selected GPL-compatible OpenJazz JJ1 rules in fixed-point C, preserving original event identifiers and episode flow.
4. Convert graphics to 16-colour palette banks and music/SFX to XGM/XGM2/PCM within VRAM, sprite-per-line, and Z80 budgets.
5. Expand gameplay and presentation fidelity, secret-level support, regression recordings, and hardware tests.

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
- The **headless emulator suite** needs no display: it builds the Genesis Plus GX libretro core, runs a `-DJAZZ_AUTOTEST` ROM whose 68000-side assertions report through an SRAM marker, then scripts a title → menu → New Game → run/jump playthrough of the release ROM and asserts framebuffer pixels (title artwork, Jazz sprite, sky, converted terrain, advancing animation, camera scrolling, live HUD). Screenshots land in `tests/artifacts/`.
- `tests/run_emulator_test.sh` remains as an optional X-based BlastEm variant.

See `PORTING_STATUS.md` for the latest results and remaining gaps. `TEST_REPORT.md` is an earlier report.

## Still missing

See `PORTING_STATUS.md` for the exact scope and test results of this revision.


This is not a completed shareware port; see `PORTING_STATUS.md` for the remaining gameplay and presentation gaps.
