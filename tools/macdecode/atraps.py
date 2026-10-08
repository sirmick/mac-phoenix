#!/usr/bin/env python3
"""Who calls which A-trap on a real boot.

Reads atraps.tsv from a snapshot taken with `mac-phoenix --trace-atraps`
(UAE): one row per distinct (trap word, caller PC, enclosing trap). Each
caller is resolved to where its code came from:

  ROM                         the Quadra ROM
  System 'lpch' 31 +1a2c      a resource on the boot disk, matched by its
  Finder 'CODE' 4 +3e         bytes in the snapshot's heaps
  (code) File 'TYPE' id       matched by the code bytes recorded at the
                              call, for code that moved or was purged
  ?                           unresolved (heap code built at runtime, etc.)

and grouped: ROM, patch (System ptch/lpch/gpch/PTCH and the Process
Manager's scod; code Executor never runs), or the file the code is in.

  atraps.py SNAPSHOT                     summary + unnamed traps by caller
  atraps.py SNAPSHOT --trap A81E         every site of one trap, with parents
  atraps.py SNAPSHOT --needed            traps called from code Executor runs
                                         (not ROM, not patches), by name status
  atraps.py SNAPSHOT --json OUT
"""
import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import macdecode as md                      # noqa: E402
from mvdb import trap_slot                  # noqa: E402
from origins import Origins                 # noqa: E402
from rsrc import Disk                       # noqa: E402

PATCH_TYPES = {"ptch", "lpch", "gpch", "PTCH", "scod"}
UI_TRAPS = Path(__file__).resolve().parents[2] / "private/Universal Interfaces/Universal/Interfaces/CIncludes/Traps.h"


def trap_key(word):
    """Trap word -> table slot, ignoring the auto-pop / flag bits."""
    return trap_slot(word)


def ui_names():
    """(table, index) -> names from Universal Interfaces' Traps.h, if present."""
    names = defaultdict(list)
    if UI_TRAPS.exists():
        text = UI_TRAPS.read_text(encoding="mac_roman")
        for m in re.finditer(r"_(\w+)\s*=\s*0x(A[0-9A-Fa-f]{3})", text):
            names[trap_slot(int(m.group(2), 16))].append(m.group(1))
    return names


def read_sites(path):
    rows = []
    for line in Path(path).read_text(encoding="latin-1").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        f = line.split("\t")
        rows.append({"trap": int(f[0], 16), "pc": int(f[1], 16), "parent": int(f[2], 16),
                     "irq": int(f[3]), "count": int(f[4]), "seq": int(f[5]),
                     "app": f[6], "code": bytes.fromhex(f[7]) if len(f) > 7 else b""})
    return rows


class Resolver:
    def __init__(self, snap, zones, disk, world=None):
        self.snap = snap
        self.world = world
        d = Disk(disk, snap.dir / "disk-cache")
        try:
            self.files = d.system_files()
        finally:
            d.close()
        # Search the System heap and every heap inside the Process Manager
        # heap (app partitions), not the PM heap's free space.
        sys_zone = zones.get(snap.u32(0x2A6))
        ranges = [(sys_zone.addr, sys_zone.end)] if sys_zone else []
        ranges += [(z.addr, z.end) for z in zones.values()
                   if z.parent is not None and z.parent is not sys_zone
                   and not (sys_zone and sys_zone.addr <= z.addr < sys_zone.end)]

        def is_address(v):
            return snap.in_rom(v) or 0x2000 <= v < len(snap.ram)
        self.origins = Origins(snap.ram, ranges, self.files, is_address)
        # Code resources concatenated, for matching recorded code bytes.
        self.blob, self.spans = bytearray(), []
        for fname, res in self.files:
            for (rtype, rid), (_, data) in res.items():
                if rtype in ("CODE", "ptch", "lpch", "gpch", "PTCH", "scod", "INIT", "DRVR", "PACK",
                             "MDEF", "MBDF", "WDEF", "CDEF", "LDEF", "cdev", "proc", "dcmp",
                             "thng", "PDEF", "FKEY", "RDEV", "adev", "atlk", "ltlk", "boot",
                             "appe", "osax", "snth", "sfil", "fmtr", "ADBS", "node", "vdpm"):
                    self.spans.append((len(self.blob), len(self.blob) + len(data), fname, rtype, rid))
                    self.blob += data
        self.span_starts = [s[0] for s in self.spans]
        self.cache = {}

    def _by_code(self, code):
        """Find the recorded bytes (pc-8 .. pc+16) in a code resource."""
        if len(code) < 16:
            return None
        i = self.blob.find(code)
        pc_off = 8
        if i < 0:   # bytes before pc may differ (relocated); try from pc
            i = self.blob.find(code[8:])
            pc_off = 0
        if i < 0:
            return None
        import bisect
        k = bisect.bisect_right(self.span_starts, i) - 1
        start, end, fname, rtype, rid = self.spans[k]
        if i + len(code) - (8 - pc_off) > end:
            return None
        return f"{fname} '{rtype}' {rid} +{i + pc_off - start:x} (code)"

    def resolve(self, site):
        pc = site["pc"]
        if self.snap.in_rom(pc):
            return "ROM"
        key = (pc, site["code"])
        if key not in self.cache:
            live = None
            if self.snap.ok(pc, 2) and site["code"][8:10] == self.snap.ram[pc:pc + 2]:
                live = self.origins.describe(pc)
                if not live and self.world:
                    # A handle the resource maps know: World.where tags it.
                    m = re.search(r"rsrc '(.{4})' (-?\d+) \((.+?)\) ?\+?([0-9a-f]*)",
                                  self.world.where(pc))
                    if m:
                        live = f"{m.group(3)} '{m.group(1)}' {m.group(2)} +{m.group(4) or '0'}"

            self.cache[key] = live or self._by_code(site["code"]) or "?"
        return self.cache[key]


def group(origin):
    if origin in ("ROM", "?"):
        return origin
    fname, rest = origin.split(" '", 1)
    rtype = rest.split("'", 1)[0]
    if fname == "System" and rtype in PATCH_TYPES:
        return "patch"
    return fname


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("snapshot")
    ap.add_argument("--disk")
    ap.add_argument("--trap", help="show every site of one trap word (hex)")
    ap.add_argument("--needed", action="store_true",
                    help="traps called from code Executor runs (not ROM, not patches)")
    ap.add_argument("--json")
    a = ap.parse_args()

    snap = md.Snapshot(a.snapshot)
    db = md.load_db()
    ui = ui_names()
    zones = md.find_zones(snap)
    disk = a.disk or (snap.meta.get("disks") or [None])[0]
    res = Resolver(snap, zones, disk, md.World(snap, db, None, zones))
    sites = read_sites(Path(a.snapshot) / "atraps.tsv")

    def names(word):
        k = trap_key(word)
        return db.traps.get(k, []), ui.get(k, [])

    def label(word):
        mv, u = names(word)
        return (mv or u or ["-"])[0]

    for s in sites:
        s["origin"] = res.resolve(s)
        s["group"] = group(s["origin"])

    by_trap = defaultdict(list)
    for s in sites:
        by_trap[trap_key(s["trap"])].append(s)

    if a.trap:
        k = trap_key(int(a.trap, 16))
        for s in sorted(by_trap.get(k, []), key=lambda s: -s["count"]):
            print(f"{s['trap']:04X} {label(s['trap']):24} pc {s['pc']:08X} x{s['count']:<7} "
                  f"in {s['parent']:04X} {label(s['parent']) if s['parent'] else '(top)':22} "
                  f"irq{s['irq']} {s['origin']}")
        return

    def status(k):
        mv, u = db.traps.get(k, []), ui.get(k, [])
        return "multiversal" if mv else "Traps.h" if u else "unnamed"

    rows = []
    for k, ss in sorted(by_trap.items()):
        groups = Counter()
        for s in ss:
            groups[s["group"]] += s["count"]
        parents = Counter()
        for s in ss:
            parents[s["parent"]] += s["count"]
        rows.append({"table": k[0], "index": k[1], "word": ss[0]["trap"],
                     "name": label(ss[0]["trap"]), "status": status(k),
                     "calls": sum(s["count"] for s in ss), "sites": len(ss),
                     "groups": dict(groups.most_common()),
                     "parents": {f"{p:04X}": n for p, n in parents.most_common(8)},
                     "needed": any(g not in ("ROM", "patch", "?") for g in groups)})

    if a.json:
        Path(a.json).write_text(json.dumps(rows, indent=1))

    tot = Counter(r["status"] for r in rows)
    resolved = Counter(s["group"] for s in sites)
    print(f"# {a.snapshot}: {len(sites)} call sites, {len(rows)} distinct traps "
          f"({tot['multiversal']} in multiversal, {tot['Traps.h']} only in Traps.h, {tot['unnamed']} unnamed)")
    print("  call sites by caller: " + ", ".join(f"{g} {n}" for g, n in resolved.most_common(12)))

    def show(rs, title):
        print(f"\n## {title} ({len(rs)})")
        for r in rs:
            g = ", ".join(f"{k} {v}" for k, v in list(r["groups"].items())[:5])
            p = ", ".join(f"{label(int(w, 16)) if w != '0000' else '(top)'} {n}"
                          for w, n in list(r["parents"].items())[:3])
            print(f"  {r['word']:04X} {r['name']:22} {r['status']:11} calls {r['calls']:<8} "
                  f"callers: {g}  |  inside: {p}")

    if a.needed:
        need = [r for r in rows if r["needed"]]
        for st in ("unnamed", "Traps.h", "multiversal"):
            show([r for r in need if r["status"] == st],
                 f"called from code Executor runs, {st}")
        return
    show([r for r in rows if r["status"] != "multiversal"], "traps without a multiversal name")


if __name__ == "__main__":
    main()
