# Test report — 2026-07-12

## Release artifact

- `JazzJackrabbitGenesis.bin`
- Size: 524,288 bytes (Genesis-aligned ROM)
- SHA-256: `6c0caf5e3fe02731866502c898660abe238059c8fb2332ecef14a6f2335328d6`
- Toolchain validation: SGDK 2.11, native `m68k-linux-gnu-` cross compiler, BlastEm.

## Converted runtime content

- Eight original JJ1 Jazz frames in Genesis VDP column-major 4×4-tile order.
- Full `LEVEL0–2.000` 256×64 block grids, original masks, event IDs, starts,
  240-block tilesets, one-way event-122 platforms, and event 21/22/23 springs.
- An 88-block / 1,408-tile ring cache updates incoming block rows/columns only.
- Directional 4×4-pixel mask probes, rising/descending ramp handling, 4px
  ground settlement, damped 2-axis camera, and separate sky gradient Plane B.
- Converted original `MACHGUN1`, `JUMPA11`, and `YUM1` PCM clips.

## Results

| Suite | Result | Coverage |
|---|---|---|
| `tests/run_host_tests.sh` | PASS | Stage layout, run, jump/land, pause, bullet/enemy collision, pickup, damage immunity, exits, all-stage completion. |
| `tests/run_emulator_test.sh` | PASS | Builds the ROM, injects input under BlastEm, exercises streamed map foreground rendering, mask collision, animation and camera path. |

Latest BlastEm integration result:

```text
PASS: BlastEm P2P screenshot validated: terrain=13270, wood=397, hero=41213, gem=0, animation=114875
```
