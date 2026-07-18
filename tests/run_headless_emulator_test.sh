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

# Title: dark background with white text, no gameplay sprites yet.
title_dark  = count(title, lambda r, g, b: r < 24 and g < 24 and b < 80)
title_text  = count(title, lambda r, g, b: r > 200 and g > 200 and b > 200)
title_hero  = count(title, lambda r, g, b: g > 100 and r < 100 and b < 100)
assert title_dark > title.width * title.height * 0.5, f"title background missing ({title_dark})"
assert title_text > 800, f"title text missing ({title_text})"
assert title_hero == 0, "gameplay sprite on the title screen"

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
late_hud     = count(late, lambda r, g, b: r > 200 and g > 200 and b > 200)
assert scrolled > early.width * early.height * 0.2, f"world did not scroll ({scrolled})"
assert late_terrain > 5000, f"terrain lost after scrolling ({late_terrain})"
assert late_hud > 300, f"HUD lost after scrolling ({late_hud})"

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
