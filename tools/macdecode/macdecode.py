#!/usr/bin/env python3
"""Decode a Mac RAM snapshot using multiversal as the database.

    tools/macdecode/macdecode.py SNAPSHOT [section ...] [--json OUT] [--defs DIR]

Sections: summary lowmem zones resources traps coverage placeholders
(default: summary lowmem zones traps coverage).
--serve [--port 8765] serves the same decode as HTML, side by side with
multiversal, reloading the defs whenever a YAML file changes.

SNAPSHOT is a directory written by POST /api/snapshot (ram.bin, rom.bin,
meta.json). Names, types and addresses come from
src/executor/multiversal/defs/*.yaml; anything the decoder can't name is
reported so it can be added there (see `placeholders`). Vocabulary:
docs/executor/MEMORY_MAP.md.
"""
import argparse
import bisect
import json
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mvdb import DB, DEFAULT_DEFS  # noqa: E402
import learned  # noqa: E402

ZONE_HDR = 52          # 32-bit Zone header (bkLim .. allocPtr)
BLK_HDR = 12           # 32-bit block header
OS_TABLE = (0x400, 256)
TOOL_TABLE = (0xE00, 1024)
LOWMEM_END = 0x2000    # SysZone starts here on 7.5.5; lowmem lives below


class Snapshot:
    def __init__(self, path):
        self.dir = Path(path).expanduser()
        self.meta = json.loads((self.dir / "meta.json").read_text())
        self.ram = (self.dir / "ram.bin").read_bytes()
        rom = self.dir / "rom.bin"
        self.rom = rom.read_bytes() if rom.exists() else b""
        self.rom_base = int(self.meta["rom_base"], 16)

    def ok(self, a, n=1): return 0 <= a and a + n <= len(self.ram)
    def u8(self, a): return self.ram[a]
    def u16(self, a): return struct.unpack_from(">H", self.ram, a)[0]
    def u32(self, a): return struct.unpack_from(">I", self.ram, a)[0]
    def s32(self, a): return struct.unpack_from(">i", self.ram, a)[0]
    def in_rom(self, a): return self.rom_base <= a < self.rom_base + len(self.rom)

    def pstr(self, a, maxlen=255):
        n = min(self.u8(a), maxlen)
        return self.ram[a + 1:a + 1 + n].decode("mac_roman", "replace")

    def uint(self, a, size, signed=False):
        v = int.from_bytes(self.ram[a:a + size], "big")
        if signed and v >= 1 << (8 * size - 1):
            v -= 1 << (8 * size)
        return v


# ── Memory Manager ────────────────────────────────────────────────────

class Zone:
    def __init__(self, snap, addr, name):
        self.addr, self.name = addr, name
        self.end = snap.u32(addr)
        self.free = snap.u32(addr + 12)
        self.more_mast = snap.u16(addr + 20)
        self.blocks, self.error = [], None
        a = addr + ZONE_HDR
        while a < self.end:
            if not snap.ok(a, BLK_HDR):
                self.error = f"block header outside RAM at ${a:08x}"
                break
            size = snap.u32(a + 4)
            if size < BLK_HDR or a + size > self.end:
                self.error = f"bad block size {size} at ${a:08x}"
                break
            tag = snap.u8(a) >> 6
            b = {"addr": a, "data": a + BLK_HDR, "size": size,
                 "kind": ("free", "ptr", "handle", "?")[tag],
                 "flags": snap.u8(a + 1), "corr": snap.u8(a + 3)}
            if b["kind"] == "handle":
                b["mp"] = addr + snap.s32(a + 8)
            elif b["kind"] == "ptr":
                b["owner"] = snap.u32(a + 8)
            self.blocks.append(b)
            a += size
        self.starts = [b["addr"] for b in self.blocks]

    def block_at(self, a):
        i = bisect.bisect_right(self.starts, a) - 1
        if i >= 0:
            b = self.blocks[i]
            if b["addr"] <= a < b["addr"] + b["size"]:
                return b
        return None


def find_zones(snap):
    """Zones named by lowmem, plus any other well-formed zone in RAM.

    A zone is recognised by its header: bkLim above it, and the block
    chain from heapData landing exactly on bkLim. The Process Manager
    heap and each app's partition heap are found this way.
    """
    named = {}
    for name, lm in (("SysZone", 0x2A6), ("ApplZone", 0x2AA), ("TheZone", 0x118)):
        z = snap.u32(lm)
        if snap.ok(z, ZONE_HDR):
            named.setdefault(z, name)

    ram = np.frombuffer(snap.ram, dtype=">u4")
    n = len(ram)
    pos = np.arange(n, dtype=np.int64) * 4
    bklim = ram.astype(np.int64)
    # first block header at pos+52: size word at pos+56
    first_size = np.zeros(n, dtype=np.int64)
    first_size[:n - 14] = ram[14:].astype(np.int64)
    first_tag = np.zeros(n, dtype=np.int64)
    first_tag[:n - 13] = ram[13:].astype(np.int64) >> 30
    cand = ((bklim > pos + ZONE_HDR + BLK_HDR) & (bklim <= len(snap.ram)) &
            (first_size >= BLK_HDR) & (first_size <= bklim - pos - ZONE_HDR) &
            (first_tag < 3))
    zones = {}
    for a in pos[cand]:
        a = int(a)
        z = Zone(snap, a, named.get(a, f"zone@{a:08x}"))
        if z.error is None and len(z.blocks) >= 2:
            zones[a] = z
    for a, name in named.items():
        if a not in zones:
            zones[a] = Zone(snap, a, name)
    # A zone found by scanning that sits inside a block of another zone is
    # a sub-heap (an app partition inside the Process Manager heap).
    for z in zones.values():
        z.parent = None
        for o in zones.values():
            if o is not z and o.addr < z.addr < o.end:
                b = o.block_at(z.addr)
                if b and (z.parent is None or o.addr > z.parent.addr):
                    z.parent = o
    return dict(sorted(zones.items()))


# ── Resource Manager ──────────────────────────────────────────────────

def fcb_name(snap, refnum):
    fcbs = snap.u32(0x34E)
    if not snap.ok(fcbs, 2) or refnum <= 0 or refnum >= snap.u16(fcbs):
        return None
    return snap.pstr(fcbs + refnum + 62, 31)


def fcb_type(snap, refnum):
    fcbs = snap.u32(0x34E)
    if not snap.ok(fcbs, 2) or refnum <= 0 or refnum >= snap.u16(fcbs):
        return None
    return snap.ram[fcbs + refnum + 50:fcbs + refnum + 54].decode("mac_roman", "replace")


APP_TYPES = ("APPL", "FNDR", "appe", "APPC", "APPD", "dfil")


def resource_maps(snap):
    """Walk TopMapHndl → … → SysMap. Returns maps with their resources."""
    maps, h, seen = [], snap.u32(0xA50), set()
    while h and snap.ok(h, 4) and h not in seen:
        seen.add(h)
        rmap = parse_map(snap, h)
        if not rmap:
            break
        rmap["chained"] = True
        maps.append(rmap)
        h = snap.u32(rmap["addr"] + 16)
    return maps


def looks_like_map(snap, m, size):
    """Resource map header: data offset 256, valid refnum, sane offsets."""
    if size < 30 or not snap.ok(m, 30) or snap.u32(m) != 256:
        return False
    tl, nl = snap.u16(m + 24), snap.u16(m + 26)
    refnum = snap.u16(m + 20)
    return 28 <= tl < size and tl <= nl <= size and fcb_name(snap, refnum) is not None


def parse_map(snap, h):
    m = snap.u32(h)
    if not snap.ok(m, 28):
        return None
    if True:
        refnum = snap.u16(m + 20)
        tl = m + snap.u16(m + 24)
        nl = m + snap.u16(m + 26)
        rmap = {"handle": h, "addr": m, "refnum": refnum,
                "file": fcb_name(snap, refnum), "ftype": fcb_type(snap, refnum),
                "resources": []}
        ntypes = (snap.u16(tl) + 1) & 0xFFFF
        for i in range(ntypes if ntypes < 2000 else 0):
            te = tl + 2 + 8 * i
            rtype = snap.ram[te:te + 4].decode("mac_roman", "replace")
            count = snap.u16(te + 4) + 1
            refs = tl + snap.u16(te + 6)
            for j in range(count):
                r = refs + 12 * j
                rid = struct.unpack_from(">h", snap.ram, r)[0]
                name_off = snap.u16(r + 2)
                rh = snap.u32(r + 8)
                rmap["resources"].append({
                    "type": rtype, "id": rid, "attrs": snap.u8(r + 4),
                    "name": snap.pstr(nl + name_off) if name_off != 0xFFFF else None,
                    "handle": rh})
    return rmap


# ── The world: everything needed to name an address ──────────────────

def load_db(defs=DEFAULT_DEFS, learned_path=learned.DEFAULT_LEARNED):
    """Multiversal with the learned overlay applied."""
    return learned.apply(DB(defs), learned_path)


def load_origins(snap, zones, disk=None):
    """Match the System heap (and heaps inside it) against the boot disk's
    resources. disk defaults to the first disk recorded in meta.json."""
    disk = disk or (snap.meta.get("disks") or [None])[0]
    if not disk or not Path(disk).expanduser().exists():
        return None
    from origins import Origins
    from rsrc import Disk
    d = Disk(disk, snap.dir / "disk-cache")
    try:
        files = d.system_files()
    finally:
        d.close()
    sys_zone = zones.get(snap.u32(0x2A6))
    if not sys_zone:
        return None

    def is_address(v):
        return snap.in_rom(v) or 0x2000 <= v < len(snap.ram)
    o = Origins(snap.ram, [(sys_zone.addr, sys_zone.end)], files, is_address)
    o.disk = disk
    return o


class World:
    def __init__(self, snap, db, origins=None, zones=None):
        self.snap, self.db = snap, db
        self.origins = origins
        self.zones = zones if zones is not None else find_zones(snap)
        self.maps = resource_maps(snap)
        # Maps of other processes aren't on the current chain: find them
        # by their header in every heap's handle blocks.
        chained = {m["handle"] for m in self.maps}
        for z in self.zones.values():
            for b in z.blocks:
                if b["kind"] == "handle" and b["mp"] not in chained and \
                        snap.ok(b["mp"], 4) and snap.u32(b["mp"]) == b["data"] and \
                        looks_like_map(snap, b["data"], b["size"] - BLK_HDR - b["corr"]):
                    rmap = parse_map(snap, b["mp"])
                    if rmap:
                        rmap["chained"] = False
                        self.maps.append(rmap)
                        chained.add(b["mp"])
        self.rsrc_by_mp = {}
        for m in self.maps:
            for r in m["resources"]:
                if r["handle"]:
                    self.rsrc_by_mp[r["handle"]] = (m, r)
        self.lowmem_by_addr = {g["address"]: g for g in db.lowmem}
        # Name each heap after the application whose map lives in it;
        # fall back to any file's map.
        for apps_only in (True, False):
            for m in self.maps:
                if apps_only != (m["ftype"] in APP_TYPES) or not m["file"]:
                    continue
                z = self.zone_chain(m["addr"])
                if z and z.name.startswith("zone@"):
                    z.name = f"heap:{m['file']}"
                elif z and z.name in ("ApplZone", "TheZone"):
                    z.name = f"{z.name}:{m['file']}"

    def zone_chain(self, a):
        """Innermost zone containing a."""
        best = None
        for z in self.zones.values():
            if z.addr <= a < z.end and (best is None or z.addr > best.addr):
                best = z
        return best

    def where(self, a):
        s = self.snap
        if a == 0:
            return "nil"
        if s.in_rom(a):
            return f"ROM+{a - s.rom_base:x}"
        if a < LOWMEM_END:
            for t, (base, n) in (("OS trap table", OS_TABLE), ("Toolbox trap table", TOOL_TABLE)):
                if base <= a < base + 4 * n:
                    return f"{t}[{(a - base) // 4}]"
            g = self.lowmem_by_addr.get(a)
            return f"lowmem {g['name']}" if g else f"lowmem ${a:x}"
        z = self.zone_chain(a)
        if not z:
            return "RAM" if s.ok(a) else "unmapped"
        if a < z.addr + ZONE_HDR:
            return f"{z.name} header+{a - z.addr}"
        b = z.block_at(a)
        if not b:
            return f"{z.name} (between blocks)"
        off = a - b["data"]
        desc = f"{z.name} {b['kind']} ${b['data']:08x}"
        if b["kind"] == "handle":
            hit = self.rsrc_by_mp.get(b["mp"])
            if hit:
                m, r = hit
                return f"{z.name} rsrc '{r['type']}' {r['id']} ({m['file'] or m['refnum']})" + \
                    (f" +{off:x}" if off else "")
        desc += f" +{off:x}" if off else ""
        o = self.origin(a)
        return f"{desc} ← {o}" if o else desc

    def follow(self, a, depth=0):
        """Skip loader glue: a JMP island, or a come-from stub's header
        (BRA.S *+8; JMP old), to reach the code that does the work."""
        s = self.snap
        if depth > 4 or not s.ok(a, 8):
            return a
        op = s.u16(a)
        if op == 0x4EF9:                                  # JMP abs.L
            t = s.u32(a + 2)
            if s.ok(t, 2) and t != a:
                return self.follow(t, depth + 1)
        if op == 0x6006 and s.u16(a + 2) == 0x4EF9:       # BRA.S +6 over JMP old
            return self.follow(a + 8, depth + 1)
        return a

    def origin(self, a):
        """Which resource on disk this RAM came from, if we can tell."""
        if self.snap.in_rom(a):
            return "ROM"
        if not self.origins:
            return None
        return self.origins.describe(a) or (
            self.origins.describe(self.follow(a)) if self.follow(a) != a else None)


# ── Sections ──────────────────────────────────────────────────────────

def fmt_value(world, t, a, depth=0):
    """Render the value of type t at address a, multiversal-typed."""
    db, s = world.db, world.snap
    size = db.size(t)
    if size is None or not s.ok(a, size or 1):
        return "?"
    if db.is_pointer(t):
        v = s.u32(a)
        return f"${v:08x} → {world.where(v)}" if v else "nil"
    arr = db.array(t)
    if arr:
        et, n = arr
        if db.resolve(et) in ("uint8_t", "unsigned char", "char") and t.startswith("Str"):
            return repr(s.pstr(a, n - 1))
        es = db.size(et)
        raw = s.ram[a:a + size]
        if es == 1 and 0 < raw[0] < n and all(32 <= c < 127 for c in raw[1:1 + raw[0]]):
            return f"{raw.hex()} = {s.pstr(a, n - 1)!r}"
        if es in (1, 2, 4) and not db.fields(et) and not db.is_pointer(et):
            return raw.hex()
        if depth < 1:
            return "[" + ", ".join(fmt_value(world, et, a + i * es, depth + 1) for i in range(n)) + "]"
        return s.ram[a:a + size].hex()
    fields = db.fields(t)
    if fields is not None:
        if depth > 1:
            return s.ram[a:a + size].hex()
        return "{" + ", ".join(f"{f.name}: {fmt_value(world, f.type, a + f.offset, depth + 1)}"
                               for f in fields) + "}"
    if t == "Point" or db.resolve(t) == "Point":
        return f"(v {s.uint(a, 2, True)}, h {s.uint(a + 2, 2, True)})"
    if size in (1, 2, 4, 8):
        v = s.uint(a, size, db.signed(t))
        return f"{v}" if abs(v) < 10 else f"{v} (${s.uint(a, size):0{2 * size}x})"
    return s.ram[a:a + size].hex()


def sec_summary(world, out):
    s = world.snap
    out.append(f"# {s.dir.name}: {s.meta.get('backend')}, {s.meta.get('boot_phase')}, "
               f"RAM {len(s.ram) >> 20} MB, ROM @ ${s.rom_base:08x} ({len(s.rom) >> 10} KB)")
    out.append(f"  multiversal: {len(world.db.lowmem)} lowmem globals, "
               f"{len(world.db.traps)} named trap slots ({world.db.defs})")
    out.append(f"  zones: {len(world.zones)}, resource maps: {len(world.maps)}, "
               f"loaded resources: {sum(1 for m in world.maps for r in m['resources'] if r['handle'])}")


def sec_lowmem(world, out):
    for g in world.db.lowmem:
        out.append(f"  {g['address']:04x} {g['name']:<18} {g['type']:<14} "
                   f"{fmt_value(world, g['type'], g['address'])}")


def sec_zones(world, out):
    for z in world.zones.values():
        kinds = {}
        for b in z.blocks:
            k = kinds.setdefault(b["kind"], [0, 0])
            k[0] += 1
            k[1] += b["size"]
        parent = f" in {z.parent.name}" if z.parent else ""
        out.append(f"  {z.name:<16} ${z.addr:08x}..${z.end:08x} {(z.end - z.addr) >> 10:6} KB{parent}"
                   f"  zcbFree {z.free}, moreMast {z.more_mast}"
                   + (f"  ERROR {z.error}" if z.error else ""))
        out.append("      " + ", ".join(f"{k} {v[0]} ({v[1] >> 10} KB)" for k, v in sorted(kinds.items())))


def sec_resources(world, out):
    for m in world.maps:
        loaded = [r for r in m["resources"] if r["handle"]]
        out.append(f"  map refnum {m['refnum']} {m['file']!r} ({m['ftype']}): {len(m['resources'])} resources, "
                   f"{len(loaded)} loaded" + ("" if m["chained"] else
                   f"  [not on current chain; in {world.where(m['addr'])}]"))
        for r in loaded:
            mp = r["handle"]
            data = world.snap.u32(mp) if world.snap.ok(mp, 4) else 0
            out.append(f"      '{r['type']}' {r['id']:6} {r['name'] or '':<24} "
                       f"handle ${mp:08x} → {world.where(data) if data else 'purged'}")


def trap_entries(world):
    s = world.snap
    rows = []
    for table, (base, n) in (("os", OS_TABLE), ("tool", TOOL_TABLE)):
        ents = [s.u32(base + 4 * i) for i in range(n)]
        unimpl = max(set(ents), key=ents.count)
        for i, e in enumerate(ents):
            word = (0xA000 | i) if table == "os" else (0xA800 | i)
            origin = None if e == unimpl else (world.origin(e) or (
                "app heap" if (world.zone_chain(e) and world.zone_chain(e).parent) else None))
            rows.append({"table": table, "index": i, "trap": word,
                         "names": world.db.traps.get((table, i), []),
                         "addr": e, "unimplemented": e == unimpl,
                         "where": "unimplemented" if e == unimpl else world.where(e),
                         "origin": origin, "origin_group": origin_group(origin)})
    return rows


def origin_group(origin):
    """Coarse bucket for an origin string: ROM, System 'lpch', <INIT file>…"""
    if not origin:
        return "unknown"
    if origin in ("ROM", "app heap"):
        return origin
    parts = origin.split(" '")
    rtype = parts[1].split("'")[0] if len(parts) > 1 else "?"
    return f"{parts[0]} '{rtype}'" if parts[0] == "System" else parts[0]


def sec_traps(world, out):
    rows = trap_entries(world)
    for table in ("os", "tool"):
        t = [r for r in rows if r["table"] == table]
        tally = {}
        for r in t:
            k = "unimplemented" if r["unimplemented"] else r["where"].split(" ")[0].split("+")[0]
            tally[k] = tally.get(k, 0) + 1
        out.append(f"  {table}: " + ", ".join(f"{k} {v}" for k, v in sorted(tally.items())))
    groups = {}
    for r in rows:
        if not r["unimplemented"]:
            groups[r["origin_group"]] = groups.get(r["origin_group"], 0) + 1
    out.append("  by origin: " + ", ".join(f"{k} {v}" for k, v in
                                          sorted(groups.items(), key=lambda kv: -kv[1])))
    out.append("  patched (not ROM):")
    for r in rows:
        if not r["unimplemented"] and not world.snap.in_rom(r["addr"]):
            name = "/".join(r["names"]) or "?"
            out.append(f"    ${r['trap']:04X} {name:<28} ${r['addr']:08x}  {r['origin'] or r['where']}")
    unnamed = [r for r in rows if not r["unimplemented"] and not r["names"]]
    out.append(f"  implemented but unnamed in multiversal: {len(unnamed)} "
               f"(e.g. {', '.join(f'${r['trap']:04X}' for r in unnamed[:12])})")


def uncovered_runs(world):
    """Nonzero lowmem bytes no multiversal global covers."""
    covered = bytearray(LOWMEM_END)
    covered[0:0x100] = b"\1" * 0x100  # CPU vectors
    for base, n in (OS_TABLE, TOOL_TABLE):
        covered[base:base + 4 * n] = b"\1" * (4 * n)
    for g in world.db.lowmem:
        size = world.db.size(g["type"]) or 1
        a = g["address"]
        if a < LOWMEM_END:
            covered[a:min(a + size, LOWMEM_END)] = b"\1" * (min(a + size, LOWMEM_END) - a)
    runs, a = [], 0
    ram = world.snap.ram
    while a < LOWMEM_END:
        if not covered[a] and ram[a]:
            b = a
            while b < LOWMEM_END and not covered[b] and ram[b]:
                b += 1
            # merge across short zero gaps so one structure stays one run
            while b < LOWMEM_END and not covered[b]:
                c = b
                while c < LOWMEM_END and not covered[c] and not ram[c] and c - b < 4:
                    c += 1
                if c < LOWMEM_END and not covered[c] and ram[c] and c - b < 4:
                    b = c
                    while b < LOWMEM_END and not covered[b] and ram[b]:
                        b += 1
                else:
                    break
            # widen to even bounds only into bytes nobody else owns
            if a & 1 and not covered[a - 1]:
                a -= 1
            if b & 1 and b < LOWMEM_END and not covered[b]:
                b += 1
            runs.append((a, b))
            a = b
        else:
            a += 1
    return runs, covered


def sec_coverage(world, out):
    runs, covered = uncovered_runs(world)
    n = sum(e - s for s, e in runs)
    known = sum(covered[0x100:LOWMEM_END])
    out.append(f"  lowmem $100..${LOWMEM_END:x}: {known} bytes named by multiversal "
               f"(incl. trap tables), {n} nonzero bytes unnamed in {len(runs)} runs")
    for s_, e in runs:
        v = world.snap.u32(s_) if e - s_ == 4 else None
        hint = f"  → {world.where(v)}" if v and e - s_ == 4 else ""
        out.append(f"    ${s_:04x}..${e:04x} ({e - s_:3} bytes) {world.snap.ram[s_:min(e, s_ + 16)].hex()}{hint}")


def sec_placeholders(world, out):
    """Entries for the `lowmem:` list in learned.yaml; rename/retype as we learn."""
    runs, _ = uncovered_runs(world)
    src = world.snap.dir.name
    for s_, e in runs:
        n = e - s_
        t = "LONGINT" if n == 4 else ("INTEGER" if n == 2 else f"Byte[{n}]")
        if n == 4 and world.where(world.snap.u32(s_)) not in ("RAM", "unmapped"):
            t = "Ptr"
        out.append(f"  - address: 0x{s_:X}\n    name: Unknown_{s_:04X}\n    type: {t}\n"
                   f"    status: placeholder\n    source: nonzero, unnamed in {src}\n")


SECTIONS = {"summary": sec_summary, "lowmem": sec_lowmem, "zones": sec_zones,
            "resources": sec_resources, "traps": sec_traps,
            "coverage": sec_coverage, "placeholders": sec_placeholders}


def to_json(world):
    s = world.snap
    return {
        "snapshot": str(s.dir), "meta": s.meta,
        "lowmem": [{"name": g["name"], "address": g["address"], "type": g["type"],
                    "bytes": s.ram[g["address"]:g["address"] + (world.db.size(g["type"]) or 0)].hex(),
                    "value": fmt_value(world, g["type"], g["address"])} for g in world.db.lowmem],
        "zones": [{"name": z.name, "addr": z.addr, "end": z.end, "free": z.free,
                   "parent": z.parent.name if z.parent else None, "error": z.error,
                   "blocks": z.blocks} for z in world.zones.values()],
        "resource_maps": world.maps,
        "traps": trap_entries(world),
        "unnamed_lowmem": uncovered_runs(world)[0],
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("snapshot")
    ap.add_argument("sections", nargs="*")
    ap.add_argument("--defs", default=str(DEFAULT_DEFS))
    ap.add_argument("--learned", default=str(learned.DEFAULT_LEARNED),
                    help="overlay of names/types learned so far (default: tools/macdecode/learned.yaml)")
    ap.add_argument("--disk", help="boot disk image for origin matching (default: from meta.json)")
    ap.add_argument("--json", metavar="OUT", help="also write the full decode as JSON")
    ap.add_argument("--serve", action="store_true", help="serve an HTML view instead of printing")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8765)
    args = ap.parse_args()
    if args.serve:
        import web
        web.serve(args.snapshot, args.defs, args.learned, args.disk, args.host, args.port)
        return
    for sec in args.sections:
        if sec not in SECTIONS:
            ap.error(f"unknown section {sec!r}; choose from {', '.join(SECTIONS)}")
    snap = Snapshot(args.snapshot)
    db = load_db(args.defs, args.learned)
    zones = find_zones(snap)
    world = World(snap, db, load_origins(snap, zones, args.disk), zones)
    out = []
    for sec in args.sections or ["summary", "lowmem", "zones", "traps", "coverage"]:
        if sec != "summary" and sec != "placeholders":
            out.append(f"\n## {sec}")
        SECTIONS[sec](world, out)
    print("\n".join(out))
    if args.json:
        Path(args.json).write_text(json.dumps(to_json(world), indent=1, default=str))


if __name__ == "__main__":
    main()
