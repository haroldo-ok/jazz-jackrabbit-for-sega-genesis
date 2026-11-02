#!/usr/bin/env python3
"""JJ1 scene decoder unit tests plus optional shareware round-trip checks."""
import os, sys, tempfile, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from jj1_scene_decode import compact, full, unpack_rle, load_animation, SceneError

# A compacted run writes columns, not consecutive rows: fill column 0, skip
# column 1, and fill column 2 from a raw byte.
p = compact(bytes([0x40, 7, 0x80, 0x00, 3]), bytearray(320*200))
assert p[0:3] == bytes([7,0,3]) and p[320:323] == bytes([7,0,3])
# A full-frame stream alternates transparent skips with writes.
p = full(bytes([2, 0x43, 9, 1, 2, 4, 5]), bytearray(320*200))
assert p[:8] == bytes([0,0,9,9,9,0,4,5]), list(p[:8])
assert unpack_rle(bytes([0x82, 3, 2, 4, 5, 0, 6]), 5) == bytes([3,3,4,5,6])
try:
    full(bytes([1, 0x45]), bytearray(320*200))
except SceneError:
    pass
else:
    raise AssertionError('truncated FF fill accepted')
print('PASS: scene column fill, frame delta, RLE and malformed-data checks')

if 'JJ1_DATA' in os.environ:
    root=Path(os.environ['JJ1_DATA'])
    a=load_animation(root/'STARTUP.0SC',1)
    b=load_animation(root/'ENDLEVEL.0SC',1)
    c=load_animation(root/'BONUS.0SC',1)
    assert (len(a['frames']),len(b['frames']),len(c['frames'])) == (42,8,8)
    assert b['reverse'] and c['reverse']
    assert a['sounds']==['JUMPA11','BLOCK3','YEAHOO'] and len(a['sound_tags'])==5
    with tempfile.TemporaryDirectory() as td:
        out=Path(td)/'scenes'; inc=Path(td)/'scene.inc'
        subprocess.run([sys.executable,str(ROOT/'tools/build_sgdk_scenes.py'),
                        '--input',str(root),'--out-res',str(out),'--out-inc',str(inc)],
                       cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
        for name in ('level_clear.bin','bonus_clear.bin','startup.bin','startup_credits.bin','title.bin','menu_tiles.bin','menu_maps.bin'):
            assert (out/name).read_bytes()==(ROOT/'res/scenes'/name).read_bytes(), f'{name} differs'
        assert inc.read_bytes()==(ROOT/'src/jj1_scene_data.inc').read_bytes()
    print('PASS: all shareware startup and clear-scene frames regenerate byte-for-byte')
else:
    print('SKIP: set JJ1_DATA to verify scene conversion against the shareware files')
