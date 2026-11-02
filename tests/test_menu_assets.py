#!/usr/bin/env python3
"""Regenerate every original information page and compare all four tile chunks."""
import os,sys,tempfile,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_menu_pages import parse_pages
if not os.environ.get('JJ1_DATA'):
    print('SKIP: set JJ1_DATA to check original instruction/order pages')
    raise SystemExit(0)
source=Path(os.environ['JJ1_DATA'])
for file,expected in [('INSTRUCT.0SC',12),('ORDER.0SC',17)]:
    pages=parse_pages((source/file).read_bytes(),source)
    assert len(pages)==expected and all(len(pixels)==320*200 for pixels,_,_ in pages)
    assert sum(len(records) for _,_,records in pages)>100
with tempfile.TemporaryDirectory() as d:
    d=Path(d); out=d/'scenes'; inc=d/'pages.inc'; manifest=d/'pages.res'
    subprocess.run([sys.executable,str(ROOT/'tools/build_menu_pages.py'),'--input',str(source),
                    '--out-res',str(out),'--out-inc',str(inc),'--manifest',str(manifest)],check=True,stdout=subprocess.DEVNULL)
    assert inc.read_bytes()==(ROOT/'src/jj1_menu_pages.inc').read_bytes()
    assert manifest.read_bytes()==(ROOT/'res/jj1_menu_pages.res').read_bytes()
    paths=list(out.iterdir()); assert len(paths)==29*4
    for path in paths:
        assert path.stat().st_size==8000
        assert path.read_bytes()==(ROOT/'res/scenes'/path.name).read_bytes(),path.name
print('PASS: 12 instruction and 17 order pages regenerate exactly, including 116 RAM-sized tile chunks')
