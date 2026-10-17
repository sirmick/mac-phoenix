"""snapshot.py — the snapshot format (docs/provisioning/PLAN.md).

A snapshot is an HFS image holding what one product adds to a Mac:

  :Items:            the installed items, as they'd sit at the top of a volume
  :System Folder:    files added or changed in the System Folder
  :Snapshot Info     the manifest, JSON (TEXT, data fork)

The manifest records how the snapshot was made (bake, recipe hash, media
pins, base, OS and architecture, the target volume's name) and a content
listing: every file's path, type, creator, Finder flags and an MD5 per fork.
Its content hash is over that listing, so a re-bake that produced the same
software is recognised as the same even though HFS dates make every image
byte-different. Bytes 16-255 of a resource fork are left out of its MD5:
they are "reserved for system use" and the File Manager rewrites them.
"""

import hashlib
import json
import os
import re
import struct
import subprocess
import tempfile
import time

LS = re.compile(rb"^([dfF])\s+(?:\S{4}/\S{4}\s+)?.*?\s\w{3}\s+\d+\s+(?:\d{4}|\d\d:\d\d)\s(.*)$")
INFO = b"Snapshot Info"
FORMAT = 1
ALIAS_FLAG = 0x8000


class Hfs:
    """hfsutils with a private HOME (it keeps the current volume there)."""

    def __init__(self, work):
        self.home = tempfile.mkdtemp(prefix="hfs.", dir=work)
        self.env = dict(os.environ, HOME=self.home)

    def run(self, *args, check=True, input=None):
        args = [a.encode("mac_roman", "replace") if isinstance(a, str) else a for a in args]
        for _ in range(20):                     # hfsutils locks an image even to read it: another
            r = subprocess.run(args, env=self.env, capture_output=True, input=input)   # bake reading
            if b"unable to obtain lock" not in r.stderr:                              # the same one
                break
            time.sleep(0.5)
        if check and r.returncode:
            raise RuntimeError(b" ".join(args).decode("mac_roman") + ": " +
                               r.stderr.decode("mac_roman", "replace").strip())
        return r.stdout

    def mount(self, image, partition=None):
        self.run("hmount", image, *([str(partition)] if partition else []))

    def umount(self):
        self.run("humount", check=False)

    def ls(self, folder):
        """(name, is_dir) of the items in an HFS folder (MacRoman bytes)."""
        out = []
        for line in self.run("hls", "-l", "-a", folder).split(b"\n"):      # not \r: names can hold one
            m = LS.match(line)
            if m:
                out.append((m.group(2), m.group(1) == b"d"))
        return out

    def exists(self, path):
        parent, _, leaf = path.rstrip(b":").rpartition(b":")
        try:
            return leaf in dict(self.ls((parent or b"") + b":"))
        except RuntimeError:
            return False


def macbinary_forks(blob):
    """(type, creator, finder flags, data, rsrc) of a MacBinary file."""
    dlen, rlen = struct.unpack(">II", blob[83:91])
    data = blob[128:128 + dlen]
    rsrc = blob[128 + (dlen + 127) // 128 * 128:][:rlen]
    flags = (blob[73] << 8) | blob[101]
    return blob[65:69], blob[69:73], flags, data, rsrc


def fork_md5(data, rsrc=False):
    if rsrc and len(data) >= 256:
        data = data[:16] + bytes(240) + data[256:]
    return hashlib.md5(data).hexdigest()


def listing(hfs, root, work):
    """Content listing of everything under HFS folder `root` (mounted):
    [{path, type, creator, flags, data, rsrc, data_len, rsrc_len}] and the
    folders, sorted by path."""
    out = []
    tmp = tempfile.mkdtemp(prefix="ls.", dir=work)

    def walk(hpath, rel):
        for name, is_dir in hfs.ls(hpath + b":"):
            r = rel + b":" + name if rel else name
            if is_dir:
                out.append({"path": r.decode("mac_roman"), "folder": True})
                walk(hpath + b":" + name, r)
                continue
            local = os.path.join(tmp, "f.bin")
            hfs.run("hcopy", "-m", hpath + b":" + name, local)
            with open(local, "rb") as f:
                ftype, creator, flags, data, rsrc = macbinary_forks(f.read())
            out.append({"path": r.decode("mac_roman"), "type": ftype.decode("mac_roman"),
                        "creator": creator.decode("mac_roman"), "flags": flags & ~0x0100,
                        "data_len": len(data), "rsrc_len": len(rsrc),
                        "data": fork_md5(data), "rsrc": fork_md5(rsrc, True)})
    walk(root, b"")
    return sorted(out, key=lambda e: e["path"])


def content_hash(items, system):
    return hashlib.md5(json.dumps([items, system], sort_keys=True).encode()).hexdigest()


VOLATILE_NAMES = {"Installer Log File"}


def install_listing(entries, volatile=()):
    """A listing as far as the install goes: aliases (they record IDs and
    dates), installer logs (dated) and the `volatile` paths (state a first
    launch or a boot writes) by path, type and creator only."""
    out = []
    for e in entries:
        if not e.get("folder") and (e["flags"] & ALIAS_FLAG or e["path"] in volatile
                                    or e["path"].rsplit(":", 1)[-1] in VOLATILE_NAMES):
            e = {k: e[k] for k in ("path", "type", "creator")}
        out.append(e)
    return out


def install_diff(old, new, volatile=()):
    """The paths whose install differs between two manifests ([] if a
    re-bake reproduced the install; content hashes also see run noise)."""
    diff = []
    for k in ("items", "system"):
        a = {e["path"]: e for e in install_listing(old.get(k, []), volatile)}
        b = {e["path"]: e for e in install_listing(new.get(k, []), volatile)}
        diff += [f"{k}:{p}" for p in sorted(set(a) | set(b)) if a.get(p) != b.get(p)]
    return diff


def blank(hfs, path, label, size_mb):
    with open(path, "wb") as f:
        f.truncate(max(size_mb, 1) << 20)
    hfs.run("hformat", "-l", label[:27], path)


def write(work, out, label, items, system, meta):
    """A snapshot image at `out` from staged trees. `items` and `system` are
    lists of (relative path as MacRoman bytes, local MacBinary file or None
    for a folder), as containers.Volume.export gives them. Sized to fit."""
    total = sum(os.path.getsize(l) for _, l in items + system if l)
    nfiles = sum(1 for _ in items + system)
    # HFS allocation blocks grow with the volume; per-file slack plus room
    # for the catalog.
    size_mb = int(total * 1.15 / (1 << 20) + nfiles * 8 / 1024 + 4)
    hfs = Hfs(work)
    part = out + ".part"
    blank(hfs, part, label, size_mb)
    hfs.mount(part)
    try:
        for folder, tree in ((b":Items", items), (b":System Folder", system)):
            hfs.run("hmkdir", folder)
            for rel, local in tree:
                target = folder + b":" + rel
                if local is None:
                    hfs.run("hmkdir", target, check=False)
                else:
                    hfs.run("hcopy", "-m", local, target)
    finally:
        hfs.umount()
    meta = seal(work, part, meta)
    os.replace(part, out)
    return meta


def seal(work, image, meta):
    """List an image's :Items: and :System Folder:, and write its manifest."""
    hfs = Hfs(work)
    hfs.mount(image)
    try:
        for folder in (b":Items", b":System Folder"):
            if not hfs.exists(folder):
                hfs.run("hmkdir", folder)
        if hfs.exists(b":" + INFO):
            hfs.run("hdel", b":" + INFO)
        meta = dict(meta, format=FORMAT)
        meta["items"] = listing(hfs, b":Items", work)
        meta["system"] = listing(hfs, b":System Folder", work)
        meta["content_hash"] = content_hash(meta["items"], meta["system"])
        meta["aliases"] = [e["path"] for e in meta["items"] + meta["system"]
                           if not e.get("folder") and e["flags"] & ALIAS_FLAG]
        info = os.path.join(tempfile.mkdtemp(dir=work), "info")
        with open(info, "wb") as f:
            f.write(json.dumps(meta, indent=1, ensure_ascii=False)
                    .encode("mac_roman", "replace").replace(b"\n", b"\r"))
        hfs.run("hcopy", "-r", info, b":" + INFO)
        hfs.run("hattrib", "-t", "TEXT", "-c", "ttxt", b":" + INFO)
    finally:
        hfs.umount()
    return meta


def seal_volume(work, image, meta):
    """The manifest of a whole-volume snapshot (an `os` bake: a bootable
    System), written invisibly at its top: a listing of everything on it."""
    hfs = Hfs(work)
    hfs.mount(image)
    try:
        if hfs.exists(b":" + INFO):
            hfs.run("hdel", b":" + INFO)
        meta = dict(meta, format=FORMAT)
        meta["items"] = listing(hfs, b"", work)
        meta["system"] = []
        meta["content_hash"] = content_hash(meta["items"], [])
        meta["aliases"] = [e["path"] for e in meta["items"] if not e.get("folder") and e["flags"] & ALIAS_FLAG]
        info = os.path.join(tempfile.mkdtemp(dir=work), "info")
        with open(info, "wb") as f:
            f.write(json.dumps(meta, indent=1, ensure_ascii=False)
                    .encode("mac_roman", "replace").replace(b"\n", b"\r"))
        hfs.run("hcopy", "-r", info, b":" + INFO)
        hfs.run("hattrib", "-t", "TEXT", "-c", "ttxt", "+i", b":" + INFO)
    finally:
        hfs.umount()
    return meta


def manifest(path, work=None):
    """The manifest of a snapshot image."""
    hfs = Hfs(work or tempfile.gettempdir())
    hfs.mount(path)
    try:
        raw = hfs.run("hcopy", "-r", b":" + INFO, "-")
    finally:
        hfs.umount()
    return json.loads(raw.replace(b"\r", b"\n").decode("mac_roman"))
