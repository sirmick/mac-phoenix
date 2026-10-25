#!/usr/bin/env python3
"""test_hfsplus_app.py -- a real application saving to an HFS+ volume on
Executor: Apple's SimpleText (on the tests-7.5.5 System) types a note,
saves it through Standard File onto a writable copy of the blank fixture
(keyboard only: Desktop, the volume picked by typing, the name), adds a
line and saves again; then, launched again on the document, types at its
start and saves. On the host: the data fork holds the three pieces, the
resource fork is a resource file with SimpleText's 'styl', and
fsck.hfsplus finds the volume OK (docker; not checked without).

    test_hfsplus_app.py [--dump BUILD/hfsplus_dump]

Exit 77 (skip) without tests-7.5.5.
"""
import argparse
import gzip
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

FIXTURE = os.path.join(ROOT, "src", "executor", "tests", "fixtures", "hfsplus", "blank-64m.img.gz")
STORAGE = os.environ.get("MACEMU_STORAGE", os.path.expanduser("~/storage"))
LINES = ["Hello from SimpleText on HFS+.", " Second line.", "Opened again: "]


def resource_types(fork):
    """The types in a resource fork (Inside Macintosh: More Toolbox, 1-121), or None."""
    if len(fork) < 16:
        return None
    data_off, map_off, data_len, map_len = struct.unpack(">4I", fork[:16])
    if map_off + map_len > len(fork) or data_off + data_len > len(fork) or map_len < 30:
        return None
    m = fork[map_off:map_off + map_len]
    types_off = struct.unpack(">H", m[24:26])[0]
    count = struct.unpack(">H", m[types_off:types_off + 2])[0] + 1
    return [m[types_off + 2 + 8 * i:types_off + 6 + 8 * i].decode("mac_roman") for i in range(count)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", default=os.path.join(ROOT, "build", "src", "executor", "romlib", "hfsplus_dump"))
    ap.add_argument("--port", type=int, default=19500 + os.getpid() % 400)
    ap.add_argument("--keep")
    args = ap.parse_args()
    if not os.path.exists(os.path.join(STORAGE, "images", "tests-7.5.5.img")):
        print("SKIP: no tests-7.5.5.img")
        return 77
    home_tmp = os.path.join(STORAGE, "tmp")             # not /tmp: tmpfs
    work = os.path.abspath(args.keep or tempfile.mkdtemp(prefix="hfsplus-app-",
                                                         dir=home_tmp if os.path.isdir(home_tmp) else None))
    os.makedirs(work, exist_ok=True)
    volume = os.path.join(work, "Blank.img")
    with gzip.open(FIXTURE) as src, open(volume, "wb") as dst:
        shutil.copyfileobj(src, dst)
    system = subprocess.check_output(["bash", os.path.join(ROOT, "tests", "lib", "refresh_test_disk.sh"),
                                      "tests-7.5.5", str(os.getpid())], text=True).strip()
    g = guest.Guest(work, "executor", [volume], [], args.port, STORAGE, timeout=900,
                    extra=("--executor-system", os.path.basename(system), "--executor-writable-images"))
    steps = [
        {"launch": "Macintosh HD:SimpleText"}, {"wait_app": "SimpleText", "timeout": 30}, {"wait": 2},
        {"type": LINES[0]},
        # Standard File: Desktop (Command-D), into the list, the volume by
        # typing its name, opened; back to the name, saved
        {"cmd_key": 1}, {"wait": 2}, {"cmd_key": 2}, {"wait": 1}, {"key": "tab"}, {"type": "Blank"},
        {"wait": 1}, {"key": "return"}, {"wait": 1}, {"key": "tab"}, {"type": "Saved note"}, {"key": "return"},
        {"wait": 2}, {"type": LINES[1]}, {"wait": 1}, {"cmd_key": 1}, {"wait": 2}, {"shot": "saved"},
        {"quit": True}, {"wait_app_not": "SimpleText", "timeout": 30},
        {"open": "Blank:Saved note"}, {"wait_app": "SimpleText", "timeout": 30}, {"wait": 2},
        {"type": LINES[2]}, {"wait": 1},         # where it opens: at the start
        {"cmd_key": 1}, {"wait": 2}, {"shot": "saved-again"},
        {"quit": True}, {"wait_app_not": "SimpleText", "timeout": 30},
    ]
    try:
        deadline = time.time() + 120
        while not (g.get("/api/status") or {}).get("bridge_agent_connected"):
            if time.time() > deadline:
                print("FAIL: no bridge agent")
                return 1
            time.sleep(1)
        for step in steps:
            g.run(step)
        g.shutdown()                            # the volume unmounted cleanly
    finally:
        g.stop()

    fails = []
    out = subprocess.run([args.dump, volume], capture_output=True, text=True).stdout
    import json
    items = {json.loads(l).get("path"): json.loads(l) for l in out.splitlines()[1:]}
    note = items.get("/Saved note")
    if not note:
        fails.append("no Saved note on the volume: %s" % sorted(items))
    else:
        data = subprocess.run([args.dump, volume, "read", str(note["cnid"])], capture_output=True).stdout
        rsrc = subprocess.run([args.dump, volume, "read", str(note["cnid"]), "rsrc"], capture_output=True).stdout
        want = (LINES[2] + LINES[0] + LINES[1]).encode("mac_roman")
        if data != want:
            fails.append("data fork %r, %r expected" % (data, want))
        types = resource_types(rsrc)
        if not types or "styl" not in types:
            fails.append("resource fork (%d bytes): types %s, a 'styl' expected" % (len(rsrc), types))
        if bytes.fromhex(note["type"]) != b"TEXT" or bytes.fromhex(note["creator"]) != b"ttxt":
            fails.append("type/creator %s/%s" % (note["type"], note["creator"]))
    if shutil.which("docker") and not subprocess.run(["docker", "image", "inspect", "macphoenix-hfsprogs"],
                                                     capture_output=True).returncode:
        f = subprocess.run(["docker", "run", "--rm", "-v", work + ":/w", "macphoenix-hfsprogs", "fsck.hfsplus",
                            "-n", "-f", "/w/Blank.img"], capture_output=True, text=True)
        if f.returncode:
            fails.append("fsck.hfsplus: " + f.stdout.strip()[-400:])
    else:
        print("(no fsck: docker or the macphoenix-hfsprogs image missing)")
    for f in fails:
        print("FAIL " + f)
    if not fails:
        print("PASS SimpleText saved, opened and saved again a note on an HFS+ volume (%s)" % ", ".join(LINES))
    if not args.keep and not fails:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
