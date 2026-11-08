#!/usr/bin/env python3
"""qt_static.py — the QuickTime calls an application's code can make.

Static, for code paths a traced run doesn't reach (Ares' movies play
chapters into the game) and for PowerPC code, which calls QuickTimeLib
through CFM, not traps (the tracer sees only 68k A-traps):

  - PowerPC: the PEF fragment's imports from QuickTimeLib (and the other
    QuickTime libraries), by name;
  - 68k: the trap glue in its CODE resources: _QuickTime ($AAAA) and
    _CodecDispatch ($AAA3) with the selector loaded into D0 (MOVEQ,
    MOVE.W #, MOVE.L #) just before, named from Universal Interfaces; and
    component calls (MOVE.L #{paramSize,what},-(A7); MOVEQ #0,D0;
    _ComponentDispatch), listed by (paramSize, what) with the names they
    could be.

  qt_static.py IMAGE "Vol:Folder:App" [--markdown]
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/macdecode"))
sys.path.insert(0, str(ROOT / "tools/qt"))
from rsrc import macbinary_forks, parse_resource_fork, decompress  # noqa: E402
from pef import PEF  # noqa: E402
import qt_calls  # noqa: E402

QT_LIBS = ("QuickTimeLib", "QuickTimeMusicLib", "QuickTimeVRLib", "QuickTimeStreamingLib")


def extract(image, path):
    """MacBinary of one file on an HFS image (hfsutils), or None."""
    home = tempfile.mkdtemp(prefix="qtstatic-")
    env = dict(os.environ, HOME=home)
    out = os.path.join(home, "f.bin")
    mac = path.split(":", 1)[1] if ":" in path else path
    subprocess.run(["hmount", image], env=env, capture_output=True, check=True)
    try:
        subprocess.run(["hcopy", "-m", (":" + mac).encode("mac_roman"), out], env=env, capture_output=True)
    finally:
        subprocess.run(["humount"], env=env, capture_output=True)
    return Path(out).read_bytes() if os.path.exists(out) else None


def ppc_imports(data, res):
    """{(fragment, library): [symbol]} for every PEF container in the data
    fork, named from the 'cfrg' when it has an entry at that offset."""
    names = {}
    cfrg = res.get(("cfrg", 0))
    if cfrg:
        d = cfrg[1]
        o = 32
        for _ in range(struct.unpack_from(">I", d, 28)[0]):
            off = struct.unpack_from(">I", d, o + 24)[0]
            names[off] = d[o + 43:o + 43 + d[o + 42]].decode("mac_roman")
            o += struct.unpack_from(">H", d, o + 40)[0]
    out = {}
    i = data.find(b"Joy!peff")
    while i >= 0:
        try:
            p = PEF(data[i:])
            for k, (name, _cls) in enumerate(p.syms):
                out.setdefault((names.get(i, f"PEF at ${i:X}"), p.symlib.get(k, "?")), []).append(name)
        except (AssertionError, struct.error, StopIteration, ValueError, IndexError):
            pass
        i = data.find(b"Joy!peff", i + 8)
    return out


def m68k_glue(res):
    """Counter of (trap, name) for the trap glue in CODE resources."""
    calls = Counter()
    for (t, i), (_n, d) in res.items():
        if t != "CODE" or i == 0:
            continue
        try:
            d = decompress(d)
        except Exception:
            pass
        for o in range(2, len(d) - 1, 2):
            w = struct.unpack_from(">H", d, o)[0]
            if w in (0xAAAA, 0xAAA3):
                sel = None
                p = struct.unpack_from(">H", d, o - 2)[0]
                if (p & 0xFF00) == 0x7000:
                    sel = p & 0xFF
                elif o >= 4 and struct.unpack_from(">H", d, o - 4)[0] == 0x303C:
                    sel = p
                elif o >= 6 and struct.unpack_from(">H", d, o - 6)[0] == 0x203C:
                    sel = p
                name = qt_calls.UI_TRAPS.get((w, sel)) if sel is not None else None
                calls[("QuickTime" if w == 0xAAAA else "ICM",
                       name or (f"#{sel:X}" if sel is not None else "? (selector not loaded just before)"))] += 1
            elif w == 0xA82A and o >= 8 and struct.unpack_from(">H", d, o - 2)[0] == 0x7000 \
                    and struct.unpack_from(">H", d, o - 8)[0] == 0x2F3C:
                psize, what = struct.unpack_from(">HH", d, o - 6)
                cands = qt_calls.UI_COMP.get((psize, what), [])
                movies = [c for c in cands if c.split("[")[1].startswith(("Movies", "QuickTime", "Image"))]
                calls[("Component", f"what={what if what < 0x8000 else what - 0x10000} p={psize}"
                       + (f" ({' | '.join(movies[:3])})" if movies else ""))] += 1
    return calls


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("path")
    ap.add_argument("--markdown", action="store_true")
    a = ap.parse_args()
    blob = extract(a.image, a.path)
    if not blob:
        sys.exit(f"{a.path}: not found on {a.image}")
    data, rfork = macbinary_forks(blob)
    res = parse_resource_fork(rfork) if rfork else {}
    imports = ppc_imports(data, res)
    for frag in sorted({f for f, _ in imports}):
        libs = {lib: syms for (f, lib), syms in imports.items() if f == frag}
        qt = {lib: syms for lib, syms in libs.items()
              if lib in QT_LIBS or "QuickTime" in lib or "Multimedia" in lib}
        print(f"PowerPC fragment {frag}: " + ", ".join(f"{lib} ({len(v)})" for lib, v in sorted(libs.items())))
        for lib, syms in sorted(qt.items()):
            print(f"  imports from {lib}: " + ", ".join(sorted(syms)))
    calls = m68k_glue(res)
    if calls:
        print(f"68k trap glue ({sum(calls.values())} sites):")
        for (mgr, name), n in sorted(calls.items()):
            print(f"  {mgr:<10} {name:<60} x{n}")


if __name__ == "__main__":
    main()
