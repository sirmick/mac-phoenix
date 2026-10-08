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
from dispatch import Dispatchers            # noqa: E402

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
    def opt(v):
        return None if v == "-" else int(v, 16)
    rows = []
    for line in Path(path).read_text(encoding="latin-1").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        f = line.split("\t")
        rows.append({"trap": int(f[0], 16), "sel": opt(f[1]), "sub": opt(f[2]),
                     "pc": int(f[3], 16),
                     "parent": int(f[4], 16), "parent_sel": opt(f[5]), "parent_sub": opt(f[6]),
                     "irq": int(f[7]), "count": int(f[8]), "seq": int(f[9]),
                     "app": f[10], "code": bytes.fromhex(f[11]) if len(f) > 11 else b""})
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


class Names:
    """Routine names and Executor status, (trap, selector[, sub]) as the unit."""

    def __init__(self, db):
        self.db, self.ui, self.ds = db, ui_names(), Dispatchers(db)
        # Traps Executor implements as raw register-level 68k stubs; multiversal
        # declares these functions without their trap words.
        stubs = Path(__file__).resolve().parents[2] / "src/executor/romlib/base/emustubs.h"
        self.raw = {trap_key(int(t, 16)) for t in
                    re.findall(r"RAW_68K_TRAP\(\w+,\s*(0x[0-9A-Fa-f]+)", stubs.read_text())}

    def trap(self, word):
        k = trap_key(word)
        return (self.db.traps.get(k) or self.ui.get(k) or [f"_{word:04X}"])[0]

    def routine(self, word, sel=None, sub=None):
        if not word:
            return "(top)"
        base = self.trap(word)
        if sel is None:
            return base
        name = self.ds.selector_name(word, sel) or f"{base}#{sel:X}"
        if sub is not None:
            cands = self.ds.sub_selector_names(word, sub)
            what = f"what={sub & 0xFFFF:04X}"
            name += f" {what} ({'/'.join(cands[:2])}{'…' if len(cands) > 2 else ''})" if cands else f" {what}"
        return name

    def name_source(self, word, sel=None):
        k = trap_key(word)
        if sel is not None:
            d = self.ds.get(word)
            s = d and d["selectors"].get(sel & (d["mask"] or 0xFFFFFFFF))
            return s["source"].split(" ")[0] if s else "unnamed"
        return "multiversal" if self.db.traps.get(k) else "Traps.h" if self.ui.get(k) else "unnamed"

    def executor(self, word, sel=None):
        """implemented | missing | unknown (not in any table)."""
        k = trap_key(word)
        if k in self.raw:
            return "implemented"
        # One function for the whole trap (it decodes the selector itself).
        if any(e.get("kind") == "function" and e.get("executor") for e in self.db.trap_info.get(k, [])):
            return "implemented" if sel is None else "whole-trap"
        if sel is not None:
            d = self.ds.get(word)
            s = d and d["selectors"].get(sel & (d["mask"] or 0xFFFFFFFF))
            if s and s["executor"]:
                return "implemented"
            return "missing" if s else "unknown"
        info = self.db.trap_info.get(k)
        if not info:
            return "unknown"
        return "implemented" if any(e.get("executor") for e in info) else "missing"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("snapshot")
    ap.add_argument("--disk")
    ap.add_argument("--trap", help="every site of one trap word (hex), by selector")
    ap.add_argument("--all", action="store_true", help="every routine, not just the gaps")
    ap.add_argument("--json")
    a = ap.parse_args()

    snap = md.Snapshot(a.snapshot)
    db = md.load_db()
    nm = Names(db)
    zones = md.find_zones(snap)
    disk = a.disk or (snap.meta.get("disks") or [None])[0]
    res = Resolver(snap, zones, disk, md.World(snap, db, None, zones))
    sites = read_sites(Path(a.snapshot) / "atraps.tsv")
    for s in sites:
        s["origin"] = res.resolve(s)
        s["group"] = group(s["origin"])

    def rkey(s):
        return (trap_key(s["trap"]), s["sel"], s["sub"])

    if a.trap:
        k = trap_key(int(a.trap, 16))
        for s in sorted((s for s in sites if trap_key(s["trap"]) == k),
                        key=lambda s: (s["sel"] or 0, -s["count"])):
            print(f"{nm.routine(s['trap'], s['sel'], s['sub']):34} pc {s['pc']:08X} x{s['count']:<6} "
                  f"in {nm.routine(s['parent'], s['parent_sel'], s['parent_sub']):28} "
                  f"irq{s['irq']} {s['origin']}")
        return

    by = defaultdict(list)
    for s in sites:
        by[rkey(s)].append(s)
    rows = []
    for (k, sel, sub), ss in by.items():
        word = ss[0]["trap"]
        groups, parents = Counter(), Counter()
        for s in ss:
            groups[s["group"]] += s["count"]
            parents[nm.routine(s["parent"], s["parent_sel"], s["parent_sub"])] += s["count"]
        rows.append({"trap": f"{word:04X}", "sel": sel, "sub": sub,
                     "name": nm.routine(word, sel, sub), "named_by": nm.name_source(word, sel),
                     "executor": nm.executor(word, sel),
                     "calls": sum(s["count"] for s in ss), "sites": len(ss),
                     "groups": dict(groups.most_common()), "parents": dict(parents.most_common(6)),
                     "runs": any(g not in ("ROM", "patch", "?") for g in groups)})
    rows.sort(key=lambda r: (r["trap"], r["sel"] or 0, r["sub"] or 0))
    if a.json:
        Path(a.json).write_text(json.dumps(rows, indent=1))

    disp = [r for r in rows if r["sel"] is not None]
    print(f"# {a.snapshot}: {len(sites)} call sites, {len(rows)} routines "
          f"({len(rows) - len(disp)} plain traps, {len(disp)} dispatcher selectors "
          f"over {len({r['trap'] for r in disp})} dispatchers)")
    print("  named by: " + ", ".join(f"{k} {v}" for k, v in Counter(r["named_by"] for r in rows).most_common()))
    print("  call sites by caller: " + ", ".join(
        f"{g} {n}" for g, n in Counter(s["group"] for s in sites).most_common(10)))

    def show(rs, title):
        print(f"\n## {title} ({len(rs)})")
        for r in rs:
            sel = f"{r['sel']:08X}" if r["sel"] is not None else "-"
            g = ", ".join(f"{k} {v}" for k, v in list(r["groups"].items())[:4])
            p = ", ".join(f"{k} {v}" for k, v in list(r["parents"].items())[:2])
            print(f"  {r['trap']} {sel:8} {r['name'][:34]:34} {r['named_by']:11} {r['executor']:11} "
                  f"x{r['calls']:<6} by {g}  | in {p}")

    if a.all:
        show(rows, "all routines")
        return
    runs = [r for r in rows if r["runs"]]
    gap = [r for r in runs if r["executor"] not in ("implemented", "whole-trap")]
    show(gap, "called from code Executor runs (not ROM, not patches) and not in Executor")
    show([r for r in gap if "Finder" in r["groups"]], "of which Finder calls")


if __name__ == "__main__":
    main()
