#!/usr/bin/env bash
# Exercises the real title-code and bonus-player VDP rendering using a
# separately built Genesis Plus GX core and the project's libretro harness.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE="${GPGX_CORE:?Set GPGX_CORE to genesis_plus_gx_libretro.so}"
HARNESS="${GPGX_HARNESS:?Set GPGX_HARNESS to the compiled libretro_harness}"
ROM="${ROM:-$ROOT/JazzJackrabbitGenesis.bin}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
"$HARNESS" "$CORE" "$ROM" 1050 \
 '65-80:start,shot@100,130-135:up,140-145:up,150-155:down,160-165:down,170-175:left,180-185:right,190-195:left,200-205:right,210-215:b,220-225:a,shot@250,260-265:up,270-275:up,280-285:up,290-295:up,300-305:up,310-315:up,320-325:up,shot@345,360-390:start,430-460:start,shot@700,720-850:up,shot@800,shot@815'  "$TMP/frame_" > "$TMP/log" 2>&1
python3 - "$TMP" <<'PY'
import sys
from PIL import Image, ImageChops
from pathlib import Path
p = Path(sys.argv[1])
def img(n): return Image.open(p / f'frame_{n}.ppm').convert('RGB')
locked, unlocked, selected = img(100), img(250), img(345)
# The select text appears after entering the code, and changes when selecting.
rect = (45, 208, 275, 224)
assert ImageChops.difference(locked.crop(rect), unlocked.crop(rect)).getbbox(), 'code did not unlock stage select'
assert ImageChops.difference(unlocked.crop(rect), selected.crop(rect)).getbbox(), 'stage selection did not change'
bonus = img(700)
hero = bonus.crop((124, 120, 196, 208))
px = list(hero.getdata())
green = sum(g > 80 and g > r*1.5 and g > b*1.5 for r,g,b in px)
red = sum(r > 80 and r > g*1.5 and r > b*1.5 for r,g,b in px)
assert green > 400 and red > 100, f'original player colours missing: green={green}, red={red}'
a, b = img(800).crop((124,120,196,208)), img(815).crop((124,120,196,208))
assert ImageChops.difference(a,b).getbbox(), 'bonus player did not animate'
print(f'PASS: emulator unlock, bonus selection, original player colours and animation (green={green}, red={red})')
PY
