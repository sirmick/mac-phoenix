#!/usr/bin/env python3
"""make_fixture.py BLANK OUT [--work DIR]

Makes the long-name HFS+ fixture for Executor's HFS+ reader
(docs/executor/HFSPlus.md): Mac OS 9.0.4 (the G3 machine on mame-ppc) boots
a copy of tests-9.0.4 with a copy of BLANK -- an empty HFS+ volume named
LongNames:

    docker run --rm -v DIR:/w ubuntu:24.04 bash -c \\
        'apt-get update && apt-get install -y hfsprogs && mkfs.hfsplus -v LongNames /w/BLANK'

-- as its second disk, and runs MakeLongNames (this folder; tools/mpw/mpw
build tests/guest/hfsplus -o tests/guest/hfsplus), which fills it through
the HFS Plus calls only Mac OS 9 has: names over 31 characters, decomposed
and non-MacRoman names, files in more than 8 extents. Then the Finder opens
the volume and its long-named folder as lists, for what 9.0.4 shows of the
names; a clean shutdown; the volume is copied to OUT.

The work folder (default OUT's folder + /make_fixture) keeps longnames.txt
(MakeLongNames' report: each item's Unicode name and the FSSpec name Mac
OS 9 gave it), finder.json (the names the Finder drew in each window) and
the emulator's log.

MACPHOENIX_EMULATOR names the emulator (default build/mac-phoenix).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
sys.path.insert(0, os.path.join(ROOT, "tests", "guest"))
import guest          # noqa: E402
import install_macbinary  # noqa: E402

STORAGE = os.environ.get("MACEMU_STORAGE", os.path.expanduser("~/storage"))


def window_names(g):
    """The names the Finder drew in its front window."""
    g.mouse(x=0, y=479)
    time.sleep(1)
    w = next((w for w in g.ui()["windows"] if w.get("front")), None)
    if not w:
        return None, []
    l, t, r, b = w["rect"]
    texts = (g.get("/api/text") or {}).get("text", [])
    return w.get("title"), [x["text"] for x in texts
                            if x.get("app") == "Finder" and l <= x["rect"][0] < r and t <= x["rect"][1] < b]


def as_list(g):
    try:
        g.menu("View", "as List")
        time.sleep(2)
    except (guest.NotFound, RuntimeError) as e:
        print("  View > as List: %s" % e)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("blank")
    ap.add_argument("out")
    ap.add_argument("--work")
    ap.add_argument("--port", type=int, default=19400 + os.getpid() % 500)
    args = ap.parse_args()

    rom = guest.roms(STORAGE)["kpx"]
    if not os.path.exists(rom):
        print("no G3 ROM at %s" % rom)
        return 77
    work = os.path.abspath(args.work or os.path.join(os.path.dirname(os.path.abspath(args.out)), "make_fixture"))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    disk = subprocess.check_output(["bash", os.path.join(ROOT, "tests", "lib", "refresh_test_disk.sh"),
                                    "tests-9.0.4", str(os.getpid())], text=True).strip()
    volume = os.path.join(work, "LongNames.img")
    shutil.copyfile(args.blank, volume)

    g = guest.Guest(work, "kpx", [disk, volume], [], args.port, STORAGE, timeout=900,
                    extra=("--core", "mame-ppc", "--jit"))
    try:
        install_macbinary.install(os.path.join(HERE, "MakeLongNames.bin"), g.host_dir(), "MakeLongNames")
        print("booting Mac OS 9.0.4...")
        g.wait_finder(240, agent=True)
        g.close_energy_saver(5)

        print("running MakeLongNames...")
        report = os.path.join(g.host_dir(), "longnames.txt")
        r = g.bridge("/api/launch", {"path": "Host:MakeLongNames"})
        if not r.get("success"):
            raise RuntimeError("launch: %s" % r)
        for _ in range(120):
            if os.path.exists(report) and b"\r---" in open(report, "rb").read():
                break
            g.pause(1)
        else:
            raise RuntimeError("MakeLongNames did not finish")
        text = open(report, "rb").read().decode("mac_roman").replace("\r", "\n")
        open(os.path.join(work, "longnames.txt"), "w").write(text)
        g.pause(3)                              # it quits; the Finder comes back in front

        print("the Finder: LongNames as a list...")
        seen = {}
        g.finder_open("LongNames")
        as_list(g)
        title, names = window_names(g)
        seen[title] = names
        folder = next((n for n in names if n.startswith("A folder whose")), None)
        if folder:
            t, _ = g.finder_item(folder, in_window=True)
            g.dclick(t["rect"][0] + 4, (t["rect"][1] + t["rect"][3]) // 2)
            g.pause(2)
            as_list(g)
            title, names = window_names(g)
            seen[title] = names
        json.dump(seen, open(os.path.join(work, "finder.json"), "w"), indent=1, ensure_ascii=False)
        for title, names in seen.items():
            print("  %s: %s" % (title, names))

        print("shutting down...")
        g.shutdown()
    finally:
        g.stop()
    shutil.copyfile(volume, args.out)
    print("made %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
