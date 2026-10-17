#!/usr/bin/env python3
"""test_image.py — boot built images and launch every app their snapshots list.

The apps come from the images' records (build.py writes <image>.img.json:
each snapshot's "launch" list as paths on the image). Each app is launched
through the bridge, counts as running when it is the front process, and is
quit before the next; an app marked "no_quit" (a modal dialog it can't get
past here: a serial number, a missing serial port) is left by restarting the
emulator. The aliases build.py rewrote are then resolved in the guest by
Frontier (file.followAlias), when one of the images has it. Copies of the
images are booted, so the built ones are never
written. Runs on each machine (68k, ppc) unless --machine says one.

  test_image.py dev-7.6.1 Development [--machine 68k] [--only Perl]
"""

import argparse
import json
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bake  # noqa: E402
from guest import Guest  # noqa: E402


def norm(name):
    return "".join(c for c in name.lower() if c.isalnum())


def is_front(front, app):
    a, b = norm(front), norm(app.rsplit(":", 1)[-1])
    return bool(a) and a != "finder" and (a in b or b in a)


def record(name):
    path = os.path.join(bake.IMAGES, name + ".img.json")
    if not os.path.exists(path):
        sys.exit(f"{name}: no build record ({path}); build.py {name}")
    return bake.load_json(path)


def check_aliases(guest, aliases, frontier, log=print):
    """Resolve each alias with Frontier; True if all resolve."""
    guest.bridge("/api/launch", {"path": frontier})
    time.sleep(15)
    if not guest.frontier_ready():
        log(f"FAIL aliases: Frontier ({frontier}) does not answer scripts")
        return False
    lst = ", ".join('"' + a.replace('"', '\\"') + '"' for a in aliases)
    script = ('local (s = "", p); for p in {' + lst + '} {try {s = s + p + "\t" + file.followAlias (p) + "\\r"} '
              'else {s = s + p + "\tFAIL " + tryError + "\\r"}}; file.writeWholeFile ("Host:aliases.txt", s)')
    out = guest.run_script("LAND", script, "aliases.txt", 60) or ""
    ok = True
    for a in aliases:
        line = next((l for l in out.splitlines() if l.startswith(a + "\t")), None)
        good = line is not None and "\tFAIL" not in line
        ok &= good
        log(f"{'ok  ' if good else 'FAIL'} alias {a} -> {line.split(chr(9), 1)[1] if line else '(no answer)'}")
    guest.close_front("Frontier")
    return ok


def run(images, machine, port, only, log=print):
    work = tempfile.mkdtemp(prefix=f"test_image.{machine}.", dir=bake.work_root())
    disks, apps, aliases = [], [], []
    for name in images:
        local = os.path.join(work, name + ".img")
        shutil.copyfile(os.path.join(bake.IMAGES, name + ".img"), local)
        disks.append(local)
        rec = record(name)
        apps += [e for e in rec["launch"] if machine in e.get("machines", bake.BACKENDS)]
        aliases += rec.get("aliases", [])
    frontier = next((e["app"] for e in apps if e["snapshot"].startswith("frontier")), None)
    apps = [e for e in apps if not only or only.lower() in e["app"].lower()]

    def start():
        g = Guest(work, bake.BACKENDS[machine], disks, [], port, bake.STORAGE)
        try:
            g.wait_finder(240)
        except BaseException:
            g.shot("boot-failed")
            g.stop()                        # never leave it running (it holds the port)
            raise
        time.sleep(10)                      # first mount: the Finder builds each volume's desktop file
        return g

    guest = start()
    results = []
    try:
        for n, e in enumerate(apps):
            path, leaf = e["app"], e["app"].rsplit(":", 1)[-1]
            r = guest.bridge("/api/launch", {"path": path})
            ok, front = False, ""
            if r.get("success"):
                deadline = time.time() + 90
                while time.time() < deadline:
                    front = guest.app()
                    if is_front(front, path):
                        ok = True
                        break
                    time.sleep(2)
            if ok:
                time.sleep(8)               # let it finish starting: some put up an Open dialog late
            guest.shot(f"{n:02d}-" + ("ok-" if ok else "FAIL-") + leaf.replace("/", "_"))
            facts = [k for k in e if k not in ("app", "snapshot", "machines")]
            results.append(ok)
            log(f"{'ok  ' if ok else 'FAIL'} {path}  [{e['snapshot']}]"
                + (f" {', '.join(facts)}" if facts else "")
                + ("" if ok else f"  ({r.get('message') or r.get('error')}; front: {front})"))
            if ok and (e.get("no_quit") or not guest.close_front(front)):
                if not e.get("no_quit"):
                    log(f"     {leaf} would not quit; restarting the emulator")
                guest.stop()
                time.sleep(3)
                guest = start()
            time.sleep(3)
        if aliases and frontier and not only:
            results.append(check_aliases(guest, aliases, frontier, log))
        guest.shutdown()
    finally:
        guest.stop()
    failed = results.count(False)
    log(f"{machine}: {len(results) - failed}/{len(results)} passed (apps{', aliases' if len(results) > len(apps) else ''};"
        f" {', '.join(images)}); screenshots in {work}")
    return failed == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("images", nargs="+", help="built images, the boot image first")
    ap.add_argument("--machine", choices=sorted(bake.BACKENDS), action="append")
    ap.add_argument("--port", type=int, default=19470)
    ap.add_argument("--only", help="substring: test only matching apps")
    args = ap.parse_args()
    ok = True
    for i, m in enumerate(args.machine or sorted(bake.BACKENDS)):
        ok &= run(args.images, m, args.port + i, args.only)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
