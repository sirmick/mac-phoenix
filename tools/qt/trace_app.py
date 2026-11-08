#!/usr/bin/env python3
"""trace_app.py — run one application on a ROM machine with --trace-atraps
and snapshot it, for the QuickTime survey (docs/executor/QuickTime.md).

Copies of the images are booted (the built ones are never written). The app
is launched through the bridge (or a document opened with its creator), left
running WAIT seconds, optionally clicked at, then snapshotted: the snapshot's
atraps.tsv has every A-trap the app made (UAE only: the tracer is the 68k
interpreter's). qt_calls.py reads it.

  trace_app.py --boot 7.6.1 --disk games --launch "Games:Ares 1.2.0 ƒ:Ares" \\
      --wait 60 --name qt-ares
  trace_app.py --boot 7.6.1 --open "Macintosh HD:System Folder:Extensions:Demo click&drag" ...
  trace_app.py ... --click 320,240 --key return     (in order, after WAIT)
"""
import argparse
import os
import shutil
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
import bake  # noqa: E402
from guest import Guest, free_port  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--boot", required=True, help="built image to boot (7.6.1, 8.6, ...)")
    ap.add_argument("--disk", action="append", default=[], help="more built images")
    ap.add_argument("--machine", choices=["68k", "ppc"], default="68k")
    ap.add_argument("--launch", help="application to launch (Mac path)")
    ap.add_argument("--open", help="document to open with its creator (Mac path)")
    ap.add_argument("--wait", type=int, default=30, help="seconds to leave it running")
    ap.add_argument("--step", action="append", default=[],
                    help="after WAIT, in order: click:X,Y | dclick:X,Y | key:NAME | sleep:S | shot:NAME")
    ap.add_argument("--name", required=True, help="snapshot name (<storage>/snapshots/NAME)")
    ap.add_argument("--extra", action="append", default=[], help="emulator argument")
    a = ap.parse_args()

    work = tempfile.mkdtemp(prefix=f"qt-trace.{a.name}.", dir=bake.work_root())
    disks = []
    for name in [a.boot] + a.disk:
        local = os.path.join(work, name + ".img")
        shutil.copyfile(os.path.join(bake.IMAGES, name + ".img"), local)
        disks.append(local)
    extra = (["--trace-atraps"] if a.machine == "68k" else []) + a.extra
    g = Guest(work, bake.BACKENDS[a.machine], disks, [], free_port(), bake.STORAGE, extra=extra)
    try:
        g.wait_finder(300)
        time.sleep(10)
        if a.launch:
            r = g.bridge("/api/launch", {"path": a.launch})
        else:
            r = g.bridge("/api/launch", {"path": a.open, "open": True})
        print("launch:", r)
        time.sleep(a.wait)
        g.shot("after-wait")
        for s in a.step:
            kind, _, arg = s.partition(":")
            if kind in ("click", "dclick"):
                x, y = (int(v) for v in arg.split(","))
                (g.click if kind == "click" else g.dclick)(x, y)
            elif kind == "key":
                g.post("/api/keypress", {"key": arg})
            elif kind == "sleep":
                time.sleep(float(arg))
            elif kind == "shot":
                g.shot(arg)
            time.sleep(1)
        print("front:", g.app())
        g.shot("before-snapshot")
        print("snapshot:", g.post("/api/snapshot", {"name": a.name}))
        time.sleep(5)
    finally:
        g.stop()
        for d in disks:
            os.remove(d)
    print("work:", work)


if __name__ == "__main__":
    main()
