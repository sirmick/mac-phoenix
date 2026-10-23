"""containers.py — read software out of media for a bake.

A media file opens as a container: an archive unar reads is a folder tree
("Folder/File"); an HFS image (raw, CD with a partition map, Disk Copy 4.2
or 6/NDIF) is a volume (":Folder"). "!" steps into a nested container:
"CD.iso!:Folder". A trailing ":*" or "/*" takes a folder's contents.
Exported items are (relative path as MacRoman bytes, MacBinary file or None
for a folder).

Only media preparation uses this: what goes onto a guest's Media volume or
is attached as a disk for a bake, and the Installers disks (installers.py),
which hold the same. Software reaches disks through snapshots,
which the Mac makes.
"""

import json
import os
import re
import struct
import subprocess
import tempfile

import ndif
from populate_disk import build_macbinary
from snapshot import Hfs

ARCHIVE_EXTS = (".sit", ".hqx", ".bin", ".sea", ".zip", ".cpt", ".sitx", ".gz", ".tgz")
MAC_EPOCH = 2082844800


def mr(s):
    return s.encode("mac_roman", errors="replace") if isinstance(s, str) else s


# ------------------------------------------------------------ containers
#
# A container lists items and exports them as a staging tree: a list of
# (relative path as MacRoman bytes, None for a folder or a MacBinary file).

class Volume:
    def __init__(self, image, work):
        self.image, self.work = image, work
        self.hfs = Hfs(work)
        self.partition = None
        for part in (None, 1, 2, 3):
            try:
                self.hfs.mount(image, part)
                self.hfs.umount()
                self.partition = part
                return
            except RuntimeError as e:
                err = e
        raise RuntimeError(f"{image}: not an HFS volume ({err})")

    def export(self, path, stage):
        """Items under path (":A:B", "" = root) as MacBinary files in stage."""
        self.hfs.mount(self.image, self.partition)
        try:
            path = mr(path).rstrip(b":")
            items = []
            if path and not path.endswith(b"*"):
                parent, _, leaf = path.rpartition(b":")
                kinds = dict(self.hfs.ls(parent + b":" if parent else b":"))
                if leaf not in kinds:
                    raise RuntimeError(f"{self.image}: no {path.decode('mac_roman')}")
                if not kinds[leaf]:
                    return [(leaf, self._copy_out(path, stage))]
                items.append((leaf, None))
                self._walk(path, leaf, stage, items)
            else:
                self._walk(path.rstrip(b"*").rstrip(b":"), b"", stage, items)
            return items
        finally:
            self.hfs.umount()

    def _walk(self, hpath, rel, stage, items):
        for name, is_dir in self.hfs.ls((hpath or b"") + b":"):
            sub = (hpath or b"") + b":" + name
            r = rel + b":" + name if rel else name
            if is_dir:
                items.append((r, None))
                self._walk(sub, r, stage, items)
            else:
                items.append((r, self._copy_out(sub, stage)))

    def _copy_out(self, hpath, stage):
        local = os.path.join(stage, f"{len(os.listdir(stage))}.bin")
        self.hfs.run("hcopy", "-m", glob_quote(hpath), local)
        return local


def glob_quote(path):
    """An HFS path hcopy takes literally: it globs its arguments, so a name
    holding \\ * ? [ ] (AppleScript's "Edit the Current Page\\E") needs them escaped."""
    return re.sub(rb"([\\*?\[\]])", rb"\\\1", path)


class Archive:
    """What unar extracts: data forks, resource forks in NAME.rsrc
    AppleDouble files beside them (a file with only a resource fork is just
    NAME.rsrc). Finder info comes from lsar: unar only writes it beside a
    resource fork."""

    def __init__(self, path, work):
        self.root = tempfile.mkdtemp(prefix="unar.", dir=work)
        r = subprocess.run(["unar", "-q", "-f", "-o", self.root, path], capture_output=True)
        if r.returncode:
            raise RuntimeError(f"unar {path}: {r.stderr.decode(errors='replace').strip()}")
        self.finder = {}
        r = subprocess.run(["lsar", "-json", path], capture_output=True, text=True, errors="replace")
        try:
            listing = json.loads(re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", "", r.stdout))
        except json.JSONDecodeError:
            listing = {}
        for e in listing.get("lsarContents", []):
            if e.get("XADIsDirectory"):
                continue
            info = (struct.pack(">I", e.get("XADFileType", 0) or 0x3F3F3F3F),
                    struct.pack(">I", e.get("XADFileCreator", 0) or 0x3F3F3F3F),
                    e.get("XADFinderFlags", 0))
            self.finder.setdefault(e.get("XADFileName", "").replace("/", os.sep), info)

    def local(self, path):
        return os.path.join(self.root, path.rstrip("/*").rstrip("/"))

    def export(self, path, stage):
        top = self.local(path.lstrip(":"))     # ":X" after a nested "!:" is X at the top
        if not os.path.exists(top) and not os.path.exists(top + ".rsrc"):
            raise RuntimeError(f"archive has no {path}")
        items = []
        if path.endswith("*") or not path:
            self._walk(top, b"", stage, items)
        elif os.path.isdir(top):
            name = self._mac_name(os.path.basename(top))
            items.append((name, None))
            self._walk(top, name, stage, items)
        else:
            items.append((self._mac_name(os.path.basename(top)), self.macbinary(top, stage)))
        return items

    def _is_file(self, full):
        return os.path.isfile(full) or os.path.isfile(full + ".rsrc")

    def _walk(self, folder, rel, stage, items):
        for entry in sorted(os.listdir(folder)):
            full = os.path.join(folder, entry)
            if entry.endswith(".rsrc") and not os.path.isdir(full):
                if os.path.exists(full[:-5]):
                    continue                     # the fork of a file listed on its own
                entry, full = entry[:-5], full[:-5]   # a file with only a resource fork
            name = self._mac_name(entry)
            r = rel + b":" + name if rel else name
            if os.path.isdir(full):
                items.append((r, None))
                self._walk(full, r, stage, items)
            else:
                items.append((r, self.macbinary(full, stage)))

    @staticmethod
    def _mac_name(name):
        # unar shows a Mac "/" as ":"; HFS paths use ":" so put "/" back.
        return mr(name.replace(":", "/"))[:31]

    def forks(self, path):
        """data, rsrc, type, creator, flags of an extracted file."""
        data = b""
        if os.path.isfile(path):
            with open(path, "rb") as f:
                data = f.read()
        rsrc = b""
        ftype, creator, flags = self.finder.get(os.path.relpath(path, self.root), (b"????", b"????", 0))
        if os.path.exists(path + ".rsrc"):
            with open(path + ".rsrc", "rb") as f:
                ad = f.read()
            if ad[:4] == b"\x00\x05\x16\x07":
                for k in range(struct.unpack(">H", ad[24:26])[0]):
                    eid, off, n = struct.unpack(">III", ad[26 + k * 12:38 + k * 12])
                    if eid == 2:
                        rsrc = ad[off:off + n]
                    elif eid == 9 and n >= 10:
                        ftype, creator = ad[off:off + 4], ad[off + 4:off + 8]
                        flags = struct.unpack(">H", ad[off + 8:off + 10])[0]
            else:
                rsrc = ad
        return data, rsrc, ftype, creator, flags

    def macbinary(self, path, stage):
        data, rsrc, ftype, creator, flags = self.forks(path)
        stamp = int(os.path.getmtime(path if os.path.exists(path) else path + ".rsrc")) + MAC_EPOCH
        local = os.path.join(stage, f"{len(os.listdir(stage))}.bin")
        with open(local, "wb") as f:
            f.write(build_macbinary(self._mac_name(os.path.basename(path)), ftype, creator,
                                    data, rsrc, stamp, stamp, flags & ~0x0100))  # not "inited"
        return local


def open_container(path, work):
    """A container for a file: an HFS image if it is one (after NDIF/Disk
    Copy 4.2 decoding), else an archive."""
    with open(path, "rb") as f:
        head = f.read(1026)
    if (head[1024:1026] == b"BD" or head[:2] == b"ER") and not os.path.exists(path + ".rsrc"):
        return Volume(path, work)                            # raw HFS or partitioned
        # (with a resource fork it may be NDIF: its first chunk is stored raw)
    data, rsrc = ndif.forks(path)
    raw = disk_image(data, rsrc)
    if raw is not None:
        image = os.path.join(tempfile.mkdtemp(prefix="img.", dir=work), "disk.img")
        if raw is data and not path.lower().endswith(ARCHIVE_EXTS):
            os.symlink(os.path.abspath(path), image)
        else:
            with open(image, "wb") as f:
                f.write(raw)
                if len(raw) < 819200:            # hfsutils' floor: an 800K disk
                    f.truncate(819200)
        return Volume(image, work)
    return Archive(path, work)


def disk_image(data, rsrc):
    """The raw HFS image in data/rsrc, or None if it isn't one."""
    if rsrc and ndif.resource(rsrc, b"bcem", 128):
        return ndif.to_raw(data, rsrc)
    if len(data) > 84 + 1026 and data[0] < 64 and data[82:84] == b"\x01\x00":   # Disk Copy 4.2
        size = struct.unpack(">I", data[64:68])[0]
        return data[84:84 + size]
    if data[1024:1026] == b"BD" or data[:2] == b"ER":        # HFS, or a partition map
        return data
    return None


def take(path, spec, stage, work):
    """Export spec ("A/B!:C:D") from the file at path; a "!:" opens the item
    before it as a nested container (a lone "!" is part of a name:
    "Airborne!.img")."""
    container = open_container(path, work)
    parts = spec.split("!:")
    parts = parts[:1] + [":" + p for p in parts[1:]]     # as split("!") gave them
    for inner in parts[:-1]:
        items = container.export(inner, stage)
        if len(items) != 1 or items[0][1] is None:
            raise RuntimeError(f"{inner} is not a file")
        nested = os.path.join(tempfile.mkdtemp(prefix="nest.", dir=work), "item")
        macbinary_to_files(items[0][1], nested)
        container = open_container(nested, work)
    return container.export(parts[-1], stage)


def macbinary_to_files(mb, out):
    """MacBinary -> out (data) + out.rsrc (AppleDouble: rsrc + Finder info)."""
    with open(mb, "rb") as f:
        blob = f.read()
    dlen, rlen = struct.unpack(">II", blob[83:91])
    data = blob[128:128 + dlen]
    rsrc = blob[128 + (dlen + 127) // 128 * 128:][:rlen]
    finfo = blob[65:73] + bytes([blob[73], blob[101]]) + bytes(22)
    with open(out, "wb") as f:
        f.write(data)
    entries = [(9, finfo), (2, rsrc)]
    header = b"\x00\x05\x16\x07" + struct.pack(">I", 0x20000) + bytes(16) + struct.pack(">H", len(entries))
    off = len(header) + 12 * len(entries)
    table, body = b"", b""
    for eid, payload in entries:
        table += struct.pack(">III", eid, off + len(body), len(payload))
        body += payload
    with open(out + ".rsrc", "wb") as f:
        f.write(header + table + body)



def named(items, spec, name):
    """Items and the rename to pass to install() for a step's "as": a
    container's root or a folder's contents go into a new folder of that
    name; a single item is renamed."""
    if name and (not spec or spec.endswith("*") or spec.endswith("!:")):
        return [(mr(name), None)] + [(mr(name) + b":" + r, l) for r, l in items], None
    return items, name


