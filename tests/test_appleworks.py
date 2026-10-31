#!/usr/bin/env python3
"""test_appleworks.py -- AppleWorks 5.0.3 (productivity-system-7) on Executor:
a new word-processing document, a line typed, saved through Standard File
onto the Host volume (keyboard only: Desktop, the volume picked by typing
its name, the name); quit, launched again on the document, a line typed,
saved, quit. On the host: the document's data fork holds both
lines, its Finder type and creator are AppleWorks'.

    test_appleworks.py --system 7.6.1 [--ppc]     # 68k code, or the fat
    test_appleworks.py --system 8.6               # application's PowerPC code

Exit 77 (skip) without productivity-system-7.img or the System image.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
import guest  # noqa: E402

STORAGE = os.environ.get("MACEMU_STORAGE", os.path.expanduser("~/storage"))
APP = "Productivity:AppleWorks 5.0.3 ƒ:AppleWorks"
NAME = "AW note"
LINES = ["Typed in AppleWorks.", "Opened again. "]


def finder_info(path):
    """Type and creator of a file on Executor's Host volume, from its
    AppleDouble header file %NAME (entry 9, Finder info), or None."""
    header = os.path.join(os.path.dirname(path), "%" + os.path.basename(path))
    if not os.path.exists(header):
        return None
    with open(header, "rb") as f:
        d = f.read()
    if len(d) < 26 or d[:4] != b"\x00\x05\x16\x07":
        return None
    for i in range(struct.unpack(">H", d[24:26])[0]):
        entry, off, length = struct.unpack(">3I", d[26 + 12 * i:38 + 12 * i])
        if entry == 9 and length >= 8:
            return d[off:off + 8]
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--system", default="7.6.1")
    ap.add_argument("--ppc", action="store_true", help="--executor-ppc (on 8.6 always)")
    ap.add_argument("--port", type=int, default=guest.free_port())
    ap.add_argument("--keep")
    args = ap.parse_args()
    prod = os.path.join(STORAGE, "images", "productivity-system-7.img")
    system = args.system + ".img"
    for image in (prod, os.path.join(STORAGE, "images", system)):
        if not os.path.exists(image):
            print("SKIP: no %s" % image)
            return 77
    home_tmp = os.path.join(STORAGE, "tmp")             # not /tmp: tmpfs
    work = os.path.abspath(args.keep or tempfile.mkdtemp(prefix="appleworks-",
                                                         dir=home_tmp if os.path.isdir(home_tmp) else None))
    os.makedirs(work, exist_ok=True)
    disk = os.path.join(work, "Productivity.img")       # AppleWorks writes its own folder
    subprocess.run(["cp", "--reflink=auto", prod, disk], check=True)
    ppc = args.ppc or args.system != "7.6.1"
    extra = ["--executor-system", system, "--executor-writable-images"] + (["--executor-ppc"] if ppc else [])
    g = guest.Guest(work, "executor", [disk], [], args.port, STORAGE, timeout=900, extra=extra)
    # Standard File's Save: Desktop (Command-D), into the list, the Host
    # volume by typing its name, opened; back to the name, saved
    save_as = [{"cmd_key": 1}, {"wait": 2}, {"cmd_key": 2}, {"wait": 1}, {"key": "tab"}, {"type": "Host"},
               {"wait": 1}, {"key": "return"}, {"wait": 1}, {"key": "tab"}, {"type": NAME}, {"key": "return"},
               {"wait": 2}]
    steps = [
        {"launch": APP}, {"wait_app": "AppleWorks", "timeout": 60}, {"wait": 6},
        {"key": "return"}, {"wait": 3},         # New Document: Word Processing, the first
        {"type": LINES[0]}, {"wait": 1},
    ] + save_as + [
        {"shot": "saved"},
        {"quit": True}, {"wait_app_not": "AppleWorks", "timeout": 30},
        {"open": "Host:" + NAME}, {"wait_app": "AppleWorks", "timeout": 60}, {"wait": 6},
        {"type": LINES[1]}, {"wait": 1},
        {"cmd_key": 1}, {"wait": 3}, {"shot": "saved-again"},
        {"quit": True}, {"wait_app_not": "AppleWorks", "timeout": 30},
    ]
    fails = []
    try:
        deadline = time.time() + 120
        while not (g.get("/api/status") or {}).get("bridge_agent_connected"):
            if time.time() > deadline:
                print("FAIL: no bridge agent")
                return 1
            time.sleep(1)
        for step in steps:
            g.run(step)
    except Exception as e:                      # a step timed out: the document is checked anyway
        fails.append("%s (screenshots in %s)" % (e, work))
        g.shot("failed")
    finally:
        g.stop()

    doc = os.path.join(work, "host", NAME)
    if not os.path.exists(doc):
        fails.append("no %s on the Host volume: %s" % (NAME, sorted(os.listdir(os.path.join(work, "host")))))
    else:
        with open(doc, "rb") as f:
            data = f.read()
        for line in LINES:
            if line.strip().encode("mac_roman") not in data:
                fails.append("%r not in the data fork (%d bytes)" % (line.strip(), len(data)))
        info = finder_info(doc)
        if info != b"CWWPBOBO":
            fails.append("type/creator %r, CWWP/BOBO expected" % info)
    for f in fails:
        print("FAIL " + f)
    if not fails:
        print("PASS AppleWorks (%s%s) saved, opened and saved again a document on the Host volume"
              % (args.system, ", PowerPC" if ppc else ""))
    if not args.keep and not fails:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
