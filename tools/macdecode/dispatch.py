#!/usr/bin/env python3
"""Dispatch traps and their selectors, as first-class data.

Many trap slots are dispatchers: one trap word, many routines, picked by
a selector the caller puts in a register, on the stack, or in the trap
word's flag bits. Some go one level further (ComponentDispatch with
D0 = 0 calls into a component, whose own selector is the long on the
stack). The unit of "a Toolbox routine" is therefore

    (trap slot, selector[, sub-selector])

This module builds that table from three sources, later ones filling
gaps, never overriding earlier ones:

  1. multiversal          `dispatcher:` entries (selector-location) and
                          functions with `dispatcher:`/`selector:`, which
                          also say whether Executor implements them
  2. Universal Interfaces the inline glue in CIncludes: the instruction(s)
                          before the trap word load the selector, e.g.
                          MOVE.W #$37,-(SP); _OSDispatch  (3F3C 0037 A88F)
                          MOVEQ #2,D0; _AliasDispatch     (7002 A823)
  3. learned.yaml         `dispatchers:` entries: anything we work out

Locations (where the tracer reads the selector at the A-line, before the
dispatcher has pushed anything):

  D0     D0 & mask            (MOVEQ / MOVE.W / MOVE.L #sel,D0)
  StackW (A7).w & mask        (MOVE.W #sel,-(SP))
  StackL (A7).l & mask        (MOVE.L #sel,-(SP))
  TrapBits trap word & mask   (Gestalt: $A1AD / $A3AD / $A5AD)

  python3 dispatch.py                 summary + conflicts
  python3 dispatch.py --show A825     one dispatcher's selectors
  python3 dispatch.py --emit-c FILE   the tracer's table (src/core/atrap_dispatchers.inc)
"""
import argparse
import re
from collections import Counter, defaultdict
from pathlib import Path

import yaml

from mvdb import DB, DEFAULT_DEFS, trap_slot
import learned

ROOT = Path(__file__).resolve().parents[2]
UI_CINCLUDES = ROOT / "private/Universal Interfaces/Universal/Interfaces/CIncludes"
KINDS = {"D0": 1, "StackW": 2, "StackL": 3, "TrapBits": 4}
# Glue that loads D0 with an argument, not a selector.
NOT_DISPATCHERS = {"SysError"}


def ui_trap_names():
    """(table, index) -> names from Universal Interfaces' Traps.h."""
    names = defaultdict(list)
    f = UI_CINCLUDES / "Traps.h"
    if f.exists():
        for m in re.finditer(r"_(\w+)\s*=\s*0x(A[0-9A-Fa-f]{3})", f.read_text(encoding="mac_roman")):
            names[trap_slot(int(m.group(2), 16))].append(m.group(1))
    return names


def parse_location(loc):
    """multiversal selector-location -> (kind, mask)."""
    if loc is None:
        return None
    m = re.match(r"(\w+?)(?:<(0x[0-9A-Fa-f]+)>)?$", loc)
    base, mask = m.group(1), int(m.group(2), 16) if m.group(2) else None
    if base == "D0W":
        return ("D0", 0xFFFF)
    if base == "D0L":
        return ("D0", 0xFFFFFFFF)
    if base == "D0":
        return ("D0", mask or 0xFFFF)
    if base in ("StackW", "StackWMasked", "StackWLookahead"):
        return ("StackW", mask or 0xFFFF)
    if base in ("StackL", "StackLMasked"):
        return ("StackL", mask or 0xFFFFFFFF)
    if base == "TrapBits":
        return ("TrapBits", 0x0600)
    raise ValueError(f"unknown selector-location {loc}")


def glue_selector(words):
    """Inline glue words (ending in the trap word) -> (kind, value[, sub]) or None.

    Component calls (MOVE.L #paramSize<<16|what,-(SP); MOVEQ #0,D0;
    _ComponentDispatch) return the long as a sub-selector.
    """
    w = words[:-1]
    if len(w) >= 4 and w[-4] == 0x2F3C and w[-1] == 0x7000:
        return ("D0", 0, (w[-3] << 16) | w[-2])
    # The selector load is the last instruction before the trap; a sub-
    # selector (component call: MOVE.L #what,-(SP); MOVEQ #0,D0) comes
    # before it.
    for i in range(len(w) - 1, -1, -1):
        op = w[i]
        if (op & 0xFF00) == 0x7000 and i == len(w) - 1:          # MOVEQ #n,D0
            v = op & 0xFF
            return ("D0", (v - 0x100 if v & 0x80 else v) & 0xFFFF)
        if op == 0x303C and i == len(w) - 2:                     # MOVE.W #n,D0
            return ("D0", w[i + 1])
        if op == 0x203C and i == len(w) - 3:                     # MOVE.L #n,D0
            return ("D0", (w[i + 1] << 16) | w[i + 2])
        if op == 0x3F3C and i == len(w) - 2:                     # MOVE.W #n,-(SP)
            return ("StackW", w[i + 1])
        if op == 0x2F3C and i == len(w) - 3:                     # MOVE.L #n,-(SP)
            return ("StackL", (w[i + 1] << 16) | w[i + 2])
    return None


def ui_glue():
    """[(trap word, name, (kind, value), file)] from Universal Interfaces."""
    out = []
    if not UI_CINCLUDES.exists():
        return out
    for f in sorted(UI_CINCLUDES.glob("*.h")):
        text = f.read_text(encoding="mac_roman").replace("\r", "\n")
        for stmt in text.split(";"):
            if "WORDINLINE" not in stmt:
                continue
            m = re.search(r"EXTERN_API\w*\s*\([^)]*\)\s*(\w+)", stmt)
            g = re.search(r"WORDINLINE\(([^)]*)\)", stmt)
            if not m or not g:
                continue
            try:
                words = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{1,4})\b", g.group(1))]
            except ValueError:
                continue
            if not words or (words[-1] & 0xF000) != 0xA000:
                continue
            if len(words) != len([x for x in g.group(1).split(",") if x.strip()]):
                continue      # macro arguments (paramSize, callNumber): not literal
            sel = glue_selector(words)
            if sel:
                out.append((words[-1], m.group(1), sel, f.name))
    return out


class Dispatchers:
    def __init__(self, db=None, learned_path=learned.DEFAULT_LEARNED):
        self.db = db or DB(DEFAULT_DEFS)
        self.slots = {}       # slot -> dispatcher dict
        self.conflicts = []
        self._from_multiversal()
        self._from_ui()
        self._from_learned(learned_path)

    def _slot(self, trap, name, source):
        slot = trap_slot(trap)
        d = self.slots.get(slot)
        if d is None:
            d = self.slots[slot] = {"slot": slot, "trap": trap, "name": name, "kind": None,
                                    "mask": None, "source": source, "selectors": {},
                                    "sub": None}
        return d

    def _add_sel(self, d, value, name, source, executor=None, status=None):
        value &= d["mask"] if d["mask"] else 0xFFFFFFFF
        cur = d["selectors"].get(value)
        if cur is None:
            d["selectors"][value] = {"name": name, "source": source, "executor": executor,
                                     "status": status, "aka": []}
        elif name != cur["name"] and name not in cur["aka"]:
            cur["aka"].append(name)

    def _from_multiversal(self):
        for name, v in self.db.dispatchers.items():
            d = self._slot(v["trap"], name, "multiversal")
            d["kind"], d["mask"] = parse_location(v["location"])
        by_name = {d["name"]: d for d in self.slots.values()}
        for f in self.db.selector_funcs:
            d = by_name.get(f["dispatcher"])
            if d:
                self._add_sel(d, f["selector"], f["name"], "multiversal", bool(f["executor"]))

    def _from_ui(self):
        votes = defaultdict(Counter)
        routines = defaultdict(set)
        glue = ui_glue()
        names = ui_trap_names()
        for trap, fname, sel, _ in glue:
            votes[trap_slot(trap)][sel[0]] += 1
            routines[trap_slot(trap)].add(fname)
        for trap, fname, sel, hfile in glue:
            kind, value = sel[0], sel[1]
            slot = trap_slot(trap)
            d = self.slots.get(slot)
            if d is None:
                # A new dispatcher needs two or more routines behind it;
                # one routine loading D0 is just passing an argument.
                tname = (names.get(slot) or self.db.traps.get(slot) or [f"Dispatch{trap:04X}"])[0]
                if len(routines[slot]) < 2 or tname in NOT_DISPATCHERS:
                    continue
                d = self._slot(trap, tname, "Universal Interfaces")
            if d["kind"] is None:
                d["kind"] = votes[slot].most_common(1)[0][0]
                d["mask"] = 0xFFFF if d["kind"] in ("D0", "StackW") else 0xFFFFFFFF
            if kind != d["kind"]:
                self.conflicts.append((slot, fname, kind, d["kind"], hfile))
                continue
            if len(sel) > 2:
                d.setdefault("sub_selectors", {}).setdefault(sel[2], []).append(fname)
                continue
            self._add_sel(d, value, fname, f"UI {hfile}")
        # ComponentDispatch: D0 = 0 is "call a component"; the component's
        # own selector is the long on the stack (paramSize << 16 | what).
        comp = self.slots.get(trap_slot(0xA82A))
        if comp:
            comp["sub"] = {"when": 0, "kind": "StackL", "mask": 0xFFFFFFFF}
            comp["selectors"][0] = {"name": "CallComponent", "source": "Universal Interfaces",
                                    "executor": None, "status": None, "aka": []}

    def _from_learned(self, path):
        path = Path(path)
        data = (yaml.safe_load(path.read_text()) if path.exists() else {}) or {}
        for e in data.get("dispatchers") or []:
            slot = trap_slot(e["trap"])
            d = self.slots.get(slot) or self._slot(e["trap"], e["name"], "learned")
            if e.get("location"):
                d["kind"], d["mask"] = parse_location(e["location"])
            for s in e.get("selectors") or []:
                self._add_sel(d, s["value"], s["name"], "learned", status=s.get("status"))

    # ── lookups ──────────────────────────────────────────────────────
    def get(self, trap):
        return self.slots.get(trap_slot(trap))

    def selector_name(self, trap, sel):
        d = self.get(trap)
        if not d or sel is None:
            return None
        s = d["selectors"].get(sel & (d["mask"] or 0xFFFFFFFF))
        return s["name"] if s else None

    def sub_selector_names(self, trap, sub):
        """Component routines sharing this (paramSize << 16 | what)."""
        d = self.get(trap)
        return (d or {}).get("sub_selectors", {}).get(sub, [])

    def emit_c(self, path):
        rows = []
        for (table, index), d in sorted(self.slots.items()):
            if not d["kind"]:
                continue
            sub = d["sub"] or {}
            rows.append(f'    {{ {1 if table == "tool" else 0}, 0x{index:03X}, {KINDS[d["kind"]]}, '
                        f'0x{d["mask"]:08X}u, {KINDS.get(sub.get("kind"), 0)}, '
                        f'0x{sub.get("when", 0):08X}u }},  // {d["name"]}')
        Path(path).write_text(
            "// Generated by tools/macdecode/dispatch.py --emit-c; do not edit.\n"
            "// { toolbox, index, kind, mask, sub kind, sub when }\n"
            "// kind: 1 D0, 2 (A7).w, 3 (A7).l, 4 trap word bits\n"
            + "\n".join(rows) + "\n")
        return len(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--show")
    ap.add_argument("--emit-c")
    a = ap.parse_args()
    ds = Dispatchers()
    if a.emit_c:
        print(f"{ds.emit_c(a.emit_c)} dispatchers -> {a.emit_c}")
        return
    if a.show:
        d = ds.get(int(a.show, 16))
        print(f"{d['trap']:04X} {d['name']} [{d['source']}] selector {d['kind']} & {d['mask']:X}")
        for v, s in sorted(d["selectors"].items()):
            ex = {True: "executor", False: "-", None: "?"}[s["executor"]]
            aka = f" (also {', '.join(s['aka'])})" if s["aka"] else ""
            print(f"  {v:08X} {s['name']:36} {ex:8} {s['source']}{aka}")
        return
    src = Counter(d["source"] for d in ds.slots.values())
    nsel = sum(len(d["selectors"]) for d in ds.slots.values())
    print(f"{len(ds.slots)} dispatchers ({dict(src)}), {nsel} selectors, "
          f"{len(ds.conflicts)} glue entries disagreeing with the dispatcher's location")
    for (table, index), d in sorted(ds.slots.items()):
        ex = sum(1 for s in d["selectors"].values() if s["executor"])
        print(f"  {d['trap']:04X} {d['name']:24} {str(d['kind']):8} {d['mask']:08X} "
              f"{len(d['selectors']):4} sel, executor {ex:3}  [{d['source']}]")


if __name__ == "__main__":
    main()
