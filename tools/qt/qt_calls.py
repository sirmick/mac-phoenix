#!/usr/bin/env python3
"""qt_calls.py — the QuickTime calls in a --trace-atraps snapshot, by app.

Reads <snapshot>/atraps.tsv (tools/qt/trace_app.py, or any UAE run with
--trace-atraps) and counts, for the applications named (CurApName at the
call), the calls into QuickTime's traps: _QuickTime ($AAAA, selector in
D0: the Movie Toolbox, the movie controller's glue), _CodecDispatch
($AAA3: the Image Compression Manager), _ComponentDispatch ($A82A: the
Component Manager, and CallComponent by its selector), and the Sound
Manager ($A800). "direct": the app's own calls (from outside the System
heap and ROM, where QuickTime's code and components live, not at interrupt
time, not inside another QuickTime call); "nested": the rest (QuickTime
calling itself, the Sound Manager's interrupt-time component calls).

  qt_calls.py SNAPSHOT [--app Ares] [--all-traps] [--markdown]
"""
import argparse
import json
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/macdecode"))
import dispatch  # noqa: E402

QT_TRAPS = {0xAAAA: "QuickTime", 0xAAA3: "ICM", 0xA82A: "ComponentMgr", 0xA800: "Sound"}
TRAP_SLOT = lambda w: w & 0xFBFF if w & 0x0800 else w & 0xF0FF  # noqa: E731



# Universal Interfaces (private/, untracked: in the main checkout, not a worktree's).
UI = next((r / "private/Universal Interfaces/Universal/Interfaces/CIncludes"
           for r in (ROOT, Path.home() / "src/mac-phoenix")
           if (r / "private/Universal Interfaces").exists()), ROOT / "private")


def ui_tables():
    """From Universal Interfaces' inline glue: {(trap, selector): name} for
    $AAAA/$AAA3, and {(paramSize, what): [name [header]]} for component calls."""
    traps, comp = {}, defaultdict(list)
    if not UI.exists():
        return traps, comp
    for f in sorted(UI.glob("*.h")):
        text = f.read_text(encoding="mac_roman").replace("\r", "\n")
        for stmt in text.split(";"):
            g = re.search(r"WORDINLINE\(([^)]*)\)", stmt)
            m = re.search(r"(\w+)\s*\([^()]*\)\s*\w*WORDINLINE", stmt, re.S)
            if not g or not m:
                continue
            try:
                w = [int(x, 16) for x in g.group(1).replace(" ", "").split(",")]
            except ValueError:
                continue
            name = m.group(1)
            if w[-1] in (0xAAAA, 0xAAA3):
                if len(w) == 2 and (w[0] & 0xFF00) == 0x7000:
                    traps[(w[-1], w[0] & 0xFF)] = name
                elif len(w) == 3 and w[0] == 0x303C:
                    traps[(w[-1], w[1])] = name
                elif len(w) == 4 and w[0] == 0x203C:
                    traps[(w[-1], w[2])] = name
            elif w[-1] == 0xA82A and len(w) == 5 and w[0] == 0x2F3C:
                comp[(w[1], w[2])].append(f"{name} [{f.stem}]")
    pri = ["Movies", "QuickTimeComponents", "ImageCompression", "ImageCodec", "QuickTimeMusic",
           "SoundComponents", "Sound"]
    rank = lambda c: pri.index(c.split("[")[1][:-1]) if c.split("[")[1][:-1] in pri else len(pri)  # noqa: E731
    for k in comp:
        comp[k].sort(key=rank)
    return traps, comp


UI_TRAPS, UI_COMP = ui_tables()


def rows(path):
    with open(path, encoding="latin-1") as f:
        for line in f:
            if line.startswith("#"):
                continue
            c = line.rstrip("\n").split("\t")
            if len(c) < 13:
                continue
            yield c


# A component's code resource type -> the UI name prefixes of its calls.
COMP_PREFIX = {"play": ("MC",), "tune": ("Tune",), "note": ("NA",), "imdc": ("ImageCodec",),
               "imco": ("ImageCodec",), "clok": ("Clock",), "mhlr": ("Media",), "dhlr": ("DataH",),
               "sdev": ("SoundComponent",), "scom": ("SoundComponent",), "sdec": ("SoundComponent",),
               "eat ": ("MovieImport", "MovieExport"), "grip": ("GraphicsImport",), "pnot": ("Preview",),
               "musi": ("Music",), "tsvc": ("TextMedia",), "vdig": ("VD",), "barg": ("SG",)}


def instance_names(snapshot, rows_):
    """Component instance -> 'File 'type' id' of the code that ran inside
    its calls (as atraps.py names them)."""
    import macdecode as md
    from atraps import Resolver
    snap = md.Snapshot(snapshot)
    db = md.load_db()
    zones = md.find_zones(snap)
    disk = (snap.meta.get("disks") or [None])[0]
    res = Resolver(snap, zones, disk, md.World(snap, db, None, zones))
    inside = defaultdict(lambda: defaultdict(int))
    for c in rows_:
        if c[7] == "-" or c[8] == "-":
            continue
        pc = int(c[4], 16)
        if snap.in_rom(pc):
            continue
        code = bytes.fromhex(c[13]) if c[13] else b""
        origin = res.resolve({"pc": pc, "code": code, "owner": c[14] if len(c) > 15 else ""})
        if origin in ("ROM", "?") or " '" not in origin:
            continue
        fname, rest = origin.split(" '", 1)
        inside[c[8]][f"{fname} '{rest.split(' +')[0]}"] += int(c[10])
    return {inst: max(cnt, key=cnt.get) for inst, cnt in inside.items()}


def name_of(d, trap, sel, sub, comp=None):
    t = TRAP_SLOT(trap)
    if t == 0xA82A and sel == 0xFFFFFFFF or (t == 0xA82A and sel is not None and sel >= 0x10000):
        sel = None
    n = UI_TRAPS.get((t, sel)) if sel is not None else None
    n = n or (d.selector_name(t, sel) if sel is not None else None)
    if t == 0xA82A and sub is not None and n in (None, "CallComponent", "CallComponentDispatch"):
        what, psize = sub & 0xFFFF, (sub >> 16) & 0xFFFF
        cands = UI_COMP.get((psize, what), [])
        if comp:
            rtype = comp.split("'")[1] if "'" in comp else ""
            pre = COMP_PREFIX.get(rtype)
            if pre:
                cands = [c for c in cands if c.startswith(pre)] or cands
        if what >= 0x8000:
            what -= 0x10000
        guess = " | ".join(cands[:3]) + (f" +{len(cands) - 3}" if len(cands) > 3 else "")
        return (f"Call what={what} p={psize}" + (f" [{comp}]" if comp else "")
                + (f" ({guess})" if guess else ""))
    return n or (f"#{sel:X}" if sel is not None else "?")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("snapshot")
    ap.add_argument("--app", action="append", help="CurApName substring (default: every app but the Finder)")
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--objects", action="store_true", help="CallComponent: by instance too")
    ap.add_argument("--direct", action="store_true", help="only calls with direct > 0")
    ap.add_argument("--resolve", action="store_true",
                    help="name component instances by their code (slower: matches resources)")
    a = ap.parse_args()
    d = dispatch.Dispatchers()
    snap = Path(a.snapshot)
    meta = json.loads((snap / "meta.json").read_text())
    rom_base = int(meta["rom_base"], 16)
    with open(snap / "ram.bin", "rb") as f:
        ram = f.read(0x3000)
        sysz = struct.unpack(">I", ram[0x2A6:0x2AA])[0]
        f.seek(sysz)
        sys_end = struct.unpack(">I", f.read(4))[0]     # SysZone's bkLim
    direct, nested, apps = defaultdict(int), defaultdict(int), set()
    allrows = list(rows(snap / "atraps.tsv"))
    inames = instance_names(snap, allrows) if a.resolve else {}
    for c in allrows:
        trap = int(c[0], 16)
        slot = TRAP_SLOT(trap)
        if slot not in QT_TRAPS:
            continue
        app = c[12].strip("\x00 ")
        if not app.isprintable() or "\xff" in app:
            app = "(boot)"
        if a.app and not any(x.lower() in app.lower() for x in a.app):
            continue
        if not a.app and app in ("Finder", "BridgeAgent", ""):
            continue
        apps.add(app)
        sel = None if c[1] == "-" else int(c[1], 16)
        sub = None if c[2] == "-" else int(c[2], 16)
        parent, pc, irq = int(c[5], 16), int(c[4], 16), int(c[9])
        key = (app, QT_TRAPS[slot], name_of(d, trap, sel, sub, inames.get(c[3])))
        if a.objects and sub is not None:
            key += (c[3],)
        # The app's own call: from outside the System heap and ROM (QuickTime's
        # code and components live there), not at interrupt time, not inside
        # another QuickTime call.
        own = (sys_end <= pc < rom_base and irq == 0
               and not (TRAP_SLOT(parent) in QT_TRAPS and parent != 0))
        (direct if own else nested)[key] += int(c[10])
    keys = sorted(set(direct) | set(nested))
    if a.markdown:
        print("| App | Manager | Call | direct | nested |\n|---|---|---|---|---|")
    for k in keys:
        if a.direct and not direct.get(k):
            continue
        call = " ".join(k[2:])
        if a.markdown:
            print(f"| {k[0]} | {k[1]} | {call} | {direct.get(k, 0)} | {nested.get(k, 0)} |")
        else:
            print(f"{k[0]:<14} {k[1]:<13} {call:<48} {direct.get(k, 0):>7} {nested.get(k, 0):>7}")


if __name__ == "__main__":
    main()
