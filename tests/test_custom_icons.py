#!/usr/bin/env python3
"""test_custom_icons.py -- folders' custom icons survive the provisioning
pipeline. A folder's icon is an invisible "Icon\\r" file in it: snapshot.py's
listing (hls -l marks it "fi") keeps it, a bake's capture keeps it while it
leaves out the top-level noise and Mac OS X's .DS_Store files (reported as
excluded), and tidy.py gives its folder kHasCustomIcon (the snapshots keep no
folder Finder info). On an HFS image the test makes; skips (77) without
hfsutils."""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
sys.path.insert(0, os.path.join(ROOT, "tools", "diskaudit"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import bake  # noqa: E402
import disk_audit  # noqa: E402
import snapshot  # noqa: E402
import tidy  # noqa: E402
from test_disk_audit import macbinary  # noqa: E402

failures = []


def expect(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


ICON = b"Icon\r"

# hls -l lines: a visible file, an invisible one ("fi"), a folder
LINES = [b"f  TEXT/ttxt         0        12 Nov  3 20:01 Read Me",
         b"fi ????/????         0       480 Nov  3 20:01 Icon\r",
         b"d      3 items                  Nov  3 20:01 Folder"]
got = [snapshot.LS.match(line) for line in LINES]
expect(all(got), "every hls -l line matches, the invisible one too")
expect([m.group(2) for m in got if m] == [b"Read Me", ICON, b"Folder"], "names, Icon\\r with its CR")
expect([m.group(1) for m in got if m] == [b"f", b"f", b"d"], "kinds")

# The capture: top-level noise and .DS_Store out (and reported), Icon\r kept
items = [(b"Desktop Folder", None), (b"Trash", None), (b"Icons \xc4", None),
         (b"Icons \xc4:" + ICON, "i"), (b"Icons \xc4:.DS_Store", "d"),
         (b"Icons \xc4:Set:.DS_Store", "d"), (b"Icons \xc4:Set", None), (b"Icons \xc4:Set:" + ICON, "i")]
kept, excluded = bake.capture_filter(items, bake.TOP_NOISE)
expect([r for r, _ in kept] == [b"Icons \xc4", b"Icons \xc4:" + ICON, b"Icons \xc4:Set",
                                b"Icons \xc4:Set:" + ICON], "kept: the folders and their Icon\\r files")
expect(set(excluded) == {b"Desktop Folder", b"Trash", b"Icons \xc4:.DS_Store", b"Icons \xc4:Set:.DS_Store"},
       "excluded: the top-level noise and every .DS_Store")


def main():
    if not shutil.which("hformat"):
        print("skip: hfsutils not installed")
        return 77
    work = tempfile.mkdtemp(prefix="custom-icons.")
    try:
        image = os.path.join(work, "icons.img")
        with open(image, "wb") as f:
            f.truncate(1440 * 1024)
        env = dict(os.environ, HOME=work)

        def run(*args):
            subprocess.run(args, env=env, check=True, capture_output=True)
        run("hformat", "-l", "Icons", image)
        run("hmkdir", ":With")
        run("hmkdir", ":Without")
        mb = os.path.join(work, "f.bin")
        with open(mb, "wb") as f:
            f.write(macbinary("Icon\r", b"????", b"????", flags=0x4000))
        run("hcopy", "-m", mb, ":With:")
        with open(mb, "wb") as f:
            f.write(macbinary("Plain", b"TEXT", b"ttxt"))
        run("hcopy", "-m", mb, ":Without:")
        run("humount")

        hfs = snapshot.Hfs(work)
        hfs.mount(image)
        listed = dict(hfs.ls(b":With:"))
        hfs.umount()
        expect(ICON in listed and not listed[ICON], "snapshot's listing has the invisible Icon\\r file")

        tidy.tidy(image)
        v = disk_audit.Volume(image)
        folders = {n.name: n for n in v.nodes.values() if n.is_dir}
        expect(folders["With"].flags & tidy.CUSTOM_ICON, "tidy: a folder holding Icon\\r gets kHasCustomIcon")
        expect(not folders["Without"].flags & tidy.CUSTOM_ICON, "tidy: a folder without one doesn't")
        icon = next(n for n in v.nodes.values() if n.name == "Icon\r")
        expect(icon.flags & disk_audit.INVISIBLE, "the Icon\\r file stays invisible")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    rc = main()
    if rc == 77:
        sys.exit(77)
    print(f"{len(failures)} failure(s)" if failures else "all passed")
    sys.exit(1 if failures else 0)
