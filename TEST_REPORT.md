# Test report — 2026-07-12 (event/physics update)

## Release artifact

- `JazzJackrabbitGenesis.bin`
- Size: 655,360 bytes (Genesis-aligned ROM)
- SHA-256: `e4731679bd2f4bfae1f3ff1f2020af7aab2fe92407eb69095cb819279ef8efca`
- Toolchain: SGDK 2.11, native `m68k-linux-gnu-` cross compiler (GCC 13.3).

## What this build adds

- Movement rebuilt in 8.8 fixed-point per frame from the original model (via
  the GPL OpenJazz engine): walk tier to 2.50 px/f, run tier to 5.08 px/f,
  separate stop/turn decelerations, gravity applied only while rising, an
  immediate terminal fall speed, and an 84 px target-height variable jump
  with a running-speed bonus that cancels on button release or ceiling bonk.
- Items, enemies, springs, one-way platforms, and the end-of-level sign now
  come from the converted original 256x64 event grids of all three levels,
  through a per-level event-property table (`src/jj1_eventset.c`) using the
  original classification rules (enemy = hurting + killable, item = scoring,
  modifier 6 = one-way, 27 = level end, 29 = spring).
- A 2 KB taken-bitmap tracks collected items / killed enemies per event cell;
  an 8-slot enemy pool activates walkers/flyers/hazards from the grid as the
  player approaches and never respawns killed ones.
- Springs retarget the shared jump-ascent rule (button-independent), as in
  the original engine: the player's FEET rise to `magnitude * 21` px above the
  spring block. Per-ID magnitudes are derived from the converted masks (the
  highest ledge actually landable from each spring, i.e. one below the ceiling
  over that spring), so every spring clears the platform its level geometry
  intends, and a ceiling still clamps the ascent.
- `tools/build_sgdk_jj1_level_data.py --eventset` now also emits ground-truth
  `jj1_levelN_eventset.inc` tables from an installed shareware copy, which
  override the provisional tables automatically at compile time.
- All platform-neutral JJ1 logic moved to `src/jj1_runtime.c`, so the host
  tests exercise the same collision/event code and converted data as the ROM.

## Fixes in this pass

- **Destructible scenery (wooden signs and walls) now breaks.** The original
  implements these as JJ1 *behaviour 21*: the engine counts hits against the
  cell and swaps the block out once they reach the event's `strength`
  (`setTile(gridX, gridY, multiA)`), and *modifier 7* marks them as
  "must not destroy/hurt on contact". Nothing implemented this, so signs and
  walls stayed permanently solid - which is what walled the player off from
  the level 1 exit. There is now a `JJ1_CLASS_DESTRUCT` class, a per-cell
  destroyed overlay, and per-cell hit counters; every collision probe consults
  the overlay, so a broken block stops blocking the player and his bullets,
  and the renderer repaints it as open space.
- **Carrots and rapid-fire no longer hurt.** Event IDs 3/4/5 were classified as
  hazards. The grid says otherwise: they lie in horizontal trails (66-100% have
  a same-ID neighbour) and *never* sit inside terrain - the shape of a pickup
  run, not a hazard strip. They are items now, and a test sweeps every ITEM
  cell in all three levels and fails if any one of them costs the player health.
- **Destructible walls identified from the masks.** IDs 124/125 sit *inside*
  solid rock (41-79% of placements) in long runs (67-96% adjacency); ID 15 is a
  short run buried in rock (the wooden sign). ID 123 is decorative background
  (344 cells, never inside terrain) and stays ignored.
- **Level select.** UP/DOWN on the title screen choose the starting stage.

- **Spring launch strength.** Springs were bucketed into three hard-coded
  heights and targeted the player's top edge. They now implement the original
  rule (feet to `magnitude * 21` px), with magnitudes derived from level
  geometry. Six springs across the three levels are asserted to clear their
  ledges (224/160/224/192/192/96 px), plus a ceiling-clamp assertion.
- **Player drawn ~20 px below the floor.** The 32 px MAINCHAR frames carry
  their feet on the bottom row, so the sprite is now drawn at
  `y + PLAYER_H - 32` instead of `y + 10`.
- **Enemy and spring sprites.** Enemies were a 4-tile blob drawn on PAL3 (the
  sky gradient) and the converted spring frames were corrupt. Both are
  replaced with original stand-in art (32x16 two-frame walker, 16x16 two-frame
  flyer, 16x16 hazard, three spring variants) drawn against the JJ1 sprite
  palette. NOTE: PAL2 is unusable for sprites - `render_backdrop()` overwrites
  it with the per-stage level palette - so all entity sprites (enemies,
  springs, gem, bullet) now live on PAL1, the line the player already uses.
- **Font corruption / VRAM overrun (latent).** SGDK places the plane maps at
  0xC000, leaving 1536 tiles with the 96-tile font at 1440-1535, but the block
  cache (88 blocks x 16 tiles) plus assets ran to ~1622 and the streaming DMA
  overwrote the font. The cache is keyed by block ID, so it was resized to 72
  slots against a measured worst case of 62 distinct blocks visible at once
  (level 1), with a compile-time assert to prevent regression.
- **Flagless builds silently shipped the prototype level.** The JJ1 runtime was
  selected only by `-DJAZZ_JJ1_RUNTIME` in the project Makefile, so a build
  driven by SGDK's stock makefile still compiled and still drew the original
  terrain, but ran the hand-made prototype stage: no event grid, hence no
  enemies, items or springs, and an 18-gem level ("GEMS 0/18" instead of
  "0/198"). The runtime is now the default in `inc/jazz_game.h`
  (opt out with `JAZZ_LEGACY_PROTOTYPE`), and `tests/test_default_runtime.c`
  is compiled *without* the flag to assert it.
- **Jittery scrolling and stair-stepped slopes.** The camera was capped at
  3 px/frame while Jazz runs at over 5 px/frame, so it could never keep up:
  it fell behind and lurched, which read as back-and-forth jitter, and on a
  45-degree ramp (where the player rises 4-6 px/frame) it climbed in visible
  chunks that made smooth slopes look like stairs. It now follows with lag
  proportional to distance, as the original viewport does; measured on target,
  it settles into a constant 18 px lag and then tracks the player exactly.
  The ramps themselves were never stepped - the core follows the 45-degree
  mask smoothly (verified dy +4..+6 against dx +5).
- **HUD flicker.** The status line cleared and rewrote the top two window rows
  every frame from just after vblank - racing the beam over those exact rows,
  so a delayed redraw left them blank. It now writes fixed-width padded lines
  and only touches VRAM when the text actually changes.

## Results

| Suite | Result | Coverage |
|---|---|---|
| `tests/run_host_tests.sh` | PASS | Real-map spawn settling on original masks for all 3 levels, walk/run speed tiers, 84 px variable jump with release cut-off, immediate terminal fall, item collection + taken bitmap, enemy activation/kill/no-respawn, contact damage + invulnerability, spring launch heights against real level geometry + ceiling clamp, end-sign exits, full 3-level episode completion, pause. |
| `tests/run_headless_emulator_test.sh` | PASS | Headless Genesis Plus GX libretro run: on-target 68000 self-test reported through an SRAM marker, plus a scripted title -> START -> run/jump playthrough asserting title text, Jazz sprite, sky, converted terrain, advancing animation, camera scrolling, and a live HUD. |

Latest headless emulator result:

```text
PASS: headless emulator P2P: hero=3695, sky=8533, terrain=897, anim=30853, scrolled=63521, deep_terrain=28953
```

## Known remaining gaps

- The provisional event tables classify IDs from placement statistics; exact
  per-ID behaviour parameters (enemy movement paths, item point values, warp
  targets) need `--eventset` regeneration against an installed shareware copy.
- Enemy/item/spring sprites are original stand-in art, not the game's own:
  the SPRITES.000 animation mapping is still to be written. Regenerating
  visuals from a shareware install emits original spring frames and defines
  `JJ1_HAVE_ORIGINAL_SPRINGS`, which supersedes the stand-in automatically.
- Music, bosses, warps, water, and save data remain open.
- The pickup *kind* for IDs 3/4/5 is not recoverable from the shipped data, so
  they score rather than heal or grant ammo. Which one is the carrot and which
  is the rapid-fire is in the event records - regenerate with `--eventset`
  (the importer now also derives destructible strength from behaviour 21).
