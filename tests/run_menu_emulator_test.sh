#!/usr/bin/env bash
# Exercise real menus and persistent SRAM across independent emulator sessions.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${GPGX_CORE:?Set GPGX_CORE to genesis_plus_gx_libretro.so}"
: "${GPGX_HARNESS:?Set GPGX_HARNESS to the compiled libretro harness}"
ROM="${ROM:-$ROOT/JazzJackrabbitGenesis.bin}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
run() { "$GPGX_HARNESS" "$GPGX_CORE" "$ROM" "$1" "$2" "$TMP/$3" > "$TMP/$3.log" 2>&1; }
# Original main menu, stage-select cheat, bonus entry.
run 1000 '120-150:start,shot@205,220-225:up,230-235:up,240-245:down,250-255:down,260-265:left,270-275:right,280-285:left,290-295:right,300-305:b,310-315:a,shot@340,350-355:right,360-365:right,370-375:right,380-385:right,390-395:right,400-405:right,410-415:right,shot@435,450-480:start,shot@700' m_
# Empty load slot must reject, remain in slot menu and return to menu.
run 520 '120-150:start,shot@205,220-225:down,250-255:start,shot@320,350-355:start,shot@400,430-435:b,shot@480' empty_
# Instructions and order info are one text page each: shown, unchanged by
# RIGHT/LEFT, closed with C back to the main menu.
run 1000 '120-150:start,220-225:down,240-245:down,270-275:start,shot@340,380-385:right,shot@440,460-465:left,shot@500,520-525:c,shot@560,580-585:down,600-605:down,630-635:start,shot@700,730-735:right,shot@780,800-805:c,shot@850' pages_
# Settings: toggle music and effects off, choose control scheme 2, persist.
JAZZ_SRAM_OUT="$TMP/options.srm" run 650 '120-150:start,220-225:down,240-245:down,260-265:down,290-295:start,shot@330,350-355:right,shot@390,410-415:down,430-435:right,shot@470,490-495:down,510-515:right,shot@550,570-575:b,shot@610' opt_
JAZZ_SRAM_IN="$TMP/options.srm" run 400 '120-150:start,220-225:down,240-245:down,260-265:down,290-295:start,audio@300,shot@350,audio@380' optreload_
# Start, move, pause, save into slot 1, return to paused gameplay.
JAZZ_SRAM_OUT="$TMP/game.srm" run 820 '120-150:start,190-220:start,290-295:start,360-470:right,490-495:start,shot@530,550-555:c,shot@610,640-645:start,shot@720' save_
# Fresh emulator session imports SRAM, load slot and enter the saved stage.
JAZZ_SRAM_IN="$TMP/game.srm" run 600 '120-150:start,220-225:down,250-255:start,shot@310,340-345:start,390-395:start,shot@510' reload_
# Existing slot 1 remains intact while writing slot 2 in a separate session.
JAZZ_SRAM_IN="$TMP/game.srm" JAZZ_SRAM_OUT="$TMP/two.srm" run 680 '120-150:start,190-220:start,290-295:start,370-375:start,410-415:c,shot@450,470-475:down,shot@500,520-525:start,shot@600' slot2_
JAZZ_SRAM_IN="$TMP/two.srm" run 550 '120-150:start,220-225:down,250-255:start,300-305:down,shot@330,350-355:start,400-405:start,shot@500' load2_
# Flip one saved byte; checksum must reject it.
python3 - "$TMP/game.srm" "$TMP/corrupt.srm" <<'PY'
from pathlib import Path
import sys
b=bytearray(Path(sys.argv[1]).read_bytes()); b[2*(256+8+40)+1]^=0x80; Path(sys.argv[2]).write_bytes(b)
PY
JAZZ_SRAM_IN="$TMP/corrupt.srm" run 400 '120-150:start,220-225:down,250-255:start,shot@310,340-345:start,shot@370' corrupt_
python3 - "$TMP" <<'PY'
from PIL import Image, ImageChops
from pathlib import Path
import sys,re
p=Path(sys.argv[1])
def image(prefix,n): return Image.open(p/f'{prefix}_{n}.ppm').convert('RGB')
def changed(a,b,box=None):
    if box: a,b=a.crop(box),b.crop(box)
    return sum(x!=(0,0,0) for x in ImageChops.difference(a,b).getdata())
def count(im,pred): return sum(1 for c in im.getdata() if pred(*c))
base,unlocked,selected,bonus=[image('m',n) for n in (205,340,435,700)]
assert count(base,lambda r,g,b:g>100 and g>r*1.5 and g>b*1.5)>2000
rect=(45,208,275,224)
assert changed(base,unlocked,rect)>20 and changed(unlocked,selected,rect)>5, 'cheat broken'
assert count(bonus.crop((124,120,196,208)),lambda r,g,b:g>80 and g>r*1.5 and g>b*1.5)>400
empty,notice,back=[image('empty',n) for n in (320,400,480)]
assert changed(empty,notice,(50,145,270,175))>5, 'empty slot must report error'
assert changed(back,base)>1000, 'menu did not return with correct selection'
a,b,c,d,e,f,g=[image('pages',n) for n in (340,440,500,560,700,780,850)]
menu_green=lambda im: count(im,lambda r,g,b:g>100 and g>r*1.5 and g>b*1.5)
assert changed(a,base)>2000 and changed(a,b)<100 and changed(a,c)<100, 'instructions page not shown'
assert menu_green(d)>2000, 'instructions exit did not restore main menu'
assert changed(e,d)>2000 and changed(e,f)<100 and menu_green(g)>2000, 'order page / exit failed'
assert changed(image('opt',330),image('opt',390))>10
assert changed(image('opt',470),image('opt',550))>10
assert changed(image('opt',550),image('optreload',350),(0,45,320,150))<300, 'settings did not persist'
b=(p/'options.srm').read_bytes(); assert bytes(b[2*i+1] for i in range(16,21))==bytes([79,0,0,1,0xA4]), 'wrong settings record'
rows=re.findall(r'AUDIO \d+ rms=([\d.]+)', (p/'optreload_.log').read_text()); assert rows and float(rows[-1])<10, f'music off not silent: {rows}'
s=(p/'game.srm').read_bytes(); assert bytes(s[2*i+1] for i in range(256,259))==b'JS\x01', 'save signature absent'
t=(p/'two.srm').read_bytes(); assert t[512:512+12000]==s[512:512+12000], 'slot 1 changed while saving slot 2'
assert bytes(t[2*i+1] for i in range(256+6144,259+6144))==b'JS\x01', 'slot 2 signature missing'
assert changed(image('slot2',450),image('slot2',500))>5, 'slot cursor did not move'
assert count(image('load2',500),lambda r,g,b:b>120 and r<90 and g>40)>2000, 'second slot did not load'
assert changed(image('empty',320),image('reload',310),(90,55,225,90))>10, 'saved slot not shown'
assert changed(image('reload',510),base)>5000, 'loaded game did not start'
assert changed(image('corrupt',310),image('empty',320))<200, 'corrupt slot accepted'
assert changed(image('corrupt',310),image('corrupt',370),(50,145,270,175))>5, 'corrupt slot not rejected'
print('PASS: cheat, empty/corrupt save rejection, SRAM save/reload, persistent setup, music mute, instruction/order pages')
PY
