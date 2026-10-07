#!/usr/bin/env python3
"""Read a guest memory snapshot (POST /api/snapshot) and describe it.

    tools/memmap/memmap.py ~/storage/snapshots/<name> [lowmem|traps|heap|all]

Vocabulary: docs/executor/MEMORY_MAP.md. Reference for the Executor
System reconstruction: everything printed here is what Executor's C++
has to produce by itself.
"""
import json
import struct
import sys
from pathlib import Path

LOWMEM = [  # (name, address, size)
    ("MemTop", 0x108, 4), ("BufPtr", 0x10C, 4), ("HeapEnd", 0x114, 4),
    ("TheZone", 0x118, 4), ("UTableBase", 0x11C, 4), ("ApplLimit", 0x130, 4),
    ("EventQueue", 0x14A, 10), ("VBLQueue", 0x160, 10), ("Ticks", 0x16A, 4),
    ("UnitNtryCnt", 0x1D2, 2), ("Time", 0x20C, 4), ("BootDrive", 0x210, 2),
    ("ROM85", 0x28E, 2), ("SysZone", 0x2A6, 4), ("ApplZone", 0x2AA, 4),
    ("ROMBase", 0x2AE, 4), ("RAMBase", 0x2B2, 4), ("ExpandMem", 0x2B6, 4),
    ("DrvQHdr", 0x308, 10), ("Lo3Bytes", 0x31A, 4), ("MinStack", 0x31E, 4),
    ("DefltStack", 0x322, 4), ("FCBSPtr", 0x34E, 4), ("VCBQHdr", 0x356, 10),
    ("FSQHdr", 0x360, 10), ("ScrnBase", 0x824, 4), ("MainDevice", 0x8A4, 4),
    ("DeviceList", 0x8A8, 4), ("CurApRefNum", 0x900, 2), ("CurrentA5", 0x904, 4),
    ("CurStackBase", 0x908, 4), ("CurApName", 0x910, 32), ("CurJTOffset", 0x934, 2),
    ("WindowList", 0x9D6, 4), ("TopMapHndl", 0xA50, 4), ("SysMapHndl", 0xA54, 4),
    ("SysMap", 0xA58, 2), ("CurMap", 0xA5A, 2), ("HWCfgFlags", 0xB22, 2),
    ("MBarHeight", 0xBAA, 2), ("MMU32Bit", 0xCB2, 1), ("TheGDevice", 0xCC8, 4),
]

ZONE_HDR = 52  # bkLim..allocPtr, 32-bit Memory Manager


class Snap:
    def __init__(self, path):
        self.dir = Path(path).expanduser()
        self.meta = json.loads((self.dir / "meta.json").read_text())
        self.ram = (self.dir / "ram.bin").read_bytes()
        rom = self.dir / "rom.bin"
        self.rom = rom.read_bytes() if rom.exists() else b""
        self.rom_base = int(self.meta["rom_base"], 16)

    def u8(self, a): return self.ram[a]
    def u16(self, a): return struct.unpack_from(">H", self.ram, a)[0]
    def u32(self, a): return struct.unpack_from(">I", self.ram, a)[0]
    def in_ram(self, a): return 0 <= a < len(self.ram)
    def in_rom(self, a): return self.rom_base <= a < self.rom_base + len(self.rom)

    def pstr(self, a):
        n = self.u8(a)
        return self.ram[a + 1:a + 1 + n].decode("mac_roman", "replace")

    def zone(self, z):
        bklim = self.u32(z)
        return {"start": z, "data": z + ZONE_HDR, "end": bklim,
                "free": self.u32(z + 12), "moreMast": self.u16(z + 20)}

    def blocks(self, z):
        """Walk a 32-bit zone: 12-byte headers, tag in the top 2 bits."""
        zi = self.zone(z)
        a = zi["data"]
        while a < zi["end"] and self.in_ram(a + 12):
            tag = self.u8(a) >> 6
            size = self.u32(a + 4)
            if size < 12 or a + size > zi["end"]:
                yield {"addr": a, "kind": "BAD", "size": size}
                return
            kind = ("free", "ptr", "handle", "?")[tag]
            b = {"addr": a, "kind": kind, "size": size,
                 "flags": self.u8(a + 1), "corr": self.u8(a + 3)}
            if kind == "handle":
                b["mp"] = z + struct.unpack_from(">i", self.ram, a + 8)[0]
            yield b
            a += size

    def zones(self):
        """Named zones we know of, for classifying addresses."""
        out = {}
        for name, lm in (("SysZone", 0x2A6), ("ApplZone", 0x2AA), ("TheZone", 0x118)):
            z = self.u32(lm)
            if self.in_ram(z + ZONE_HDR):
                out.setdefault(z, name)
        return {z: (name, self.zone(z)) for z, name in out.items()}

    def where(self, a, zones):
        if self.in_rom(a):
            return f"ROM+{a - self.rom_base:05x}"
        for z, (name, zi) in zones.items():
            if zi["start"] <= a < zi["end"]:
                return name
        if a < 0x2800:
            return "lowmem"
        return "RAM?"


def cmd_lowmem(s):
    for name, addr, size in LOWMEM:
        raw = s.ram[addr:addr + size]
        if name == "CurApName":
            val = repr(s.pstr(addr))
        elif size == 1:
            val = f"${raw[0]:02x}"
        elif size == 2:
            val = f"${s.u16(addr):04x}"
        elif size == 4:
            val = f"${s.u32(addr):08x}"
        else:
            val = raw.hex()
        print(f"  {addr:04x}  {name:<14} {val}")


def cmd_traps(s):
    zones = s.zones()
    for title, base, count, prefix in (("OS", 0x400, 256, 0xA000),
                                       ("Toolbox", 0xE00, 1024, 0xA800)):
        tally, unimpl = {}, None
        ents = [s.u32(base + 4 * i) for i in range(count)]
        # The most common entry is the "unimplemented" handler.
        unimpl = max(set(ents), key=ents.count)
        for e in ents:
            w = "unimpl" if e == unimpl else s.where(e, zones).split("+")[0]
            tally[w] = tally.get(w, 0) + 1
        print(f"{title} trap table @ ${base:04x}: {count} entries, "
              f"unimplemented handler ${unimpl:08x}")
        print("   " + ", ".join(f"{k}: {v}" for k, v in sorted(tally.items())))
    print("\nNon-ROM entries (patched):")
    for base, count, prefix in ((0x400, 256, 0xA000), (0xE00, 1024, 0xA800)):
        ents = [s.u32(base + 4 * i) for i in range(count)]
        unimpl = max(set(ents), key=ents.count)
        for i, e in enumerate(ents):
            if e != unimpl and not s.in_rom(e):
                print(f"  ${prefix + i:04X} -> ${e:08x}  {s.where(e, zones)}")


def cmd_heap(s):
    for z, (name, zi) in s.zones().items():
        counts, sizes = {}, {}
        for b in s.blocks(z):
            counts[b["kind"]] = counts.get(b["kind"], 0) + 1
            sizes[b["kind"]] = sizes.get(b["kind"], 0) + b["size"]
        print(f"{name} @ ${z:08x}..${zi['end']:08x} "
              f"({(zi['end'] - z) // 1024} KB, zcbFree {zi['free']}, moreMast {zi['moreMast']})")
        for k in sorted(counts):
            print(f"   {k:<7} {counts[k]:6} blocks {sizes[k] // 1024:8} KB")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    s = Snap(sys.argv[1])
    what = sys.argv[2] if len(sys.argv) > 2 else "all"
    print(f"# {s.dir.name}: {s.meta['backend']}, {s.meta['boot_phase']}, "
          f"RAM {len(s.ram) >> 20} MB, ROM @ ${s.rom_base:08x}")
    for name, fn in (("lowmem", cmd_lowmem), ("heap", cmd_heap), ("traps", cmd_traps)):
        if what in (name, "all"):
            print(f"\n## {name}")
            fn(s)


if __name__ == "__main__":
    main()
