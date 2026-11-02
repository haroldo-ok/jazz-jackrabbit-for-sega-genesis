#!/usr/bin/env python3
"""Read JJ1 .0SC animation blocks, following OpenJazz jj1sceneload.cpp.

OpenJazz: Copyright 2005-2017 AJ Thomson, 2015-2024 Carsten Teibes;
GPL-2.0-or-later. This decoder is distributed under the same terms.

Returns 320x200 indexed frames and the original six-bit RGB palette. This
handles the actual 1L/11/FF/][ tags in the shareware animation blocks; it
raises on unsupported image tags instead of silently substituting art.
"""
from pathlib import Path
import struct

W,H=320,200
class SceneError(ValueError): pass

def u16(data,p):
    if p+2>len(data): raise SceneError('truncated 16-bit value')
    return struct.unpack_from('<H',data,p)[0]

def u32(data,p):
    if p+4>len(data): raise SceneError('truncated 32-bit value')
    return struct.unpack_from('<I',data,p)[0]

def unpack_rle(data,size):
    out=bytearray(); p=0
    while len(out)<size:
        if p>=len(data): raise SceneError('truncated RLE stream')
        c=data[p]; p+=1
        n=c&127
        if c&128:
            if p>=len(data): raise SceneError('truncated RLE repeat')
            v=data[p];p+=1;out.extend([v]*n)
        elif n:
            if p+n>len(data): raise SceneError('truncated RLE literal')
            out.extend(data[p:p+n]);p+=n
        else:
            if p>=len(data): raise SceneError('truncated RLE terminator')
            out.append(data[p]);break
        if len(out)>size: raise SceneError('RLE expands beyond image')
    out.extend(bytes(size-len(out)))
    return out

def compact(data,pixels):
    p=0; dst=0; limit=W*H
    while p<len(data):
        code=data[p]; p+=1
        if code==0x7f:
            n=u16(data,p);p+=2
            if p>=len(data): raise SceneError('truncated compact fill')
            color=data[p];p+=1; mode='fill'
        elif code==0xff:
            n=u16(data,p);p+=2;mode='skip'
        elif code&0x80:
            n=code-0x80+1;mode='skip'
        elif code&0x40:
            n=code-0x40+1
            if p>=len(data): raise SceneError('truncated compact fill')
            color=data[p];p+=1;mode='fill'
        else:
            n=(code&0x3f)+1;mode='literal'
        if dst+n>limit: raise SceneError('compact stream exceeds image')
        if mode=='fill':
            for row in range(dst,limit,W):
                take=min(n,limit-row);pixels[row:row+take]=bytes([color])*take
        elif mode=='literal':
            if p+n>len(data):raise SceneError('truncated compact literal')
            for col,color in enumerate(data[p:p+n]):
                if color!=0xff:
                    for row in range(dst+col,limit,W):pixels[row]=color
            p+=n
        dst+=n
    return pixels

def full(data,pixels):
    p=0; dst=0; trans=True
    while p<len(data) and dst<W*H:
        h=data[p];p+=1
        if (h&0x7f)==0x7f:
            n=u16(data,p);p+=2
            if trans:n+=255
        elif h:
            if trans:n=h
            else:
                n=h&0x1f; op=h&0x60
                if op==0:
                    if p+n>len(data):raise SceneError('truncated FF literal')
                    if dst+n>W*H:raise SceneError('FF literal overflow')
                    pixels[dst:dst+n]=data[p:p+n];p+=n
                elif op==0x20:
                    if dst>=W:
                        if dst+n>W*H:raise SceneError('FF copy overflow')
                        # Sequential copy matches the original, including overlap.
                        for k in range(n):pixels[dst+k]=pixels[dst-W+k]
                else:
                    if op==0x60:n=h&0x3f
                    if p>=len(data) or dst+n>W*H:raise SceneError('FF fill overflow')
                    pixels[dst:dst+n]=bytes([data[p]])*n;p+=1
        else: raise SceneError('zero FF header')
        dst+=n
        # The original allows the final transparent skip to run past the
        # screen; the decoder stops once its cursor reaches the end.
        trans=False if h&0x80 else not trans
    return pixels

def data_offsets(b):
    if b[:19]!=b'Digital Dimensions\x1a':raise SceneError('invalid .0SC signature')
    off=u32(b,19);count=u16(b,off)+1
    return [u32(b,off+2+i*4) for i in range(count)]

def load_animation(path,index):
    b=Path(path).read_bytes(); offsets=data_offsets(b)
    if index>=len(offsets):raise SceneError('animation index out of range')
    p=offsets[index]
    if b[p:p+2]!=b'AN':raise SceneError('item is not animation')
    declared=u16(b,p+4);p+=6; sounds=[]
    while True:
        tag=b[p:p+2];size=u16(b,p+2);p+=4
        if tag==b'PL':
            if size!=768 or p+768>len(b):raise SceneError('bad animation palette')
            pal=b[p:p+768];p+=768;break
        if tag==b'SL':
            if p+size>len(b):raise SceneError('bad sound list')
            c=b[p];q=p+1
            for _ in range(c):
                n=b[q];q+=1;sounds.append(b[q:q+n].decode('ascii'));q+=n
        else:raise SceneError(f'unsupported animation header {tag!r}')
        p+=size
    palette=[tuple((x<<2)|(x>>4) for x in pal[i:i+3]) for i in range(0,768,3)]
    frames=[];sound_tags=[];bg=None;reverse=False
    while p+4<=len(b):
        tag=b[p:p+2];size=u16(b,p+2);p+=4
        if p+size>len(b):raise SceneError('animation block exceeds file')
        d=b[p:p+size];p+=size
        if tag==b'_E': break
        if tag==b'1L': bg=compact(d,bytearray(W*H))
        elif tag==b'11': bg=unpack_rle(d,W*H)
        elif tag in (b'FF',b']['):
            if bg is None:raise SceneError('frame before background')
            if not frames: frames.append(bytes(bg))
            cur=bytearray(frames[-1]);(full if tag==b'FF' else compact)(d,cur);frames.append(bytes(cur))
        elif tag==b'RR': reverse=True
        elif tag==b'ST':
            if size!=3 or not frames:raise SceneError('invalid sound tag')
            sound_tags.append((len(frames)-1,d[0],d[1],d[2]))
        elif tag==b'\0\0':
            # OpenJazz skips zero 32-bit padding after a zero block.
            while p+4<=len(b) and u32(b,p)==0:p+=4
        else:raise SceneError(f'unsupported animation block {tag!r} at {p-size-4}')
    else:raise SceneError('animation has no end marker')
    if bg is None:raise SceneError('animation has no background')
    if not frames:frames=[bytes(bg)]
    return {'palette':palette,'frames':frames,'sounds':sounds,'sound_tags':sound_tags,'reverse':reverse,'declared':declared}
