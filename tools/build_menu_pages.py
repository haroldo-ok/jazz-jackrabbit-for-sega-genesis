#!/usr/bin/env python3
"""Convert all original instruction/order script pages into Genesis tile pages.

Based on OpenJazz's JJ1Scene script, image and font loading (GPL-2.0+).
Retains background layers, original glyphs, layout, page numbers and text.
A per-page 15-colour palette is needed on Genesis. Pages are compressed by
SGDK rescomp (LZ4W) at build time and unpacked on demand into RAM.
"""
import argparse
import struct
from pathlib import Path
from PIL import Image
from import_jj1_shareware import Reader, load_palette
from jj1_scene_decode import data_offsets, unpack_rle, u16, u32
from build_sgdk_scenes import palettize, color_line
from build_planets import font


def image_at(data, offset):
    size=u16(data,offset); typ=data[offset+2]
    w=u16(data,offset+3); h=data[offset+5] if typ==3 else u16(data,offset+5)
    if typ not in (3,4,5,6,7) or not (0<w<=320 and 0<h<=200):
        raise ValueError(f'bad scene image type/dimensions {typ} {w}x{h}')
    if typ >= 5:
        pixels=unpack_rle(data[offset+2:offset+2+size],w*h+4)
        if pixels[:4] != struct.pack('<HH',w,h):raise ValueError('bad compressed image header')
        pixels=pixels[4:]
    else:
        start=offset+(6 if typ==3 else 7)
        pixels=unpack_rle(data[start:offset+2+size],w*h)
    return Image.frombytes('L',(w,h),bytes(pixels))


def parse_pages(data, font_dir):
    offsets=data_offsets(data); count=u16(data,23)
    starts=[u32(data,25+i*4) for i in range(count)]
    data_table=u32(data,19)
    faces={}; selected=0; align=0; color=0; shadow=-1
    pages=[]
    for number,start in enumerate(starts):
        if data[start]!=0x50:raise ValueError('missing page marker')
        palidx=u16(data,start+3);pos=start+5
        end=starts[number+1] if number+1<count else data_table
        backgrounds=[];records=[];rect=None;point=None;extra=None
        while pos<end:
            op=data[pos];pos+=1
            if op in (0x3e,0x50): break
            if op==0x58:
                index=u16(data,pos);n=data[pos+2];pos+=3
                name=data[pos:pos+n].decode('ascii');pos+=n
                # OpenJazz uses the first font with a given ID.
                if index not in faces:faces[index]=font(font_dir/(name+'.0FN'))
            elif op==0x4c:
                x,y,index=struct.unpack_from('<HHH',data,pos);pos+=6
                backgrounds.append((x,y,index))
            elif op==0x46:selected=u16(data,pos);pos+=2
            elif op==0x4a:align=data[pos];pos+=1
            elif op==0x41:color=u16(data,pos);pos+=2
            elif op==0xdb:shadow=data[pos+1] if data[pos] else -1;pos+=2
            elif op==0x57:
                x,y,right,bottom=struct.unpack_from('<HHHH',data,pos);pos+=8
                rect=(x,y,right-x,bottom-y)
            elif op==0x47:point=struct.unpack_from('<HH',data,pos);pos+=4
            elif op==0x5f:extra=u16(data,pos);pos+=2
            elif op in (0x40,0x5e):
                n=data[pos];pos+=1;txt=bytearray(data[pos:pos+n]);pos+=n
                if op==0x5e:
                    for j in range(1,len(txt)):
                        if txt[j]==0x8b:
                            if number+1>=10:txt[j-1]=(number+1)//10+53
                            txt[j]=(number+1)%10+53
                        elif txt[j]==0x8a:
                            if count>=10:txt[j-1]=count//10+53
                            txt[j]=count%10+53
                        elif txt[j]==0x8c and j>=2:txt[j-2:j+1]=b'000'
                        elif txt[j]>0x8c:txt[j]=ord('0')
                records.append((bytes(txt),selected,align,color,shadow,rect,point,extra));rect=point=extra=None
            elif op in (0x3f,0xa7,0x51):pos+=1
            elif op in (0xb1,0x5d):pos+=2
            elif op==0x45:
                if pos+2>end:break
                pos+=2
            elif op==0x2a:n=data[pos];pos+=1+n
            else:raise ValueError(f'unsupported page opcode {op:02x}, page {number+1}, offset {pos-1}')
            if pos>end:raise ValueError('page command crosses script boundary')
        if not backgrounds:raise ValueError('page without original artwork')
        r=Reader(data);r.seek(offsets[palidx]);palette=load_palette(r)
        canvas=Image.new('L',(320,200),0)
        for x,y,index in backgrounds:canvas.paste(image_at(data,offsets[index]),(x,y))
        # Simulate OpenJazz's per-text rectangle/coordinate/line-height state.
        rx=ry=x=y=0;rw=320;lineextra=0
        for txt,fi,al,col,sh,newrect,point,newextra in records:
            if point is not None:x,y=point
            if newrect is not None:rx,ry,rw,_=newrect;x=y=0
            if newextra is not None:lineextra=newextra
            if fi not in faces:raise ValueError(f'font id {fi} undefined')
            space,height,glyphs=faces[fi]
            width=sum(glyphs[c][0]+1 if c<len(glyphs) else space for c in txt)
            px=rx+x+(rw-width if al==1 else (rw-width)//2 if al==2 else 0);py=ry+y
            for c in txt:
                if c>=len(glyphs):px+=space;continue
                gw,gh,pixels=glyphs[c]
                if sh>=0:
                    for gy in range(gh):
                        for gx in range(gw):
                            if pixels[gy*gw+gx] and 0<=px+gx-1<320 and 0<=py+gy+2<200:canvas.putpixel((px+gx-1,py+gy+2),sh)
                for gy in range(gh):
                    for gx in range(gw):
                        v=pixels[gy*gw+gx]
                        if v and 0<=px+gx<320 and 0<=py+gy<200:canvas.putpixel((px+gx,py+gy),(v+col)&255)
                px+=gw+1
            y+=height+lineextra
        pages.append((canvas.tobytes(),palette,records))
    return pages


def convert(root,out,inc,res):
    out.mkdir(parents=True,exist_ok=True)
    lines=['/* Generated by tools/build_menu_pages.py; do not edit. */']
    resources=['// Original INSTRUCT.0SC and ORDER.0SC pages, compressed by SGDK LZ4W.']
    for prefix,filename in [('instruct','INSTRUCT.0SC'),('order','ORDER.0SC')]:
        pages=parse_pages((root/filename).read_bytes(),root)
        for i,(pixels,palette,records) in enumerate(pages):
            cols,packed=palettize([pixels],palette);name=f'{prefix}_{i:02}'
            lines.append(color_line(name,cols))
            # Four 250-tile segments fit comfortably in Genesis RAM.
            for part in range(4):
                partname=f'{name}_{part}'
                (out/(partname+'.bin')).write_bytes(packed[0][part*8000:(part+1)*8000])
                resources.append(f'BIN jj1_page_{partname} "scenes/{partname}.bin" 2 2 0 FAST')
        lines.append(f'static const MenuPage {prefix}_pages[] = {{')
        for i in range(len(pages)):
            name=f'{prefix}_{i:02}';lines.append(f'    {{{{{", ".join(f"jj1_page_{name}_{p}" for p in range(4))}}}, {name}_pal}},')
        lines.append('};')
        print(filename,len(pages),'pages,',sum(len(r) for _,_,r in pages),'text lines')
    inc.write_text('\n'.join(lines)+'\n');res.write_text('\n'.join(resources)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,required=True)
    p.add_argument('--out-res',type=Path,default=Path('res/scenes'));p.add_argument('--out-inc',type=Path,default=Path('src/jj1_menu_pages.inc'));p.add_argument('--manifest',type=Path,default=Path('res/jj1_menu_pages.res'))
    a=p.parse_args();convert(a.input,a.out_res,a.out_inc,a.manifest)
