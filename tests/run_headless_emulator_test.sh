#!/usr/bin/env bash
# Headless emulator point-to-point test.
#
# It runs the released ROM under the Genesis Plus GX libretro core with no
# display, injecting a scripted START + Right playthrough of the level intro,
# and asserts on framebuffer pixels.  It also runs a JAZZ_AUTOTEST build whose
# in-ROM assertions execute on the emulated 68000 and report through an SRAM
# marker.  No X server, GPU, or audio device is required.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${GDK:?Set GDK to your SGDK directory (e.g. /opt/sgdk).}"
PREFIX="${PREFIX:-m68k-elf-}"
TMP="$ROOT/tests/.tmp"
ART="$ROOT/tests/artifacts"
CORE="${GPGX_CORE:-$TMP/genesis_plus_gx_libretro.so}"
mkdir -p "$TMP" "$ART"

# 1. Build (or locate) the emulator core.
if [[ ! -f "$CORE" ]]; then
  if [[ ! -d "$TMP/gpgx" ]]; then
    git clone --depth 1 https://github.com/ekeeke/Genesis-Plus-GX.git "$TMP/gpgx"
  fi
  make -C "$TMP/gpgx" -f Makefile.libretro platform=unix -j"$(nproc)"
  cp "$TMP/gpgx/genesis_plus_gx_libretro.so" "$CORE"
fi

# 2. Build the harness.
cc -O2 -o "$TMP/libretro_harness" "$ROOT/tests/libretro_harness.c" \
   -I"$TMP/gpgx/libretro/libretro-common/include" -ldl -lm

# 3. Build the release ROM and the on-target self-test ROM.
PATH="$GDK/bin:$PATH" make -C "$ROOT" clean GDK="$GDK" PREFIX="$PREFIX" >/dev/null
PATH="$GDK/bin:$PATH" make -C "$ROOT" release GDK="$GDK" PREFIX="$PREFIX"
cp "$ROOT/out/rom.bin" "$ROOT/JazzJackrabbitGenesis.bin"
cp "$ROOT/out/rom.bin" "$TMP/release.bin"

PATH="$GDK/bin:$PATH" make -C "$ROOT" clean GDK="$GDK" PREFIX="$PREFIX" >/dev/null
PATH="$GDK/bin:$PATH" make -C "$ROOT" release GDK="$GDK" PREFIX="$PREFIX" \
  EXTRA_FLAGS=-DJAZZ_AUTOTEST
cp "$ROOT/out/rom.bin" "$TMP/autotest.bin"
PATH="$GDK/bin:$PATH" make -C "$ROOT" clean GDK="$GDK" PREFIX="$PREFIX" >/dev/null

# 4. On-target self test: the 68000 runs jazz_game_init/jazz_step assertions
#    against the converted data and reports 'J','A','Z','P',1 through SRAM.
SRAM_LINE="$("$TMP/libretro_harness" "$CORE" "$TMP/autotest.bin" 300 \
  "shot@290" "$ART/headless_autotest_" | tail -n 1)"
echo "$SRAM_LINE"
# SGDK SRAM lives on odd bytes; Genesis Plus GX interleaves it with 0xFF.
case "$SRAM_LINE" in
  *"4A 41 5A 50 01"*|*"4A FF 41 FF 5A FF 50 FF 01"*)
    echo "PASS: on-target self-test marker" ;;
  *) echo "FAIL: on-target self-test marker missing ($SRAM_LINE)"; exit 1 ;;
esac

# 5. Scripted playthrough of the release ROM: title -> START -> run right.
"$TMP/libretro_harness" "$CORE" "$TMP/release.bin" 900 \
  "120-150:start,190-220:start,290-295:start,360-850:right,570-574:b,shot@110,shot@340,shot@356,shot@860" \
  "$ART/headless_" > "$TMP/headless_release.out"
cat "$TMP/headless_release.out"

python3 - "$ART" <<'PY'
import sys
from PIL import Image, ImageChops

art = sys.argv[1]
title = Image.open(f"{art}/headless_110.ppm").convert("RGB")
early = Image.open(f"{art}/headless_340.ppm").convert("RGB")
anim  = Image.open(f"{art}/headless_356.ppm").convert("RGB")
late  = Image.open(f"{art}/headless_860.ppm").convert("RGB")

def count(im, predicate):
    return sum(1 for r, g, b in im.getdata() if predicate(r, g, b))

# Title: original green logo, purple background and copyright artwork.
title_green = count(title, lambda r, g, b: g > 100 and g > r * 1.5 and g > b * 1.5)
title_purple = count(title, lambda r, g, b: r > 40 and b > 80 and g < 50)
assert title_green > 3000, f"converted title logo missing ({title_green})"
assert title_purple > 10000, f"converted title background missing ({title_purple})"

# Gameplay after START: Jazz's green sprite, the sky gradient, and converted
# purple Diamondus terrain must all be present.
hero    = count(early, lambda r, g, b: g > 100 and r < 100 and b < 100)
sky     = count(early, lambda r, g, b: b > 120 and r < 90 and g > 40)
terrain = count(early, lambda r, g, b: r > 24 and b > 100 and g < 60)
assert hero > 500, f"missing Jazz sprite pixels ({hero})"
assert sky > 2000, f"missing sky gradient pixels ({sky})"
assert terrain > 100, f"missing converted terrain pixels ({terrain})"

# The resident walk animation / simulation advances between nearby frames.
moved = sum(1 for p in ImageChops.difference(early, anim).getdata() if p != (0, 0, 0))
assert moved > 40, f"animation/simulation did not advance ({moved})"

# After ~8 seconds of running right the camera has scrolled deep into the
# level: heavy terrain coverage and a substantially different frame, with a
# HUD still drawn (no crash/exception screen).
scrolled = sum(1 for p in ImageChops.difference(early, late).getdata() if p != (0, 0, 0))
late_terrain = count(late, lambda r, g, b: r > 24 and b > 100 and g < 60)
late_hud     = sum(1 for r, g, b in late.crop((0, 0, 320, 16)).getdata() if r > 200 and g > 200 and b > 200)  # HUD text, converted font
assert scrolled > early.width * early.height * 0.2, f"world did not scroll ({scrolled})"
assert late_terrain > 5000, f"terrain lost after scrolling ({late_terrain})"
assert late_hud > 100, f"HUD lost after scrolling ({late_hud})"

print(f"PASS: headless emulator P2P: hero={hero}, sky={sky}, terrain={terrain}, "
      f"anim={moved}, scrolled={scrolled}, deep_terrain={late_terrain}")
PY

# 6. Music: the XGM driver must actually be producing sound, on the title
#    screen and in-game.  The harness only reports signal energy (RMS/peak),
#    never audio, which is enough to catch a silent-music regression such as a
#    bad VGM conversion, a missing resource, or a stalled Z80 driver.
AUDIO_OUT="$("$TMP/libretro_harness" "$CORE" "$TMP/release.bin" 700 \
  "audio@110,120-150:start,190-220:start,290-295:start,audio@650" "$ART/headless_audio_")"
echo "$AUDIO_OUT" | grep '^AUDIO' || true

python3 - <<PY
import re, sys
out = """$AUDIO_OUT"""
rows = [(int(m.group(1)), float(m.group(2)), int(m.group(3)))
        for m in re.finditer(r'^AUDIO (\d+) rms=([\d.]+) peak=(\d+)', out, re.M)]
assert len(rows) >= 2, f"expected two audio windows, got {rows}"
title_rms, title_peak = rows[0][1], rows[0][2]
play_rms, play_peak = rows[1][1], rows[1][2]
assert title_rms > 100 and title_peak > 1000, \
    f"title music silent (rms={title_rms}, peak={title_peak})"
assert play_rms > 100 and play_peak > 1000, \
    f"in-game music silent (rms={play_rms}, peak={play_peak})"
print(f"PASS: music plays (title rms={title_rms:.0f} peak={title_peak}, "
      f"in-game rms={play_rms:.0f} peak={play_peak})")
PY

# 6. Title cheat, bonus selection, original player art and animation.
GPGX_CORE="$CORE" GPGX_HARNESS="$TMP/libretro_harness" ROM="$TMP/release.bin" \
  bash "$ROOT/tests/run_bonus_player_emulator_test.sh"
GPGX_CORE="$CORE" GPGX_HARNESS="$TMP/libretro_harness" ROM="$TMP/release.bin" \
  bash "$ROOT/tests/run_menu_emulator_test.sh"

# Shared pad script: skip intro, unlock selection, select BONUS 2, start.
BSCRIPT="130-135:up,140-145:up,150-155:down,160-165:down,170-175:left,180-185:right,190-195:left,200-205:right,210-215:b,220-225:a"
for i in 0 1 2 3 4 5 6 7; do a=$((260 + i * 10)); BSCRIPT="$BSCRIPT,$a-$((a + 5)):up"; done
BSCRIPT="$BSCRIPT,360-390:start,430-460:start,720-1200:up,720-1200:a"

# 8. Bonus items must be visible.  A test build starts BONUS 2 one tile in front
#    of a gem (BONUS_DEBUG_X/Y); the gem's blue sprite has to show against the
#    lighter sky gradient, with no black box around it.
PATH="$GDK/bin:$PATH" make -C "$ROOT" clean GDK="$GDK" PREFIX="$PREFIX" >/dev/null
PATH="$GDK/bin:$PATH" make -C "$ROOT" release GDK="$GDK" PREFIX="$PREFIX" \
  EXTRA_FLAGS="-DBONUS_DEBUG_X=21 -DBONUS_DEBUG_Y=2" >/dev/null
cp "$ROOT/out/rom.bin" "$TMP/bonusdbg.bin"
PATH="$GDK/bin:$PATH" make -C "$ROOT" clean GDK="$GDK" PREFIX="$PREFIX" >/dev/null
IS="130-135:up,140-145:up,150-155:down,160-165:down,170-175:left,180-185:right,190-195:left,200-205:right,210-215:b,220-225:a"
for i in 0 1 2 3 4 5 6; do a=$((260 + i * 10)); IS="$IS,$a-$((a + 5)):up"; done
"$TMP/libretro_harness" "$CORE" "$TMP/bonusdbg.bin" 850 "$IS,360-390:start,430-460:start,shot@700" "$ART/items_" > "$TMP/items.out"
python3 - "$ART" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1] + "/items_700.ppm").convert("RGB")
box = im.crop((120, 96, 200, 150))
gem = sum(1 for r, g, b in box.getdata() if r < 20 and g < 20 and b > 150)
black = sum(1 for r, g, b in im.crop((140, 112, 180, 128)).getdata() if r < 8 and g < 8 and b < 8)
assert gem > 150, f"bonus gem not visible ({gem})"
assert black < 40, f"bonus gem has an opaque black box ({black})"
print(f"PASS: bonus gem is visible against the sky (blue pixels={gem}, black={black})")
PY

# 9. The bonus level has its own music: audio must be playing inside it.
"$TMP/libretro_harness" "$CORE" "$TMP/release.bin" 1300 "$BSCRIPT,audio@1000" "$ART/bonusaudio_" > "$TMP/bonus_audio.out" 2>&1 || true
python3 - "$TMP/bonus_audio.out" <<'PY'
import re, sys
txt = open(sys.argv[1]).read()
m = re.search(r"AUDIO 1000 rms=([0-9.]+) peak=(\d+)", txt)
assert m, "no audio statistics for the bonus level"
rms, peak = float(m.group(1)), int(m.group(2))
assert rms > 300 and peak > 3000, f"bonus music not playing (rms={rms}, peak={peak})"
print(f"PASS: bonus music plays (rms={rms:.0f}, peak={peak})")
PY

# 10. Bonus floor renderer: the assembly rows (src/floor_asm.s) must equal the
#     C reference word for word at all 1024 angles and six positions.  The
#     BONUS_SELFTEST build checks this on the 68000 when BONUS 1 starts and
#     reports 'F','L','O','R',1 through SRAM.
PATH="$GDK/bin:$PATH" make -C "$ROOT" release GDK="$GDK" PREFIX="$PREFIX" \
  EXTRA_FLAGS="-DBONUS_SELFTEST" >/dev/null
cp "$ROOT/out/rom.bin" "$TMP/floorself.bin"
PATH="$GDK/bin:$PATH" make -C "$ROOT" clean GDK="$GDK" PREFIX="$PREFIX" >/dev/null
SRAM_LINE="$("$TMP/libretro_harness" "$CORE" "$TMP/floorself.bin" 60010 \
  "$IS,360-390:start,430-460:start,shot@60000" "$ART/floorself_" | tail -n 1)"
case "$SRAM_LINE" in
  *"46 4C 4F 52 01"*|*"46 FF 4C FF 4F FF 52 FF 01"*)
    echo "PASS: bonus floor assembly matches the C reference at every angle" ;;
  *) echo "FAIL: bonus floor self-test ($SRAM_LINE)"; exit 1 ;;
esac

# 11. Map streaming: an autopilot build plays 800 iterations and, after every
#     camera update, reads plane A back from VRAM to check that each visible
#     cell shows the cached copy of the right block.  Stages 0 (slopes) and 3
#     (the busiest block mix) are checked.
PATH="$GDK/bin:$PATH" make -C "$ROOT" release GDK="$GDK" PREFIX="$PREFIX" \
  EXTRA_FLAGS="-DJAZZ_MAPCHECK -DJAZZ_AUTOPILOT" >/dev/null
cp "$ROOT/out/rom.bin" "$TMP/mapcheck.bin"
PATH="$GDK/bin:$PATH" make -C "$ROOT" clean GDK="$GDK" PREFIX="$PREFIX" >/dev/null
for st in 0 3; do
  MS="130-135:up,140-145:up,150-155:down,160-165:down,170-175:left,180-185:right,190-195:left,200-205:right,210-215:b,220-225:a"
  for ((i = 0; i < st; i++)); do a=$((260 + i * 10)); MS="$MS,$a-$((a + 5)):up"; done
  SRAM_LINE="$("$TMP/libretro_harness" "$CORE" "$TMP/mapcheck.bin" 6000 \
    "$MS,360-390:start,430-460:start,shot@5990" "$ART/mapcheck${st}_" | tail -n 1)"
  case "$SRAM_LINE" in
    *"4D 41 50 43 01"*|*"4D FF 41 FF 50 FF 43 FF 01"*)
      echo "PASS: stage $st map streaming shows the right blocks" ;;
    *) echo "FAIL: stage $st map check ($SRAM_LINE)"; exit 1 ;;
  esac
done
