#!/usr/bin/env python3
"""What the System's extensions do on a real boot, file by file.

Reads a snapshot taken with `mac-phoenix --trace-atraps` (see atraps.py)
and the boot disk, and reports every file in Extensions and Control
Panels (and the System file's own INITs):

  inits.py SNAPSHOT                 one line per file: kind, load order,
                                    calls, routines Executor lacks
  inits.py SNAPSHOT --file NAME     that file's routines: Executor status,
                                    calls, the traps they ran inside
  inits.py SNAPSHOT --nest NAME     that file's call sites in first-seen
                                    order, with the enclosing trap
  inits.py SNAPSHOT --json OUT      everything, for tools

The kind is a first sort for an allow list (inits.yaml): what the file is
by its type, refined by what its code called on the boot (Component
Manager, Slot/SCSI/ADB hardware, shared libraries).
"""
import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import atraps  # noqa: E402
import macdecode as md  # noqa: E402
import rsrc  # noqa: E402

FOLDERS = [b"System Folder:Extensions", b"System Folder:Control Panels"]

# File types: what the file is before looking at its code.
TYPE_KIND = {
    "INIT": "extension", "cdev": "control panel", "RDEV": "chooser device",
    "thng": "component", "libr": "shared library", "help": "guide", "mixn": "guide",
    "PRER": "printer driver", "PRES": "printer driver",
    "comd": "network", "adev": "network", "fext": "finder extension",
    "APPL": "application", "appe": "background app", "scri": "script system",
}
# Resource types holding code that runs at startup or on demand.
CODE_TYPES = {"INIT", "cdev", "thng", "DRVR", "PACK", "RDEV", "PDEF", "proc", "code",
              "dcmp", "WDEF", "CDEF", "LDEF", "MDEF", "sysz", "comp", "cmpo", "adev"}
CM_TRAP, SLOT_TRAPS = 0xA82A, {0xA06E, 0xA075, 0xA076, 0xA06F, 0xA070}
HW_TAGS = {"hardware"}


def file_kind(ftype, res, calls):
    kind = TYPE_KIND.get(ftype, ftype or "?")
    types = {t for t, _ in res}
    words = {s["trap"] for s in calls}
    notes = []
    if "thng" in types:
        notes.append("registers components")
    if CM_TRAP in words:
        notes.append("Component Manager")
    if words & SLOT_TRAPS or 0xA815 in words:
        notes.append("hardware")
    if "sysz" in types:
        notes.append("sysz")
    return kind, notes


class Report:
    def __init__(self, snapshot, disk=None):
        self.disk_path = disk
        self.an = atraps.Analysis(snapshot, disk, extensions=True)
        self.nm = self.an.names
        self.files = self._inventory()
        self.by_file = defaultdict(list)
        for s in self.an.sites:
            g = s["group"]
            if g not in ("ROM", "?", "patch", "Finder"):
                self.by_file[g].append(s)

    def _inventory(self):
        """[(name, folder, type, creator, resources)] from the boot disk."""
        snap = self.an.snap
        disk = self.disk_path or (snap.meta.get("disks") or [None])[0]
        d = rsrc.Disk(disk, snap.dir / "disk-cache")
        out = []
        sysres = d.resources(b"System Folder:System")
        out.append(("System", "System Folder", "zsys", "MACS",
                    {k: v for k, v in sysres.items() if k[0] == "INIT"}))
        for folder in FOLDERS:
            infos = finder_info(d, folder)
            for raw in d.files(folder):
                name = raw.decode("mac_roman")
                ftype, creator = infos.get(raw, ("", ""))
                out.append((name, folder.decode().split(":")[-1], ftype, creator,
                            d.resources(folder + b":" + raw)))
        d.close()
        return out

    def routine(self, s):
        return self.nm.routine(s["trap"], s["sel"], s["sub"], s["obj"], s["detail"])

    def parent(self, s):
        return self.nm.routine(s["parent"], s["parent_sel"], s["parent_sub"], s["parent_obj"])

    def status(self, s):
        ex = self.nm.executor(s["trap"], s["sel"], s["detail"])
        return "done" if ex in atraps.DONE else "basilisk" if ex == "basilisk" else "todo"

    def summary(self):
        rows = []
        for name, folder, ftype, creator, res in self.files:
            calls = self.by_file.get(name, [])
            kind, notes = file_kind(ftype, res, calls)
            routines = {}
            for s in calls:
                routines.setdefault(self.routine(s), self.status(s))
            code = sorted({t for t, _ in res if t in CODE_TYPES})
            rows.append({
                "file": name, "folder": folder, "type": ftype, "creator": creator,
                "kind": kind, "notes": notes, "code": code,
                "first_seq": min((s["seq"] for s in calls), default=None),
                "calls": sum(s["count"] for s in calls), "routines": len(routines),
                "todo": sorted(r for r, st in routines.items() if st == "todo"),
            })
        rows.sort(key=lambda r: (r["first_seq"] is None, r["first_seq"] or 0, r["file"]))
        return rows

    def detail(self, name):
        calls = self.by_file.get(name, [])
        by = defaultdict(lambda: {"calls": 0, "sites": 0, "parents": Counter(), "first": 1 << 62})
        for s in calls:
            r = by[self.routine(s)]
            r["status"] = self.status(s)
            r["calls"] += s["count"]
            r["sites"] += 1
            r["parents"][self.parent(s)] += s["count"]
            r["first"] = min(r["first"], s["seq"])
        return sorted(by.items(), key=lambda kv: kv[1]["first"])

    def nest(self, name):
        for s in sorted(self.by_file.get(name, []), key=lambda s: s["seq"]):
            yield s["seq"], self.parent(s), self.routine(s), s["count"], s["pc"], s["origin"], self.status(s)


def finder_info(d, folder):
    """{raw name: (type, creator)} from `hls -l`: kind, type/creator,
    resource size, data size, month, day, year or time, name."""
    d._mount()
    r = d._run("hls", "-la", b":" + folder)
    out = {}
    for line in r.stdout.split(b"\n"):
        parts = line.split(None, 7)
        if len(parts) == 8 and parts[0] == b"f":
            ftype, _, creator = parts[1].decode("mac_roman").partition("/")
            out[parts[7]] = (ftype, creator)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("snapshot")
    ap.add_argument("--disk")
    ap.add_argument("--file")
    ap.add_argument("--nest")
    ap.add_argument("--json")
    a = ap.parse_args()
    rep = Report(a.snapshot, a.disk)

    if a.json:
        out = {"files": rep.summary(),
               "detail": {r["file"]: [{"routine": k, **{x: (dict(v[x]) if x == "parents" else v[x])
                                                        for x in v}} for k, v in rep.detail(r["file"])]
                          for r in rep.summary() if r["calls"]}}
        Path(a.json).write_text(json.dumps(out, indent=1, default=str))
        print(f"wrote {a.json}")
        return
    if a.nest:
        for seq, par, me, n, pc, origin, st in rep.nest(a.nest):
            print(f"{seq:8d} {par[:34]:34s} > {me[:44]:44s} x{n:<5d} {st:5s} {origin}")
        return
    if a.file:
        for name, r in rep.detail(a.file):
            parents = ", ".join(f"{p} x{c}" for p, c in r["parents"].most_common(3))
            print(f"{r['status']:5s} {name[:46]:46s} x{r['calls']:<6d} in {parents}")
        return
    print(f"{'file':34s} {'type':9s} {'kind':16s} {'order':>8s} {'calls':>6s} {'rtn':>4s} {'todo':>4s}  notes / code")
    for r in rep.summary():
        order = "-" if r["first_seq"] is None else str(r["first_seq"])
        print(f"{r['file'][:34]:34s} {r['type'] + '/' + r['creator']:9s} {r['kind'][:16]:16s} {order:>8s} "
              f"{r['calls']:6d} {r['routines']:4d} {len(r['todo']):4d}  {' '.join(r['notes'] + r['code'])}")


if __name__ == "__main__":
    main()
