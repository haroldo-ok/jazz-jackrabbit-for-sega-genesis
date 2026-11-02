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
"$TMP/libretro_harness" "$CORE" "$TMP/release.bin" 720 \
  "120-160:start,220-700:right,420-424:b,shot@110,shot@200,shot@216,shot@710" \
  "$ART/headless_" > "$TMP/headless_release.out"
cat "$TMP/headless_release.out"

python3 - "$ART" <<'PY'
import sys
from PIL import Image, ImageChops

art = sys.argv[1]
title = Image.open(f"{art}/headless_110.ppm").convert("RGB")
early = Image.open(f"{art}/headless_200.ppm").convert("RGB")
anim  = Image.open(f"{art}/headless_216.ppm").convert("RGB")
late  = Image.open(f"{art}/headless_710.ppm").convert("RGB")

def count(im, predicate):
    return sum(1 for r, g, b in im.getdata() if predicate(r, g, b))

# Title: dark backdrop and white text in the converted game font.
title_dark  = count(title, lambda r, g, b: r < 24 and g < 24 and b < 80)
title_text  = count(title, lambda r, g, b: r > 200 and g > 200 and b > 200)
assert title_dark > title.width * title.height * 0.5, f"title background missing ({title_dark})"
assert title_text > 300, f"title text missing ({title_text})"

# Gameplay after START: Jazz's green sprite, the sky gradient, and converted
# purple Diamondus terrain must all be present.
hero    = count(early, lambda r, g, b: g > 100 and r < 100 and b < 100)
sky     = count(early, lambda r, g, b: b > 120 and r < 90 and g > 40)
terrain = count(early, lambda r, g, b: r > 24 and b > 100 and g < 60)
assert hero > 500, f"missing Jazz sprite pixels ({hero})"
assert sky > 2000, f"missing sky gradient pixels ({sky})"
assert terrain > 300, f"missing converted terrain pixels ({terrain})"

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
AUDIO_OUT="$("$TMP/libretro_harness" "$CORE" "$TMP/release.bin" 600 \
  "audio@240,120-160:start,audio@560" "$ART/headless_audio_")"
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

# 6. Stage select is always on: the picker is on the title screen from boot
#    (the old pad code is harmless) and UP must advance the stage number.
"$TMP/libretro_harness" "$CORE" "$TMP/release.bin" 260 \
  "130-132:up,136-138:up,142-144:down,148-150:down,154-156:left,160-162:right,166-168:left,172-174:right,178-180:b,184-186:a,210-212:up,216-218:up,shot@125,shot@200,shot@230" \
  "$ART/cheat_" > "$TMP/cheat.out"
python3 - "$ART" <<'PY'
import sys
art = sys.argv[1]
def lit(name, y0, y1):
    d = open(f"{art}/cheat_{name}.ppm", "rb").read().split(b"\n", 3)
    w, h = map(int, d[1].split()); px = d[3]
    n = 0
    for y in range(y0, y1):
        for x in range(w):
            i = (y * w + x) * 3
            if px[i] + px[i+1] + px[i+2] > 600: n += 1
    return n
def region(name):  # cheat line is text row 19 -> y 152..160
    return lit(name, 152, 160)
before, after, later = region("125"), region("200"), region("230")
assert before > 20, f"stage picker missing at boot ({before})"
assert after > 20, f"stage picker missing after cheat ({after})"
d1 = open(f"{art}/cheat_200.ppm","rb").read(); d2 = open(f"{art}/cheat_230.ppm","rb").read()
assert d1 != d2, "UP did not change the selected stage"
print(f"PASS: stage select is always on (at boot={before}, later={after}, changes with UP)")
PY

# 7. Bonus level via the stage-select cheat: scroll the picker past the stages
#    to BONUS 2, start it, drive forward, and check the perspective floor, the
#    sky/horizon, the HUD, the player marker and that the view keeps changing.
BSCRIPT="130-132:up,136-138:up,142-144:down,148-150:down,154-156:left,160-162:right,166-168:left,172-174:right,178-180:b,184-186:a"
for i in 0 1 2 3 4 5 6 7; do a=$((200 + i * 8)); BSCRIPT="$BSCRIPT,$a-$((a + 2)):up"; done
BSCRIPT="$BSCRIPT,300-304:start,360-1000:up,360-1000:a,shot@420,shot@900"
"$TMP/libretro_harness" "$CORE" "$TMP/release.bin" 1000 "$BSCRIPT" "$ART/bonus_" > "$TMP/bonus.out"
python3 - "$ART" <<'PY'
import sys
from PIL import Image, ImageChops
art = sys.argv[1]
a = Image.open(f"{art}/bonus_420.ppm").convert("RGB")
b = Image.open(f"{art}/bonus_900.ppm").convert("RGB")
def count(im, box, pred):
    return sum(1 for p in im.crop(box).getdata() if pred(*p))
sky   = count(a, (0, 8, 320, 110), lambda r, g, bl: bl > 150 and r < 40 and g < 40)
floor = count(a, (0, 150, 320, 224), lambda r, g, bl: not (r < 24 and g < 24 and bl < 24))
hud   = count(a, (0, 0, 320, 8), lambda r, g, bl: r > 200 and g > 200 and bl > 200)
ball  = count(a, (144, 176, 176, 208), lambda r, g, bl: r > 140 and g < 60 and bl < 60)
moved = sum(1 for p in ImageChops.difference(a.crop((0, 150, 320, 224)), b.crop((0, 150, 320, 224))).getdata() if p != (0, 0, 0))
assert sky > 10000, f"bonus sky missing ({sky})"
assert floor > 15000, f"bonus floor missing ({floor})"
assert hud > 60, f"bonus HUD missing ({hud})"
assert ball > 20, f"player marker missing ({ball})"
assert moved > 3000, f"bonus view did not change while moving ({moved})"
print(f"PASS: bonus level renders (sky={sky}, floor={floor}, hud={hud}, marker={ball}, moved={moved})")
PY

# 8. Bonus items must be visible.  A test build starts BONUS 2 one tile in front
#    of a gem (BONUS_DEBUG_X/Y); the gem's blue sprite has to show against the
#    lighter sky gradient, with no black box around it.
PATH="$GDK/bin:$PATH" make -C "$ROOT" clean GDK="$GDK" PREFIX="$PREFIX" >/dev/null
PATH="$GDK/bin:$PATH" make -C "$ROOT" release GDK="$GDK" PREFIX="$PREFIX" \
  EXTRA_FLAGS="-DBONUS_DEBUG_X=21 -DBONUS_DEBUG_Y=2" >/dev/null
cp "$ROOT/out/rom.bin" "$TMP/bonusdbg.bin"
PATH="$GDK/bin:$PATH" make -C "$ROOT" clean GDK="$GDK" PREFIX="$PREFIX" >/dev/null
IS="130-132:up,136-138:up,142-144:down,148-150:down,154-156:left,160-162:right,166-168:left,172-174:right,178-180:b,184-186:a"
for i in 0 1 2 3 4 5 6; do a=$((200 + i * 8)); IS="$IS,$a-$((a + 2)):up"; done
"$TMP/libretro_harness" "$CORE" "$TMP/bonusdbg.bin" 420 "$IS,300-304:start,shot@400" "$ART/items_" > "$TMP/items.out"
python3 - "$ART" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1] + "/items_400.ppm").convert("RGB")
box = im.crop((120, 96, 200, 150))
gem = sum(1 for r, g, b in box.getdata() if r < 20 and g < 20 and b > 150)
black = sum(1 for r, g, b in im.crop((140, 112, 180, 128)).getdata() if r < 8 and g < 8 and b < 8)
assert gem > 150, f"bonus gem not visible ({gem})"
assert black < 40, f"bonus gem has an opaque black box ({black})"
print(f"PASS: bonus gem is visible against the sky (blue pixels={gem}, black={black})")
PY

# 9. The bonus level has its own music: audio must be playing inside it.
"$TMP/libretro_harness" "$CORE" "$TMP/release.bin" 1000 "$BSCRIPT,audio@800" "$ART/bonusaudio_" > "$TMP/bonus_audio.out" 2>&1 || true
python3 - "$TMP/bonus_audio.out" <<'PY'
import re, sys
txt = open(sys.argv[1]).read()
m = re.search(r"AUDIO 800 rms=([0-9.]+) peak=(\d+)", txt)
assert m, "no audio statistics for the bonus level"
rms, peak = float(m.group(1)), int(m.group(2))
assert rms > 300 and peak > 3000, f"bonus music not playing (rms={rms}, peak={peak})"
print(f"PASS: bonus music plays (rms={rms:.0f}, peak={peak})")
PY
