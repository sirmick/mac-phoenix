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
    """atraps.tsv rows. Columns: trap sel sub obj pc parent parent_sel
    parent_sub parent_obj irq count seq app code owner detail (owner: the
    loaded resource holding the caller, found by the tracer at first hit)."""
    def opt(v):
        return None if v == "-" else int(v, 16)
    rows = []
    for line in Path(path).read_text(encoding="latin-1").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        f = line.split("\t")
        rows.append({"trap": int(f[0], 16), "sel": opt(f[1]), "sub": opt(f[2]), "obj": opt(f[3]),
                     "pc": int(f[4], 16),
                     "parent": int(f[5], 16), "parent_sel": opt(f[6]), "parent_sub": opt(f[7]),
                     "parent_obj": opt(f[8]),
                     "irq": int(f[9]), "count": int(f[10]), "seq": int(f[11]),
                     "app": f[12], "code": bytes.fromhex(f[13]) if len(f) > 13 and f[13] else b"",
                     "owner": f[14] if len(f) > 15 else "",
                     "detail": f[15] if len(f) > 15 else (f[14] if len(f) > 14 else "")})
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

            # The tracer's own answer (resource maps at the time of the
            # call) beats matching bytes after the code moved.
            self.cache[key] = live or site.get("owner") or self._by_code(site["code"]) or "?"
        return self.cache[key]


ROOT = Path(__file__).resolve().parents[2]

# System code resources Executor runs as 68k: definition procedures, unless
# the resource policy serves Executor's own. Everything else in the System
# file (patches, PACKs, drivers, boot code) Executor replaces with C++.
SYSTEM_RUN_TYPES = {"WDEF", "MDEF", "CDEF", "LDEF", "MBDF"}

def load_tags(path=None):
    """learned.yaml tags: {(slot, selector or None): (tag, reason)}."""
    import yaml
    path = Path(path) if path else Path(__file__).resolve().parent / "learned.yaml"
    data = (yaml.safe_load(path.read_text()) if path.exists() else {}) or {}
    return {(trap_slot(t["trap"]), t.get("selector")): (t["tag"], t.get("reason", ""))
            for t in data.get("tags") or []}


def read_installs(path):
    """trap_installs.tsv: every _SetTrapAddress in the traced boot, in order."""
    rows = []
    if not Path(path).exists():
        return rows
    for line in Path(path).read_text(encoding="latin-1").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        f = line.split("\t") + [""] * 3
        rows.append({"seq": int(f[0]), "trap": int(f[1], 16), "slot": (f[2], int(f[3], 16)),
                     "addr": int(f[4], 16), "old": int(f[5], 16), "pc": int(f[6], 16),
                     "app": f[7], "installer": f[8], "target": f[9]})
    return rows


def basilisk_impls():
    """Traps and drivers the Basilisk II core implements in host code
    (src/core/rom_patches.cpp, emul_op.cpp): trap slots it replaces or
    reinstalls, and drivers it provides."""
    src = (ROOT / "src/core/rom_patches.cpp").read_text()
    traps = {trap_slot(int(t, 16)) for t in re.findall(r"find_rom_trap\((0x[0-9a-fA-F]{4})\)", src)}
    traps |= {trap_slot(int(t, 16)) for t in
              re.findall(r"r\.d\[0\]\s*=\s*(0x[0-9a-fA-F]{4});\s*\n\s*Execute68kTrap\(0xa247", src)}
    if "M68K_EMUL_OP_BLOCK_MOVE" in src:
        traps.add(trap_slot(0xA02E))
    # Drivers: the replacement drivers rom_patches.cpp embeds (name bytes
    # commented as // ".Sony") and the slot drivers slot_rom.cpp declares.
    drivers = set(re.findall(r'//\s*"(\.[A-Za-z_][A-Za-z0-9_]*)"', src))
    slot = (ROOT / "src/core/slot_rom.cpp").read_text()
    drivers |= {("." + d.lstrip(".")) for d in re.findall(r'"\.?([A-Za-z_]*(?:Display_Video|ENET)[A-Za-z_]*)"', slot)}
    return traps, drivers


def group(origin):
    if origin in ("ROM", "?"):
        return origin
    fname, rest = origin.split(" '", 1)
    rtype = rest.split("'", 1)[0]
    if fname == "System" and rtype in PATCH_TYPES:
        return "patch"
    return fname


def load_policy():
    """(type, id) the resource policy serves from Executor."""
    out = set()
    for line in (ROOT / "src/executor/res/resource-policy.txt").read_text().splitlines():
        f = line.split("#")[0].split()
        if len(f) >= 3 and f[2] == "executor":
            out.add((f[0], int(f[1])))
    return out


def scope_of(origin, policy):
    """Who runs this caller's code under Executor:
    run: Finder or System code Executor executes as 68k
    rom / patch / replaced: Apple code Executor never runs
    extension: an INIT/cdev file (not loaded unless we choose to)
    unknown: unresolved caller"""
    if origin == "ROM":
        return "rom"
    if origin == "?":
        return "unknown"
    fname, rest = origin.split(" '", 1)
    rtype = rest.split("'", 1)[0]
    rid = int(re.match(r"\s*(-?\d+)", rest.split("'", 1)[1]).group(1))
    if fname == "Finder":
        return "run"
    if fname == "System":
        if rtype in PATCH_TYPES:
            return "patch"
        if rtype in SYSTEM_RUN_TYPES and (rtype, rid) not in policy:
            return "run"
        return "replaced"
    return "extension"


class Names:
    """Routine names and Executor status, (trap, selector[, sub]) as the unit."""

    def __init__(self, db):
        self.db, self.ui, self.ds = db, ui_names(), Dispatchers(db)
        # Traps Executor implements as raw register-level 68k stubs; multiversal
        # declares these functions without their trap words.
        stubs = ROOT / "src/executor/romlib/base/emustubs.h"
        self.raw = {trap_key(int(t, 16)) for t in
                    re.findall(r"RAW_68K_TRAP\(\w+,\s*(0x[0-9A-Fa-f]+)", stubs.read_text())}
        self.basilisk_traps, self.basilisk_drivers = basilisk_impls()
        self.components = {}      # instance -> where its code lives (from traced children)

    def trap(self, word):
        k = trap_key(word)
        return (self.db.traps.get(k) or self.ui.get(k) or [f"_{word:04X}"])[0]

    def routine(self, word, sel=None, sub=None, obj=None, detail=""):
        if not word:
            return "(top)"
        base = self.trap(word)
        if trap_key(word)[0] == "os" and trap_key(word)[1] in DRIVER_TRAPS:
            if not is_driver_call(word, obj):
                return base
            drv = detail or (f"refnum {obj - 0x10000 if obj and obj & 0x8000 else obj}" if obj is not None else "?")
            return f"{base} {drv}" + (f" csCode={sel}" if sel is not None else "")
        if sel is None:
            return base
        name = self.ds.selector_name(word, sel) or f"{base}#{sel:X}"
        if sub is not None:
            cands = self.ds.sub_selector_names(word, sub)
            what = f"what={sub & 0xFFFF:04X}"
            comp = self.components.get(obj)
            name += f" {what}" + (f" [{comp}]" if comp else "") + \
                (f" ({'/'.join(cands[:2])}{'…' if len(cands) > 2 else ''})" if cands and not comp else "")
        return name

    def name_source(self, word, sel=None):
        k = trap_key(word)
        if sel is not None and not (k[0] == "os" and k[1] in DRIVER_TRAPS):
            d = self.ds.get(word)
            s = d and d["selectors"].get(sel & (d["mask"] or 0xFFFFFFFF))
            return s["source"].split(" ")[0] if s else "unnamed"
        return "multiversal" if self.db.traps.get(k) else "Traps.h" if self.ui.get(k) else "unnamed"

    def executor(self, word, sel=None, detail=""):
        """implemented | whole-trap | basilisk | missing | unknown."""
        k = trap_key(word)
        if k[0] == "os" and k[1] in DRIVER_TRAPS and detail:
            return "basilisk" if detail in self.basilisk_drivers else "unknown"
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
            if k in self.basilisk_traps:
                return "basilisk"
            return "missing" if s else "unknown"
        if k in self.basilisk_traps:
            return "basilisk"
        info = self.db.trap_info.get(k)
        if not info:
            return "unknown"
        return "implemented" if any(e.get("executor") for e in info) else "missing"


DRIVER_TRAPS = {0x001, 0x002, 0x003, 0x004, 0x005, 0x006}


def is_driver_call(word, refnum):
    """PBRead/Write/Control/Status/Close/KillIO on a driver (negative refnum),
    as opposed to a file (positive FCB refnum)."""
    k = trap_key(word)
    return k[0] == "os" and k[1] in DRIVER_TRAPS and refnum is not None and refnum & 0x8000
DONE = ("implemented", "whole-trap")


class Analysis:
    """A traced snapshot, as routines with callers, scope and a verdict.

    verdict: todo        called from code we run, not in Executor
             basilisk    called from code we run; host code exists in the
                         Basilisk II core (port it)
             done        called from code we run, Executor has it
             unresolved  only callers we couldn't place (code that moved
                         or boot code); not crossed out
             out         not needed; reason says why (hardware, only Apple
                         ROM/patch callers, only replaced System code,
                         only extensions)
    """

    def __init__(self, snapshot, disk=None, extensions=False):
        self.snap = md.Snapshot(snapshot)
        self.db = md.load_db()
        self.names = Names(self.db)
        zones = md.find_zones(self.snap)
        disk = disk or (self.snap.meta.get("disks") or [None])[0]
        self.resolver = Resolver(self.snap, zones, disk, md.World(self.snap, self.db, None, zones))
        self.policy = load_policy()
        self.tags = load_tags()
        self.installs = read_installs(Path(snapshot) / "trap_installs.tsv")
        self.extensions = extensions
        self.sites = read_sites(Path(snapshot) / "atraps.tsv")
        for s in self.sites:
            s["origin"] = self.resolver.resolve(s)
            s["group"] = group(s["origin"])
            s["scope"] = scope_of(s["origin"], self.policy)
        self._components()
        self.rows = self._rows()

    def _components(self):
        """A component instance is named by the code that ran inside its calls."""
        inside = defaultdict(Counter)
        for s in self.sites:
            if s["parent_obj"] is not None and s["parent_sub"] is not None and s["origin"] not in ("ROM", "?"):
                fname, rest = s["origin"].split(" '", 1)
                inside[s["parent_obj"]][f"{fname} '{rest.split(' +')[0]}"] += s["count"]
        self.names.components = {inst: c.most_common(1)[0][0] for inst, c in inside.items()}

    def runs(self, scope):
        return scope == "run" or (self.extensions and scope == "extension")

    def _rows(self):
        nm = self.names
        by = defaultdict(list)
        for s in self.sites:
            k = trap_key(s["trap"])
            drv = is_driver_call(s["trap"], s["obj"])
            if not drv and k[0] == "os" and k[1] in DRIVER_TRAPS:
                s = dict(s, sel=None, obj=None)
            key = (k, s["sel"], s["sub"] if not drv else None,
                   s["obj"] if (drv or s["sub"] is not None) else None,
                   s["detail"] if drv else "")
            by[key].append(s)
        rows = []
        for (k, sel, sub, obj, detail), ss in by.items():
            word = ss[0]["trap"]
            name = nm.routine(word, sel, sub, obj, detail)
            groups, scopes, parents = Counter(), Counter(), Counter()
            for s in ss:
                groups[s["group"]] += s["count"]
                scopes[s["scope"]] += s["count"]
                parents[nm.routine(s["parent"], s["parent_sel"], s["parent_sub"], s["parent_obj"])] += s["count"]
            ex = nm.executor(word, sel, detail)
            needed = any(self.runs(sc) for sc in scopes)
            if needed:
                verdict = "done" if ex in DONE else "basilisk" if ex == "basilisk" else "todo"
                reason = ""
            else:
                verdict = "out"
                tag = self.tags.get((k, sel)) or self.tags.get((k, None))
                if tag:
                    reason = f"{tag[0]}: {tag[1]}"
                elif k[0] == "os" and k[1] in DRIVER_TRAPS and detail:
                    reason = "driver only Apple code uses"
                elif set(scopes) <= {"rom", "patch"}:
                    reason = "only Apple ROM/patch code calls it"
                elif set(scopes) <= {"rom", "patch", "replaced"}:
                    reason = "only System code Executor replaces"
                elif "extension" in scopes:
                    reason = "only extensions"
                else:
                    verdict, reason = "unresolved", "callers not resolved (moved/boot code)"
            tag = self.tags.get((k, sel)) or self.tags.get((k, None))
            rows.append({"slot": k, "trap": f"{word:04X}", "sel": sel, "sub": sub, "obj": obj,
                         "tag": tag[0] if tag else None, "detail": detail,
                         "name": name, "named_by": nm.name_source(word, sel), "executor": ex,
                         "verdict": verdict, "reason": reason,
                         "calls": sum(s["count"] for s in ss), "sites": len(ss),
                         "groups": dict(groups.most_common()), "scopes": dict(scopes.most_common()),
                         "parents": dict(parents.most_common(6))})
        rows.sort(key=lambda r: (r["slot"][0] != "tool", r["slot"][1], r["sel"] or 0, r["sub"] or 0,
                                 r["obj"] or 0))
        return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("snapshot")
    ap.add_argument("--disk")
    ap.add_argument("--trap", help="every site of one trap word (hex)")
    ap.add_argument("--all", action="store_true", help="every routine with its verdict")
    ap.add_argument("--extensions", action="store_true", help="count extensions as code we run")
    ap.add_argument("--json")
    a = ap.parse_args()

    an = Analysis(a.snapshot, a.disk, a.extensions)
    nm = an.names
    if a.trap:
        k = trap_key(int(a.trap, 16))
        for s in sorted((s for s in an.sites if trap_key(s["trap"]) == k),
                        key=lambda s: (s["sel"] or 0, -s["count"])):
            print(f"{nm.routine(s['trap'], s['sel'], s['sub'], s['obj'], s['detail']):40} pc {s['pc']:08X} "
                  f"x{s['count']:<6} in {nm.routine(s['parent'], s['parent_sel'], s['parent_sub'], s['parent_obj']):30} "
                  f"irq{s['irq']} {s['scope']:9} {s['origin']}")
        return
    rows = an.rows
    if a.json:
        Path(a.json).write_text(json.dumps([dict(r, slot=list(r["slot"])) for r in rows], indent=1))

    v = Counter(r["verdict"] for r in rows)
    print(f"# {a.snapshot}: {len(an.sites)} call sites, {len(rows)} routines"
          f"{' (extensions count as run)' if a.extensions else ''}")
    print(f"  todo {v['todo']}, basilisk {v['basilisk']}, done {v['done']}, unresolved {v['unresolved']}, "
          f"out {v['out']}: " + ", ".join(
        f"{k} {n}" for k, n in Counter(r["reason"] for r in rows if r["verdict"] == "out").most_common()))

    def show(rs, title):
        print(f"\n## {title} ({len(rs)})")
        for r in rs:
            sel = f"{r['sel']:08X}" if r["sel"] is not None else "-"
            g = ", ".join(f"{k} {n}" for k, n in list(r["groups"].items())[:4])
            p = ", ".join(f"{k} {n}" for k, n in list(r["parents"].items())[:2])
            tail = r["reason"] or r["executor"]
            print(f"  {r['trap']} {sel:8} {r['name'][:44]:44} {r['verdict']:8} {tail[:34]:34} "
                  f"x{r['calls']:<6} by {g}  | in {p}")

    if a.all:
        show(rows, "all routines")
        return
    show([r for r in rows if r["verdict"] == "todo"], "todo: called from code we run, not in Executor")
    show([r for r in rows if r["verdict"] == "basilisk"], "basilisk: host code to port")


if __name__ == "__main__":
    main()
