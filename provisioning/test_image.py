#!/usr/bin/env python3
"""test_image.py — boot built images and launch every app their snapshots list.

The apps come from the images' records (build.py writes <image>.img.json:
each snapshot's "launch" list as paths on the image). Each app is launched
through the bridge, counts as running when it is the front process, and is
quit before the next; an app marked "no_quit" (a modal dialog it can't get
past here: a serial number, a missing serial port) is left by restarting the
emulator, as is one that failed. "front" names the process it brings to the
front when that isn't itself (Office 98's: Microsoft Office First Run). The
aliases build.py rewrote are then resolved in the guest by Frontier
(file.followAlias), when one of the images has it. Copies of the images are
booted, so the built ones are never written. Runs on each machine (68k,
ppc) unless --machine says one.

  test_image.py dev-7.6.1 dev-tools-system-7 [--machine 68k] [--only Perl ...]
  test_image.py 7.6.1 games --depth 8             the 68k screen in 256 colours
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


def try_launch(guest, e):
    """Launch an app; (it came to the front and stayed, the front app, the result)."""
    path = e["app"]
    if guest.agent:
        r = guest.bridge("/api/launch", {"path": path})
    else:                                       # System 6: the Finder, by name
        try:
            guest.finder_open(path)
            r = {"success": True}
        except RuntimeError as ex:
            r = {"success": False, "error": str(ex)}
    ok, front = False, ""
    if r.get("success"):
        deadline = time.time() + 90
        while time.time() < deadline:
            front = guest.app()
            if is_front(front, e.get("front") or path):     # "front": the process it brings up
                ok = True
                break
            time.sleep(2)
    if ok:
        time.sleep(8)                           # let it finish starting: some put up an Open dialog late
        refused = next((t["text"] for t in (guest.get("/api/text") or {}).get("text", [])
                        if REFUSAL.search(t["text"])), None)
        if refused:                             # running, but only to say it won't
            ok = False
            r = {"success": False, "error": f"refused: {refused!r}"}
    return ok, front, r


def run(images, machine, port, only, log=print, extra=()):
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
    apps = [e for e in apps if not only or any(o.lower() in e["app"].lower() for o in only)]

    def start():
        g = Guest(work, bake.BACKENDS[machine], disks, [], port, bake.STORAGE, extra=image_args(images[0]) + list(extra))
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
            for attempt in (1, 2):              # a launch that didn't come up gets a second
                ok, front, r = try_launch(guest, e)     # go on a fresh guest: driving the
                if ok or attempt == 2 or r.get("error", "").startswith("refused"):   # Finder (System
                    break                       # 6) depends on timing; a refusal is the app's answer
                log(f"     {leaf}: {r.get('message') or r.get('error')}; once more, on a fresh guest")
                guest.stop()
                time.sleep(3)
                guest = start()
            guest.shot(f"{n:02d}-" + ("ok-" if ok else "FAIL-") + leaf.replace("/", "_"))
            facts = [k for k in e if k not in ("app", "snapshot", "machines")]
            results.append(ok)
            log(f"{'ok  ' if ok else 'FAIL'} {path}  [{e['snapshot']}]"
                + (f" {', '.join(facts)}" if facts else "")
                + ("" if ok else f"  ({r.get('message') or r.get('error')}; front: {front})"))
            # A failure can leave the guest anywhere (a refusal, a system-modal
            # alert: Dark Castle's on an SE): start afresh, so it fails alone.
            if not ok or e.get("no_quit") or not guest.close_front(front):
                if ok and not e.get("no_quit"):
                    log(f"     {leaf} would not quit; restarting the emulator")
                guest.stop()
                time.sleep(3)
                guest = start()
            time.sleep(3)
        if aliases and frontier and not only:
            results.append(check_aliases(guest, aliases, frontier, log))
        try:
            guest.shutdown()
        except RuntimeError as ex:              # (a test copy: an unclean stop is fine)
            log(f"     shutdown: {ex}")
    finally:
        guest.stop()
    failed = results.count(False)
    log(f"{machine}: {len(results) - failed}/{len(results)} passed (apps{', aliases' if len(results) > len(apps) else ''};"
        f" {', '.join(images)}); screenshots in {work}")
    return failed == 0


# What an application says when it starts only to refuse to run here.
import re
REFUSAL = re.compile(r"will not run|cannot run|can.t run|requires (a|an|system|the)|only runs in|will only run"
                     r"|must be set to|could not be opened|couldn.t (be initiali[sz]ed|create)"
                     r"|insert the .*CD|unable to find the .*CD|must have the .*CD", re.I)


def image_args(name):
    """Emulator arguments a built image needs: its base System's (bases.json
    "args": --model-id 5 for 7.1)."""
    seen = set()
    while name and name not in seen:
        seen.add(name)
        try:
            d = bake.load_json(os.path.join(bake.HERE, "images", name + ".json"))
        except (OSError, ValueError):
            break
        base = bake.bases().get(d.get("base", ""), {})
        if "args" in base:
            return base["args"]
        name = base.get("built")
    return []


def image_machines(name):
    """The machines a built image boots on: its base System's (bases.json
    "machines": 8.6 and 9.0.4 the PowerPC one), else both."""
    seen = set()
    while name and name not in seen:
        seen.add(name)
        try:
            d = bake.load_json(os.path.join(bake.HERE, "images", name + ".json"))
        except (OSError, ValueError):
            break
        base = bake.bases().get(d.get("base", ""), {})
        if "machines" in base:
            return base["machines"]
        name = base.get("built")
    return bake.BOTH


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("images", nargs="+", help="built images, the boot image first")
    ap.add_argument("--machine", choices=sorted(bake.BACKENDS), action="append")
    ap.add_argument("--port", type=int, default=19470)
    ap.add_argument("--only", action="append", help="substring: test only matching apps (repeatable)")
    ap.add_argument("--depth", type=int, help="the screen's colour depth to start in (68k): 8 for 256-colour games")
    args = ap.parse_args()
    ok = True
    for i, m in enumerate(args.machine or image_machines(args.images[0])):
        ok &= run(args.images, m, args.port + i, args.only, extra=["--depth", str(args.depth)] if args.depth else [])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
