# Test report — implemented menu actions

- Host tests pass, including regeneration of all 12 instruction pages, all 17
  order pages and their 116 chunks from the supplied shareware data.
- Genesis Plus GX: page navigation, return to menu, empty and corrupt save
  rejection, save into slots 1 and 2 without overwriting slot 1, and load in a
  fresh emulator process with an imported 64 KiB SRAM image all passed.
- Persistent setup settings, muted music after restarting, menu cursor, and the
  original stage-select cheat passed. Cartridge SRAM was exercised through the
  target's 68000, not simulated by a host-only save routine.
- Host suite, full headless gameplay/bonus suite, original scene tests, planet
  and ending tests, and firing-waveform tests passed. No real hardware or PAL
  hardware has been tested. SRAM format is specific to this port.
- Entries below are historical; refer to the latest report and
  `PORTING_STATUS.md` when older statements differ.

# Test report — planet approaches and episode presentation

- Verified all four PLANET files, scaled approach frames, labels, palette data
  and same-planet suppression in Genesis Plus GX. Stage mappings use the
  actual level world, including the guardian's PLANET.018.
- Verified the real final-stage transition to the original ENDE1 background,
  scripted text, animation, music and return to title. START/A/C/RIGHT advance
  text pages. The ending is Genesis-adapted; not all palette fades are present.
- Verified both bonus outcomes in emulator: full-frame white-out, return after
  loss and bonus-win animation after victory. Fade timing is VBlank-based.
- Level-clear selection was checked against each shareware level's end-scene
  field. Host scene asset round-trips PASS. Full headless, scene, audio-waveform
  and presentation integration suites PASS.
- Scope remains incomplete, notably the secret level/branch and other gameplay fidelity; see `PORTING_STATUS.md`.

# Test report — weapon firing audio fix

- The toaster now uses the original `FLAMER` firing sound, not `FIRE1` (the Rapid Fire voice). Blaster uses `MACHGUN1`, upward uses `PHOTON`, and bouncer uses `ROCKET2`; the fourth shot has no assigned sample in the shareware level data.
- These clips use the actual 10 kHz or 13 kHz source rates from each level's sound table, converted to the XGM driver's playback rate. All seven shareware stages, source PCM bytes, and sample-slot associations were cross-checked with the supplied game data.
- Host tests PASS. `tests/run_shot_emulator_test.sh` exercised the actual sound dispatch in Genesis Plus GX and compared emitted audio waveforms against all four original clips and the unwanted voice; it also checked silence for the unassigned shot. Scene and full headless emulator suites PASS.
- The entries below are historical reports; where they conflict with the newer report or `PORTING_STATUS.md`, use the newer information.

# Test report — 2026-11-01 (bonus levels)

- **Stage select is always on** (`cheatUnlocked = 1`); the emulator suite now asserts the picker is visible at boot and that UP changes the stage.
- **Bonus music.** The bonus levels play their own track (`res/music/bonus.vgm`, registered as `jj1_music_bonus`). It is the converter's FM-only version of the bonus theme (`vgms_fm/bonus.vgm` from your S3M-to-VGM tool, the same folder the rest of the ROM's soundtrack came from), not a conversion of `BONUS.PSM` itself. Covered by emulator step 9 (audio energy inside the bonus level).

**Fix: unreadable text.** The converted font kept the original's dithered 16-shade ramp, which turns to noise at 8x8, and had a transparent background, so the status bar was white-on-pale-sky. Glyphs are now solid white on an opaque dark backdrop, narrow letters are centred in the cell, and `tools/build_sgdk_font.py` produces it. Text checks in the emulator suite (title, HUD, cheat line, bonus HUD) were updated to the new colours.

**Fix: bonus items were invisible.** Two causes. (1) `draw_items` passed the tile coordinates to `bonus_event` as (y, x) instead of (x, y), so items were only found by coincidence. (2) The sky was one flat blue, the same blue as the gem graphics, and the converter ignored the original's colour key (pixel value 0 is transparent), leaving an opaque black box around each item. The sky is now the original gradient (palette entries 128-171) on its own palette line, and value 0 is transparent. Covered by emulator step 8 (a test build starts one tile in front of a gem and asserts it is visible with no black box). Checked by eye: gem, hourglass, glove and exit sign.

- **Bonus levels (3)** are in the ROM: the original perspective floor, tilemaps, event maps, collision masks, timers and gem counts from `BONUSMAP.000-002`, `BONUSY.000` and `BONUS.000`, converted by `tools/build_sgdk_bonus.py`. The simulation (`src/bonus_core.c`) is a port of OpenJazz's bonus player and rules and is host-tested against the real data: movement, turning, jumping, gem and clock pickups, exit, timeout, and a win by collecting every required gem on all three levels.
- Reaching them: collect the bonus diamond in a stage and finish it (the bonus level of that world follows; winning gives an extra life), or use the stage-select cheat, whose list continues with BONUS 1-3.
- Rendering: the floor is sampled at 80x20 (4x4-pixel blocks) into a RAM tile buffer and DMA'd each frame; items (hourglass, gem, glove, exit sign) are the original graphics pre-scaled to four sprite sizes; the player is drawn as a plain ball with a shadow. Measured in the emulator at about 7 fps for the floor; game time stays correct because the simulation catches up in 16 ms steps.
- Not converted: the bonus music (`BONUS.PSM`) - the menu track plays instead; the original palette-rotation effects on the track edges.
- New tests: `tests/test_bonus.c`, `tests/test_bonus_link.c` (diamond -> bonus hand-off), emulator step 7 (bonus level renders and moves).

# Test report — 2026-11-01 (SGDK 2.x migration, stage-select cheat, full SFX set)

- Migrated the build to SGDK 2.x (`makefile.gen`, `src/rom_header.c`, header ROM end 0x17FFFF); `rom_sanity.py` passes.
- Added the stage-select cheat (pad code on the title screen). Verified headless: picker hidden before the code, shown after, and UP changes the stage (`tests/run_headless_emulator_test.sh`, step 6).
- Sound: added the original BOOM, OW, 1UP, YEAHOO, FIRE1, PHOTON clips from `SOUNDS.000`; hurt, kill, life, health, exit and per-weapon fire now trigger their own effects over the music.
- Font: the game's own `FONT2.0FN` is converted (`tools/build_sgdk_font.py`) and used for all text. Spring launches play the original spring clip.
- Sound: shield and bird pickups and the special weapon now play their original clips (INVULN11, BIRDY1, ROCKET2).
- Suites: host tests PASS, on-target self-test PASS, scripted playthrough PASS, music audio PASS, cheat PASS.

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

- **Vertical sucker tubes launch the player upward.** The direction is in the
  event data, not the geometry: movement 37/38 with multiB != 0 and multiA > 0
  is a vertical repel that lifts the player to targetY = gridY - multiA*3 px,
  as OpenJazz's REPELUP does. The importer reads multiA/multiB and classifies
  vertical launches (launch height in the event strength); the runtime drives
  the climb through the spring's target-height ascent, so the player rides the
  shaft and clears the top instead of sticking. Verified on the host: a 10-tile
  rise up a real Tubelectric shaft with no input.
- **Weapon behaviours corrected against OpenJazz.** The bullet table and the
  addAmmo() map give the authoritative weapons: modifier 15/18 -> fast straight,
  16/19 -> fast angled up, 17/20 -> gravity bouncer (behaviour 4), 39/40 ->
  fourth weapon. Five shot types with parameters read from the bullet defs.
- **Weapon-switch button.** C cycles through collected weapons (B jump, A fire).
  Jazz tracks owned weapons; ammo crates grant and switch to them; the switch is
  edge-triggered and skips unowned weapons. The HUD shows the current weapon.
- **Pickups** (carrots, gems, ammo, extra life, shields, score) resolve through
  the event->anim->sprite chain and their effects match OpenJazz's setEvent.
- **Music**: the four converted VGMs (menu, Diamondus, Tubelectric, Medivo) play
  per world via SGDK's XGM driver. ROM is 3.5MB, within the 4MB cart ceiling.


- **Vertical sucker tubes.** The Tubelectric tube events all carry a horizontal
  magnitude byte, but the same event is laid down vertical shafts and horizontal
  runs (211 of 332 tube cells are vertical). The push now follows the tube's
  geometry - up/down in a shaft, left/right in a run - so the player rides shafts
  upward instead of only sliding sideways.
- **Per-world music.** The Jazz Jackrabbit soundtrack, converted to Genesis VGM
  by the convert-s3m-vgm-jazzjackrabbit-md tool and compiled here by SGDK's
  xgmtool, plays through the XGM Z80 driver: the menu theme on the title screen,
  Diamondus on stages 0-2, Tubelectric on 3-4, and Medivo on 5-7. Sound effects
  moved to the XGM driver's PCM channels so they play over the music.
  ROM: 3.5 MB (under the 4 MB unbanked limit).


- **Sucker tubes (Tubelectric) push the player.** They are movement 37/38
  events with a signed magnitude byte, pushing at magnitude * F40 >> 6 (=
  magnitude * 171 in 8.8). A fast flow overrides input; a slow one can be
  walked against.
- **Full player animation reimport.** Replaced the fixed 8-sprite walk cycle
  with the real state machine: the 38 MT_P_ANIMS indices are read per level and
  every state (stand/walk/run/jump/fall/shoot/crouch/lookup/skid/hurt/spring)
  resolves through the same event->anim->sprite chain as the enemies. The ROM
  derives the state from shared physics and streams the per-state cell.
- **Medivo sword enemies face the right way.** JJ1 stores separate pre-drawn
  art per direction and picks by direction rather than hardware-flipping; the
  "right" set can itself be drawn facing left. Both facings are now extracted
  (deduplicated when identical) and the ROM selects by direction with no flip.
- **All player shot types.** Derived from the level's bullet-definition block:
  blaster, toaster, rising, and the bouncer (arcs under gravity, reflects off
  surfaces). Bullets gained vertical velocity, gravity and a behaviour byte;
  ammo pickups switch the weapon.


- **All eight shareware levels are now in the ROM** (was three). The shareware
  ships eight levels across three worlds; the `.018` file is a secret level that
  reuses the world 2 tile bank. Level select (UP/DOWN on the title) now covers
  1-8, and every stage boots with its own map, masks, palette, sprites and
  event table.
- **Per-world tile banks.** The 120KB block tileset belongs to the *world*, not
  the level, so it is emitted once per world (3 banks) instead of once per level
  (8 copies): that alone saves ~600KB of ROM. Maps, event grids, masks, sprite
  palettes, Jazz frames and event sprites stay per level, because each level
  carries its **own animation and event tables** (verified: their checksums
  differ even between levels of the same world).
- **A bug the extra levels exposed:** the event-table selector fell back to
  level 2's table for any stage past 2. Every event in the five new levels was
  therefore misclassified - they reported *zero* enemies. Each stage now has its
  own table, and a test asserts every level has an end sign, a valid spawn and
  collectables from its own table.

  ROM: 1MB (was 640KB).

- **Breakable walls restored, and the destructible set put on real evidence.**
  Reclassifying event 15 as a pickup silently made the walls solid again. It is
  in fact the breakable wall: it always appears as a horizontal PAIR of cells
  buried 100% inside solid rock (three pairs per level), which nothing
  collectable could be. Restored to destructible and pinned by a test.
  Conversely, 124/125 were never destructible: they blanket hundreds of cells
  of ordinary terrain and only 41-79% of them are even inside solid rock, so
  many sit in open air - they are region markers like 123, and treating them as
  breakable let the player shoot away arbitrary scenery. A test now asserts
  they are NOT shootable. The original hard-codes only two tile-level event
  IDs (JJ1Level::checkMaskUp / checkSpikes): 122 one-way (already handled) and
  126 spikes, which none of the three levels use.

  Destructible scenery is therefore exactly: **15 = breakable wall**,
  **17 = wooden signpost**.

- **The wooden signposts are destructible now.** They were still solid after
  destructible walls started working, because they are not what I first
  assumed. Identified from the data: event 17 sits on exactly one block in the
  Diamondus levels - block 41, the "RABBITS STINK" signpost graphic - and it
  was classified as *food*, so bullets passed straight through it. It is now
  destructible scenery. (The "KILL this rabbit" boards, blocks 43+53, genuinely
  carry no event in the original data and so are plain scenery.) The earlier
  guess that ID 15 was the sign was wrong - it sits on gem-studded rock - and
  has been reverted to a pickup. Verified on target: running and firing through
  level 0 leaves destroyCount=2 with the last cell at grid (40,8), the exact
  signpost coordinates.

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


## Latest run (palette cycling, event sounds, status panel, optimisations)

- Host tests (`tests/run_host_tests.sh` with `JJ1_DATA` set): all PASS.
- Headless emulator (`tests/run_headless_emulator_test.sh`): all PASS, including the on-target self-test marker (which now covers palette cycling), music, stage select, and the bonus level.
- Measured on stage 0 under the emulator: about 13 ms of work per frame (was over 20 ms); stage 2 about 8 ms. Bonus floor render about 48 ms (was 66 ms).
- Not tested: real hardware, and the sound effects by ear. Only the data and playback path were exercised.

After merging with the remote's scenes/saves/options/bonus-player work: host tests, the headless suite, and the menu, bonus-player and scene emulator tests all pass on the merged ROM. `run_presentation_emulator_test.sh` fails with "episode artwork/text missing" on the unmerged remote ROM as well, so it is not caused by the merge.

Bonus-level floor check: with the game stepped a fixed amount and the same held input, the merged ROM's whole frame (floor, hero, items, HUD) is pixel-identical to the remote's renderer after 25 and 70 steps. A hand-written near/far shortcut that changed the distant floor was removed.

## Optimisation pass

- Host tests: all PASS. A host trace (7 stages x 6000 frames of random input, hashing player, enemies, bullets, score and destroyed blocks every frame) is identical before and after the game-core changes.
- `tests/run_headless_emulator_test.sh`: all PASS, including two new checks:
  - step 10: `BONUS_SELFTEST` build, the assembly floor equals the C reference at all 1024 angles x 6 positions (6144 cases, SRAM marker `FLOR`,1);
  - step 11: `JAZZ_MAPCHECK` + `JAZZ_AUTOPILOT` build, plane A read back from VRAM after every camera update for 800 iterations on stages 0 and 3 (SRAM marker `MAPC`,1). Run by hand on all seven stages: no mismatches (the previous build had some on stages 0 and 2).
- Bonus floor and items: fixed-step builds match the original renderer pixel for pixel after 25 and 70 steps.
- Menu, bonus-player and scene emulator tests: PASS.
- `run_presentation_emulator_test.sh` now gets past the episode-ending checks but fails "loss white-out missing": it expects the bonus floor at frame 180 and full white at frame 234. The previous build fails the same assertion (its bonus also starts earlier than the test assumes); in the new build the white-out is complete at frame 200. The test's fixed frame numbers are out of date. `run_shot_emulator_test.sh` prints nothing on either build.
- Not tested on real hardware.

## Music re-conversion (four PCM voices)

- Host tests, `run_headless_emulator_test.sh` (all 11 steps, including title, in-game and bonus music energy) and `run_scene_emulator_test.sh`: PASS.
- Tubelectric (stage 3) and Medivo (stage 5) play in-game (RMS about 2,400-2,965).
- xgmtool reads every generated `.xgm` back with the expected sample count and length (for example, the menu has 23 renderings over 6,614 frames).
- Menu theme, captured from the emulator and compared with the previous ROM: overall level about the same (RMS 2,557 against 2,701), no clipping, more energy at 2-6 kHz (real hi-hats). Drum notes from the module with an audible onset in the first 14 s went from 31/83 to 47/83, and the onsets line up with the module's note times.
- Not checked by ear, and not on real hardware.

## Second optimisation pass

- Host tests PASS; host trace (7 stages x 6000 frames) identical to the previous commit.
- `run_headless_emulator_test.sh`: all steps PASS. `JAZZ_MAPCHECK` run by hand on all seven stages: PASS.
- 63 fixed-pace autopilot screenshots (9 per stage) pixel-identical to the previous build plus the vblank-scroll change.
- Missed frames over 1440 autopilot iterations, stages 0-6: before 87/47/26/52/39/2/0, after 11/3/0/5/4/0/0.
- On 8 frames of the stage 0 run (respawn pans) the vblank DMA queue exceeds about 7.2 KB, so on real hardware those transfers would run into the visible frame. Not tested on real hardware.

## All episodes (SSF2 packs) — what was checked

Cartridge: 31.0 MB, 10 episodes (1-9 and Holiday Hare), 66 stages, built from the CD data (`JAZZ/`).

- **Host suite** (`tests/run_host_tests.sh`, `JJ1_DATA` = shareware install): all pass. Episode 1's data, planet screens, ending pages and bonus maps now live in `packs/ep1` and regenerate byte-for-byte.
- **Headless emulator suite** (`tests/run_headless_emulator_test.sh`): all pass with the pack-based cartridge — on-target self-test, scripted playthrough, audio, cheat/bonus selection, menu/save/setup, bonus floor and map-streaming checks (episode 1).
- **Every stage of every episode** was started in Genesis Plus GX through the menu (episode chosen with A, stage with the cheat) and screenshotted after walking right: all 66 draw their own tiles, sprites, HUD and player.
- **Planet approaches** for episodes 1 and 2, **bonus levels** from episodes 2 and 5 (their own maps, gem counts and timers) and the **episode 3 ending** (stills with text, animation, scripted timing) were checked on screen.
- **Saves:** a game saved in episode 5 reloads into episode 5.
- **Guardians of episodes 2-9:** a host-side bot that walks up to the guardian and fires defeats it and finishes the stage in episodes 2, 3, 4, 6, 7 and 9; in 5 and 8 the guardian sits above the bot's line of fire (the bot does not jump), so those two were not shown to be beatable.

Not checked: real hardware or a flash cart (the SSF2 mapper was only exercised in Genesis Plus GX); the new episodes' music by ear; full playthroughs of episodes 2-10; per-stage frame rates for the new episodes. Known glitch: in Battleships 2 (episode 6, stage 6) a row of pickups near the start is drawn with the wrong picture.

## Lite cartridge (4 MB, all episodes) — what was checked

`JazzJackrabbitGenesisLite.bin`: exactly 4,194,304 bytes, no mapper; program 0.94 MB + lite packs 3.05 MB, 14 KB free.

- **Every stage of every episode** (66) started through the menu in Genesis Plus GX and screenshotted after walking right: all draw their tiles, sprites, HUD and player.
- **Text screens** (planet approach) checked; **bonus level** from episode 5's menu runs episode 1's map with its timer and gem count; **save in episode 5 / reload** returns to episode 5 (battery RAM shares $200000 with lite pack data; saving only touches it with interrupts off and the Z80 stopped).
- RAM: the level map and event grid (32 KB) live in RAM; 3.7 KB of heap stays free after the 4 KB DMA buffer (measured on target).
- The full cartridge's host and headless emulator suites were re-run after the shared code changes (tile-load macros, per-stage map pointer, menu cheat fix): all pass.

Not checked: the level-clear, bonus-clear and ending text screens (reached only by finishing a stage or episode); how the reduced tile sets look across every stage at full size (checked at thumbnail size, and one stage close up); lite frame rate (gathering pooled tiles costs some CPU on each block and sprite-frame upload; not measured); real hardware.

## Lite cartridge at 5 MB (music with drums)

`JazzJackrabbitGenesisLite.bin` is now 5,242,880 bytes: program 0.90 MB and level packs 2.47 MB below `$380000`, music 1.46 MB in banks 7-9 (32 tracks, each with its PCM drums cut down to fit 53 KB).

- **Right track from every music bank:** the audio of the menu (bank 7), the bonus level (bank 9) and a Holiday Hare stage (banks 7/9) was captured in Genesis Plus GX and its loudness envelope compared with the full cartridge's: correlation 0.78-0.82, i.e. the same arrangement playing at the same tempo.
- All 66 stages started and screenshotted again (none blank); save in episode 5 and reload gives an identical screen.

Not checked: the drums by ear (how much the shortened hits change the feel); the level-clear/ending screens; real hardware or flash carts with this mapper use.

### Fix: corrupted jump frames and pickups in the lite cartridge

Reported on Gens: Jazz's jump frames and the shootable weapon monitors were garbled. Two causes in `tools/build_lite.py`: (1) "every second frame" was applied to every multi-frame sprite, but items choose their frame by content (the ammo and 1-up monitors are frames of one set) and Jazz chooses frames by state and speed; now only enemies' walk/fly cycles are halved. (2) Jazz's tiles were capped at 75 % with nearest-colour substitution, which mixed tiles of different poses; Jazz now keeps every tile. To pay for it, tile indices are stored compactly (0.46 -> 0.33 MB) and the world tile share went from 45 % to 36 %.

Checked: a jump sequence in Diamondus 2 (6 frames) and the monitors next to it now match the full cartridge frame by frame; all 66 stages start (none blank).

## Original episode-selection screen

NEW GAME now opens the original episode screen (list, black-on-white highlight, red for episodes the cartridge lacks, the episode's picture beside it), converted from MENU.000 / FONTMN2.0FN by `tools/build_episode_menu.py`.

Checked in Genesis Plus GX on both cartridges: the screen draws all 11 rows and each picture with its own colours; UP/DOWN move the highlight and change the picture (wrapping both ways); START on an episode starts it (episode 2 -> Letni, episode 4 -> Turtemple); START on "Z bonus levels" plays a bonus level; B returns to the main menu. Host tests and the headless emulator suite (which build without the full game's data, so NEW GAME goes straight into episode 1) pass.

Not checked: the red/grey "not on this cartridge" look (every build I made carries all ten episodes); the picture change costs a few frames (the picture is unpacked first), so a very quick second tap of UP/DOWN can be missed.

## The 5 MB cartridge becomes the only one (full version removed)

Reported: the 31 MB full cartridge failed on most emulators tried; the 5 MB lite one worked. The full version was removed and the lite build is now what `make` produces as `JazzJackrabbitGenesis.bin`:

- `#ifdef JAZZ_LITE` branches made unconditional and the full-only code deleted (pack switching, the aux packs, the cutscene players, the picture-based instruction/order pages); `res_lite/` became `res/`; the startup/clear/planet/ending/menu-page assets and their converters and tests removed; `tools/build_lite.py` renamed `tools/build_cartridge.py`; `tools/build_packs.py` now only converts and links the episodes for it.
- The music conversion is cached in `packs/music` (gitignored) so `make clean` does not need the game files again.
- Tests updated to the cartridge's flow (no startup animation: the title appears at boot, so the scripted runs no longer press START to skip it; the instruction/order checks look for one text page each). Scene and presentation emulator tests that exercised the removed animations were deleted.

Checked: host tests pass; the headless emulator suite passes on the new build (self-test, scripted playthrough, music, cheat/bonus selection, menus/saves/setup, bonus floor, map streaming). The built all-episodes cartridge is the same program as the lite one delivered before, apart from the text of the instructions/order pages.
