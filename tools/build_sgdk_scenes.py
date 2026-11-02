#!/usr/bin/env python3
"""Convert JJ1 startup, title, main-menu and clear scenes for Genesis.

Preserves the decoded background and every animation delta from OpenJazz's
1L/11/FF/][ formats. Uses one common 15-colour Genesis palette per scene;
frames are stored as 40x25 row-major 4bpp tiles, plus changed-tile runs for
forward and reverse playback; the startup uses sparse tile deltas. Output is GPL-2.0-or-later; original game
art and audio remain subject to the rights of their owners.
"""
import argparse
from pathlib import Path
from PIL import Image
from jj1_scene_decode import load_animation, data_offsets, unpack_rle, u16, u32
from import_jj1_shareware import Reader, load_palette, decode_menu
import struct

SCENES=[('level_clear','ENDLEVEL.0SC',1),('bonus_clear','BONUS.0SC',1)]
W,H=320,200

def palettize(frames, source_palette):
    img=Image.new('RGB',(320,200*len(frames)))
    img.putdata([source_palette[p] for frame in frames for p in frame])
    q=img.quantize(colors=15,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    p=q.getpalette(); p += [0] * (45-len(p)); cols=[tuple(p[3*i:3*i+3]) for i in range(15)]
    remap=[1+min(range(15),key=lambda j:sum((cols[j][k]-c[k])**2 for k in range(3))) for c in source_palette]
    packed=[]
    for frame in frames:
        data=bytearray()
        for ty in range(25):
            for tx in range(40):
                for y in range(8):
                    pos=(ty*8+y)*320+tx*8
                    for x in range(0,8,2):data.append(remap[frame[pos+x]]*16+remap[frame[pos+x+1]])
        assert len(data)==32000
        packed.append(bytes(data))
    return cols, packed


def color_line(name,cols):
    colors=[(0,0,0)]+cols
    return 'static const u16 '+name+'_pal[16] = { '+', '.join('0x%03X'%(((b>>5)<<9)|((g>>5)<<5)|((r>>5)<<1)) for r,g,b in colors)+' };'


def startup_credits(root, source):
    """Render page scripts 1 and 2 with their actual FONTMN2 glyphs."""
    rr=Reader((root/'FONTMN2.0FN').read_bytes())
    if rr.block(19)!=b'Digital Dimensions\x1a': raise ValueError('bad font')
    space=rr.byte();lineheight=rr.byte();glyph=[]
    while rr.pos<len(rr.data):
        size=rr.u16()
        if size>4:
            data=rr.rle(size);w=data[0]|data[1]<<8;h=data[2]|data[3]<<8
            if 4+w*h>len(data):raise ValueError('invalid glyph')
            glyph.append((w,h,data[4:4+w*h]))
        else:glyph.append((0,0,b''))
    pages=[]; align=0
    for page in (1,2):
        pos=u32(source,25+page*4)
        if source[pos]!=0x50:raise ValueError('missing startup page')
        pos+=5; lines=[]
        while True:
            tag=source[pos];pos+=1
            if tag==0x58:  # define font
                pos+=2;n=source[pos];pos+=1
                if source[pos:pos+n]!=b'FONTMN2': raise ValueError('unexpected credit font')
                pos+=n
            elif tag==0x46:pos+=2
            elif tag==0x4a:align=source[pos];pos+=1
            elif tag in (0x40,0x5e):
                n=source[pos];pos+=1;lines.append(source[pos:pos+n]);pos+=n
            elif tag==0x5d:pos+=2
            elif tag==0x3e:break
            else:raise ValueError(f'unexpected startup credit opcode {tag:02x}')
        buf=bytearray(320*200);y=0
        for line in lines:
            def gw(c): return (glyph[c][0]+1) if c<len(glyph) else space
            width=sum(gw(c) for c in line)
            x=(320-width)//2 if align==2 else 320-width if align==1 else 0
            for c in line:
                if c<len(glyph):
                    w,h,pix=glyph[c]
                    for gy in range(h):
                        for gx in range(w):
                            v=pix[gy*w+gx]
                            if v and 0<=x+gx<320 and 0<=y+gy<200:buf[(y+gy)*320+x+gx]=v
                x+=gw(c)
            y+=lineheight
        pages.append(bytes(buf))
    return pages


def startup_assets(root,out,lines):
    source=(root/'STARTUP.0SC').read_bytes();offsets=data_offsets(source)
    anim=load_animation(root/'STARTUP.0SC',1)
    cols,tiles=palettize(anim['frames'],anim['palette'])
    data=bytearray(tiles[0]);pairs=[]
    for a,b in zip(tiles,tiles[1:]):
        changed=[i for i in range(1000) if a[i*32:(i+1)*32]!=b[i*32:(i+1)*32]]
        runs=[]
        for i in changed:
            if runs and i==runs[-1][0]+runs[-1][1]: runs[-1]=(runs[-1][0],runs[-1][1]+1)
            else:runs.append((i,1))
        begin=len(data)
        for start,count in runs:data.extend(b[start*32:(start+count)*32])
        pairs.append((runs,begin))
    (out/'startup.bin').write_bytes(data)
    lines.append(color_line('startup',cols))
    for i,(runs,_) in enumerate(pairs):
        if not runs:raise ValueError('empty startup transition')
        lines.append(f'static const SceneRun startup_runs_{i}[] = {{ '+', '.join('{%d,%d}'%x for x in runs)+' };')
    tags={f:s for f,s,_,_ in anim['sound_tags']}
    if len(tags)!=len(anim['sound_tags']) or any(i not in (1,2,3) for i in tags.values()):raise ValueError('unsupported startup sound tag')
    lines.append('static const SparseDelta startup_deltas[] = {')
    for i,(runs,begin) in enumerate(pairs):lines.append(f'    {{startup_runs_{i}, {len(runs)}, {begin}UL, {tags.get(i+1,0)}}},')
    lines.append('};')
    # The two text pages use the script-selected palette and original font.
    rr=Reader(source);rr.seek(offsets[2]);credit_pal=load_palette(rr)
    credits=startup_credits(root,source)
    cc,packed=palettize(credits,credit_pal)
    (out/'startup_credits.bin').write_bytes(b''.join(packed))
    lines.append(color_line('startup_credits',cc))
    # Final startup page is the original 320x200 title image and palette.
    rr=Reader(source);rr.seek(offsets[3]);title_pal=load_palette(rr)
    p=offsets[4];size=u16(source,p);typ=source[p+2];w=u16(source,p+3);h=u16(source,p+5)
    if (typ,w,h)!=(4,320,200):raise ValueError('unexpected title image')
    title=unpack_rle(source[p+7:p+2+size],w*h)
    tc,packed=palettize([title],title_pal)
    (out/'title.bin').write_bytes(packed[0]);lines.append(color_line('title',tc))
    print('startup animation',len(tiles),'frames,',len(data),'bytes; credits and title converted')


def menu_assets(root,out,lines):
    """Build original menu and its six highlight states into a tile dictionary."""
    normal,bright=decode_menu(root/'MENU.000')
    source_palette=[tuple(normal.getpalette()[3*i:3*i+3]) for i in range(256)]
    base=bytes(normal.getdata());hi=bytes(bright.getdata())
    rects=[(92,35,136,22),(92,57,140,22),(88,83,144,22),
           (86,109,150,23),(82,137,156,26),(78,166,166,29)]
    frames=[]
    for x,y,w,h in rects:
        frame=bytearray(base)
        for py in range(y,y+h):
            for px in range(x,x+w):
                p=py*320+px
                if hi[p]:frame[p]=hi[p]
        frames.append(bytes(frame))
    cols,packed=palettize(frames,source_palette)
    dictionary={};tilebytes=bytearray();maps=[]
    for frame in packed:
        indexes=[]
        for pos in range(0,len(frame),32):
            t=frame[pos:pos+32]
            if t not in dictionary:
                dictionary[t]=len(dictionary);tilebytes.extend(t)
            indexes.append(dictionary[t])
        maps.append(indexes)
    if len(dictionary)>1400:raise ValueError('menu tile dictionary exceeds VRAM')
    (out/'menu_tiles.bin').write_bytes(tilebytes)
    (out/'menu_maps.bin').write_bytes(b''.join(struct.pack('>H',v) for m in maps for v in m))
    lines.append(color_line('menu',cols))
    lines.append(f'#define JJ1_MENU_TILE_COUNT {len(dictionary)}')
    print('menu',len(dictionary),'unique tiles and six original highlight states')


def convert(root,out,inc):
    out.mkdir(parents=True,exist_ok=True)
    meta=[]
    for name,filename,idx in SCENES:
        scene=load_animation(root/filename,idx)
        frames=scene['frames']; pal=scene['palette']
        cols,tiles=palettize(frames,pal)
        # Retain only changed tiles, both directions, so reverse-playback
        # scenes do not duplicate seven 32 KB frames per animation.
        data=bytearray(tiles[0]); pairs=[]
        for a,b in zip(tiles,tiles[1:]):
            changed=[i for i in range(1000) if a[i*32:(i+1)*32]!=b[i*32:(i+1)*32]]
            runs=[]
            for i in changed:
                if runs and i==runs[-1][0]+runs[-1][1]: runs[-1]=(runs[-1][0],runs[-1][1]+1)
                else:runs.append((i,1))
            forward=len(data)
            for start,count in runs: data.extend(b[start*32:(start+count)*32])
            backward=len(data)
            for start,count in runs: data.extend(a[start*32:(start+count)*32])
            pairs.append((runs,forward,backward))
        (out/(name+'.bin')).write_bytes(data)
        meta.append((name,cols,scene,pairs))
        print(name,'frames',len(frames),'bytes',len(data), 'changes',[(sum(n for _,n in r),len(r)) for r,_,_ in pairs])
    lines=['/* Generated by tools/build_sgdk_scenes.py; do not edit. */']
    for name,cols,scene,pairs in meta:
        lines.append(color_line(name,cols))
        for i,(runs,_,_) in enumerate(pairs):
            lines.append(f'static const SceneRun {name}_runs_{i}[] = {{ '+', '.join('{%d,%d}'%x for x in runs)+' };')
        lines.append(f'static const SceneDelta {name}_deltas[] = {{')
        for i,(runs,forward,backward) in enumerate(pairs):lines.append(f'    {{{name}_runs_{i}, {len(runs)}, {forward}UL, {backward}UL}},')
        lines.append('};')
        lines.append(f'static const SceneData {name}_scene = {{ jj1_scene_{name}, {name}_pal, {name}_deltas, {len(scene["frames"])}, {1 if scene["reverse"] else 0} }};')
    startup_assets(root,out,lines)
    menu_assets(root,out,lines)
    inc.write_text('\n'.join(lines)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--input',type=Path,required=True);p.add_argument('--out-res',type=Path,default=Path('res/scenes'));p.add_argument('--out-inc',type=Path,default=Path('src/jj1_scene_data.inc'));a=p.parse_args();convert(a.input,a.out_res,a.out_inc)
