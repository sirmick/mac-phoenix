#!/usr/bin/env python3
"""Turn a run_trace.sh output directory into readable tables.

  analyze.py OUTDIR --lst docs/roms/dissam/<dir>/rom.lst [--map Apple.lst.txt]

Reads trace.raw.tsv (CPU tracer), hw.raw.tsv / hw.unique.tsv (boottrace.lua),
*.lowmem-*.bin / lowmem-*.bin; writes routines.tsv, flow.txt, ramexec.tsv,
traps.tsv, hwprobes.tsv, hwunique.tsv, slots.txt, lowmem-*.txt.
"""
import argparse
import glob
import os
import re
import sys
from bisect import bisect_right

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
LOMEM = os.path.join(REPO, "docs/roms/dissam/tools/vendor/classic-mac-rom-ghidra-tools/lomem_globals.txt")
TRAPDEFS = os.path.join(REPO, "src/executor/multiversal/defs")

LOCAL = re.compile(r"^(LAB|caseD|switchD|switchdataD|default|USHORT|PTR_DAT|DAT|PTR)(_|$)")


def load_labels(lst, amap):
    """offset -> name. Named labels from the listing (Apple's names merged in
    by the listing export) and, if given, the Apple map itself."""
    labels = {}
    if lst:
        pending = []
        with open(lst, errors="replace") as f:
            for line in f:
                m = re.match(r"^([A-Za-z_][A-Za-z0-9_.@$]*):\s*$", line)
                if m:
                    pending.append(m.group(1))
                    continue
                m = re.match(r"^([0-9a-f]{8})(?:\.\.[0-9a-f]{8})?\s", line)
                if m and pending:
                    off = int(m.group(1), 16) & 0xFFFFF
                    names = [p for p in pending if not LOCAL.match(p)]
                    if names:
                        # prefer Apple-style names over FUN_
                        names.sort(key=lambda n: (n.startswith("FUN_") or n.startswith("thunk_"), n))
                        labels.setdefault(off, names[0])
                    pending = []
    if amap:
        with open(amap) as f:
            for line in f:
                p = line.split()
                if len(p) >= 2 and p[1].startswith("0x"):
                    off = int(p[1], 16)
                    cur = labels.get(off)
                    if cur is None or cur.startswith("FUN_"):
                        labels[off] = p[0]
    offs = sorted(labels)
    return labels, offs


def load_traps():  # noqa: C901
    names = {}
    try:
        import yaml
        for f in glob.glob(os.path.join(TRAPDEFS, "*.yaml")):
            d = yaml.safe_load(open(f)) or []
            for it in d:
                if not isinstance(it, dict):
                    continue
                for v in it.values():
                    if isinstance(v, dict) and "trap" in v and "name" in v and "selector" not in v:
                        names.setdefault(tkey(int(v["trap"])), v["name"])
    except Exception as e:  # noqa: BLE001
        print("trap names unavailable:", e, file=sys.stderr)
    sys.path.insert(0, os.path.join(REPO, "docs/roms/dissam/tools"))
    try:
        from mac_traps import TRAP_NAMES
        for k, v in TRAP_NAMES.items():
            names.setdefault(tkey(k), v.lstrip("_"))
    except Exception:  # noqa: BLE001
        pass
    return names


def tkey(w):
    return 0xA000 | (w & 0xFF) if w < 0xA800 else w & 0xFBFF


def trapname(names, w):
    return names.get(tkey(w)) or "$%04X" % w


class Labeler:
    def __init__(self, labels, offs, romsize):
        self.labels, self.offs, self.romsize = labels, offs, romsize

    def rom_off(self, pc):
        a24 = pc & 0xFFFFFF
        top = pc >> 24
        if (a24 & 0xF00000) == 0x800000 and top < 0xF0 and top != 0x50:
            return (a24 - 0x800000) % self.romsize
        return None

    def routine(self, pc):
        off = self.rom_off(pc)
        if off is None:
            return None, None
        i = bisect_right(self.offs, off) - 1
        if i < 0:
            return None, off
        return self.offs[i], off

    def name(self, pc):
        start, off = self.routine(pc)
        if off is None:
            return "RAM:%08x" % pc if (pc >> 24) < 0xF0 else "SLOT%X:%08x" % ((pc >> 24) & 0xF, pc)
        if start is None:
            return "ROM+%05X" % off
        d = off - start
        return self.labels[start] + ("+%X" % d if d else "")


def read_trace(path):
    clock = 1
    rows = []
    with open(path) as f:
        for line in f:
            if line.startswith("# clock"):
                clock = int(line.split()[2])
                continue
            rows.append(line.rstrip("\n").split("\t"))
    return clock, rows


IO_DEV = [  # (start, end, name, regfn) offsets within the $50Fxxxxx window (low 18 bits)
    (0x00000, 0x01FFF, "VIA1", "via"), (0x02000, 0x03FFF, "VIA2", "via"),
    (0x04000, 0x05FFF, "SCC", None), (0x06000, 0x07FFF, "SCSI-dma", None),
    (0x10000, 0x11FFF, "SCSI", "scsi"), (0x12000, 0x13FFF, "SCSI-drq", None),
    (0x14000, 0x15FFF, "ASC", None), (0x16000, 0x17FFF, "IWM/SWIM", "iwm"),
    (0x24000, 0x25FFF, "RBV-vid", None), (0x26000, 0x27FFF, "RBV", "rbv"),
    (0x40000, 0x41FFF, "VIA1(alt)", "via"),
]
VIA_REGS = ["vBufB", "vBufAH", "vDirB", "vDirA", "vT1C", "vT1CH", "vT1L", "vT1LH",
            "vT2C", "vT2CH", "vSR", "vACR", "vPCR", "vIFR", "vIER", "vBufA"]
SCSI_REGS = ["CDR/ODR", "ICR", "MR", "TCR", "CSR/SER", "BSR/DMAS", "IDR/DTR", "RPI/DIRR"]
RBV_REGS = {0x00: "RBufB", 0x01: "RExp", 0x02: "RSIFR", 0x03: "RIFR", 0x10: "RMonP", 0x11: "RChpT",
            0x12: "RSIER", 0x13: "RIER"}


def lanes(addr, data, mask):
    """Split a 32-bit-bus access into (byte address, byte value) pairs."""
    out = []
    for i in range(4):
        sh = 24 - 8 * i
        if (mask >> sh) & 0xFF:
            out.append((addr + i, (data >> sh) & 0xFF))
    return out


def io_name(a):
    if (a >> 24) == 0x50:
        o = a & 0xFFFFF
        win = "$50%X" % ((a >> 20) & 0xF)
        lo = o & 0x3FFFF if o < 0x80000 else o
        for s, e, n, r in IO_DEV:
            if s <= lo <= e:
                reg = ""
                if r == "via":
                    reg = VIA_REGS[(lo >> 9) & 0xF]
                elif r == "scsi":
                    reg = SCSI_REGS[(lo >> 4) & 7]
                elif r == "iwm":
                    reg = "r%X" % ((lo >> 9) & 0xF)
                elif r == "rbv":
                    reg = RBV_REGS.get(lo & 0x1F, "r%02X" % (lo & 0x1F))
                return "%s %s" % (n, reg) if reg else n
        return "IO? %s+%05X" % (win, o)
    if (a >> 28) == 0xF:
        return "slot %X +%06X" % ((a >> 24) & 0xF, a & 0xFFFFFF)
    if 0x9 <= (a >> 28) <= 0xE:
        return "superslot %X" % (a >> 28)
    return "?"


def load_lomem():
    out = []
    try:
        with open(LOMEM) as f:
            for line in f:
                p = line.split(None, 3)
                if len(p) >= 3 and re.match(r"^[0-9A-Fa-f]{4}$", p[1]):
                    out.append((p[0].lstrip("*"), int(p[1], 16), p[2], p[3].strip() if len(p) > 3 else ""))
    except OSError:
        pass
    return out


def lowmem_txt(binpath, txtpath, lomem):
    data = open(binpath, "rb").read()
    with open(txtpath, "w") as o:
        o.write("# %s\n# name\taddr\tvalue\n" % os.path.basename(binpath))
        for name, a, kind, _desc in lomem:
            if a + 4 > len(data):
                continue
            if kind == "long":
                v = "%08X" % int.from_bytes(data[a:a + 4], "big")
            elif kind == "word":
                v = "%04X" % int.from_bytes(data[a:a + 2], "big")
            elif kind == "byte":
                v = "%02X" % data[a]
            elif kind.startswith("str") or kind == "pstr":
                n = data[a]
                v = repr(data[a + 1:a + 1 + min(n, 31)].decode("mac_roman"))
            else:
                v = data[a:a + 8].hex()
            o.write("%s\t$%04X\t%s\n" % (name, a, v))
        o.write("\n# hex dump $0000-$2FFF\n")
        for a in range(0, len(data), 16):
            o.write("%04X  %s\n" % (a, data[a:a + 16].hex(" ")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--lst")
    ap.add_argument("--map")
    ap.add_argument("--romsize", type=lambda s: int(s, 0), default=0x40000)
    a = ap.parse_args()
    labels, offs = load_labels(a.lst, a.map)
    L = Labeler(labels, offs, a.romsize)
    traps = load_traps()
    clock, rows = read_trace(os.path.join(a.out, "trace.raw.tsv"))

    def secs(cyc):
        return int(cyc) / clock

    # routines: first entry into each routine
    seen = {}
    first_ram = None
    flow = []
    prev_rt = None
    ram_groups = []   # [first_pc, last_pc, n, icount, cyc, from]
    for r in rows:
        k = r[0]
        if k in ("R", "M"):
            pc, frm, ic, cyc = int(r[1], 16), int(r[2], 16), int(r[3]), int(r[4])
            if k == "R":
                start, off = L.routine(pc)
                key = start if start is not None else off
                if key not in seen:
                    seen[key] = (ic, cyc, pc, frm, off == start)
                rt = L.labels.get(start, "ROM+%05X" % off) if start is not None else "ROM+%05X" % off
            else:
                if first_ram is None:
                    first_ram = (pc, frm, ic, cyc)
                g = ram_groups[-1] if ram_groups else None
                if g and g[1] <= pc <= g[1] + 0x40 and (pc >> 24) == (g[1] >> 24):
                    g[1] = pc
                    g[2] += 1
                else:
                    ram_groups.append([pc, pc, 1, ic, cyc, frm])
                rt = "RAM" if (pc >> 24) < 0xF0 else "SLOT%X" % ((pc >> 24) & 0xF)
                rt += ":%06X" % ((pc & 0xFFFFFF) & ~0xFFF)
            if rt != prev_rt:
                flow.append((ic, cyc, rt, pc, frm))
                prev_rt = rt
        elif k == "S":
            flow.append((int(r[3]), int(r[4]), "== lowmem snapshot '%s'" % r[2], int(r[1], 16), 0))

    with open(os.path.join(a.out, "routines.tsv"), "w") as o:
        o.write("# order\tseconds\tinstr\trom_off\troutine\tentered_at\tfrom_pc\tfrom\n")
        for i, (key, (ic, cyc, pc, frm, at_entry)) in enumerate(sorted(seen.items(), key=lambda kv: kv[1][0])):
            name = L.labels.get(key, "ROM+%05X" % key) if key in L.labels else "ROM+%05X" % key
            o.write("%d\t%.6f\t%d\t%05X\t%s\t%s\t%08X\t%s\n" % (
                i, secs(cyc), ic, key, name, "entry" if at_entry else L.name(pc), frm, L.name(frm)))

    with open(os.path.join(a.out, "flow.txt"), "w") as o:
        o.write("# first-execution flow: a line each time newly executed code moves to another routine\n")
        o.write("# seconds\tinstr\troutine\tpc\tfrom\n")
        for ic, cyc, rt, pc, frm in flow:
            o.write("%.6f\t%d\t%s\t%08X\t%s\n" % (secs(cyc), ic, rt, pc, L.name(frm) if frm else ""))

    with open(os.path.join(a.out, "ramexec.tsv"), "w") as o:
        o.write("# first execution outside ROM, grouped into runs of nearby new PCs\n")
        if first_ram:
            o.write("# first non-ROM instruction: %08X at %.6f s (instr %d), from %s\n" % (
                first_ram[0], secs(first_ram[3]), first_ram[2], L.name(first_ram[1])))
        o.write("# seconds\tinstr\tfrom_pc\tto_pc\tnew_pcs\tentered_from\n")
        for g in ram_groups:
            o.write("%.6f\t%d\t%08X\t%08X\t%d\t%s\n" % (secs(g[4]), g[3], g[0], g[1], g[2], L.name(g[5])))

    with open(os.path.join(a.out, "traps.tsv"), "w") as o:
        o.write("# A-traps: every one up to MACTRACE_TRAPS, then first of each; T = total counts\n")
        o.write("# seconds\tinstr\tpc\ttrap\tname\tcaller\n")
        for r in rows:
            if r[0] == "A":
                pc, w = int(r[1], 16), int(r[2], 16)
                o.write("%.6f\t%s\t%08X\t%04X\t%s\t%s\n" % (secs(r[4]), r[3], pc, w, trapname(traps, w), L.name(pc)))
        for r in rows:
            if r[0] == "T":
                w = int(r[1], 16)
                o.write("T\t%04X\t%s\t%s\n" % (w, trapname(traps, w), r[2]))
        be = [r for r in rows if r[0] == "B"]
        if be:
            o.write("# bus errors\n")
            for r in be:
                o.write("BERR\t%.6f\t%s\t%s\t%s\n" % (secs(r[4]), r[3], L.name(int(r[1], 16)), r[2]))

    hwraw = os.path.join(a.out, "hw.raw.tsv")
    if os.path.exists(hwraw):
        with open(hwraw) as f, open(os.path.join(a.out, "hwprobes.tsv"), "w") as o, \
                open(os.path.join(a.out, "slots.txt"), "w") as so:
            o.write("# seconds\tpc\troutine\trw\tbyte_addr\tregister\tvalue\trepeat\n")
            so.write("# slot-space accesses (declaration ROM end of each slot and super-slot space)\n")
            so.write("# seconds\tpc\troutine\trw\taddr\tvalue\trepeat\n")
            for line in f:
                if line.startswith("#"):
                    continue
                t, pc, rw, addr, data, mask, rep = line.split()
                pc, addr, data, mask = int(pc, 16), int(addr, 16), int(data, 16), int(mask, 16)
                bl = lanes(addr, data, mask)
                if (addr >> 24) == 0x50:
                    for ba, bv in bl:
                        o.write("%s\t%08X\t%s\t%s\t%08X\t%s\t%02X\t%s\n" % (t, pc, L.name(pc), rw, ba, io_name(ba), bv, rep))
                else:
                    if len(bl) == 4:
                        so.write("%s\t%08X\t%s\t%s\t%08X\t%08X\t%s\n" % (t, pc, L.name(pc), rw, addr, data, rep))
                    else:
                        for ba, bv in bl:
                            so.write("%s\t%08X\t%s\t%s\t%08X\t%02X\t%s\n" % (t, pc, L.name(pc), rw, ba, bv, rep))
    hwu = os.path.join(a.out, "hw.unique.tsv")
    if os.path.exists(hwu):
        with open(hwu) as f, open(os.path.join(a.out, "hwunique.tsv"), "w") as o:
            o.write("# every distinct (pc, rw, address): first time, first/last value, count\n")
            o.write("# first_s\tpc\troutine\trw\taddr\tregister\tfirst\tlast\tcount\n")
            ents = []
            for line in f:
                if line.startswith("#"):
                    continue
                key, t, d0, n, d1, mask = line.rstrip("\n").split("\t")
                pc, rw, addr = key.split()
                pc, addr, d0, d1, mask = int(pc, 16), int(addr, 16), int(d0, 16), int(d1, 16), int(mask, 16)
                b0 = lanes(addr, d0, mask)
                b1 = lanes(addr, d1, mask)
                ba = b0[0][0] if b0 else addr
                v0 = "".join("%02X" % v for _, v in b0)
                v1 = "".join("%02X" % v for _, v in b1)
                ents.append((float(t), "%s\t%08X\t%s\t%s\t%08X\t%s\t%s\t%s\t%s\n" % (
                    t, pc, L.name(pc), rw, ba, io_name(ba), v0, v1, n)))
            for _, s in sorted(ents, key=lambda e: e[0]):
                o.write(s)

    lomem = load_lomem()
    for b in glob.glob(os.path.join(a.out, "*lowmem-*.bin")):
        name = re.sub(r"^.*lowmem-", "", os.path.basename(b))[:-4]
        lowmem_txt(b, os.path.join(a.out, "lowmem-%s.txt" % name), lomem)
    print("routines: %d, flow lines: %d, first non-ROM: %s" % (
        len(seen), len(flow), "%08X" % first_ram[0] if first_ram else "-"))


if __name__ == "__main__":
    main()
