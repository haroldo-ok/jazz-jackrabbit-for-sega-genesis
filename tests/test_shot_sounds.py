#!/usr/bin/env python3
"""Cross-check original shot sample IDs, clip bytes, rates and ROM resources."""
import os
import re
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
from build_shot_sounds import SHOTS, read_archive
from build_sgdk_jj1_level_data import runtime_records
from import_jj1_shareware import Reader

res = (ROOT / 'res/jj1_sounds.res').read_text()
for symbol, (_, _, filename) in zip(('jj1_fire', 'jj1_flame', 'jj1_photon', 'jj1_rocket'), SHOTS):
    assert re.search(r'^WAV\s+' + symbol + r'\s+"jj1/' + filename + r'"\s+XGM\s*$', res, re.M), symbol
    with wave.open(str(ROOT / 'res/jj1' / filename), 'rb') as w:
        assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (1, 1, dict((v[2],v[1]) for v in SHOTS)[filename])

source = os.environ.get('JJ1_DATA')
if source:
    source = Path(source)
    clips = read_archive(source / 'SOUNDS.000')
    for name, rate, filename in SHOTS:
        with wave.open(str(ROOT / 'res/jj1' / filename), 'rb') as w:
            assert w.readframes(w.getnframes()) == bytes((b+128)&255 for b in clips[name]), name
    for filename in ('LEVEL0.000', 'LEVEL1.000', 'LEVEL0.001', 'LEVEL1.001', 'LEVEL0.002', 'LEVEL1.002', 'LEVEL0.018'):
        p = source / filename
        r = Reader(p.read_bytes()); r.seek(39)
        for _ in range(8): r.skip_rle()
        r.skip(16 * 9 + 9)
        rates = [r.u16() for _ in range(32)]
        names = []
        for _ in range(32):
            size = r.byte(); assert size <= 8
            names.append(r.block(size).decode('ascii')); r.skip(8-size)
        bullets = runtime_records(p)[8]
        for i, (name, rate, _) in enumerate(SHOTS):
            slot = bullets[i * 20 + 19]
            assert slot and names[slot-1] == name and rates[slot-1] == rate, (filename, i, slot)
        slot = bullets[4 * 20 + 19]
        assert not slot or not names[slot-1], (filename, slot)
    print('PASS: original shareware shot definitions, PCM bytes and rates')
else:
    print('PASS: weapon resource declarations (set JJ1_DATA for original-data verification)')
