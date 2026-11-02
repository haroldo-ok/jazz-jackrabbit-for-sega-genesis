#!/usr/bin/env python3
"""Convert ENDE1.0SC pages and animation, including original scripted text.

OpenJazz's scene parser defines glyph-index text and frame deltas. The three
static pages are pre-composited using FONT2/FONTBIG and the source palettes;
the 28 animation frames use sparse Genesis tile transitions.
"""
import argparse, struct
from pathlib import Path
from build_planets import font
from build_sgdk_scenes import palettize, color_line
from jj1_scene_decode import data_offsets, load_animation, unpack_rle, u16, u32
from import_jj1_shareware import Reader, load_palette


def page_text(data, pos, canvas, fonts, initial_font=2, initial_align=2):
    if data[pos] != 0x50: raise ValueError('invalid page')
    pos+=5; selected=initial_font; align=initial_align; x0=y0=0; width=320; y=0; shadow=False
    lines=[]
    while pos < u32(data,19):
        op=data[pos];pos+=1
        if op in (0x3e,0x50):break
        if op == 0x2a:
            size=data[pos];pos+=1+size
        elif op == 0x58:
            size=data[pos+2];pos+=3+size
        elif op == 0x4c:pos+=6
        elif op == 0x46:selected=u16(data,pos);pos+=2
        elif op == 0x4a:align=data[pos];pos+=1
        elif op == 0x57:
            x0,y0,right,bottom=struct.unpack_from('<HHHH',data,pos);pos+=8;width=right-x0;y=0
        elif op == 0xdb:shadow=bool(data[pos]);pos+=2
        elif op in (0xb1,0x5d):pos+=2
        elif op in (0x40,0x5e):
            n=data[pos];pos+=1; string=data[pos:pos+n];pos+=n
            space,height,glyphs=fonts[selected]; total=sum(glyphs[c][0]+1 if c<len(glyphs) else space for c in string)
            x=x0+(width-total)//2 if align==2 else x0+width-total if align==1 else x0
            for c in string:
                if c>=len(glyphs): x+=space;continue
                w,h,pixels=glyphs[c]
                if shadow:
                    for gy in range(h):
                        for gx in range(w):
                            if pixels[gy*w+gx] and 0<=x+gx-1<320 and 0<=y0+y+gy+2<200:canvas[(y0+y+gy+2)*320+x+gx-1]=0
                for gy in range(h):
                    for gx in range(w):
                        value=pixels[gy*w+gx]
                        if value and 0<=x+gx<320 and 0<=y0+y+gy<200:canvas[(y0+y+gy)*320+x+gx]=value
                x+=w+1
            lines.append(string);y+=height
        elif op==0x51:pos+=1
        elif op==0x45:pos+=2
        else:raise ValueError(f'unsupported ENDE1 page opcode {op:02x} at {pos-1}')
    return lines


def convert(root,out,inc):
    data=(root/'ENDE1.0SC').read_bytes(); offsets=data_offsets(data)
    p=offsets[1];size=u16(data,p);typ=data[p+2];width=u16(data,p+3);height=data[p+5]
    if (typ,width,height)!=(3,320,200):raise ValueError('unexpected ending image')
    base=unpack_rle(data[p+6:p+2+size],64000)
    fonts={1:font(root/'FONT2.0FN'),2:font(root/'FONTBIG.0FN')}
    pages=[]; lines=[]
    for script,palindex in ((0,0),(1,2),(3,5)):
        b=bytearray(base)
        text=page_text(data,u32(data,25+script*4),b,fonts)
        r=Reader(data);r.seek(offsets[palindex]);palette=load_palette(r)
        colors,packed=palettize([b],palette)
        (out/f'ending_page_{script}.bin').write_bytes(packed[0]);pages.append(colors)
        lines.append(color_line(f'ending_page_{script}',colors))
        print('ending page',script+1,'text lines',len(text))
    anim=load_animation(root/'ENDE1.0SC',4)
    if anim['reverse'] or anim['sounds']:raise ValueError('unexpected ending animation options')
    colors,tiles=palettize(anim['frames'],anim['palette']); blob=bytearray(tiles[0]);pairs=[]
    for a,b in zip(tiles,tiles[1:]):
        changed=[i for i in range(1000) if a[i*32:i*32+32]!=b[i*32:i*32+32]];runs=[]
        for i in changed:
            if runs and i==sum(runs[-1]):runs[-1]=(runs[-1][0],runs[-1][1]+1)
            else:runs.append((i,1))
        offset=len(blob)
        for start,count in runs:blob.extend(b[start*32:(start+count)*32])
        pairs.append((runs,offset))
    (out/'ending_animation.bin').write_bytes(blob);lines.append(color_line('ending_animation',colors))
    for i,(runs,_) in enumerate(pairs):
        if not runs: raise ValueError('empty ending transition')
        lines.append(f'static const SceneRun ending_animation_runs_{i}[] = {{ '+', '.join('{%d,%d}'%v for v in runs)+' };')
    lines.append('static const SparseDelta ending_animation_deltas[] = {')
    for i,(runs,offset) in enumerate(pairs): lines.append(f'    {{ending_animation_runs_{i}, {len(runs)}, {offset}UL, 0}},')
    lines.append('};')
    inc.write_text('\n'.join(lines)+'\n')
    print('ending animation',len(tiles),'frames',len(blob),'bytes')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,required=True)
    p.add_argument('--out-res',type=Path,default=Path('res/scenes'));p.add_argument('--out-inc',type=Path,default=Path('src/jj1_ending_data.inc'))
    a=p.parse_args();a.out_res.mkdir(parents=True,exist_ok=True);convert(a.input,a.out_res,a.out_inc)
