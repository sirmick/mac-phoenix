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
  atraps.py REF --compare CAND [--compare-disk IMG]
                                         first divergence of Finder's own call
                                         sequence (by CODE location + routine)
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
                     "detail": f[15] if len(f) > 15 else (f[14] if len(f) > 14 else ""),
                     "res_d0": opt(f[16]) if len(f) > 17 else None,
                     "res_top": opt(f[17]) if len(f) > 17 else None,
                     "in_d0": opt(f[18]) if len(f) > 18 else None,
                     "in_a0": opt(f[19]) if len(f) > 19 else None,
                     "in_zone": opt(f[20]) if len(f) > 20 else None})
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


def read_mem(path):
    """lowmem_access.tsv: who read/wrote which low-memory or ExpandMem byte.
    Returns (rows, expandmem base, expandmem size)."""
    rows, em, em_size = [], 0, 0
    if not Path(path).exists():
        return rows, em, em_size
    for line in Path(path).read_text(encoding="latin-1").splitlines():
        if line.startswith("# expandmem"):
            f = line.split()
            em, em_size = int(f[2], 16), int(f[4])
            continue
        if line.startswith("#") or not line.strip():
            continue
        f = line.split("\t") + [""] * 3
        rows.append({"addr": int(f[0], 16), "size": int(f[1]), "write": f[2] == "w",
                     "pc": int(f[3], 16), "count": int(f[4]), "seq": int(f[5]), "app": f[6],
                     "owner": f[7], "expandmem": f[8] == "1"})
    return rows, em, em_size


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
        self.mem, self.expandmem, self.expandmem_size = read_mem(Path(snapshot) / "lowmem_access.tsv")
        for m in self.mem:
            m["origin"] = self.place_pc(m["pc"], m["owner"])
            m["scope"] = scope_of(m["origin"], self.policy)

    def place_pc(self, pc, recorded=""):
        """Where the code at pc came from: ROM, the resource the tracer saw,
        or a match of the live bytes in the snapshot."""
        if self.snap.in_rom(pc):
            return "ROM"
        if recorded:
            return recorded
        code = bytes(self.snap.ram[max(pc - 8, 0):pc + 16]) if self.snap.ok(pc, 16) else b""
        return self.resolver.resolve({"pc": pc, "code": code})

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


def app_sequence(an, app="Finder"):
    """The traced app's own calls in first-seen order, as comparable keys:
    (code location, routine). Locations are resource-relative, so the same
    call matches across backends whatever address the code ended up at."""
    out = []
    for s in sorted(an.sites, key=lambda s: s["seq"]):
        o = s["origin"]
        if not o.startswith(app + " "):
            continue
        loc = re.sub(r" \((code|masked match[^)]*)\)$", "", o)
        name = an.names.routine(s["trap"], s["sel"], s["sub"], s["obj"], s["detail"])
        out.append({"key": (loc, re.sub(r" \[.*\]", "", name)), "site": s, "name": name, "loc": loc})
    return out


def result_difference(names, name, ra, cb):
    """How two first results of the same call differ, judged by the
    routine's return type (multiversal), or None if they agree. Addresses
    and refnums legitimately differ between backends, so pointers compare
    as nil / non-nil and integers only when one side is an error."""
    if ra["res_d0"] is None or cb["res_d0"] is None:
        return None
    base = name.split(" ")[0]
    rtype = names.db.returns.get(base, "?")
    os_trap = (ra["trap"] & 0x0800) == 0
    if os_trap:
        x, y = ra["res_d0"] & 0xFFFF, cb["res_d0"] & 0xFFFF
        sx, sy = x - 0x10000 if x & 0x8000 else x, y - 0x10000 if y & 0x8000 else y
        if (sx < 0 or sy < 0) and sx != sy:
            return f"D0 {sx} vs {sy}"
        return None
    if rtype in (None, "void"):
        return None
    if rtype == "?":
        return None
    top_a, top_b = ra["res_top"], cb["res_top"]
    if rtype in ("Boolean", "bool"):
        x, y = top_a >> 24, top_b >> 24
        return f"{rtype} {x} vs {y}" if (x != 0) != (y != 0) else None
    if rtype in ("OSErr", "INTEGER", "int16_t", "short", "OSStatus16", "ResFileRefNum", "SInt16"):
        x, y = top_a >> 16, top_b >> 16
        sx, sy = x - 0x10000 if x & 0x8000 else x, y - 0x10000 if y & 0x8000 else y
        if (sx < 0 or sy < 0) and sx != sy:
            return f"{rtype} {sx} vs {sy}"
        return None
    if "Handle" in rtype or "Ptr" in rtype or rtype.endswith("*") or rtype in ("WindowPtr", "GrafPtr", "MenuHandle", "THz"):
        if (top_a == 0) != (top_b == 0):
            return f"{rtype} {'nil' if not top_a else 'set'} vs {'nil' if not top_b else 'set'}"
        return None
    if rtype in ("LONGINT", "int32_t", "Size", "OSType", "ResType", "uint32_t", "UInt32"):
        return f"{rtype} {top_a:#x} vs {top_b:#x}" if top_a != top_b and (top_a == 0 or top_b == 0) else None
    return None


def compare(ref, cand, context=6, more=12, window=None, app="Finder"):
    """Print where the candidate's app call sequence first leaves the reference's
    (or, with window=(start, count), the aligned listing from reference #start)."""
    import difflib
    a, b = app_sequence(ref, app), app_sequence(cand, app)
    sm = difflib.SequenceMatcher(a=[x["key"] for x in a], b=[x["key"] for x in b], autojunk=False)
    if window:
        start, count = window
        shown = 0
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if i2 <= start and not (tag == "insert" and i1 == start):
                continue
            for k in range(max(i1, start), i2):
                if shown >= count:
                    return
                mark = "  = " if tag == "equal" else "  -R"
                print(f"{mark} R#{k:<4} {a[k]['loc']:34} {a[k]['name']}")
                shown += 1
            if tag != "equal":
                for k in range(j1, j2):
                    if shown >= count:
                        return
                    print(f"  +C C#{k:<4} {b[k]['loc']:34} {b[k]['name']}")
                    shown += 1
        return
    ops = [op for op in sm.get_opcodes() if op[0] != "equal"]
    same = sum(j2 - j1 for tag, i1, i2, j1, j2 in sm.get_opcodes() if tag == "equal")
    print(f"# app calls: reference {len(a)}, candidate {len(b)}, matching {same}")
    if not ops:
        print("  sequences match")
        return
    # A lone extra call before anything matched (e.g. made before the trace
    # caught up) isn't where the paths split: report the next one.
    if len(ops) > 1 and ops[0][1] == 0 and ops[0][3] == 0 and ops[0][2] - ops[0][1] + ops[0][4] - ops[0][3] <= 2:
        print(f"  (skipping leading {ops[0][0]}: {'; '.join(x['loc'] + ' ' + x['name'] for x in a[ops[0][1]:ops[0][2]] + b[ops[0][3]:ops[0][4]])})")
        ops = ops[1:]
    tag, i1, i2, j1, j2 = ops[0]
    print(f"\n## first divergence ({tag}) at reference #{i1}, candidate #{j1}")
    for k in range(max(0, i1 - context), i1):
        print(f"   = {a[k]['loc']:34} {a[k]['name']}")
    for k in range(i1, min(i2, i1 + more)):
        print(f"  -R {a[k]['loc']:34} {a[k]['name']}  x{a[k]['site']['count']}")
    for k in range(j1, min(j2, j1 + more)):
        print(f"  +C {b[k]['loc']:34} {b[k]['name']}  x{b[k]['site']['count']}")
    # Calls both sides made (in order) whose first results differ in a way
    # that matters: a result that differs just before a divergence is
    # usually its cause.
    print("\n## matched calls with different results (before the divergence)")
    shown = 0
    for tag2, x1, x2, y1, y2 in sm.get_opcodes():
        if tag2 != "equal" or x1 >= i1:
            continue
        for k in range(x2 - x1):
            ra, cb = a[x1 + k]["site"], b[y1 + k]["site"]
            why = result_difference(ref.names, a[x1 + k]["name"], ra, cb)
            if why:
                print(f"  R#{x1 + k:<4} {a[x1 + k]['loc']:30} {a[x1 + k]['name'][:32]:32} {why}")
                shown += 1
    if not shown:
        print("  (none)")
    print(f"\n## next divergences")
    for tag, i1, i2, j1, j2 in ops[1:8]:
        ra = "; ".join(f"{x['loc']} {x['name']}" for x in a[i1:min(i2, i1 + 2)])
        cb = "; ".join(f"{x['loc']} {x['name']}" for x in b[j1:min(j2, j1 + 2)])
        print(f"  {tag:7} R#{i1}: {ra[:70]:70} | C#{j1}: {cb[:70]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("snapshot")
    ap.add_argument("--disk")
    ap.add_argument("--compare", metavar="CANDIDATE",
                    help="first divergence of the app's call sequence from this snapshot's")
    ap.add_argument("--compare-disk", help="disk for the candidate's resources (Executor snapshots have none)")
    ap.add_argument("--window", nargs=2, type=int, metavar=("START", "COUNT"),
                    help="with --compare: the aligned listing from reference #START")
    ap.add_argument("--app", default="Finder", help="whose calls --compare follows (default Finder)")
    ap.add_argument("--trap", help="every site of one trap word (hex)")
    ap.add_argument("--all", action="store_true", help="every routine with its verdict")
    ap.add_argument("--extensions", action="store_true", help="count extensions as code we run")
    ap.add_argument("--json")
    a = ap.parse_args()

    an = Analysis(a.snapshot, a.disk, a.extensions)
    nm = an.names
    if a.compare:
        cand = Analysis(a.compare, a.compare_disk or a.disk or (an.snap.meta.get("disks") or [None])[0],
                        a.extensions)
        compare(an, cand, window=a.window, app=a.app)
        return
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
