#!/usr/bin/env python3
"""Convert the seven BONUSMAP player animations and BONUS.000 art to VDP tiles.

The emitted player frames are 72x88 at original resolution, split into nine
VDP sprites (32/32/8 pixels wide by 32/32/24 pixels high). One 99-tile frame
is streamed into VRAM.  An independent palette preserves Jazz's colours.
Usage: python3 tools/build_bonus_player.py --input INSTALL --output src/jj1_bonus_player.inc
"""
import argparse
from pathlib import Path
from PIL import Image
import bonus_parse as bp


def convert(root, out):
    _, palette, _ = bp.parse_tileset(root / 'BONUSY.000')
    sprites = bp.parse_sprites(root / 'BONUS.000')
    levels = [bp.parse_level(root / f'BONUSMAP.00{i}') for i in range(3)]
    sequences = []
    for level in levels:
        seqs = []
        for i in range(7):
            a = level['anims'][i*64:(i+1)*64]
            n = a[6]
            if not 1 <= n <= 19:
                raise ValueError(f'invalid player animation {i}: {n} frames')
            ids = list(a[7:7+n])
            if any(j >= len(sprites) or sprites[j][0] > 72 or sprites[j][1] > 88 or not sprites[j][0] or not sprites[j][1] for j in ids):
                raise ValueError(f'unsupported player sprite in animation {i}: {ids}')
            seqs.append(ids)
        sequences.append(seqs)
    ids = sorted({j for level in sequences for seq in level for j in seq})
    samples = []
    for sid in ids:
        w,h,px,mask = sprites[sid]
        samples.extend(palette[p] for p,m in zip(px,mask) if m and p)
    if not samples:
        raise ValueError('no visible player pixels')
    im = Image.new('RGB',(512,(len(samples)+511)//512))
    im.putdata(samples + [samples[0]]*(im.width*im.height-len(samples)))
    q = im.quantize(colors=15,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    p = q.getpalette(); colors = [tuple(p[i:i+3]) for i in range(0,45,3)]
    def closest(c):
        return 1+min(range(15),key=lambda i:sum((colors[i][j]-c[j])**2 for j in range(3)))
    remap = [closest(c) for c in palette]
    pal = [(0,0,0)] + colors
    lines=['/* Generated from BONUS.000 / BONUSY.000 / BONUSMAP.00n by build_bonus_player.py. */',
           'static const u16 jj1_bonus_player_palette[16] = {',
           '    '+', '.join('0x%03X'%(((b>>5)<<9)|((g>>5)<<5)|((r>>5)<<1)) for r,g,b in pal), '};']
    for sid in ids:
        w,h,px,mask = sprites[sid]
        pixels=[0]*(72*88)
        for y in range(h):
            for x in range(w):
                i=y*w+x
                pixels[y*72+x]=remap[px[i]] if mask[i] and px[i] else 0
        words=[]
        # Each sprite stores columns of tiles top-to-bottom, then next column.
        for sy, th in [(0,4),(32,4),(64,3)]:
            for sx, tw in [(0,4),(32,4),(64,1)]:
                for tx in range(tw):
                    for ty in range(th):
                        for y in range(8):
                            word=0
                            for x in range(8): word=(word<<4)|pixels[(sy+ty*8+y)*72+sx+tx*8+x]
                            words.append(word)
        assert len(words)==99*8
        lines += [f'static const u32 jj1_bonus_player_{sid}[{len(words)}] = {{']
        for i in range(0,len(words),8): lines.append('    '+', '.join('0x%08X'%w for w in words[i:i+8])+',')
        lines.append('};')
    lines.append('static const u32 *const jj1_bonus_player_frames[] = { '+', '.join(f'jj1_bonus_player_{i}' for i in ids)+' };')
    # Animation tables index the compact frame array rather than sprite IDs.
    lines.append('static const u8 jj1_bonus_player_counts[3][7] = {')
    for level in sequences: lines.append('    {'+', '.join(str(len(s)) for s in level)+'},')
    lines.append('};')
    lines.append('static const u8 jj1_bonus_player_anims[3][7][19] = {')
    for level in sequences:
        lines.append('    {')
        for seq in level: lines.append('        {'+', '.join(str(ids.index(i)) for i in seq)+'},')
        lines.append('    },')
    lines.append('};')
    out.write_text('\n'.join(lines)+'\n')
    print('Converted player sprite IDs:',ids)

if __name__ == '__main__':
    ap=argparse.ArgumentParser(); ap.add_argument('--input',type=Path,required=True); ap.add_argument('--output',type=Path,default=Path('src/jj1_bonus_player.inc'))
    a=ap.parse_args(); convert(a.input,a.output)
