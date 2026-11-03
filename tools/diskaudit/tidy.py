#!/usr/bin/env python3
"""tidy.py — put an HFS image's Finder info in order (what disk_audit.py checks).

    tidy.py IMAGE [--screen 640x480] [--dry-run]

  icons     every visible item of every folder is placed on a grid inside
            its folder's window, by name, as the Finder's Clean Up does; the
            folder's cells as wide as its longest label (disk_audit.label_width),
            the first column far enough in for that label
  windows   every folder's window gets a frame on the screen sized to its
            icons (up to the screen), opened a little lower and to the right
            the deeper the folder, and scroll origin 0,0
  folders   the volume's Desktop Folder and Trash are invisible; a folder
            holding an "Icon\r" file shows it (kHasCustomIcon)

The Desktop Folder's items keep their places (desktop coordinates). Used by
tools/web/make_boot_disk.py and tools/web/make_library.py on the disks they
make; writes the image in place (catalog records only). Aliases to what a
cut leaves out are make_boot_disk.py's to drop (it doesn't copy them).
"""

import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import disk_audit  # noqa: E402

GRID_H, GRID_V = 128, 64         # a cell: an icon and its label (8.6's Finder's grid), at least
ORIGIN_H, ORIGIN_V = 48, 8       # the first cell: room for a long label left of its icon, at least
MAX_COLUMNS = 4
INITED = 0x0100                  # kHasBeenInited: without it the Finder places the item itself
CUSTOM_ICON = 0x0400             # kHasCustomIcon: a folder's "Icon\r" file is its icon
ICON_VIEW = 256                  # DInfo frView of a folder viewed by icon (8.6's Finder writes it)
WINDOW_LEFT, WINDOW_TOP = 12, 64  # the first level's window; deeper ones step down and right
STEP = 16
MARGIN = 24                      # the window's scroll bar and some air
FINDER_FOLDERS = ("Desktop Folder", "Trash")


def _catalog_extents(vol):
    """The catalog file's extents, the overflow file's after its first three."""
    hv = vol.hfs
    mdb = hv._read(1024, 162)
    exts, blocks, out = hv._extents(mdb[150:162]), 0, []
    while exts:
        for start, count in exts:
            if count:
                out.append((start, count))
                blocks += count
        exts = hv.overflow.get((4, 0, blocks))
    return out


class CatalogWriter:
    """Writes fields of catalog records in place, through the catalog's extents."""

    def __init__(self, vol, dry_run=False):
        if vol.kind != "HFS":
            raise ValueError(f"{vol.path}: tidy writes HFS only ({vol.kind})")
        self.vol, self.dry_run = vol, dry_run
        self.extents = _catalog_extents(vol)
        self.f = None if dry_run else open(vol.path, "r+b")
        self.writes = 0

    def write(self, node, field, data):
        hv, off = self.vol.hfs, node.offset + field
        for start, count in self.extents:
            n = count * hv.block
            if off < n:
                self.writes += 1
                if self.f:
                    self.f.seek(hv.base + hv.first + start * hv.block + off)
                    self.f.write(data)
                return
            off -= n
        raise ValueError(f"{node.name}: record past the catalog's extents")

    def close(self):
        if self.f:
            self.f.close()


# HFS catalog record fields (Inside Macintosh: Files, "Catalog File Data")
FILE_FLAGS, FILE_LOCATION = 12, 14                                # FInfo fdFlags, fdLocation
DIR_RECT, DIR_FLAGS, DIR_LOCATION, DIR_VIEW, DIR_SCROLL = 22, 30, 32, 36, 38   # DInfo; DXInfo frScroll


def depth(vol, node):
    d = 0
    while node.parent in vol.nodes and node.id != disk_audit.ROOT:
        node = vol.nodes[node.parent]
        d += 1
    return d


def arrange(vol, w, screen):
    """Icons on the grid, window frames, scroll 0,0. Returns (folders, icons) placed."""
    W, H = screen
    desktop = {n.id for n in vol.nodes.values()
               if n.is_dir and n.parent == disk_audit.ROOT and n.name in FINDER_FOLDERS}
    # Folders with an "Icon\r" file get kHasCustomIcon: the snapshots keep
    # no Finder info for folders, so a custom icon's flag is lost on the way
    # (the icon itself is there)
    custom = {n.parent for n in vol.nodes.values() if not n.is_dir and n.name == "Icon\r"}
    def flags(n):
        return n.flags | (CUSTOM_ICON if n.is_dir and n.id in custom else 0)
    folders = icons = 0
    for folder in vol.nodes.values():
        if not folder.is_dir or folder.id in desktop:
            continue
        kids = sorted((k for k in vol.children(folder.id) if not k.flags & disk_audit.INVISIBLE),
                      key=lambda k: k.name.lower())
        level = depth(vol, folder)
        left = min(WINDOW_LEFT + STEP * level, W // 6)
        top = min(WINDOW_TOP + STEP * level, H // 4)
        # cells as wide as the longest label; labels are centred under their
        # icons, so the first column is in by half of it
        label = max([disk_audit.label_width(k.name) for k in kids], default=0)
        grid_h = max(GRID_H, label + disk_audit.LABEL_GAP)
        origin_h = max(ORIGIN_H, label // 2 - disk_audit.ICON // 2 + disk_audit.LABEL_GAP)
        tail = disk_audit.ICON // 2 + (label + 1) // 2 + MARGIN   # past the last column's icon
        cols = max(1, min(len(kids), MAX_COLUMNS, (W - left - 8 - origin_h - tail) // grid_h + 1))
        rows = max(1, -(-len(kids) // cols))
        width = max(min(origin_h + (cols - 1) * grid_h + tail, W - left - 8), 260)
        height = max(min(rows * GRID_V + ORIGIN_V + MARGIN, H - top - 24), 120)   # its frame on the screen too
        for i, k in enumerate(kids):
            v, h = ORIGIN_V + (i // cols) * GRID_V, origin_h + (i % cols) * grid_h
            w.write(k, DIR_LOCATION if k.is_dir else FILE_LOCATION, struct.pack(">hh", v, h))
            w.write(k, DIR_FLAGS if k.is_dir else FILE_FLAGS, struct.pack(">H", flags(k) | INITED))
            icons += 1
        w.write(folder, DIR_RECT, struct.pack(">4h", top, left, top + height, left + width))
        w.write(folder, DIR_SCROLL, struct.pack(">hh", 0, 0))
        w.write(folder, DIR_FLAGS, struct.pack(">H", flags(folder) | INITED))   # else its frame isn't used
        if folder.view == 0:
            w.write(folder, DIR_VIEW, struct.pack(">h", ICON_VIEW))
        folders += 1
    for n in vol.nodes.values():
        if n.id in desktop and not n.flags & disk_audit.INVISIBLE:
            w.write(n, DIR_FLAGS, struct.pack(">H", n.flags | disk_audit.INVISIBLE))
    return folders, icons


def tidy(image, screen=(640, 480), dry_run=False):
    """Tidy image in place; returns what was done."""
    vol = disk_audit.Volume(image)
    w = CatalogWriter(vol, dry_run)
    try:
        folders, icons = arrange(vol, w, screen)
    finally:
        w.close()
    return {"folders": folders, "icons": icons}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("--screen", default="640x480")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args(argv)
    screen = tuple(int(x) for x in args.screen.lower().split("x"))
    done = tidy(args.image, screen, args.dry_run)
    print(f"{args.image}: {done['icons']} icons placed in {done['folders']} folders"
          + (" (dry run)" if args.dry_run else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
