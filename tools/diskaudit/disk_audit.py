#!/usr/bin/env python3
"""disk_audit.py — audit Mac disk images on the host (HFS and HFS+), read-only.

What a user of the image would trip over in the Finder, per image:

  alias     every alias file ('alis': Apple Menu Items, Startup Items,
            Launcher Items, anywhere) resolved on its own volume or on the
            companion images (--with; images named together are each
            other's companions; a library boot disk, whose record beside it
            names its "system", only for disks whose record lists that
            System in "systems"): its volume by name (its own first, then
            the one with the alias's creation date), then its path (one
            into a System Folder found also in the disk's own System Folder
            Additions, which the tab copies there), then its
            target's ID *with the same name* (the Alias Manager would find
            it moved). "dangling": the volume is there, the target isn't (a
            reused ID is said); "external": no such volume here (a CD, the
            installer's disk): listed, not counted.
  icons     Finder icon locations (FInfo/DInfo fdLocation) in each folder:
            piles at (0,0) beside placed icons, overlapping icons, icons
            outside the folder's window (left/above its content, or past it
            when the window is small); on the desktop (Desktop Folder, the
            volume icon) off a --screen sized screen.
  windows   folder window frames (DInfo frRect) off the screen (under the
            menu bar, past its right or bottom edge), too small to use;
            scroll origins (DXInfo frScroll) past the folder's content.
  invisible invisible files and folders the Finder doesn't make (stray
            Snapshot Info, ._ files, ...; a library disk's "System Folder
            Additions", tools/web/make_disk.py's, is not one), items a session leaves behind
            (Recent Applications/Documents/Servers). (Desktop Folder and
            Trash may be visible: the 7.6 and 8.6 Finders leave them so and
            hide them themselves.)

  disk_audit.py IMAGE... [--with IMAGE]... [--report FILE.md] [--json] [--screen 640x480]

Exit status 1 when an image has an error-level finding (a dangling alias, a
window off the screen), else 0. Images are opened read-only.
"""

import argparse
import json
import os
import struct
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "provisioning"))
import fat_cfm  # noqa: E402
from aliases import HfsVolume, parse_alias  # noqa: E402

ALIAS, INVISIBLE, HAS_CUSTOM_ICON = 0x8000, 0x4000, 0x0400
ADDITIONS = "System Folder Additions"     # tools/web/make_disk.py --system-from
ROOT = 2
ICON = 32                       # an icon's box in icon view, without its label
LABEL_GAP = 8                   # between two labels side by side


def label_width(name):
    """An icon label's width in pixels, a little over what the Finder draws:
    the views font, Geneva 10 on 8.6 (Geneva 9 on 7.6.1 is narrower).
    Measured: "MR Browser 0.39 ƒ" 95 (this says 98), "Serial MReader
    0.03 ƒ" 109 (113)."""
    w = 0
    for c in name:
        if c in "il|!.,:;'`":
            w += 3
        elif c in "jft1 rI()[]":
            w += 4
        elif c in "mwMW@%":
            w += 9
        elif c.isupper() or c in "&#\u0192":     # ƒ
            w += 7
        else:
            w += 6
    return w
MENU_BAR = 20

# Invisible items the System and the Finder make
FINDER_INVISIBLE = {
    "Desktop DB", "Desktop DF", "Desktop", "Icon\r", "Desktop Folder", "Trash", "Temporary Items",
    "Network Trash Folder", "TheVolumeSettingsFolder", "TheFindByContentFolder", "Shutdown Check",
    "Cleanup At Startup", "OpenFolderListDF\r", "DesktopPrinters DB", "AppleShare PDS", "Move&Rename",
    "VM Storage", "Desktop Pictures", "Find.idx", "Shared Libraries", "Serial Port Monitor",
}
# Folders a session fills (Apple Menu Items' Recent ...)
SESSION_FOLDERS = {"Recent Applications", "Recent Documents", "Recent Servers"}


class Node:
    __slots__ = ("id", "parent", "name", "is_dir", "type", "creator", "flags", "loc", "rect", "scroll",
                 "view", "rsrc", "offset", "size")

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k))


def _dinfo(b):
    """Folder Finder info, HFS's DInfo + DXInfo or HFS+'s FolderInfo + ExtendedFolderInfo
    (the same layout): window rect, flags, location, view, scroll."""
    rect = struct.unpack(">4h", b[0:8])
    flags, = struct.unpack(">H", b[8:10])
    loc = struct.unpack(">2h", b[10:14])
    view, = struct.unpack(">h", b[14:16])
    scroll = struct.unpack(">2h", b[16:20])
    return rect, flags, loc, view, scroll


class Volume:
    """A volume's catalog as Nodes: HFS through provisioning/aliases.py's
    reader, HFS+ (bare or in an HFS wrapper) here."""

    def __init__(self, path):
        self.path = path
        self.image = os.path.basename(path)
        try:                                    # make_library.py's record, beside it
            with open(path + ".json") as f:
                self.record = json.load(f)
        except (OSError, ValueError):
            self.record = {}
        self.nodes = {}
        f = open(path, "rb")
        self.f = f
        base = self._partition(f)
        f.seek(base + 1024)
        head = f.read(512)
        if head[:2] == b"BD" and head[124:126] in (b"H+", b"HX"):     # an HFS wrapper
            block, = struct.unpack(">I", head[20:24])
            first, = struct.unpack(">H", head[28:30])
            start, _ = struct.unpack(">HH", head[126:130])
            base += first * 512 + start * block
            f.seek(base + 1024)
            head = f.read(512)
        if head[:2] in (b"H+", b"HX"):
            self.kind = "HFS+"
            self._read_hfsplus(base, head)
        elif head[:2] == b"BD":
            self.kind = "HFS"
            self._read_hfs()
        else:
            raise ValueError(f"{path}: neither HFS nor HFS+")

    @staticmethod
    def _partition(f):
        f.seek(0)
        if f.read(2) != b"ER":
            return 0
        for i in range(1, 64):
            f.seek(512 * i)
            pm = f.read(512)
            if pm[:2] != b"PM":
                break
            if pm[48:57] == b"Apple_HFS":
                return struct.unpack(">I", pm[8:12])[0] * 512
        raise ValueError("no HFS partition")

    # HFS ------------------------------------------------------------------

    def _read_hfs(self):
        v = self.hfs = HfsVolume(self.path)
        self.name, self.created = v.name, v.created
        mdb = v._read(1024, 162)
        size, = struct.unpack(">I", mdb[146:150])
        catalog = v._fork(v._extents(mdb[150:162]), size, 4, 0)
        for key, data, off in _leaves(catalog, 512):
            parent, = struct.unpack(">I", key[1:5])
            name = key[6:6 + key[5]].decode("mac_roman")
            if data[0] == 1:                             # a folder
                did, = struct.unpack(">I", data[6:10])
                rect, flags, loc, view, scroll = _dinfo(data[22:54])
                self.nodes[did] = Node(id=did, parent=parent, name=name, is_dir=True, flags=flags, loc=loc,
                                       rect=rect, view=view, scroll=scroll, offset=off)
            elif data[0] == 2:                           # a file
                fid, = struct.unpack(">I", data[20:24])
                ftype, creator = data[4:8], data[8:12]
                flags, = struct.unpack(">H", data[12:14])
                loc = struct.unpack(">2h", data[14:18])
                rsrc = (v._extents(data[86:98]), struct.unpack(">I", data[36:40])[0])
                dsize, = struct.unpack(">I", data[26:30])
                self.nodes[fid] = Node(id=fid, parent=parent, name=name, is_dir=False, type=ftype,
                                       creator=creator, flags=flags, loc=loc, rsrc=rsrc, offset=off,
                                       size=dsize + rsrc[1])

    # HFS+ (TN1150) ---------------------------------------------------------

    def _read_hfsplus(self, base, head):
        self.base = base
        self.block, = struct.unpack(">I", head[40:44])
        self.created, = struct.unpack(">I", head[16:20])
        self.overflow = {}
        xt = self._fork_plus(head[192:272], 3, 0)
        for key, data, _ in _leaves(xt, None, plus=True):
            ftype, fid, start = key[0], *struct.unpack(">II", key[2:10])
            self.overflow[(fid, ftype, start)] = data[:64]
        catalog = self._fork_plus(head[272:352], 4, 0)
        threads = {}
        for key, data, off in _leaves(catalog, None, plus=True):
            parent, = struct.unpack(">I", key[0:4])
            n, = struct.unpack(">H", key[4:6])
            name = _hfsplus_name(key[6:6 + 2 * n])
            rtype, = struct.unpack(">h", data[0:2])
            if rtype == 1:
                did, = struct.unpack(">I", data[8:12])
                rect, flags, loc, view, scroll = _dinfo(data[48:80])
                self.nodes[did] = Node(id=did, parent=parent, name=name, is_dir=True, flags=flags, loc=loc,
                                       rect=rect, view=view, scroll=scroll, offset=off)
            elif rtype == 2:
                fid, = struct.unpack(">I", data[8:12])
                flags, = struct.unpack(">H", data[56:58])
                self.nodes[fid] = Node(id=fid, parent=parent, name=name, is_dir=False, type=data[48:52],
                                       creator=data[52:56], flags=flags, loc=struct.unpack(">2h", data[58:62]),
                                       rsrc=data[168:248], offset=off,
                                       size=struct.unpack(">Q", data[88:96])[0]
                                       + struct.unpack(">Q", data[168:176])[0])
            elif rtype == 3 and parent == 1:
                threads[parent] = name
        root = self.nodes.get(ROOT)
        self.name = root.name if root else "?"

    def _fork_plus(self, fork, fid, ftype):
        size, = struct.unpack(">Q", fork[0:8])
        exts, out, blocks = fork[16:80], bytearray(), 0
        while len(out) < size:
            for i in range(8):
                start, count = struct.unpack(">II", exts[8 * i:8 * i + 8])
                if count:
                    self.f.seek(self.base + start * self.block)
                    out += self.f.read(count * self.block)
                    blocks += count
            if len(out) >= size:
                break
            exts = self.overflow.get((fid, ftype, blocks))
            if not exts:
                break
        return bytes(out[:size])

    # Both --------------------------------------------------------------------

    def resource_fork(self, node):
        if self.kind == "HFS":
            exts, size = node.rsrc
            return self.hfs._fork(exts, size, node.id, 0xFF)
        return self._fork_plus(node.rsrc, node.id, 0xFF)

    def path_of(self, node):
        parts, n = [node.name], node
        while n.parent in self.nodes and n.parent != 1:
            n = self.nodes[n.parent]
            parts.append(n.name)
        return ":".join(reversed(parts))

    def children(self, did):
        return [n for n in self.nodes.values() if n.parent == did and n.id != did]


def _hfsplus_name(b):
    import unicodedata
    s = b.decode("utf-16-be", "replace")
    return unicodedata.normalize("NFC", s).replace("/", ":") if "/" in s else unicodedata.normalize("NFC", s)


def _leaves(tree, node_size, plus=False):
    """(key, data, offset of the record in the tree) of every leaf record of a B-tree."""
    if len(tree) < 512:
        return
    head = tree[14:14 + 106]
    node_size = struct.unpack(">H", head[18:20])[0] or node_size or 512
    node = struct.unpack(">I", head[10:14])[0]        # the first leaf
    seen = set()
    while node and node not in seen:
        seen.add(node)
        n = tree[node * node_size:(node + 1) * node_size]
        if len(n) < node_size:
            break
        flink, _, kind, _, count = struct.unpack(">IIbBH", n[:12])
        if kind == -1:
            for i in range(count):
                off, = struct.unpack(">H", n[node_size - 2 * (i + 1):node_size - 2 * i])
                if plus:
                    klen, = struct.unpack(">H", n[off:off + 2])
                    key = n[off + 2:off + 2 + klen]
                    d = off + 2 + klen
                else:
                    klen = n[off]
                    key = n[off + 1:off + 1 + klen]
                    d = off + 1 + klen
                    d += d & 1
                yield key, n[d:], node * node_size + d
        node = flink


# The checks ------------------------------------------------------------------

class Audit:
    def __init__(self, vol, companions, screen):
        # a boot disk for a System this disk isn't for: not one of its companions
        systems = vol.record.get("systems")
        companions = [w for w in companions
                      if w is vol or not systems or not w.record.get("system") or w.record["system"] in systems]
        self.v, self.companions, self.screen = vol, companions, screen
        self.findings = []
        self.stats = Counter()

    def add(self, severity, category, node, message):
        where = self.v.path_of(node) if isinstance(node, Node) else node
        where = where.replace("\r", "\\r")
        self.findings.append({"image": self.v.image, "severity": severity, "category": category,
                              "path": where, "message": message})

    def run(self):
        self.aliases()
        self.icons()
        self.windows()
        self.invisible()
        return self

    # aliases
    def aliases(self):
        v = self.v
        for node in v.nodes.values():
            if node.is_dir or not node.flags & ALIAS:
                continue
            try:
                alis = [d for t, _, _, _, d in fat_cfm.read_rsrc(v.resource_fork(node)) if t == b"alis"]
            except (struct.error, IndexError, ValueError):
                alis = []
            if not alis:                     # pre-System 7 software set the bit freely
                continue
            self.stats["aliases"] += 1
            t = parse_alias(alis[0])
            if not t:
                self.add("error", "alias", node, "unreadable 'alis'")
                continue
            target_name = t["path"].rsplit(":", 1)[-1]
            named = [w for w in self.companions if w.name == t["volume"]]
            vols = ([w for w in named if w is v] or [w for w in named if w.created == t["volume_date"]]
                    or named)
            if not vols:
                self.stats["aliases external"] += 1
                self.add("info", "alias", node, f"external: {t['path']} (no volume \"{t['volume']}\" here)")
                continue
            paths = {w: {w.path_of(n): n for n in w.nodes.values()} for w in vols}
            if any(t["path"] in ps for ps in paths.values()):
                self.stats["aliases ok"] += 1
                continue
            # into the System Folder: the disk's own additions go there
            _, sep, rest = t["path"].partition(":System Folder:")
            if sep and v is not vols[0] and f"{v.name}:{ADDITIONS}:{rest}" in {v.path_of(n) for n in v.nodes.values()}:
                self.stats["aliases ok"] += 1
                continue
            by_id = [(w, w.nodes[t["id"]]) for w in vols if t["id"] in w.nodes]
            moved = [(w, n) for w, n in by_id if n.name == target_name]
            if moved:
                w, n = moved[0]
                self.add("warning", "alias", node, f"moved: {t['path']} is now {w.path_of(n)} (found by ID)")
                continue
            reused = f"; its ID {t['id']} is now {by_id[0][1].name!r}" if by_id else ""
            self.add("error", "alias", node, f"dangling: {t['path']} isn't on {vols[0].image}{reused}")

    # icons
    def icons(self):
        v = self.v
        W, H = self.screen
        by_parent = defaultdict(list)
        for n in v.nodes.values():
            if n.id != ROOT and not n.flags & INVISIBLE:
                by_parent[n.parent].append(n)
        desktop = [n for n in v.nodes.values() if n.parent == ROOT and n.is_dir and n.name == "Desktop Folder"]
        desktop_id = desktop[0].id if desktop else None
        for did, kids in by_parent.items():
            folder = v.nodes.get(did)
            if folder is None or not folder.is_dir:
                continue
            placed = [k for k in kids if k.loc != (0, 0)]
            piled = [k for k in kids if k.loc == (0, 0)]
            self.stats["icons"] += len(kids)
            self.stats["icons unplaced"] += len(piled)
            if did == desktop_id:
                for k in placed:
                    y, x = k.loc
                    if x < 0 or y < MENU_BAR or x > W - ICON or y > H - ICON:
                        self.add("warning", "icons", k, f"desktop icon at {x},{y}: off a {W}x{H} screen")
                continue
            if placed and len(piled) >= 2:
                self.add("warning", "icons", folder,
                         f"{len(piled)} of {len(kids)} icons piled at (0,0) beside placed ones: "
                         + ", ".join(k.name for k in piled[:5]) + (" ..." if len(piled) > 5 else ""))
            seen = set()
            for i, a in enumerate(placed):
                for b in placed[i + 1:]:
                    if abs(a.loc[0] - b.loc[0]) < ICON and abs(a.loc[1] - b.loc[1]) < ICON:
                        key = (a.id, b.id)
                        if key not in seen:
                            seen.add(key)
                            self.add("warning", "icons", folder,
                                     f"overlap: {a.name!r} at {a.loc[1]},{a.loc[0]} and {b.name!r} "
                                     f"at {b.loc[1]},{b.loc[0]}")
            # labels: centred under their icons, cut at the window's left
            # edge (it opens scrolled to 0,0) or running into the next one's
            for k in placed:
                if k.loc[1] + ICON // 2 - label_width(k.name) // 2 < 0:
                    self.add("warning", "icons", k, f"label {label_width(k.name)} px wide: cut at the "
                             f"window's left edge (icon at {k.loc[1]},{k.loc[0]})")
            rows = defaultdict(list)
            for k in placed:
                rows[k.loc[0]].append(k)
            for row in rows.values():
                row.sort(key=lambda k: k.loc[1])
                for a, b in zip(row, row[1:]):
                    room = b.loc[1] - a.loc[1]
                    need = (label_width(a.name) + label_width(b.name)) // 2 + LABEL_GAP
                    if need > room:
                        self.add("warning", "icons", folder, f"labels wider than their cell: {a.name!r} and "
                                 f"{b.name!r}, {room} px apart, {need} px needed")
            top, left, bottom, right = folder.rect or (0, 0, 0, 0)
            width, height = right - left, bottom - top
            beyond = []
            for k in placed:
                y, x = k.loc
                if x < -ICON or y < -ICON:
                    self.add("warning", "icons", k, f"at {x},{y}: left of / above its window's content")
                elif width > 0 and height > 0 and (x > width + 4 * ICON or y > height + 4 * ICON):
                    beyond.append(k)
            if beyond:
                self.add("info", "icons", folder, f"{len(beyond)} of {len(kids)} icons beyond its "
                         f"{width}x{height} window (scrolled to): " + ", ".join(k.name for k in beyond[:3])
                         + (" ..." if len(beyond) > 3 else ""))
        # the volume's own icon on the desktop
        root = v.nodes.get(ROOT)
        if root and root.loc != (0, 0):
            y, x = root.loc
            if x < 0 or y < MENU_BAR or x > W - ICON or y > H - ICON:
                self.add("warning", "icons", root, f"volume icon at {x},{y}: off a {W}x{H} screen")

    # windows
    def windows(self):
        W, H = self.screen
        for f in self.v.nodes.values():
            if not f.is_dir:
                continue
            top, left, bottom, right = f.rect
            if (top, left, bottom, right) != (0, 0, 0, 0):
                self.stats["windows"] += 1
                if bottom <= top or right <= left:
                    self.add("error", "windows", f, f"window rect {left},{top},{right},{bottom} is empty")
                elif right <= 0 or left >= W or bottom <= MENU_BAR or top >= H:
                    self.add("error", "windows", f, f"window {left},{top},{right},{bottom} off a {W}x{H} screen")
                elif top < MENU_BAR or left < 0 or right > W or bottom > H:
                    self.add("warning", "windows", f,
                             f"window {left},{top},{right},{bottom} partly off a {W}x{H} screen")
                elif right - left < 100 or bottom - top < 50:
                    self.add("warning", "windows", f, f"window {right - left}x{bottom - top}: too small to use")
            sv, sh = f.scroll
            if (sv, sh) == (0, 0) or (f.parent == ROOT and f.name == "Desktop Folder"):   # the desktop doesn't scroll
                continue
            kids = [k for k in self.v.children(f.id) if not k.flags & INVISIBLE and k.loc != (0, 0)]
            max_v = max([k.loc[0] for k in kids], default=0) + 2 * ICON
            max_h = max([k.loc[1] for k in kids], default=0) + 2 * ICON
            min_v = min([k.loc[0] for k in kids], default=0) - 2 * ICON
            min_h = min([k.loc[1] for k in kids], default=0) - 2 * ICON
            if not (min_v <= sv <= max_v and min_h <= sh <= max_h):
                self.add("warning", "windows", f, f"scroll origin {sh},{sv} past its content "
                         f"({min_h}..{max_h}, {min_v}..{max_v})")

    # invisible and stray items
    def invisible(self):
        v = self.v
        for n in v.nodes.values():
            if n.id == ROOT:
                continue
            parent = v.nodes.get(n.parent)
            if n.parent == ROOT and n.is_dir and n.name == ADDITIONS:
                continue                                # (the tab's: make_disk.py)
            if n.flags & INVISIBLE and n.name not in FINDER_INVISIBLE:
                self.stats["invisible"] += 1
                system = self.in_system_folder(n)       # Apple's own preferences hide some
                self.add("info" if system else "warning", "invisible", n,
                         f"invisible {'folder' if n.is_dir else 'file'}"
                         + (" in the System Folder" if system else ": stray"))
            elif n.name.startswith("._") or n.name in (".DS_Store", "Thumbs.db"):
                self.add("warning", "invisible", n, "host file system debris")
            if parent and parent.name in SESSION_FOLDERS and parent.is_dir and n.name != "Icon\r":
                self.add("info", "session", n, f"left by a session in {parent.name}")


    def in_system_folder(self, n):
        while n.parent in self.v.nodes and n.parent != 1:
            n = self.v.nodes[n.parent]
            if n.parent == ROOT and n.name == "System Folder":
                return True
        return False


# The report ------------------------------------------------------------------

SEVERITIES = ("error", "warning", "info")


def markdown(audits, screen, limit=40):
    out = [f"# Disk audit ({screen[0]}x{screen[1]} screen)", ""]
    out.append("| image | kind | volume | aliases (ok/external) | icons (unplaced) | errors | warnings | info |")
    out.append("|---|---|---|---|---|---|---|---|")
    for a in audits:
        c = Counter(f["severity"] for f in a.findings)
        s = a.stats
        out.append(f"| {a.v.image} | {a.v.kind} | {a.v.name} | {s['aliases']} ({s['aliases ok']}/"
                   f"{s['aliases external']}) | {s['icons']} ({s['icons unplaced']}) | {c['error']} | "
                   f"{c['warning']} | {c['info']} |")
    for a in audits:
        out += ["", f"## {a.v.image}", ""]
        if not a.findings:
            out.append("Nothing found.")
            continue
        by_cat = defaultdict(list)
        for f in a.findings:
            by_cat[f["category"]].append(f)
        for cat in sorted(by_cat, key=lambda c: min(SEVERITIES.index(f["severity"]) for f in by_cat[c])):
            fs = sorted(by_cat[cat], key=lambda f: (SEVERITIES.index(f["severity"]), f["path"]))
            out += [f"### {cat} ({len(fs)})", ""]
            for f in fs[:limit]:
                out.append(f"- **{f['severity']}** `{f['path']}`: {f['message']}")
            if len(fs) > limit:
                out.append(f"- ... {len(fs) - limit} more")
            out.append("")
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("images", nargs="+")
    ap.add_argument("--with", dest="companions", action="append", default=[],
                    help="an image aliases may point into (not audited itself)")
    ap.add_argument("--report", help="write the Markdown report here (default: stdout)")
    ap.add_argument("--json", action="store_true", help="one JSON finding per line instead")
    ap.add_argument("--screen", default="640x480")
    args = ap.parse_args(argv)
    screen = tuple(int(x) for x in args.screen.lower().split("x"))
    vols, extra = [], []
    for paths, into in ((args.images, vols), (args.companions, extra)):
        for p in paths:
            try:
                into.append(Volume(p))
            except (ValueError, OSError) as e:
                print(f"skipped {p}: {e}", file=sys.stderr)
    audits = [Audit(v, vols + extra, screen).run() for v in vols]
    if args.json:
        for a in audits:
            for f in a.findings:
                print(json.dumps(f, ensure_ascii=False))
    else:
        text = markdown(audits, screen)
        if args.report:
            with open(args.report, "w") as f:
                f.write(text)
        else:
            sys.stdout.write(text)
    return 1 if any(f["severity"] == "error" for a in audits for f in a.findings) else 0


if __name__ == "__main__":
    sys.exit(main())
