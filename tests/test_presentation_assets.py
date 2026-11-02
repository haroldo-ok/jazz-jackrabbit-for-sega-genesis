#!/usr/bin/env python3
"""Verify regenerated presentation data and the original shareware scripts."""
import os, sys, tempfile, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_planets import planet, SOURCES, SCALES
from build_episode_ending import page_text
from build_planets import font
from jj1_scene_decode import load_animation, u32
from import_jj1_shareware import Reader

if not os.environ.get('JJ1_DATA'):
    print('SKIP: set JJ1_DATA to check planet and episode ending assets')
    raise SystemExit(0)
root=Path(os.environ['JJ1_DATA'])
assert [(planet(root/f'PLANET.{e}')[0],planet(root/f'PLANET.{e}')[1]) for e in SOURCES] == [(0,'diamondus'),(3,'tubelectric'),(17,'medivo'),(18,'guardian')]
assert len(SCALES)==24 and SCALES[19] == 2 and SCALES[-1] == 10
scene=root/'ENDE1.0SC'; b=scene.read_bytes(); a=load_animation(scene,4)
assert len(a['frames'])==28 and not a['reverse'] and not a['sounds']
fonts={1:font(root/'FONT2.0FN'),2:font(root/'FONTBIG.0FN')}
text=page_text(b,u32(b,29),bytearray(64000),fonts)
# Exact glyph indices of distinctive original lines, including punctuation.
assert bytes.fromhex('0a 1b 34 34 73 2d 7f 1b 1e 30 1f 28 2e 2f 2c 1f 2d') in [x[:17] for x in text]
assert any(bytes.fromhex('05 16 01 7f 05 01 12 0c 0f 0e 07') in x for x in text)
for name in ('LEVEL0.000','LEVEL1.000','LEVEL0.001','LEVEL1.001','LEVEL0.002','LEVEL1.002','LEVEL0.018'):
    r=Reader((root/name).read_bytes());r.seek(39)
    for _ in range(8):r.skip_rle()
    r.skip(16*9+9+32*2+32*9)
    fields=[]
    for _ in range(3):
        n=r.byte();fields.append(r.block(n).decode('ascii'));r.skip(12-n)
    assert bool(fields[-1]) == name.startswith('LEVEL1.'), (name,fields)
with tempfile.TemporaryDirectory() as d:
    out=Path(d)/'res';out.mkdir()
    for script,inc in (('build_planets.py','jj1_planet_data.inc'),('build_episode_ending.py','jj1_ending_data.inc')):
        target=Path(d)/inc
        subprocess.run([sys.executable,str(ROOT/'tools'/script),'--input',str(root),'--out-res',str(out),'--out-inc',str(target)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
        assert target.read_bytes()==(ROOT/'src'/inc).read_bytes(), inc
    for path in out.iterdir():assert path.read_bytes()==(ROOT/'res/scenes'/path.name).read_bytes(),path.name
print('PASS: planet metadata, ending text/frames and all presentation assets regenerate exactly')
