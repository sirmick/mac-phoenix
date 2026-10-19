#!/usr/bin/env python3
"""Where do patch_rom_32's patch sites land in another $067C ROM?

For every patch in src/core/rom_patches.cpp:patch_rom_32 (transcribed in
PATCHES below), resolve the site in the reference ROM (Quadra 800/650,
F1ACAD13 -- the one we boot), name it with Apple's ROM map
("ROUTINE+delta"), then look for it in the target ROM three ways:

  same     the same offset holds the same original bytes
  sig      the patch's own byte signature, searched over the whole ROM
  sym      Apple's map has the same routine in the target; routine+delta
           holds the same original bytes

and print one line per patch with the verdict. "-" means no way found it:
that patch has to be worked out by hand for the target ROM.

    tools/patch_sites.py                       # Quadra -> IIci
    tools/patch_sites.py --target PATH --target-map MacIIsiROM.lst.txt
"""

import argparse
import bisect
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAPS = os.path.join(HERE, "vendor", "68k-mac-rom-maps", "rom-maps")
ROMS = os.path.expanduser("~/storage/roms")

# name, how, arg, patched bytes, note
#   how = "off": arg is a fixed ROM offset
#   how = "sig": arg is (signature bytes, delta of the patched word from the match)
#   how = "trap": arg is the A-trap whose ROM routine gets patched (generic)
#   how = "rsrc": arg is (type, id) of a ROM resource (generic)
PATCHES = [
    ("reset shim (EMUL_OP_RESET+JMP $BA)", "off", 0x8c, 8, ""),
    ("skip GetHardwareInfo", "off", 0xc2, 4, ""),
    ("skip InitVIAs", "off", 0xc6, 30, ""),
    ("fake CPU type test", "off", 0x7c0, 4, ""),
    ("don't clear BootGlobs to end of RAM", "sig", (bytes([0x42,0x9a,0x36,0x0a,0x66,0xfa]), 2), 4, ""),
    ("InitMMU: no MMU / unknown CPU (040 form)", "sig", (bytes([0x0c,0x47,0x00,0x04,0x62,0x00,0xfd]), 0), 14, "1MB form"),
    ("InitMMU: no MMU / unknown CPU (030 form)", "sig", (bytes([0x0c,0x47,0x00,0x03,0x62,0x00,0xfe]), 0), 14, "512K form"),
    ("InitMMU: no RBV", "sig", (bytes([0x08,0x06,0x00,0x0d,0x67]), 4), 1, ""),
    ("InitMMU: don't init MMU", "sig", (bytes([0x0c,0x2e,0x00,0x01,0xff,0xe6,0x66,0x0c,0x4c,0xed,0x03,0x87,0xff,0xe8]), 6), 2, ""),
    ("XPRAM read (ROM10 form)", "sig", (bytes([0x26,0x4e,0x41,0xf9,0x50,0xf0,0x00,0x00,0x08,0x90,0x00,0x02]), 0), 4, ""),
    ("XPRAM read (ROM11 form)", "sig", (bytes([0x26,0x4e,0x08,0x92,0x00,0x02,0xea,0x59,0x02,0x01,0x00,0x07,0x00,0x01,0x00,0xb8]), 0), 4, ""),
    ("XPRAM read (ROM15 form)", "sig", (bytes([0x48,0xe7,0xe0,0x60,0x02,0x01,0x00,0x70,0x0c,0x01,0x00,0x20]), 0), 4, "1MB only"),
    ("ClkNoMem", "trap", 0xa053, 4, "generic"),
    ("PATCH_BOOT_GLOBS", "off", 0x10e, 4, ""),
    ("don't init SCC", "sig", (bytes([0x08,0x38,0x00,0x01,0x0d,0xd1,0x67,0x04]), 0), 2, ""),
    ("don't access $50F1A101", "off", 0x4232, 10, "ROM32 only (checks bytes)"),
    ("don't init IWM", "off", 0x9c0, 2, ""),
    ("don't init SCSI", "off", 0x9a0, 2, ""),
    ("don't init ASC", "sig", (bytes([0x26,0x68,0x00,0x30,0x12,0x00,0xeb,0x01]), 0), 2, ""),
    ("don't EnableExtCache", "off", 0x190, 4, ""),
    ("don't DisableIntSources", "off", 0x9f4c, 2, ""),
    ("fake SetupTimeK", "off", 0x800, 26, ""),
    ("fake handle at 0 -> ScratchMem", "off", 0xccaa, 6, ""),
    ("InitTimeMgr: no VIA write", "off", 0xb0e2, 6, ""),
    ("ModelID from $5FFFFFFC (ROM20)", "sig", (bytes([0x20,0x7c,0x5f,0xff,0xff,0xfc,0x72,0x07,0xc2,0x90]), 8), 8, ""),
    ("ModelID from $5FFFFFFC (ROM27/32)", "sig", (bytes([0x45,0xf9,0x5f,0xff,0xff,0xfc,0x20,0x12]), 6), 6, ""),
    ("don't probe NuBus slots", "sig", (bytes([0x45,0xfa,0x00,0x0a,0x42,0xa7,0x10,0x11]), 6), 6, "ROM10/11"),
    ("don't EnableOneSecInts", "off", 0x226, 10, "lea at $226..$22a"),
    ("don't EnableParityPatch/60HzInts", "off", 0x230, 10, "lea at $230..$234"),
    ("CompBootStack + FIX_MEMSIZE", "off", 0x490, 24, ""),
    ("fix_memsize2", "sig", (bytes([0x22,0x30,0x81,0xe2,0x0d,0xdc,0xff,0xba,0xd2,0xb0,0x81,0xe2,0x0d,0xdc,0xff,0xec,0x21,0xc1,0x1e,0xf8]), 16), 4, ""),
    ("skip .netBOOT (RTS)", "off", 0x1256, 2, ""),
    ("INSTALL_DRIVERS", "off", 0x1142, 2, ""),
    ("don't access SonyVars", "off", 0x1144, 14, ""),
    ("InitADB: no VIA write (512K/old form)", "off", 0xa8a8, 6, "else-branch"),
    ("InitADB: no VIA write (2)", "off", 0xa662, 18, "else-branch"),
    ("don't EnableSlotInts", "off", 0x2ee, 10, "lea at $2ee..$2f2"),
    ("GetDevBase: don't mangle fb base", "off", 0x5b78, 8, ""),
    ("frame base (1MB)", "sig", (bytes([0x22,0x78,0x0d,0xd8,0xd3,0xe9,0x00,0x08]), 0), 4, "1MB only"),
    ("don't write VIA2", "sig", (bytes([0x20,0x78,0x0c,0xec,0x11,0x7c,0x00,0x90]), 4), 2, ""),
    ("don't write VIA2 (ROM19/20)", "sig", (bytes([0x20,0x78,0x0c,0xec,0x11,0x7c,0x00,0x90,0x00,0x13,0x4e,0x75]), 4), 2, ""),
    ("BlockMove PTEST (040)", "sig", (bytes([0x20,0x5f,0x22,0x5f,0x0c,0x38,0x00,0x04,0x01,0x2f]), 4), 6, "1MB only"),
    ("SANE PTEST (040)", "sig", (bytes([0x0c,0x38,0x00,0x04,0x01,0x2f,0x6d,0x54,0x48,0xe7,0xf8,0x60]), 8), 10, "1MB only"),
    ("MemoryDispatch not unimpl", "sig", (bytes([0x30,0x3c,0xa8,0x9f,0xa7,0x46,0x30,0x3c,0xa0,0x5c,0xa2,0x47]), 10), 2, ""),
    (".EDisk scan", "rsrc", ("DRVR", 51), 0, "generic"),
    (".Sony/.Disk/.AppleCD drivers", "rsrc", ("DRVR", 4), 0, "generic"),
    ("SERD + serial drivers", "rsrc", ("SERD", 0), 0, "generic"),
    ("ADBOp", "trap", 0xa07c, 0, "generic"),
    ("InsTime", "trap", 0xa058, 0, "generic"),
    ("RmvTime", "trap", 0xa059, 0, "generic"),
    ("PrimeTime", "trap", 0xa05a, 0, "generic"),
    ("SCSIDispatch", "trap", 0xa815, 0, "generic"),
    ("vCheckLoad hook", "off", 0x1b8f4, 6, ""),
    ("PowerOff", "trap", 0xa05b, 0, "generic"),
    ("PutScrap", "trap", 0xa9fe, 0, "generic"),
    ("GetScrap", "trap", 0xa9fd, 0, "generic"),
    ("VIA level-1 handler: always 60Hz", "off", 0x9bc4, 10, ""),
    ("60Hz handler -> EMUL_OP_IRQ", "off", 0xa296, 10, ""),
]


def load_map(name):
    """Apple ROM map -> sorted [(offset, name)]."""
    out = []
    with open(os.path.join(MAPS, name)) as f:
        for line in f:
            p = line.split()
            if len(p) >= 2 and p[1].startswith("0x"):
                out.append((int(p[1], 16), p[0]))
    out.sort()
    return out


def sym_at(m, off):
    keys = [o for o, _ in m]
    i = bisect.bisect_right(keys, off) - 1
    if i < 0:
        return None, 0
    return m[i][1], off - m[i][0]


def sym_offset(m, name):
    for o, n in m:
        if n == name:
            return o
    return None


def find_trap(rom, trap):
    """find_rom_trap() from rom_patches.cpp."""
    bp = int.from_bytes(rom[0x22:0x26], "big")
    ofs = 0
    for base in (0xa800, 0xa000):
        t = base
        for _ in range(0x400):
            b = rom[bp]; bp += 1
            unimpl = False
            if b == 0x80:
                unimpl = True
            elif b == 0xff:
                ofs = int.from_bytes(rom[bp:bp + 4], "big"); bp += 4
            elif b & 0x80:
                add = (b & 0x7f) << 1
                if not add:
                    return None
                ofs += add
            else:
                add = (((b << 8) | rom[bp]) << 1) & 0xffff; bp += 1   # int16 in C
                if add & 0x8000:
                    add -= 0x10000
                if not add:
                    return None
                ofs += add
            if t == trap:
                return None if unimpl else ofs & 0xffffffff
            t += 1
    return None


def find_rsrc(rom, rtype, rid):
    """find_rom_resource() from rom_patches.cpp (offsets are ROM-relative)."""
    lp = int.from_bytes(rom[0x1a:0x1e], "big")
    p = int.from_bytes(rom[lp:lp + 4], "big")
    while p:
        data = int.from_bytes(rom[p + 12:p + 16], "big")
        t = rom[p + 16:p + 20].decode("latin-1")
        i = int.from_bytes(rom[p + 20:p + 22], "big", signed=True)
        if t == rtype and i == rid:
            return data
        p = int.from_bytes(rom[p + 8:p + 12], "big")
    return None


def find_all(rom, sig):
    out, i = [], rom.find(sig)
    while i >= 0:
        out.append(i)
        i = rom.find(sig, i + 1)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default=os.path.expanduser("~/roms/quadra.rom"))
    ap.add_argument("--ref-map", default="Quadra800ROM.lst.txt")
    ap.add_argument("--target", default=os.path.join(ROMS, "iici.rom"))
    ap.add_argument("--target-map", default="MacIIciROM.lst.txt")
    ap.add_argument("--ctx", type=int, default=8, help="bytes compared at a site")
    ap.add_argument("-v", "--verbose", action="store_true", help="bytes of the misses")
    a = ap.parse_args()

    ref, tgt = open(a.ref, "rb").read(), open(a.target, "rb").read()
    rmap, tmap = load_map(a.ref_map), load_map(a.target_map)

    def same_bytes(ro, to, n):
        return ro is not None and to is not None and to + n <= len(tgt) \
            and ref[ro:ro + n] == tgt[to:to + n]

    counts = {}
    print("%-40s %-24s %-8s %-26s %s" % ("patch", "quadra site", "how", "target site", "note"))
    for name, how, arg, nbytes, note in PATCHES:
        n = max(nbytes, a.ctx)
        rsite = tsite = cand = None
        verdict = "-"
        if how in ("trap", "rsrc"):
            f = (lambda r: find_trap(r, arg)) if how == "trap" else (lambda r: find_rsrc(r, *arg))
            rsite, tsite = f(ref), f(tgt)
            verdict = "generic" if tsite else "-"
        else:
            if how == "off":
                rsite = arg
            else:
                sig, delta = arg
                hits = find_all(ref, sig)
                rsite = hits[0] + delta if hits else None
            if rsite is not None and same_bytes(rsite, rsite, n):
                tsite, verdict = rsite, "same"
            if tsite is None and how == "sig":
                hits = find_all(tgt, arg[0])
                if hits:
                    tsite, verdict = hits[0] + arg[1], "sig" + ("" if len(hits) == 1 else "x%d" % len(hits))
            if tsite is None and rsite is not None:
                s, d = sym_at(rmap, rsite)
                to = sym_offset(tmap, s) if s else None
                if to is not None and same_bytes(rsite, to + d, n):
                    tsite, verdict = to + d, "sym"
                elif to is not None and a.verbose:
                    cand = to + d
        if a.verbose and verdict == "-" and rsite is not None:
            print("    quadra %05x: %s" % (rsite, ref[rsite:rsite + n].hex(" ")))
            if cand is not None:
                print("    iici?  %05x: %s  (same routine+delta)" % (cand, tgt[cand:cand + n].hex(" ")))
            if rsite + n <= len(tgt):
                print("    iici=  %05x: %s  (same offset)" % (rsite, tgt[rsite:rsite + n].hex(" ")))
        rs = "-" if rsite is None else "%05x %s+%x" % ((rsite,) + sym_at(rmap, rsite)) if sym_at(rmap, rsite)[0] else "%05x" % rsite
        ts = "-" if tsite is None else "%05x %s+%x" % ((tsite,) + sym_at(tmap, tsite)) if sym_at(tmap, tsite)[0] else "%05x" % tsite
        print("%-40s %-24s %-8s %-26s %s" % (name[:40], rs[:24], verdict, ts[:26], note))
        counts[verdict.split("x")[0]] = counts.get(verdict.split("x")[0], 0) + 1
    print("\n" + ", ".join("%s: %d" % kv for kv in sorted(counts.items())))


if __name__ == "__main__":
    sys.exit(main())
