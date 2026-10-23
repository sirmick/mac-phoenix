#!/usr/bin/env python3
"""A small boot disk for Executor in the browser, cut from a System image.

    tools/web/make_boot_disk.py SOURCE.img OUT.img [--label NAME]

Executor runs only the startup files its policy allows
(src/executor/res/extension-policy.txt), and in a browser tab every
megabyte of disk is download time. This keeps what Executor uses and the
user sees, and leaves out the rest:

  kept     the System Folder: System, Finder, Fonts, Control Panels, Apple
           Menu Items, Preferences, Startup/Shutdown/Launcher Items, the
           Scrapbook File; of Extensions only the files the policy names,
           Scripting Additions and Voices; the volume's top-level files
           (SimpleText, the About documents)
  left out the rest of Extensions (Apple Guide help, printer drivers,
           QuickTime, Apple's Open Transport, ...), MacsBug, the
           top-level folders (Apple Extras, Utilities), and the desktop
           database (keyed by the source volume's file IDs)

Files go across as MacBinary (hfsutils' hcopy -m), so both forks, the
Finder info and the dates stay as they were; the Finder keys its caches on
dates. The new volume is sized to what it holds and its System Folder
blessed. 7.5.5.img: 250 MB (34 MB used) -> about 12 MB.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/macdecode"))
from rsrc import Disk  # noqa: E402

POLICY = ROOT / "src/executor/res/extension-policy.txt"
EXTENSION_FOLDERS = {b"Scripting Additions", b"Voices"}
# The Finder's desktop database (its file IDs are the source volume's) and
# the provisioning record: not copied.
TOP_SKIP = {b"Desktop DB", b"Desktop DF", b"Desktop", b"Snapshot Info"}
SYSTEM_FOLDER_SKIP = {b"MacsBug"}


def policy_names(path):
    names = set()
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#") and not line.startswith("System AINI"):
            names.add(line.encode("mac_roman"))
    return names


class Writer:
    """hfsutils on the new volume, with a private HOME (hfsutils keeps the
    current volume there; other tools use it at the same time)."""

    def __init__(self, image):
        self.image = image
        self.home = tempfile.mkdtemp(prefix="bootdisk-hfs-")
        self.env = dict(os.environ, HOME=self.home)

    def run(self, *args):
        r = subprocess.run(list(args), env=self.env, capture_output=True)
        if r.returncode:
            raise RuntimeError(f"{args[0]}: {r.stderr.decode(errors='replace').strip()}")

    def format(self, size, label):
        with open(self.image, "wb") as f:
            f.truncate(size)
        self.run("hformat", "-l", label, str(self.image))

    def mkdir(self, path):
        self.run("hmkdir", b":" + path)

    def put(self, path, macbinary, scratch):
        tmp = Path(scratch) / "file.bin"
        tmp.write_bytes(macbinary)
        self.run("hcopy", "-m", str(tmp), b":" + path)

    def bless(self, path):
        self.run("hattrib", "-b", b":" + path)

    def close(self):
        subprocess.run(["humount"], env=self.env, capture_output=True)


def plan(disk, allowed):
    """(folders, files) to copy: raw Mac paths, parents first."""
    folders, files = [], []

    def tree(path, keep_file=lambda name: True, keep_folder=lambda name: True):
        for name in disk.files(path):
            if keep_file(name):
                files.append(path + b":" + name)
        for name in disk.folders(path):
            if keep_folder(name):
                folders.append(path + b":" + name)
                tree(path + b":" + name)

    for name in disk.files(b""):
        if name not in TOP_SKIP:
            files.append(name)                           # SimpleText, About ...
    sysf = b"System Folder"
    folders.append(sysf)
    for name in disk.files(sysf):
        if name not in SYSTEM_FOLDER_SKIP:
            files.append(sysf + b":" + name)
    for name in disk.folders(sysf):
        path = sysf + b":" + name
        folders.append(path)
        if name == b"Extensions":
            tree(path, keep_file=lambda n: n in allowed,
                 keep_folder=lambda n: n in EXTENSION_FOLDERS)
        else:
            tree(path)
    return folders, files


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("source")
    ap.add_argument("out")
    ap.add_argument("--label", default=None, help="volume name (default: the source's)")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory(prefix="bootdisk-") as scratch:
        disk = Disk(args.source, Path(scratch) / "cache")
        try:
            folders, files = plan(disk, policy_names(POLICY))
            blobs = {}
            for path in files:
                blob = disk.macbinary(path)
                if blob is None:
                    print(f"skipped (unreadable): {path.decode('mac_roman')}", file=sys.stderr)
                    continue
                blobs[path] = blob
            label = args.label
            if label is None:
                r = disk._run("hvol")
                line = next((l for l in r.stdout.split(b"\n") if l.startswith(b"Volume name is")), b"")
                label = line.split(b'"')[1].decode("mac_roman") if b'"' in line else "Macintosh HD"
        finally:
            disk.close()

        # Room for the files at the allocation-block size, the catalog and
        # some slack (the Finder reads it read-only in the browser).
        content = sum(len(b) for b in blobs.values())
        size = (int(content * 1.15) + 2 * 1024 * 1024 + 511) // 512 * 512
        out = Path(args.out)
        w = Writer(out)
        try:
            w.format(size, label)
            for path in folders:
                w.mkdir(path)
            for path, blob in blobs.items():
                w.put(path, blob, scratch)
            w.bless(b"System Folder")
        finally:
            w.close()
    print(f"{out}: {size / 1e6:.1f} MB, {len(blobs)} files in {len(folders)} folders, "
          f"\"{label}\"")


if __name__ == "__main__":
    main()
