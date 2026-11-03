#!/usr/bin/env python3
"""test_disk_audit.py — tools/diskaudit/disk_audit.py and tidy.py on fixture images it makes.

An HFS volume (hfsutils: hformat, hmkdir, hcopy of MacBinary files) whose
Finder info the test then writes into the catalog records disk_audit.py
reads (icon locations, a window rect, a scroll origin, flags), with alias
files of each kind; and a small HFS+ volume written here from TN1150's
layout (no HFS+ tool is installed). Each planted problem must be reported,
and nothing else. Skips (77) without hfsutils.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "diskaudit"))
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
import disk_audit  # noqa: E402
import tidy  # noqa: E402
import fat_cfm  # noqa: E402

FAILS = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def macbinary(name, ftype, creator, flags=0, data=b"", rsrc=b""):
    h = bytearray(128)
    n = name.encode("mac_roman")
    h[1] = len(n)
    h[2:2 + len(n)] = n
    h[65:69], h[69:73] = ftype, creator
    h[73] = flags >> 8
    h[101] = flags & 0xFF
    struct.pack_into(">II", h, 83, len(data), len(rsrc))
    h[122] = h[123] = 129
    struct.pack_into(">H", h, 124, fat_cfm.crc(bytes(h[:124])))      # MacBinary II's header CRC
    pad = lambda b: b + bytes(-len(b) % 128)  # noqa: E731
    return bytes(h) + pad(data) + pad(rsrc)


def alias_record(volume, created, path, file_id, parent_id=2):
    """An 'alis' record as the Alias Manager writes one (the fields
    disk_audit.py's parse_alias reads), with the full path (tag 2)."""
    r = bytearray(150)
    struct.pack_into(">H", r, 8, 0)                      # a file
    v = volume.encode("mac_roman")
    r[10] = len(v)
    r[11:11 + len(v)] = v
    struct.pack_into(">I", r, 38, created)
    name = path.rsplit(":", 1)[-1].encode("mac_roman")
    struct.pack_into(">I", r, 46, parent_id)
    r[50] = len(name)
    r[51:51 + len(name)] = name
    struct.pack_into(">I", r, 114, file_id)
    p = path.encode("mac_roman")
    r += struct.pack(">hH", 2, len(p)) + p + bytes(len(p) & 1) + struct.pack(">hH", -1, 0)
    struct.pack_into(">H", r, 4, len(r))
    return bytes(r)


class Hfs:
    """hfsutils on one image, with its state file in a private HOME."""

    def __init__(self, image, work):
        self.image, self.env = image, dict(os.environ, HOME=work)

    def run(self, *args):
        r = subprocess.run(args, env=self.env, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f"{' '.join(args)}: {r.stderr.strip()}")

    def __enter__(self):
        self.run("hmount", self.image)
        return self

    def __exit__(self, *a):
        self.run("humount")


def patch(vol, node, field, data):
    """Write data at a field of node's catalog record (HFS), through the
    catalog's extents."""
    hv = vol.hfs
    mdb = hv._read(1024, 162)
    off = node.offset + field
    for start, count in hv._extents(mdb[150:162]):
        n = count * hv.block
        if off < n:
            with open(vol.path, "r+b") as f:
                f.seek(hv.base + hv.first + start * hv.block + off)
                f.write(data)
            return
        off -= n
    raise ValueError("past the catalog's first extents")


# The Internet library disk's folders: labels wider than a 128-pixel cell
LONG = ("Hotline Connect Client 1.9.1 \u0192", "MR Browser 0.39 \u0192", "Serial MReader 0.03 \u0192")


def hfs_fixture(work):
    image = os.path.join(work, "fixture.img")
    with open(image, "wb") as f:
        f.truncate(1440 * 1024)
    env = dict(os.environ, HOME=work)
    subprocess.run(["hformat", "-l", "Fixture", image], env=env, check=True, capture_output=True)

    def put(h, folder, mb):
        p = os.path.join(work, "f.bin")
        with open(p, "wb") as f:
            f.write(mb)
        h.run("hcopy", "-m", p, folder)

    with Hfs(image, work) as h:
        for d in ("Piles", "Scrolled", "Desktop Folder", "Elsewhere", "Labels"):
            h.run("hmkdir", ":" + d)
        put(h, ":", macbinary("Here", b"TEXT", b"ttxt"))
        put(h, ":Elsewhere:", macbinary("Moved", b"TEXT", b"ttxt"))
        put(h, ":", macbinary("Junk", b"????", b"????", flags=0x4000))
        for n in "ABCD":
            put(h, ":Piles:", macbinary(n, b"TEXT", b"ttxt"))
        for n in LONG:
            put(h, ":Labels:", macbinary(n, b"TEXT", b"ttxt"))
    vol = disk_audit.Volume(image)
    ids = {vol.path_of(n): n for n in vol.nodes.values()}
    created = vol.created
    with Hfs(image, work) as h:
        def alias(name, path, fid, volume="Fixture"):
            rsrc = fat_cfm.write_rsrc([(b"alis", 0, 0, None, alias_record(volume, created, path, fid))])
            put(h, ":", macbinary(name, b"TEXT", b"ttxt", flags=0x8000, rsrc=rsrc))
        alias("Alias Here", "Fixture:Here", ids["Fixture:Here"].id)
        alias("Alias Gone", "Fixture:Gone", 9999)
        alias("Alias Moved", "Fixture:Moved", ids["Fixture:Elsewhere:Moved"].id)
        alias("Alias Reused", "Fixture:Radio", ids["Fixture:Here"].id)    # boot-8.6's Apple FM Radio
        alias("Alias Away", "Install Disk:Thing", 77, volume="Install Disk")
    vol = disk_audit.Volume(image)
    by = {vol.path_of(n): n for n in vol.nodes.values()}
    loc = lambda n, v, h: patch(vol, by[n], 14, struct.pack(">hh", v, h))          # noqa: E731 FInfo fdLocation
    loc("Fixture:Piles:A", 40, 40)
    loc("Fixture:Piles:D", 50, 45)                       # overlaps A; B and C stay at (0,0)
    for i, n in enumerate(LONG):                         # the old 128-pixel grid: labels too wide
        loc("Fixture:Labels:" + n, 8, 48 + 128 * i)
    patch(vol, by["Fixture"], 22, struct.pack(">4h", 300, 700, 600, 900))      # the root window: off screen
    patch(vol, by["Fixture:Scrolled"], 38, struct.pack(">hh", 5000, 5000))     # DXInfo frScroll
    return image


def hfsplus_fixture(work):
    """A two-node HFS+ catalog: the root, a folder whose window is off the
    screen, an invisible stray file."""
    block, node = 4096, 4096
    def key(parent, name):
        u = name.encode("utf-16-be")
        k = struct.pack(">IH", parent, len(name)) + u
        return struct.pack(">H", len(k)) + k

    def folder(fid, rect=(0, 0, 0, 0)):
        r = bytearray(88)
        struct.pack_into(">hHII", r, 0, 1, 0, 0, fid)
        struct.pack_into(">4h", r, 48, *rect)
        return bytes(r)

    def file(fid, flags):
        r = bytearray(248)
        struct.pack_into(">hHII", r, 0, 2, 0, 0, fid)
        r[48:56] = b"TEXTttxt"
        struct.pack_into(">H", r, 56, flags)
        return bytes(r)

    recs = [key(1, "Plus") + folder(2), key(2, "Junk") + file(17, 0x4000),
            key(2, "Window") + folder(16, (10, 700, 300, 1000))]
    leaf = bytearray(node)
    struct.pack_into(">IIbBHH", leaf, 0, 0, 0, -1, 1, len(recs), 0)
    off, offs = 14, []
    for r in recs:
        offs.append(off)
        leaf[off:off + len(r)] = r
        off += len(r)
    offs.append(off)
    for i, o in enumerate(offs):
        struct.pack_into(">H", leaf, node - 2 * (i + 1), o)
    head = bytearray(node)
    struct.pack_into(">IIbBHH", head, 0, 0, 0, 1, 0, 3, 0)
    struct.pack_into(">HIIIIHH", head, 14, 1, 1, len(recs), 1, 1, node, 516)
    vh = bytearray(512)
    vh[0:2] = b"H+"
    struct.pack_into(">H", vh, 2, 4)
    struct.pack_into(">I", vh, 16, 0x12345678)
    struct.pack_into(">I", vh, 40, block)
    struct.pack_into(">QIIII", vh, 272, 2 * node, block, 2, 2, 2)    # catalog: blocks 2-3
    image = os.path.join(work, "plus.img")
    with open(image, "wb") as f:
        f.write(bytes(1024) + vh + bytes(2 * block - 1536) + head + leaf)
    return image


def findings(images, *extra):
    import io
    import contextlib
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = disk_audit.main(list(images) + ["--json", *extra])
    import json
    return rc, [json.loads(l) for l in out.getvalue().splitlines() if l.strip()]


def has(fs, category, path, text, severity=None):
    return any(f["category"] == category and f["path"] == path and text in f["message"]
               and (severity is None or f["severity"] == severity) for f in fs)


def main():
    if not shutil.which("hformat"):
        print("SKIP: hfsutils not installed")
        return 77
    work = tempfile.mkdtemp(prefix="disk-audit-", dir=os.environ.get("TMPDIR"))
    try:
        image = hfs_fixture(work)
        before = open(image, "rb").read()
        rc, fs = findings([image])
        for f in fs:
            print("    ", f["severity"], f["category"], f["path"], f["message"])
        check(rc == 1, "exit status 1 (errors found)")
        check(has(fs, "alias", "Fixture:Alias Gone", "dangling", "error"), "dangling alias")
        check(has(fs, "alias", "Fixture:Alias Reused", "is now 'Here'", "error"),
              "dangling alias whose ID another file has now")
        check(has(fs, "alias", "Fixture:Alias Moved", "moved", "warning"), "moved alias, found by ID")
        check(has(fs, "alias", "Fixture:Alias Away", "external", "info"), "external alias")
        check(not any(f["path"] == "Fixture:Alias Here" for f in fs), "the good alias isn't reported")
        check(has(fs, "icons", "Fixture:Piles", "2 of 4 icons piled at (0,0)"), "pile at (0,0)")
        check(has(fs, "icons", "Fixture:Piles", "overlap: 'A'"), "overlapping icons")
        check(has(fs, "windows", "Fixture", "off a 640x480 screen", "error"), "window off the screen")
        check(has(fs, "windows", "Fixture:Scrolled", "scroll origin 5000,5000"), "scroll past the content")
        check(has(fs, "invisible", "Fixture:Junk", "stray"), "stray invisible file")
        check(not any(f["path"] == "Fixture:Desktop Folder" for f in fs),
              "a visible Desktop Folder isn't reported (the Finder hides it itself)")
        check(has(fs, "icons", "Fixture:Labels:" + LONG[0], "cut at the window's left edge"),
              "a long label cut at the window's left edge")
        check(has(fs, "icons", "Fixture:Labels", "labels wider than their cell"), "labels wider than their cell")
        check(len(fs) == 11, f"nothing else reported ({len(fs)} findings)")
        check(open(image, "rb").read() == before, "the image is unchanged")
        rc, fs = findings([image], "--screen", "1600x1200")
        check(not has(fs, "windows", "Fixture", "off a"), "the window fits a bigger screen")

        # tidy.py puts the Finder info right: icons and windows placed
        tidy.tidy(image)
        rc, fs = findings([image])
        for f in fs:
            print("    ", f["severity"], f["category"], f["path"], f["message"])
        left = [f for f in fs if f["severity"] != "info" and f["path"] != "Fixture:Junk"
                and f["category"] != "alias"]
        check(not left, "after tidy: no error or warning but the stray file and the aliases")
        v = disk_audit.Volume(image)
        placed = [n for n in v.nodes.values() if n.id != 2 and not n.flags & disk_audit.INVISIBLE]
        check(all(n.loc != (0, 0) and n.flags & tidy.INITED for n in placed), "every icon placed and inited")
        check(all(n.rect != (0, 0, 0, 0) and n.scroll == (0, 0) for n in v.nodes.values()
                  if n.is_dir and n.name not in tidy.FINDER_FOLDERS), "every folder window framed, scroll 0,0")
        check(all(n.flags & disk_audit.INVISIBLE for n in v.nodes.values()
                  if n.is_dir and n.parent == 2 and n.name in tidy.FINDER_FOLDERS), "Desktop Folder invisible")
        labels = next(n for n in v.nodes.values() if n.is_dir and n.name == "Labels")
        kids = list(v.children(labels.id))
        right = max(k.loc[1] + disk_audit.ICON // 2 + disk_audit.label_width(k.name) // 2 for k in kids)
        top, left, bottom, rright = labels.rect
        check(len({k.loc[0] for k in kids}) == 1 and right + 16 <= rright - left,
              f"long labels: one row, the last label inside the window ({right} of {rright - left})")

        plus = hfsplus_fixture(work)
        v = disk_audit.Volume(plus)
        check(v.kind == "HFS+" and v.name == "Plus", f"HFS+ volume read ({v.kind}, {v.name!r})")
        rc, fs = findings([plus])
        for f in fs:
            print("    ", f["severity"], f["category"], f["path"], f["message"])
        check(has(fs, "invisible", "Plus:Junk", "stray"), "HFS+: stray invisible file")
        check(has(fs, "windows", "Plus:Window", "off a 640x480 screen", "error"), "HFS+: window off the screen")

        md = disk_audit.markdown([disk_audit.Audit(disk_audit.Volume(image), [], (640, 480)).run()], (640, 480))
        check("| fixture.img | HFS | Fixture |" in md and "### alias" in md, "Markdown report")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("FAILED: " + "; ".join(FAILS) if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
