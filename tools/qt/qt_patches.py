#!/usr/bin/env python3
"""qt_patches.py — the traps an extension patches on a real boot.

Reads <snapshot>/trap_installs.tsv (any UAE run with --trace-atraps: every
_SetTrapAddress and variant, in order) and resolves the installing code and
the new routine to the resources they came from (as atraps.py does: bytes
matched in the snapshot's heaps against the boot disk's System Folder).
Lists the installs made by FILE's code or pointing into it, plus the
unresolved installs made while FILE's INIT ran (code it copied into a heap
block of its own matches no resource).

  qt_patches.py SNAPSHOT [--file "QuickTime™"] [--markdown]
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/macdecode"))
import macdecode as md  # noqa: E402
from atraps import Resolver, ui_names  # noqa: E402
from mvdb import trap_slot  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("snapshot")
    ap.add_argument("--file", default="QuickTime™")
    ap.add_argument("--markdown", action="store_true")
    a = ap.parse_args()
    snap = md.Snapshot(a.snapshot)
    db = md.load_db()
    zones = md.find_zones(snap)
    res = Resolver(snap, zones, (snap.meta.get("disks") or [None])[0], md.World(snap, db, None, zones))
    names = ui_names()

    def where(addr):
        if snap.in_rom(addr):
            return "ROM"
        code = bytes(snap.ram[max(addr - 8, 0):addr + 16]) if snap.ok(addr, 16) else b""
        return res.resolve({"pc": addr, "code": code, "owner": ""})

    def trap_name(table, index):
        word = (0xA800 | index) if table == "tool" else (0xA000 | index)
        k = trap_slot(word)
        n = (db.traps.get(k) or names.get(k) or [""])[0]
        return f"${word:04X} {n}".strip()

    rows = []
    for line in (Path(a.snapshot) / "trap_installs.tsv").read_text(encoding="latin-1").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        f = line.split("\t")
        seq, table, index = int(f[0]), f[2], int(f[3], 16)
        addr, old, pc = int(f[4], 16), int(f[5], 16), int(f[6], 16)
        inst = f[8] if len(f) > 8 and f[8] else where(pc)
        tgt = f[9] if len(f) > 9 and f[9] else where(addr)
        rows.append((seq, trap_name(table, index), inst, tgt, where(old), addr))
    mine = [i for i, r in enumerate(rows) if a.file in r[2] or a.file in r[3]]
    if not mine:
        sys.exit(f"no installs by {a.file}")
    # The INIT's window: from its first install to the next install by
    # another file's (resolved) code.
    lo = mine[0]
    hi = lo
    while hi + 1 < len(rows) and (a.file in rows[hi + 1][2] or rows[hi + 1][2] in ("?", "") or
                                  rows[hi + 1][2].startswith("(code)")):
        hi += 1
    sel = sorted(set(mine) | set(range(lo, hi + 1)))
    if a.markdown:
        print("| seq | trap | installed by | new routine | previous routine |\n|---|---|---|---|---|")
    for i in sel:
        seq, tn, inst, tgt, old, addr = rows[i]
        if a.markdown:
            print(f"| {seq} | {tn} | {inst} | {tgt} | {old} |")
        else:
            print(f"{seq:>8} {tn:<28} by {inst:<40} new {tgt:<40} was {old}")


if __name__ == "__main__":
    main()
