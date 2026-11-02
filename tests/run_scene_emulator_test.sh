#!/usr/bin/env bash
# Checks actual VDP output from the release boot, both animations, and the
# gameplay->level-clear->next-stage path with two dedicated test builds.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${GDK:?Set GDK to SGDK 2.11 or compatible}"
: "${GPGX_CORE:?Set GPGX_CORE to genesis_plus_gx_libretro.so}"
: "${GPGX_HARNESS:?Set GPGX_HARNESS to the compiled libretro harness}"
PREFIX="${PREFIX:-m68k-elf-}"
ROM="${ROM:-$ROOT/JazzJackrabbitGenesis.bin}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
"$GPGX_HARNESS" "$GPGX_CORE" "$ROM" 850 \
 'shot@60,shot@120,shot@250,shot@450,shot@750,audio@120' "$TMP/boot_" >"$TMP/boot.log" 2>&1
build_test() {
  PATH="$GDK/bin:$PATH" make -C "$ROOT" GDK="$GDK" PREFIX="$PREFIX" clean >/dev/null 2>&1
  PATH="$GDK/bin:$PATH" make -C "$ROOT" GDK="$GDK" PREFIX="$PREFIX" release -j4 EXTRA_FLAGS="$1" >"$TMP/build.log" 2>&1 || { cat "$TMP/build.log"; exit 1; }
}
build_test -DJAZZ_SCENE_TEST
"$GPGX_HARNESS" "$GPGX_CORE" "$ROOT/out/rom.bin" 1100 \
 'shot@60,shot@100,shot@420,shot@500,shot@1000,audio@200' "$TMP/scene_" >"$TMP/scene.log" 2>&1
build_test -DJAZZ_SCENE_FLOW_TEST
"$GPGX_HARNESS" "$GPGX_CORE" "$ROOT/out/rom.bin" 920 \
 '130-135:start,650-655:start,700-900:right,shot@800' "$TMP/flow_" >"$TMP/flow.log" 2>&1
python3 - "$TMP" <<'PY'
from PIL import Image, ImageChops
from pathlib import Path
import sys, re
p=Path(sys.argv[1])
def image(prefix,n): return Image.open(p / f'{prefix}_{n}.ppm').convert('RGB')
def count(im, predicate): return sum(1 for rgb in im.getdata() if predicate(*rgb))
def changed(a,b): return sum(1 for c in ImageChops.difference(a,b).getdata() if c!=(0,0,0))
green=lambda r,g,b:g>80 and g>r*1.5 and g>b*1.4
bright=lambda r,g,b:r>190 and g>190 and b>190
purple=lambda r,g,b:r>40 and b>80 and g<50
intro1,intro2=image('boot',60),image('boot',120)
credit1,credit2,title=image('boot',250),image('boot',450),image('boot',750)
assert changed(intro1,intro2)>500, 'startup animation did not advance'
boot_audio=(p/'boot.log').read_text();bm=re.search(r'AUDIO 120 rms=([0-9.]+) peak=(\d+)',boot_audio)
assert bm and float(bm.group(1))>100 and int(bm.group(2))>1000, 'startup sound effects silent'
assert count(credit1,bright)>150 and count(credit2,bright)>150 and changed(credit1,credit2)>200, 'startup credits missing'
assert count(title,green)>3000 and count(title,purple)>10000, 'original title missing'
level1,level2=image('scene',60),image('scene',100)
bonus1,bonus2=image('scene',420),image('scene',500)
assert count(level1,green)>1000 and count(level1,bright)>500, 'level clear art missing'
assert changed(level1,level2)>100, 'level clear animation did not advance'
assert count(bonus1,bright)>20000 and count(bonus1,green)>500, 'bonus clear art missing'
assert changed(bonus1,bonus2)>1000, 'bonus clear animation did not advance'
audio=(p/'scene.log').read_text();m=re.search(r'AUDIO 200 rms=([0-9.]+) peak=(\d+)',audio)
assert m and float(m.group(1))>100 and int(m.group(2))>1000, 'level-clear music silent'
flow=image('flow',800)
assert count(flow,lambda r,g,b:r>160 and g>130 and b<130)>5000 and count(flow.crop((0,0,320,16)),bright)>100, 'Tubelectric gameplay not restored after level clear'
print('PASS: startup, credit pages, title, clear scenes, music and gameplay return in Genesis Plus GX')
PY
