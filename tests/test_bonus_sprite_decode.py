#!/usr/bin/env python3
"""Zero bytes in a masked pixel stream are padding, not opaque pixels."""
from pathlib import Path
from tempfile import TemporaryDirectory
import struct, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from bonus_parse import parse_sprites

# width 1 quarter * 4 = four pixels; mask says all opaque. There is an
# embedded zero between 1 and 2, and padding to the encoded record boundary.
with TemporaryDirectory() as tmp:
    f = Path(tmp) / 'BONUS.000'
    f.write_bytes(struct.pack('<HHHHHH', 0, 1, 1, 1, 2, 1) + bytes([15, 1, 0, 2, 3, 4, 0, 0, 0]))
    sprites = parse_sprites(f)
    assert len(sprites) == 1
    w, h, pixels, mask = sprites[0]
    assert (w, h) == (4, 1)
    assert pixels == bytes([1, 2, 3, 4]), pixels
    assert mask == bytes([1, 1, 1, 1]), mask
print('PASS: masked bonus sprite skips transparent-key padding bytes')
