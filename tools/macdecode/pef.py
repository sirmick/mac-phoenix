#!/usr/bin/env python3
"""pef.py FILE [--imports] [--uses SYMBOL] [--dis START END]: a PEF (CFM
PowerPC) container: its sections, imported libraries and symbols (with
--imports, and its exports), the TOC slots relocated to an import and the
code that loads them (--uses), and a disassembly of its code section with
TOC loads and cross-TOC glue calls named (--dis; capstone). From the
Mac OS Runtime Architectures' description of PEF (loader section,
relocations, pattern-initialized data). FILE is a data fork (`hcopy -r`);
--offset for a container inside a bigger file."""
import struct, sys, argparse
import capstone

def u32(b, o): return struct.unpack_from('>I', b, o)[0]
def u16(b, o): return struct.unpack_from('>H', b, o)[0]

def unpack_pidata(d):
    out = bytearray(); i = 0
    def arg():
        nonlocal i
        v = 0
        while True:
            c = d[i]; i += 1
            v = (v << 7) | (c & 0x7F)
            if not c & 0x80: return v
    while i < len(d):
        b = d[i]; i += 1
        op, cnt = b >> 5, b & 0x1F
        if cnt == 0: cnt = arg()
        if op == 0:
            out += bytes(cnt)
        elif op == 1:
            out += d[i:i+cnt]; i += cnt
        elif op == 2:
            rep = arg() + 1
            blk = d[i:i+cnt]; i += cnt
            out += blk * rep
        elif op == 3:
            cs = cnt; ds = arg(); rc = arg()
            common = d[i:i+cs]; i += cs
            for _ in range(rc):
                out += common; out += d[i:i+ds]; i += ds
            out += common
        elif op == 4:
            cs = cnt; ds = arg(); rc = arg()
            for _ in range(rc):
                out += bytes(cs); out += d[i:i+ds]; i += ds
            out += bytes(cs)
        else:
            raise ValueError('pidata op %d' % op)
    return bytes(out)

class PEF:
    def __init__(self, b):
        assert b[:8] == b'Joy!peff', b[:8]
        self.b = b
        nsec = u16(b, 0x20)
        self.secs = []
        for s in range(nsec):
            o = 40 + 28 * s
            nameoff, addr, exec_size, init_size, raw_size, cont_off = struct.unpack_from('>iIIIII', b, o)
            kind = b[o + 24]
            raw = b[cont_off:cont_off + raw_size]
            if kind == 2:
                data = unpack_pidata(raw)
            else:
                data = raw
            data = data + bytes(max(0, exec_size - len(data)))
            self.secs.append(dict(kind=kind, addr=addr, size=exec_size, data=bytearray(data), off=cont_off))
        ld = next(s for s in self.secs if s['kind'] == 4)
        L = bytes(ld['data'])
        (self.main_sec, self.main_off, self.init_sec, self.init_off, self.term_sec, self.term_off,
         nlibs, nsyms, nrelsec, relinstr_off, strings_off, hash_off, hash_pow, nexports) = struct.unpack_from('>iIiIiIIIIIIIII', L, 0)
        def cstr(o):
            e = L.index(b'\0', strings_off + o); return L[strings_off + o:e].decode('mac_roman')
        self.libs = []
        for i in range(nlibs):
            o = 56 + 24 * i
            name, _, _, cnt, first = struct.unpack_from('>IIIII', L, o)
            self.libs.append((cstr(name), first, cnt))
        so = 56 + 24 * nlibs
        self.syms = []
        for i in range(nsyms):
            v = u32(L, so + 4 * i)
            self.syms.append((cstr(v & 0xFFFFFF), v >> 24))
        self.symlib = {}
        for name, first, cnt in self.libs:
            for k in range(first, first + cnt): self.symlib[k] = name
        # relocations: record which data offsets get which import
        ro = so + 4 * nsyms
        self.slots = {}  # (sec, off) -> ('import', idx) | ('sec', n)
        for r in range(nrelsec):
            sec, _, nrel, first = struct.unpack_from('>HHII', L, ro + 12 * r)
            self.run_relocs(L, relinstr_off + first, nrel, sec)
        # exports
        self.exports = []
        ho = hash_off
        kto = ho + 4 * (1 << hash_pow)
        eso = kto + 4 * nexports
        for i in range(nexports):
            key = u32(L, kto + 4 * i)
            nlen = key >> 16
            cls, val, sec = struct.unpack_from('>IIh', L, eso + 10 * i)
            name = L[strings_off + (cls & 0xFFFFFF):strings_off + (cls & 0xFFFFFF) + nlen].decode('mac_roman')
            self.exports.append((name, cls >> 24, sec, val))

    def run_relocs(self, L, o, n, sec):
        # decode into instructions (word lists), then run with repeats
        ins = []; end = o + 2 * n
        while o < end:
            h = u16(L, o)
            ln = 2 if (h >> 10) in (0x28, 0x29, 0x2C, 0x2D) else 1
            ins.append([u16(L, o + 2 * k) for k in range(ln)]); o += 2 * ln
        st = dict(addr=0, imp=0, code=0, data=1)
        def put(kind, v): self.slots[(sec, st['addr'])] = (kind, v)
        def run(i0, i1):
            i = i0
            while i < i1:
                w = ins[i]; h = w[0]
                if h >> 14 == 0:
                    skip = (h >> 6) & 0xFF; rc = h & 0x3F
                    st['addr'] += 4 * skip
                    for _ in range(rc): put('sec', st['data']); st['addr'] += 4
                elif h >> 13 == 2:
                    sub = (h >> 9) & 0xF; rc = (h & 0x1FF) + 1
                    for _ in range(rc):
                        if sub == 0: put('sec', st['code']); st['addr'] += 4
                        elif sub == 1: put('sec', st['data']); st['addr'] += 4
                        elif sub == 2:
                            put('sec', st['code']); st['addr'] += 4; put('sec', st['data']); st['addr'] += 8
                        elif sub == 3: put('sec', st['code']); st['addr'] += 4; put('sec', st['data']); st['addr'] += 4
                        elif sub == 4: put('sec', st['data']); st['addr'] += 4
                        elif sub == 5: put('import', st['imp']); st['imp'] += 1; st['addr'] += 4
                        else: raise ValueError('group %d' % sub)
                elif h >> 13 == 3:
                    sub = (h >> 9) & 0xF; idx = h & 0x1FF
                    if sub == 0: put('import', idx); st['imp'] = idx + 1; st['addr'] += 4
                    elif sub == 1: st['code'] = idx
                    elif sub == 2: st['data'] = idx
                    elif sub == 3: put('sec', idx); st['addr'] += 4
                    else: raise ValueError('smidx %d' % sub)
                elif h >> 12 == 8:
                    st['addr'] += (h & 0xFFF) + 1
                elif h >> 12 == 9:
                    blk = ((h >> 8) & 0xF) + 1; rep = (h & 0xFF) + 1
                    for _ in range(rep): run(i - blk, i)
                elif h >> 10 == 0x28:
                    st['addr'] = ((h & 0x3FF) << 16) | w[1]
                elif h >> 10 == 0x29:
                    idx = ((h & 0x3FF) << 16) | w[1]
                    put('import', idx); st['imp'] = idx + 1; st['addr'] += 4
                elif h >> 10 == 0x2C:
                    blk = ((h >> 6) & 0xF) + 1; rep = ((h & 0x3F) << 16) | w[1]
                    for _ in range(rep): run(i - blk, i)
                elif h >> 10 == 0x2D:
                    sub = (h >> 6) & 0xF; idx = ((h & 0x3F) << 16) | w[1]
                    if sub == 0: put('sec', idx); st['addr'] += 4
                    elif sub == 1: st['code'] = idx
                    elif sub == 2: st['data'] = idx
                else:
                    raise ValueError('reloc %04x' % h)
                i += 1
        run(0, len(ins))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file'); ap.add_argument('--offset', type=lambda x: int(x, 0), default=0)
    ap.add_argument('--imports', action='store_true')
    ap.add_argument('--uses', help='symbol: TOC slots and the code that loads them')
    ap.add_argument('--dis', nargs=2, type=lambda x: int(x, 0))
    a = ap.parse_args()
    b = open(a.file, 'rb').read()[a.offset:]
    p = PEF(b)
    for i, s in enumerate(p.secs):
        print('section %d kind %d size $%x' % (i, s['kind'], s['size']))
    print('main', p.main_sec, hex(p.main_off), 'init', p.init_sec, hex(p.init_off))
    if a.imports:
        for i, (n, c) in enumerate(p.syms):
            print('%4d %-24s %s class %d' % (i, p.symlib.get(i, '?'), n, c))
        for e in p.exports: print('export', e)
    code = next(i for i, s in enumerate(p.secs) if s['kind'] == 0)
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    if a.uses:
        idx = [i for i, (n, c) in enumerate(p.syms) if n == a.uses]
        slots = [(sec, off) for (sec, off), (k, v) in p.slots.items() if k == 'import' and v in idx]
        print('slots', [(s, hex(o)) for s, o in slots])
        # TOC: r2 = the data section's base + main's TOC? find code "lwz rX, d(r2)" with d+toc == slot
        # The TOC base is the second word of any TVector; take main's.
        dsec = p.secs[p.main_sec]['data'] if p.main_sec >= 0 else None
        tocs = set()
        for (sec, off), (k, v) in p.slots.items():
            pass
        if dsec is not None:
            toc = u32(dsec, p.main_off + 4)
            tocs.add(toc)
        print('toc', [hex(t) for t in tocs])
        cd = bytes(p.secs[code]['data'])
        for t in tocs:
            want = {o - t for s, o in slots}
            for ins in md.disasm(cd, 0):
                if ins.mnemonic == 'lwz' and ins.op_str.endswith('(r2)'):
                    d = int(ins.op_str.split(',')[1].strip().split('(')[0], 0)
                    if d in want:
                        print('use at $%x: %s %s' % (ins.address, ins.mnemonic, ins.op_str))
    if a.dis:
        dis(p, a.dis[0], a.dis[1])

def slot_name(p, toc, d):
    dsec = p.secs[1]['data']; off = toc + d
    k = p.slots.get((1, off))
    if not k: return None
    if k[0] == 'import': return p.syms[k[1]][0]
    v = u32(dsec, off)
    if k[1] == 1:
        s = bytes(dsec[v:v+40])
        if 0 < s[0] < 40 and all(32 <= x < 127 for x in s[1:1+s[0]]): return 'data+$%x "%s"' % (v, s[1:1+s[0]].decode())
        e = s.find(b'\0')
        if e > 2 and all(32 <= x < 127 for x in s[:e]): return 'data+$%x c"%s"' % (v, s[:e].decode())
        return 'data+$%x' % v
    return 'code+$%x' % v

def glue_target(p, toc, c, t):
    ws = [u32(c, t + 4 * k) for k in range(6)] if t + 24 <= len(c) else []
    if ws and ws[0] >> 16 == 0x8182:  # lwz r12, d(r2)
        d = ws[0] & 0xFFFF; d = d - 0x10000 if d & 0x8000 else d
        return slot_name(p, toc, d)
    return None

def dis(p, start, end):
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    c = bytes(p.secs[0]['data'])
    toc = u32(p.secs[1]['data'], p.main_off + 4)
    for a in range(start, end, 4):
        got = list(md.disasm(c[a:a+4], a))
        if not got:
            print('%06x  .long 0x%08x' % (a, u32(c, a))); continue
        ins = got[0]; note = ''
        if ins.op_str.endswith('(r2)'):
            try:
                d = int(ins.op_str.split(',')[-1].strip().split('(')[0], 0)
                note = slot_name(p, toc, d) or ''
            except ValueError: pass
        if ins.mnemonic in ('bl', 'b'):
            t = int(ins.op_str, 0)
            note = glue_target(p, toc, c, t) or ''
        print('%06x  %-8s %-24s %s' % (a, ins.mnemonic, ins.op_str, ('; ' + note) if note else ''))
if __name__ == "__main__":
    main()
