#!/usr/bin/env python3
"""test_hfs_glob.py -- names hfsutils would glob (\\ * ? [ ] { }: Photoshop
5.5's "{Adobe ImageReady™ 2.0 alias}") survive a snapshot's write and
listing and bake.put: folders made, files copied in and out by those names.
No emulator; hfsutils only."""
import os
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
import bake  # noqa: E402
import snapshot  # noqa: E402
from populate_disk import build_macbinary  # noqa: E402

failures = []


def expect(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


if not shutil.which("hformat"):
    print("SKIP: no hfsutils")
    sys.exit(77)

work = tempfile.mkdtemp(prefix="hfsglob.", dir=os.environ.get("TMPDIR"))
try:
    blob = os.path.join(work, "f.bin")
    with open(blob, "wb") as f:
        f.write(build_macbinary(b"x", b"TEXT", b"ttxt", b"hello", b""))
    folder, name = b"{Helpers} [1]", b"{Adobe ImageReady 2.0 alias}*?"
    items = [(folder, None), (folder + b":" + name, blob)]

    out = os.path.join(work, "snap.img")
    snapshot.write(work, out, "Glob", items, [], {})
    hfs = snapshot.Hfs(work)
    hfs.mount(out)
    try:
        expect(hfs.exists(b":Items:" + folder), "write: the folder, by its own name")
        expect(hfs.exists(b":Items:" + folder + b":" + name), "write: the file in it")
        listed = [e["path"].encode("mac_roman") for e in snapshot.listing(hfs, b":Items", work)]
        expect(folder + b":" + name in listed, "listing: copies the file out")
    finally:
        hfs.umount()

    vol = os.path.join(work, "put.img")
    snapshot.blank(hfs, vol, "Put", 2)
    bake.put(hfs, vol, items)
    hfs.mount(vol)
    try:
        expect(hfs.exists(b":" + folder + b":" + name), "bake.put: folder and file")
    finally:
        hfs.umount()
finally:
    shutil.rmtree(work, ignore_errors=True)

print("PASS" if not failures else f"{len(failures)} failed")
sys.exit(1 if failures else 0)
