#!/usr/bin/env python3
"""test_dev_disks.py — boot a dev boot disk with the Development volume and
launch every development app on it.

Each app is launched through the bridge, counts as running when it is the
front process, and is quit before the next. Copies of the images are used,
so the built ones are never written.

  test_dev_disks.py --boot dev-7.6.1.img --dev Development.img --backend uae
"""

import argparse
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from guest_install import Guest, work_root  # noqa: E402

# (path on Development, front process name, architectures it runs on)
APPS = [
    ("CodeWarrior Pro 4:Metrowerks CodeWarrior:CodeWarrior IDE 3.2", "CodeWarrior IDE 3.2", "68k ppc"),
    ("MPW:MPW Shell", "MPW Shell", "68k ppc"),
    ("THINK C 6.0:THINK C:THINK Project Manager", "THINK Project Manager", "68k ppc"),
    ("Symantec C++ 7:Symantec C++ for Macintosh:THINK Project Manager", "THINK Project Manager", "68k ppc"),
    ("Languages:THINK Pascal 4.5d4:Think Pascal:THINK Pascal 4.5d4:THINK Pascal 4.5", "THINK Pascal 4.5", "68k ppc"),
    ("Languages:MacPerl ƒ:MacPerl", "MacPerl", "68k ppc"),
    ("Languages:Python 1.5.2c1:PythonInterpreter", "PythonInterpreter", "68k ppc"),
    ("Languages:HyperCard 2.4:HyperCard", "HyperCard", "68k ppc"),
    ("Tools:ResEdit 2.1.3:ResEdit 2.1.3:ResEdit", "ResEdit", "68k ppc"),
    ("Tools:BBEdit Lite 4.1:BBEdit Lite 4.1", "BBEdit Lite", "68k ppc"),
    ("Tools:Disk Copy 6.3.3:Disk Copy", "Disk Copy", "68k ppc"),
    ("Tools:StuffIt Expander™ 5.5:StuffIt Expander™", "StuffIt Expander", "68k ppc"),
    ("Languages:Frontier 5.0.1:UserLand Frontier™", "UserLand Frontier", "68k ppc"),
    # These don't quit on their own (a modal dialog): the
    # emulator is restarted after each.
    ("Tools:ZTerm 1.0.1:ZTerm 1.0.1", "ZTerm", "68k ppc"),          # "Modem Port not available": no serial port
    ("Tools:Resorcerer 2.0:Resorcerer® 2.0 (Fat)", "Resorcerer", "68k ppc"),   # asks for a serial number
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--boot", required=True)
    ap.add_argument("--dev", required=True)
    ap.add_argument("--backend", default="uae")
    ap.add_argument("--port", type=int, default=19470)
    ap.add_argument("--only", help="substring: test only matching apps")
    args = ap.parse_args()
    arch = "ppc" if args.backend == "kpx" else "68k"
    work = tempfile.mkdtemp(prefix="test_dev.", dir=work_root())
    boot, dev = os.path.join(work, "boot.img"), os.path.join(work, "dev.img")
    shutil.copyfile(args.boot, boot)
    shutil.copyfile(args.dev, dev)
    def start():
        g = Guest(work, args.backend, [boot, dev], [], args.port)
        g.wait_finder(240)
        time.sleep(45)                      # first mount: the Finder builds Development's desktop file
        return g

    guest = start()
    results = []
    try:
        for n, (path, proc, archs) in enumerate(APPS):
            if arch not in archs.split() or (args.only and args.only not in path):
                continue
            r = guest.post("/api/launch", {"path": "Development:" + path}, 240) or {}
            ok, front = False, ""
            if r.get("success"):
                deadline = time.time() + 90
                while time.time() < deadline:
                    front = guest.app()
                    if proc.lower() in front.lower():
                        ok = True
                        break
                    time.sleep(2)
            name = path.rsplit(":", 1)[-1]
            if ok:
                time.sleep(8)               # let it finish starting: some put up an Open dialog late
            guest.shot(f"{n:02d}-" + ("ok-" if ok else "FAIL-") + name.replace("/", "_"))
            results.append((ok, name, r.get("message") or r.get("error") or "", front))
            print(f"{'ok  ' if ok else 'FAIL'} {name}  ({r.get('message') or r.get('error')}; front: {front})",
                  flush=True)
            if ok and not guest.close_front(proc):
                print(f"     {name} would not quit; restarting the emulator", flush=True)
                guest.proc.terminate()
                guest.proc.wait(timeout=60)
                time.sleep(3)
                guest = start()
            time.sleep(3)
        guest.shutdown()
    finally:
        if guest.proc.poll() is None:
            guest.proc.terminate()
    failed = [r for r in results if not r[0]]
    print(f"{len(results) - len(failed)}/{len(results)} apps ran ({args.backend}, {os.path.basename(args.boot)})"
          f"; screenshots in {work}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
