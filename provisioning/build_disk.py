#!/usr/bin/env python3
"""build_disk.py — build an HFS disk from a provisioning list.

A list (provisioning/lists/*.json) is a macfetch manifest whose entries also
say how to install them:

  {"name": "Frontier_5.0.1.sit", ..., "install": [
      {"take": "Frontier 5.0.1", "to": "Languages"}]}

Each step takes something out of the entry's file and puts it on a disk:

  take   what to take, "" for everything. A file opens as a container:
         an archive unar reads (.sit .hqx .bin .sea .zip ...) is a folder
         tree ("Folder/File"); an HFS image (raw, .iso/.toast with a
         partition map, Disk Copy 4.2 or 6/NDIF) is a volume (":Folder").
         "!" steps into a nested container: "CD.iso!:Folder".
         A trailing ":*" or "/*" takes a folder's contents, not the folder.
  to     the folder to put it in on the disk, colon-separated ("" = root)
  as     the name to give the taken item (needed for a container's root)
  skip   names of items inside it to leave out
  replace  true: overwrite files already on the disk (default: keep them,
         so a base System's own copy of an extension wins)
  disk   which disk the step is for: "development" (the default) or "boot"
  os     only for these System versions (with --os), e.g. ["7.5.5"]
  size   [preferred K, minimum K]: rewrite the taken app's SIZE resources
         (an app whose SIZE asks for more than a guest has)
  fat_with  another list file holding the other architecture's build of
         the taken file (a CFM library or app): the two are merged into
         one fat file (fat_cfm.py), for disks that serve 68k and PowerPC

Taking a file copies it whole, forks and Finder info kept: a Disk Copy
image stays an image, for mounting in the guest.

  build_disk.py LIST --disk development -o Development.img --size 1024 --label Development
  build_disk.py LIST --disk boot --base macos-7.6.1.img.orig -o dev-7.6.1.img --os 7.6.1

The list is provisioned first (macfetch: verified against its pins). hfsutils
runs with a private HOME, so builds don't disturb each other.
"""

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "macfetch"))
sys.path.insert(0, HERE)
import fat_cfm  # noqa: E402
import macfetch  # noqa: E402
import ndif  # noqa: E402
from populate_disk import build_macbinary  # noqa: E402

ARCHIVE_EXTS = (".sit", ".hqx", ".bin", ".sea", ".zip", ".cpt", ".sitx", ".gz", ".tgz")
MAC_EPOCH = 2082844800
# hls -l: kind, type/creator for files, ..., date, name
LS = re.compile(rb"^([dfF])\s+(?:\S{4}/\S{4}\s+)?.*?\s\w{3}\s+\d+\s+(?:\d{4}|\d\d:\d\d)\s(.*)$")


def work_root():
    """Scratch space for images: the storage disk, not /tmp (often a small
    RAM disk) -- builds stage whole volumes."""
    root = os.path.join(macfetch.STORAGE, "work")
    os.makedirs(root, exist_ok=True)
    return root


def mr(s):
    return s.encode("mac_roman", errors="replace") if isinstance(s, str) else s


class Hfs:
    """hfsutils with a private HOME (it keeps the current volume there)."""

    def __init__(self, work):
        self.env = dict(os.environ, HOME=tempfile.mkdtemp(prefix="hfs.", dir=work))

    def run(self, *args, check=True):
        r = subprocess.run([mr(a) for a in args], env=self.env, capture_output=True)
        if check and r.returncode:
            raise RuntimeError(b" ".join(mr(a) for a in args).decode("mac_roman") + ": " +
                               r.stderr.decode("mac_roman", "replace").strip())
        return r.stdout

    def mount(self, image, partition=None):
        self.run("hmount", image, *([str(partition)] if partition else []))

    def umount(self):
        self.run("humount", check=False)

    def ls(self, folder):
        """(name, is_dir) of the items in an HFS folder (MacRoman bytes)."""
        out = []
        for line in self.run("hls", "-l", "-a", folder).splitlines():
            m = LS.match(line)
            if m:
                out.append((m.group(2), m.group(1) == b"d"))
        return out


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
        self.hfs.run("hcopy", "-m", hpath, local)
        return local


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
        top = self.local(path)
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
    """Export spec ("A/B!:C:D") from the file at path; a "!" opens the item
    before it as a nested container."""
    container = open_container(path, work)
    parts = spec.split("!")
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


# ------------------------------------------------------------- the disk

def install(hfs, image, items, to, rename, skip, replace=False):
    """Copy staged items into folder `to` on image; returns the names of
    files left alone because the disk already had them."""
    hfs.mount(image)
    kept = []
    listed = {}

    def exists(path):
        folder, _, leaf = path.rpartition(b":")
        if folder not in listed:
            try:
                listed[folder] = {n for n, _ in hfs.ls(folder + b":")}
            except RuntimeError:
                listed[folder] = set()
        return leaf in listed[folder]

    try:
        dest = b":" + mr(to).strip(b":") if to else b""
        made = set()
        parts = [p for p in dest.split(b":") if p]
        for i in range(len(parts)):
            d = b":" + b":".join(parts[:i + 1])
            hfs.run("hmkdir", d, check=False)
            made.add(d)
        skip = {mr(s) for s in skip}
        for rel, local in items:
            segs = rel.split(b":")
            if rename:
                segs[0] = mr(rename)
            if any(s in skip for s in segs):
                continue
            target = dest + b":" + b":".join(segs)
            if local is None:
                if target not in made:
                    hfs.run("hmkdir", target, check=False)
                    made.add(target)
            elif not replace and exists(target):
                kept.append(target.decode("mac_roman"))
            else:
                hfs.run("hcopy", "-m", local, target)
        return kept
    finally:
        hfs.umount()


def set_size(macbinary, preferred_k, minimum_k):
    """Rewrite the preferred and minimum partition in every SIZE resource of
    an app (as a MacBinary file)."""
    with open(macbinary, "rb") as f:
        blob = f.read()
    header, data, rsrc = fat_cfm.macbinary(blob)
    res = []
    for t, i, attrs, name, payload in fat_cfm.read_rsrc(rsrc):
        if t == b"SIZE":
            payload = payload[:2] + struct.pack(">II", preferred_k << 10, minimum_k << 10) + payload[10:]
        res.append((t, i, attrs, name, payload))
    rsrc = fat_cfm.write_rsrc(res)
    header = bytearray(header)
    struct.pack_into(">II", header, 83, len(data), len(rsrc))
    header[124:126] = struct.pack(">H", fat_cfm.crc(bytes(header[:124])))
    with open(macbinary, "wb") as f:
        f.write(bytes(header) + data + bytes((-len(data)) % 128) + rsrc + bytes((-len(rsrc)) % 128))


def named(items, spec, name):
    """Items and the rename to pass to install() for a step's "as": a
    container's root or a folder's contents go into a new folder of that
    name; a single item is renamed."""
    if name and (not spec or spec.endswith("*") or spec.endswith("!:")):
        return [(mr(name), None)] + [(mr(name) + b":" + r, l) for r, l in items], None
    return items, name


def build(args):
    manifest = macfetch.load_manifest(args.list)
    out_dir = macfetch.manifest_out(args.list)
    if not args.no_provision:
        rc = subprocess.run([sys.executable, macfetch.__file__, "provision", args.list]).returncode
        if rc:
            sys.exit("provision failed; nothing built")
    work = tempfile.mkdtemp(prefix="build_disk.", dir=work_root())
    hfs = Hfs(work)
    image = args.out + ".part"           # renamed into place only when complete
    if args.base:
        shutil.copyfile(args.base, image)
    else:
        with open(image, "wb") as f:
            f.truncate(args.size << 20)
        hfs.run("hformat", "-l", args.label, image)
    steps = 0
    try:
        for p in manifest["files"]:
            src = os.path.join(out_dir, macfetch.pin_path(p))
            for step in p.get("install", []):
                if step.get("disk", "development") != args.disk:
                    continue
                if step.get("os") and args.os not in step["os"]:
                    continue
                stage = tempfile.mkdtemp(prefix="stage.", dir=work)
                spec = step.get("take", "")
                items = take(src, spec, stage, work)
                if step.get("fat_with"):
                    other = next(q for q in manifest["files"] if q["name"] == step["fat_with"])
                    stage2 = tempfile.mkdtemp(prefix="stage.", dir=work)
                    (_, mine), = items
                    (_, theirs), = take(os.path.join(out_dir, macfetch.pin_path(other)), spec, stage2, work)
                    with open(mine, "rb") as a, open(theirs, "rb") as b:
                        fat = fat_cfm.merge(a.read(), b.read())
                    with open(mine, "wb") as f:
                        f.write(fat)
                if step.get("size"):
                    (_, mine), = items
                    set_size(mine, *step["size"])
                items, rename = named(items, spec, step.get("as"))
                files = sum(1 for _, l in items if l)
                print(f"  {macfetch.pin_path(p)}  {spec or '(all)'} -> :{step.get('to', '')}"
                      f"{':' + (step.get('as') or '') if step.get('as') else ''}  ({files} files)")
                kept = install(hfs, image, items, step.get("to", ""), rename,
                               step.get("skip", []), step.get("replace", False))
                if kept:
                    more = f" and {len(kept) - 3} more" if len(kept) > 3 else ""
                    print(f"    kept the disk's own {', '.join(kept[:3])}{more}")
                shutil.rmtree(stage)
                steps += 1
    except BaseException:
        os.unlink(image)
        raise
    finally:
        shutil.rmtree(work, ignore_errors=True)
    os.replace(image, args.out)
    image = args.out
    hfs = Hfs(tempfile.gettempdir())
    hfs.mount(image)
    vol = hfs.run("hvol").decode("mac_roman", "replace")
    hfs.umount()
    shutil.rmtree(hfs.env["HOME"], ignore_errors=True)
    free = re.search(r"(\d+(?:\.\d+)?\s*\w+) free", vol)
    print(f"{image}: {steps} steps" + (f", {free.group(1)} free" if free else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("list")
    ap.add_argument("-o", "--out", required=True, help="image to write")
    ap.add_argument("--disk", default="development", help="which steps: development | boot")
    ap.add_argument("--base", help="start from a copy of this image (else a new volume)")
    ap.add_argument("--size", type=int, default=1024, help="new volume size, MB")
    ap.add_argument("--label", default="Development", help="new volume name")
    ap.add_argument("--os", help="System version, for steps limited to some")
    ap.add_argument("--no-provision", action="store_true", help="files are already provisioned")
    build(ap.parse_args())


if __name__ == "__main__":
    main()
