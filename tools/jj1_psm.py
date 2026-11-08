"""Epic MegaGames MASI (PSM) modules as used by Jazz Jackrabbit: parser and a
tick-level player that turns every note into an instance with its sample,
start/end time and per-tick (frequency, volume) envelope.

Format notes (from the files themselves and the layout OpenMPT documents):
chunks FILE/TITL/SDFT/PBOD/SONG/DSMP; notes are octave/semitone nibbles with
0x40 = C-5 at the sample's C-5 rate; volumes 0..127; samples are delta-coded
8-bit; effects 0x01-0x04 volume slides, 0x0B-0x0E portamento, 0x34 pattern
break, 0x3D speed, 0x3E tempo.
"""
import struct, collections, math
class PSM:
    def __init__(self, path):
        d = open(path, 'rb').read()
        assert d[:4] == b'PSM ' and d[8:12] == b'FILE', path
        self.patterns = {}; self.samples = {}; self.songs = []; self.title = ''
        i = 12
        while i + 8 <= len(d):
            cid = d[i:i+4]; ln = struct.unpack_from('<I', d, i+4)[0]; body = d[i+8:i+8+ln]; i += 8 + ln
            if cid == b'TITL': self.title = body.split(b'\0')[0].decode('latin1')
            elif cid == b'PBOD': self._pattern(body)
            elif cid == b'DSMP': self._sample(body)
            elif cid == b'SONG': self._song(body)
    def _pattern(self, b):
        # u32 length, char[4] id, u16 rows, rows...
        pid = b[4:8].decode('latin1').strip()
        nrows = struct.unpack_from('<H', b, 8)[0]
        p = 10; rows = []
        for r in range(nrows):
            size = struct.unpack_from('<H', b, p)[0]
            end = p + size; q = p + 2; ev = []
            while q < end:
                flags, ch = b[q], b[q+1]; q += 2
                note = ins = vol = eff = None
                if flags & 0x80: note = b[q]; q += 1
                if flags & 0x40: ins = b[q]; q += 1
                if flags & 0x20: vol = b[q]; q += 1
                if flags & 0x10:
                    e = b[q]; q += 1
                    if e == 0x29:        # sample offset: 3 parameter bytes
                        par = b[q] | (b[q+1] << 8) | (b[q+2] << 16); q += 3
                    elif e in (0x2a,):   # retrig: 1 byte
                        par = b[q]; q += 1
                    else:
                        par = b[q]; q += 1
                    eff = (e, par)
                ev.append((ch, note, ins, vol, eff))
            rows.append(ev); p = end
        self.patterns[pid] = rows
    def _sample(self, b):
        flags = b[0]; sid = b[9:13].decode('latin1')
        name = b[13:46].split(b'\0')[0].decode('latin1').strip()
        num, length, ls, le = struct.unpack_from('<HIII', b, 52)
        vol = b[68]; c5 = struct.unpack_from('<I', b, 73)[0] & 0xFFFF
        raw = b[96:96+length]
        acc = 0; pcm = bytearray()
        for x in raw:
            acc = (acc + x) & 0xFF; pcm.append(acc)
        s8 = [v - 256 if v > 127 else v for v in pcm]
        self.samples[num] = dict(id=sid, name=name, flags=flags, length=length, loop=(flags & 0x80) != 0,
                                 ls=ls, le=min(le, length), vol=vol, c5=c5, data=s8)
    def _song(self, b):
        name = b[:9].decode('latin1'); nch = b[10]
        i = 11; song = dict(name=name, channels=nch, order=[], speed=6, tempo=125, pan={}, chvol={})
        while i + 8 <= len(b):
            cid = b[i:i+4]; ln = struct.unpack_from('<I', b, i+4)[0]; body = b[i+8:i+8+ln]; i += 8 + ln
            if cid == b'OPLH':
                q = 2
                while q < len(body):
                    op = body[q]; q += 1
                    if op == 0: break
                    elif op == 1: song['order'].append(body[q:q+4].decode('latin1').strip()); q += 4
                    elif op == 2: q += 4
                    elif op == 3: song['restart'] = struct.unpack_from('<H', body, q)[0]; q += 3
                    elif op == 4: q += 2
                    elif op == 5: q += 2
                    elif op == 6: q += 1
                    elif op == 7: song['speed'] = body[q]; q += 1
                    elif op == 8: song['tempo'] = body[q]; q += 1
                    elif op == 0x0c: q += 6
                    elif op == 0x0d: song['pan'][body[q]] = (body[q+1], body[q+2]); q += 3
                    elif op == 0x0e: song['chvol'][body[q]] = body[q+1]; q += 2
                    else: raise Exception('opcode %x' % op)
        self.songs.append(song)
ST3_CLOCK = 14187580.0          # freq = clock / period (S3M period units)

def note_freq(raw, c5):
    n = (raw & 0x0F) + 12 * (raw >> 4) + 13          # OpenMPT numbering, C-5 = 61
    return c5 * 2.0 ** ((n - 61) / 12.0)

def porta(param):
    """MASI new-format portamento parameter -> (fine, amount) in S3M period units"""
    if param < 4:
        return True, param                          # extra fine, first tick only
    return False, (param >> 2) * 4

def play(path, max_seconds=400):
    m = PSM(path); song = m.songs[0]
    order = song['order']; speed = song['speed']; tempo = song['tempo']
    nch = song['channels']
    chans = [dict(inst=None, smp=None, period=None, vol=64, active=None) for _ in range(nch)]
    notes = []                       # finished instances
    t = 0.0; pos = 0; row = 0; visited = set(); loop_time = None
    def end_note(c):
        if c['active'] is not None:
            c['active']['end'] = t
            notes.append(c['active'])
            c['active'] = None
    while pos < len(order) and t < max_seconds:
        key = (pos, row)
        if key in visited:
            break
        visited.add(key)
        if row == 0 and pos == song.get('restart', 0) and loop_time is None:
            loop_time = t
        pat = m.patterns[order[pos]]
        evs = pat[row] if row < len(pat) else []
        brk = None
        per_ch = {}
        for ch, n, ins, v, e in evs:
            per_ch[ch] = (n, ins, v, e)
            if e and e[0] == 0x3d and e[1]: speed = e[1]
            if e and e[0] == 0x3e and e[1] >= 32: tempo = e[1]
            if e and e[0] == 0x34: brk = e[1]
        tick_len = 2.5 / tempo
        for ch in range(nch):
            c = chans[ch]
            n, ins, v, e = per_ch.get(ch, (None, None, None, None))
            if ins is not None:
                c['inst'] = ins
                s = m.samples.get(ins)
                if s is not None and n is None:
                    c['vol'] = s['vol'] // 2 + (s['vol'] & 1)
            if v is not None:
                c['vol'] = (min(v, 127) + 1) // 2
            if n is not None:
                end_note(c)
                s = m.samples.get(c['inst'])
                if n == 0xFF or s is None or s['length'] < 16:
                    c['period'] = None
                else:
                    if ins is not None and v is None:
                        c['vol'] = (min(s['vol'], 127) + 1) // 2
                    c['period'] = ST3_CLOCK / note_freq(n, s['c5'])
                    c['active'] = dict(ch=ch, smp=c['inst'], start=t, env=[], note=n)
            c['eff'] = e
        for tick in range(speed):
            for ch in range(nch):
                c = chans[ch]; e = c.get('eff')
                if e:
                    cmd, par = e
                    if cmd == 0x04 and tick > 0: c['vol'] = max(0, c['vol'] - ((par >> 1) & 0x0F))
                    if cmd == 0x02 and tick > 0: c['vol'] = min(64, c['vol'] + ((par << 3) >> 4 & 0x0F))
                    if cmd == 0x03 and tick == 0: c['vol'] = max(0, c['vol'] - (par >> 1))
                    if cmd == 0x01 and tick == 0: c['vol'] = min(64, c['vol'] + ((par & 0x1E) >> 1))
                    if c['period'] and cmd in (0x0c, 0x0e, 0x0b, 0x0d):
                        fine, amt = porta(par)
                        if cmd in (0x0b, 0x0d): fine, amt = True, (par & 0x0F) * 4
                        if (fine and tick == 0) or (not fine and tick > 0):
                            c['period'] += -amt if cmd in (0x0c, 0x0b) else amt
                            c['period'] = min(max(c['period'], 56), 27392 * 4)
                if c['active'] is not None:
                    c['active']['env'].append((ST3_CLOCK / c['period'] if c['period'] else 0.0, c['vol']))
            t += tick_len
        if brk is not None:
            pos += 1; row = (brk >> 4) * 10 + (brk & 15) if brk else 0
            if pos >= len(order): break
        else:
            row += 1
            if row >= len(pat):
                row = 0; pos += 1
    for c in chans: end_note(c)
    return m, song, notes, t, (loop_time or 0.0), 2.5 / tempo
