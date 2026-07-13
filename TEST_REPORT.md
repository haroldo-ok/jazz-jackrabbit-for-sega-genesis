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
  the original engine.
- `tools/build_sgdk_jj1_level_data.py --eventset` now also emits ground-truth
  `jj1_levelN_eventset.inc` tables from an installed shareware copy, which
  override the provisional tables automatically at compile time.
- All platform-neutral JJ1 logic moved to `src/jj1_runtime.c`, so the host
  tests exercise the same collision/event code and converted data as the ROM.

## Results

| Suite | Result | Coverage |
|---|---|---|
| `tests/run_host_tests.sh` | PASS | Real-map spawn settling on original masks for all 3 levels, walk/run speed tiers, 84 px variable jump with release cut-off, immediate terminal fall, item collection + taken bitmap, enemy activation/kill/no-respawn, contact damage + invulnerability, spring launch height, end-sign exits, full 3-level episode completion, pause. |
| `tests/run_headless_emulator_test.sh` | PASS | Headless Genesis Plus GX libretro run: on-target 68000 self-test reported through an SRAM marker, plus a scripted title -> START -> run/jump playthrough asserting title text, Jazz sprite, sky, converted terrain, advancing animation, camera scrolling, and a live HUD. |

Latest headless emulator result:

```text
PASS: headless emulator P2P: hero=3547, sky=8533, terrain=908, anim=30695, scrolled=45959, deep_terrain=20094
```

## Known remaining gaps

- The provisional event tables classify IDs from placement statistics; exact
  per-ID behaviour parameters (enemy movement paths, item point values, warp
  targets) need `--eventset` regeneration against an installed shareware copy.
- Enemy/item sprites are still placeholder tiles; original SPRITES.000
  animation mapping, music, bosses, warps, water, and save data remain open.
