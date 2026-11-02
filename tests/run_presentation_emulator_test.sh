#!/usr/bin/env bash
# Exercise all four planet approaches, the real victory hand-off, and both
# bonus outcomes with deterministic Genesis Plus GX test ROMs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${GDK:?Set GDK}"
: "${GPGX_CORE:?Set GPGX_CORE}"
: "${GPGX_HARNESS:?Set GPGX_HARNESS}"
PREFIX="${PREFIX:-m68k-elf-}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
build() {
  PATH="$GDK/bin:$PATH" make -C "$ROOT" GDK="$GDK" PREFIX="$PREFIX" clean >"$TMP/build.log" 2>&1
  PATH="$GDK/bin:$PATH" make -C "$ROOT" GDK="$GDK" PREFIX="$PREFIX" release -j4 EXTRA_FLAGS="$1" >>"$TMP/build.log" 2>&1 || { tail -100 "$TMP/build.log"; exit 1; }
}
for stage in 0 2 4 6; do
  build "-DJAZZ_PLANET_TEST=$stage"
  "$GPGX_HARNESS" "$GPGX_CORE" "$ROOT/out/rom.bin" 430 'shot@75,shot@130,shot@210,shot@360' "$TMP/p${stage}_" >"$TMP/run.log" 2>&1
done
build '-DJAZZ_EPISODE_FLOW_TEST'
"$GPGX_HARNESS" "$GPGX_CORE" "$ROOT/out/rom.bin" 800 \
 '130-135:start,shot@200,300-305:start,shot@350,400-405:start,shot@455,shot@465,shot@600,650-655:start,shot@730,audio@250' "$TMP/e_" >"$TMP/end.log" 2>&1
for outcome in -1 1; do
  build "-DJAZZ_BONUS_END_TEST -DBONUS_FORCE_RESULT=$outcome"
  "$GPGX_HARNESS" "$GPGX_CORE" "$ROOT/out/rom.bin" 740 \
   'shot@180,shot@234,shot@300,shot@340,shot@700' "$TMP/b${outcome}_" >"$TMP/run.log" 2>&1
done
python3 - "$TMP" <<'PY'
from pathlib import Path
from PIL import Image, ImageChops
import re,sys
p=Path(sys.argv[1]); image=lambda key,n:Image.open(p/f'{key}_{n}.ppm').convert('RGB')
def count(im, f): return sum(1 for c in im.getdata() if f(*c))
def diff(a,b):return sum(1 for c in ImageChops.difference(a,b).getdata() if c!=(0,0,0))
purple=lambda r,g,b:r>40 and b>80 and g<50
bright=lambda r,g,b:r>180 and g>180 and b>180
red=lambda r,g,b:r>110 and g<75 and b<75
green=lambda r,g,b:g>80 and g>r*1.5 and g>b*1.4
planets=[]
for stage in (0,2,4,6):
    small,large,held,title=[image('p'+str(stage),n) for n in (75,130,210,360)]
    assert diff(small,large)>500, ('planet did not grow',stage)
    assert diff(large,held)>100, ('planet scale frozen early',stage)
    assert count(title,purple)>10000 and count(title,green)>3000, ('same planet replayed instead of returning',stage)
    planets.append(held)
for i in range(4):
    for j in range(i):assert diff(planets[i],planets[j])>2000, ('duplicate planet assets',i,j)
page1,page2,animation,animation2,page4,title=[image('e',n) for n in (200,350,455,465,600,730)]
assert count(page1,red)>10000 and diff(page1,page2)>1000, 'episode artwork/text missing'
assert diff(animation,animation2)>40, 'episode animation did not advance'
assert count(page4,red)>10000 and diff(page2,page4)>500, 'final text page missing'
assert count(title,purple)>10000 and count(title,green)>3000, 'ending did not return to title'
m=re.search(r'AUDIO 250 rms=([0-9.]+)',(p/'end.log').read_text());assert m and float(m.group(1))>100, 'episode ending music silent'
loss_early,loss_white,loss_title=[image('b-1',n) for n in (180,234,300)]
win_white,win_a,win_b,win_title=[image('b1',n) for n in (234,300,340,700)]
assert count(loss_early,green)>100 and count(loss_white,bright)>70000, 'loss white-out missing'
assert count(loss_title,purple)>10000, 'loss did not return to title'
assert count(win_white,bright)>70000 and count(win_a,bright)>20000 and diff(win_a,win_b)>500, 'win white-out/scene missing'
assert count(win_title,purple)>10000, 'win did not return to title'
print('PASS: four original planet approaches grow and do not repeat on same planet')
print('PASS: real episode victory -> original text/animation/music -> title')
print('PASS: bonus loss white-out and bonus win white-out -> win scene')
PY
