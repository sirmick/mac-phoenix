#!/usr/bin/env python3
"""A small boot disk for Executor in the browser, cut from a System image.

    tools/web/make_boot_disk.py SOURCE.img OUT.img [--label NAME] [--keep PATH]

Executor runs only the startup files its policy allows
(src/executor/res/extension-policy.txt), and in a browser tab every
megabyte of disk is download time. This keeps what Executor uses and the
user sees, and leaves out the rest:

  kept     the System Folder: System, Finder, Fonts, Control Panels, Apple
           Menu Items, Preferences, Startup/Shutdown/Launcher Items, the
           Scrapbook File (8.x: Appearance, Scripting Additions, ...); of
           Extensions only the files the policy allows on the source's
           System version, the shared libraries CFM finds there (LIBRARIES),
           Scripting Additions and Voices, the printer driver chosen in the
           Chooser (STR -8192); the volume's top-level files
           (SimpleText, the About documents) and what --keep names
  left out the rest of Extensions (Apple Guide help, other printer drivers,
           QuickTime, Apple's Open Transport, ...), what doesn't work on
           Executor (DROP: Energy Saver, AppleTalk's and QuickTime's
           items; the policy's "-NAME" ones: the Monitors control panels
           and their Control Strip modules), MacsBug, the top-level folders (Apple Extras,
           Utilities), and the desktop database (keyed by the source
           volume's file IDs)

What each cut leaves out and why: docs/executor/APPLE-DISKS.md, "Boot
disks for the browser".

Files go across as MacBinary (hfsutils' hcopy -m), so both forks, the
Finder info and the dates stay as they were; the Finder keys its caches on
dates. The new volume is sized to what it holds and its System Folder
blessed. 7.5.5.img: 250 MB (34 MB used) -> about 12 MB; 8.6.img (HFS too)
needs --keep "Applications:SimpleText" (no SimpleText at its top level).
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/macdecode"))
sys.path.insert(0, str(ROOT / "tools/diskaudit"))
from rsrc import Disk, glob_quote  # noqa: E402
import tidy  # noqa: E402

POLICY = ROOT / "src/executor/res/extension-policy.txt"
EXTENSION_FOLDERS = {b"Scripting Additions", b"Voices"}
# The Finder's desktop database (its file IDs are the source volume's) and
# the provisioning record: not copied.
TOP_SKIP = {b"Desktop DB", b"Desktop DF", b"Desktop", b"Snapshot Info"}
SYSTEM_FOLDER_SKIP = {b"MacsBug"}
# What a session leaves in Apple Menu Items: made empty.
SESSION_FOLDERS = {b"Recent Applications", b"Recent Documents", b"Recent Servers"}
# The Finder's own folders, made empty and invisible as on the source.
FINDER_FOLDERS = [b"Desktop Folder", b"Trash"]
# Extensions files that run no code at startup but are found there: CFM's
# shared libraries (docs/executor/MacOS8-9.md, "Where the libraries come
# from") and 8.x's AppleScript (the policy names 7.x's "AppleScript™").
# AppleScriptLib and Open Transport's are Executor's own. And the speech
# synthesizers the Voices folder's voices use (components Executor
# registers: extensions.cpp register_component_files).
LIBRARIES = {
    "MacinTalk 2",
    "MacinTalk 3",
    "ObjectSupportLib",
    "File Sharing Library",
    "Text Encoding Converter",
    "SOMobjects™ for Mac OS",
    "AppleScript",
    "Internet Config Extension",                         # InternetConfigLib (Hotline imports it)
}
# Anywhere in the System Folder, by name: what doesn't work on Executor.
DROP = {
    # No power management: Energy Saver says its extension isn't running.
    "Energy Saver", "Energy Settings",
    # AppleTalk (Executor's Open Transport has TCP only): the control panel
    # only alerts; file sharing, its users and the network browser run on it.
    "AppleTalk", "AppleTalk Switch", "Network Browser",
    "File Sharing", "File Sharing Monitor", "File Sharing Strip",
    "Sharing Setup", "Users & Groups",
    # QuickTime: not until M2.
    "QuickTime™ Settings",
}
# 8.x's System Folder: Mac OS Help (Apple Guide and HTML help, 12 MB),
# ColorSync's profiles (its extension stays off), Sherlock's sites, and
# Office's panels (not the System's).
# By creator: MacinTalk Pro's voices (8.x's Agnes, Bruce, Victoria and their
# "high quality" sets, 13 MB), whose synthesizer is left out with it.
DROP_CREATORS = {b"gala"}
SYSTEM_SUBFOLDER_DROP = {b"Help", b"ColorSync Profiles", b"Internet Search Sites",
                         b"MS Preference Panels"}


def mac(name):
    return name.encode("mac_roman")


def system_version(disk):
    """The System file's version as Executor reads it (version.cpp): 'vers'
    1, BCD major and minor.bugfix, e.g. 0x0761."""
    res = disk.resources(b"System Folder:System")
    data = res.get(("vers", 1), (None, b""))[1]
    if len(data) < 2:
        return 0
    return ((data[0] >> 4) * 10 + (data[0] & 0xF)) << 8 | data[1]


def chosen_printer(disk):
    """The driver the Chooser chose: the System file's STR -8192 (a Pascal
    string; provisioning/build.py's "printer"), or None. Kept with
    Extensions: applications that print, or check for a printer at launch
    (Persuasion 3.0), open it through the Printing Manager."""
    data = disk.resources(b"System Folder:System").get(("STR ", -8192), (None, b""))[1]
    return data[1:1 + data[0]] if data and data[0] else None


def policy_names(path, version, denied=False):
    """The policy's names, read as Executor reads them (extensions.cpp):
    '#' comments and spaces cut, "NAME @ 7.6" only from that version. With
    denied, its "-NAME" ones instead (the Monitors control panels: never
    used, so never copied)."""
    names = set()
    for line in Path(path).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line or line.startswith("System AINI"):
            continue
        if line.startswith("-") != denied:
            continue
        line = line[1:] if denied else line
        name, at, since = line.partition(" @ ")
        if at:
            major, _, minor = since.strip().partition(".")
            if version < (int(major) << 8 | int(minor or 0) << 4):
                continue
        names.add(mac(name.strip()))
    return names


def trimmed(name):
    """Names compared as Executor's listed() does: " CFM-68K Runtime
    Enabler" begins with a space so that it loads first."""
    return name.strip(b" ")


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
        self.run("hmkdir", glob_quote(b":" + path))     # (hcopy's destination is literal)

    def put(self, path, macbinary, scratch):
        tmp = Path(scratch) / "file.bin"
        tmp.write_bytes(macbinary)
        self.run("hcopy", "-m", str(tmp), b":" + path)

    def hide(self, path):
        self.run("hattrib", "+i", glob_quote(b":" + path))

    def bless(self, path):
        self.run("hattrib", "-b", glob_quote(b":" + path))

    def close(self):
        subprocess.run(["humount"], env=self.env, capture_output=True)


def plan(disk, allowed, keep=(), denied=()):
    """(folders, files) to copy: raw Mac paths, parents first."""
    folders, files = [], []
    drop = {mac(n) for n in DROP} | set(denied)
    libraries = {mac(n) for n in LIBRARIES}
    printer = chosen_printer(disk)

    def tree(path, keep_file=lambda name: True, keep_folder=lambda name: True):
        if path.rsplit(b":", 1)[-1] in SESSION_FOLDERS:
            return
        for name in disk.files(path):
            if keep_file(name) and name not in drop:
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
        if name not in SYSTEM_FOLDER_SKIP and name not in drop:
            files.append(sysf + b":" + name)
    for name in disk.folders(sysf):
        if name in SYSTEM_SUBFOLDER_DROP:
            continue
        path = sysf + b":" + name
        folders.append(path)
        if name == b"Extensions":
            tree(path, keep_file=lambda n: trimmed(n) in allowed or trimmed(n) in libraries or n == printer,
                 keep_folder=lambda n: n in EXTENSION_FOLDERS)
        else:
            tree(path)
    for path in keep:                                    # --keep
        parts = path.split(b":")
        for i in range(1, len(parts)):
            if b":".join(parts[:i]) not in folders:
                folders.append(b":".join(parts[:i]))
        if parts[-1] in disk.folders(b":".join(parts[:-1])):
            folders.append(path)
            tree(path)
        else:
            files.append(path)
    return folders, files


def alias_target(blob):
    """An alias file's target as a path on its volume (b"Applications:Apple
    FM Radio") and the volume's name, from its 'alis' 0 (the alias record's
    full path, tag 2); None if the blob isn't an alias or has no path."""
    if not blob[73] & 0x80:                              # kIsAlias
        return None
    import struct
    from rsrc import macbinary_forks, parse_resource_fork
    rec = parse_resource_fork(macbinary_forks(blob)[1]).get(("alis", 0), (None, b""))[1]
    p = 150                                              # the fixed part
    while p + 4 <= len(rec):
        tag, ln = struct.unpack_from(">hH", rec, p)
        if tag == -1:
            break
        if tag == 2:
            vol, _, path = rec[p + 4:p + 4 + ln].partition(b":")
            return vol, path.rstrip(b":")
        p += 4 + ln + (ln & 1)
    return None


def dangling_aliases(blobs, folders, label):
    """The aliases whose target is on this volume but not in the cut (8.6's
    Apple Menu Items: Apple FM Radio and AppleCD Audio Player point into
    Applications): the Finder would only say the original can't be found."""
    have = {p.lower() for p in folders} | {p.lower() for p in blobs}
    out = []
    for path, blob in blobs.items():
        target = alias_target(blob)
        if target and target[0] == label.encode("mac_roman") and target[1].lower() not in have:
            out.append(path)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("source")
    ap.add_argument("out")
    ap.add_argument("--label", default=None, help="volume name (default: the source's)")
    ap.add_argument("--keep", action="append", default=[], metavar="PATH",
                    help="also copy this file or folder (Mac path from the volume's root, "
                         "e.g. Applications:SimpleText); repeatable")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory(prefix="bootdisk-") as scratch:
        disk = Disk(args.source, Path(scratch) / "cache")
        try:
            version = system_version(disk)
            folders, files = plan(disk, policy_names(POLICY, version),
                                  [mac(k) for k in args.keep],
                                  policy_names(POLICY, version, denied=True))
            blobs = {}
            for path in files:
                blob = disk.macbinary(path)
                if blob is None:
                    print(f"skipped (unreadable): {path.decode('mac_roman')}", file=sys.stderr)
                    continue
                if blob[69:73] in DROP_CREATORS:
                    continue
                blobs[path] = blob
            r = disk._run("hvol")
            line = next((l for l in r.stdout.split(b"\n") if l.startswith(b"Volume name is")), b"")
            source_label = line.split(b'"')[1].decode("mac_roman") if b'"' in line else "Macintosh HD"
            label = args.label or source_label
        finally:
            disk.close()
        for path in dangling_aliases(blobs, folders, source_label):
            print(f"left out (alias to what isn't copied): {path.decode('mac_roman')}", file=sys.stderr)
            del blobs[path]

        # Room for the files at the allocation-block size, the catalog and
        # some slack (the Finder reads it read-only in the browser).
        content = sum(len(b) for b in blobs.values())
        size = (int(content * 1.15) + 2 * 1024 * 1024 + 511) // 512 * 512
        out = Path(args.out)
        w = Writer(out)
        try:
            w.format(size, label)
            for path in FINDER_FOLDERS:
                w.mkdir(path)
                w.hide(path)
            for path in folders:
                w.mkdir(path)
            for path, blob in blobs.items():
                w.put(path, blob, scratch)
            w.bless(b"System Folder")
        finally:
            w.close()
        tidy.tidy(str(out))                              # icons and windows placed
    print(f"{out}: {size / 1e6:.1f} MB, {len(blobs)} files in {len(folders)} folders, "
          f"\"{label}\"")


if __name__ == "__main__":
    main()
