#!/usr/bin/env python3
"""aliases.py — find broken aliases on HFS disk images, on the host.

Reads each image's catalog directly (no guest, no hfsutils): every file
whose Finder flags say alias, its 'alis' resource, and where it points: the
target volume (name and creation date), the full path, the file ID. Each
alias is looked up on the images scanned together:

  ok        the path is there (on the alias's own volume when it has that
            name, else the one with the same creation date, else any of
            that name: many images are "Macintosh HD")
  by-id     the path isn't, the target's file ID is: the Alias Manager
            would find it, and fix the path
  missing   the volume is there, the target isn't
  external  no volume of that name among those scanned (a CD, a disk the
            product came on, the bake's own boot disk): not checked

  aliases.py dev-7.6.1 dev-tools-system-7     images in storage/images, or paths
  aliases.py --all                           every image there, looked up together
  aliases.py --all --show external           ... also list the external ones
  aliases.py IMAGE --json                    one JSON object per alias
"""

import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fat_cfm  # noqa: E402

ALIAS_FLAG = 0x8000
DATA, RSRC = 0x00, 0xFF


class HfsVolume:
    """An HFS volume in an image file (bare, or in an Apple partition map)."""

    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.base = self._find_volume()
        mdb = self._read(1024, 162)
        if mdb[:2] != b"BD":
            raise ValueError(f"{path}: not an HFS volume")
        self.created = struct.unpack(">I", mdb[2:6])[0]
        self.block = struct.unpack(">I", mdb[20:24])[0]
        self.first = struct.unpack(">H", mdb[28:30])[0] * 512
        self.name = mdb[37:37 + mdb[36]].decode("mac_roman")
        xt_size, ct_size = struct.unpack(">I", mdb[130:134])[0], struct.unpack(">I", mdb[146:150])[0]
        self.overflow = {}
        xt = self._fork(self._extents(mdb[134:146]), xt_size, 3, DATA)
        for key, data in self._leaves(xt):
            if len(key) >= 7:                            # fork type, file ID, first block
                ftype, fnum, start = struct.unpack(">BIH", key[:7])
                self.overflow[(fnum, ftype, start)] = self._extents(data[:12])
        catalog = self._fork(self._extents(mdb[150:162]), ct_size, 4, DATA)
        self.dirs, self.files = {}, {}       # dir ID -> (parent ID, name); file ID -> record
        for key, data in self._leaves(catalog):
            parent = struct.unpack(">I", key[1:5])[0]       # (key[0] reserved)
            name = key[6:6 + key[5]]
            if data[0] == 1:
                self.dirs[struct.unpack(">I", data[6:10])[0]] = (parent, name)
            elif data[0] == 2:
                fid = struct.unpack(">I", data[20:24])[0]
                self.files[fid] = {"parent": parent, "name": name, "type": data[4:8], "creator": data[8:12],
                                   "flags": struct.unpack(">H", data[12:14])[0],
                                   "rsrc": (self._extents(data[86:98]), struct.unpack(">I", data[36:40])[0])}

    def _find_volume(self):
        self.base = 0
        if self._read(1024, 2) == b"BD":
            return 0
        if self._read(0, 2) == b"ER":                    # an Apple partition map
            for i in range(1, 64):
                pm = self._read(512 * i, 512)
                if pm[:2] != b"PM":
                    break
                if pm[48:57] == b"Apple_HFS":
                    return struct.unpack(">I", pm[8:12])[0] * 512
        raise ValueError(f"{self.path}: no HFS volume")

    def _read(self, off, n):
        self.f.seek(self.base + off)
        return self.f.read(n)

    @staticmethod
    def _extents(rec):
        return [struct.unpack(">HH", rec[i:i + 4]) for i in range(0, 12, 4)]

    def _fork(self, extents, size, fnum, ftype):
        """A fork's bytes: its first three extents, then the overflow file's."""
        out, blocks = bytearray(), 0
        exts = list(extents)
        while len(out) < size:
            for start, count in exts:
                if count:
                    out += self._read(self.first + start * self.block, count * self.block)
                    blocks += count
            if len(out) >= size:
                break
            exts = self.overflow.get((fnum, ftype, blocks))
            if not exts:
                break                                    # truncated: the caller sees short data
        return bytes(out[:size])

    @staticmethod
    def _leaves(tree):
        """(key, data) of every leaf record of a B-tree file."""
        if len(tree) < 512:
            return
        head = tree[14:14 + 106]
        node_size = struct.unpack(">H", head[18:20])[0] or 512
        node = struct.unpack(">I", head[10:14])[0]       # bthFNode: the first leaf
        seen = set()
        while node and node not in seen:
            seen.add(node)
            n = tree[node * node_size:(node + 1) * node_size]
            if len(n) < node_size:
                break
            flink, _, kind, _, count = struct.unpack(">IIbBH", n[:12])
            if kind == -1:
                for i in range(count):
                    off = struct.unpack(">H", n[node_size - 2 * (i + 1):node_size - 2 * i])[0]
                    klen = n[off]
                    key = n[off + 1:off + 1 + klen]
                    d = off + 1 + klen
                    d += d & 1
                    yield key, n[d:]
            node = flink

    def path_of(self, parent, name):
        parts = [name]
        while parent in self.dirs and parent != 1:
            parent, n = self.dirs[parent]
            parts.append(n)
        return b":".join(reversed(parts)).decode("mac_roman")     # the root's name is the volume's

    def paths(self):
        out = {self.path_of(p, n) for p, n in self.dirs.values()}
        return out | {self.path_of(r["parent"], r["name"]) for r in self.files.values()}

    def resource_fork(self, rec, fid):
        exts, size = rec["rsrc"]
        return self._fork(exts, size, fid, RSRC)


def parse_alias(rec):
    """Target of an 'alis' record: volume name, its creation date, kind, file
    ID, full path (volume first) or None."""
    if len(rec) < 150:
        return None
    vol = rec[11:11 + min(rec[10], 27)].decode("mac_roman")
    kind = struct.unpack(">H", rec[8:10])[0]
    vdate = struct.unpack(">I", rec[38:42])[0]
    fid = struct.unpack(">I", rec[114:118])[0]
    tags, o = {}, 150
    while o + 4 <= len(rec):
        tag, n = struct.unpack(">hH", rec[o:o + 4])
        if tag == -1:
            break
        tags[tag] = rec[o + 4:o + 4 + n]
        o += 4 + n + (n & 1)
    path = tags[2].decode("mac_roman") if 2 in tags else None
    if path is None:                                   # no full path: volume + name
        path = vol + ":" + rec[51:51 + min(rec[50], 63)].decode("mac_roman")
    return {"volume": vol, "volume_date": vdate, "kind": "folder" if kind == 1 else "file", "id": fid, "path": path}


def scan(images):
    vols = []
    for p in images:
        try:
            vols.append(HfsVolume(p))
        except (ValueError, OSError) as e:
            print(f"skipped {p}: {e}", file=sys.stderr)
    index = [(v, v.paths()) for v in vols]
    out = []
    for v in vols:
        for fid, rec in v.files.items():
            if not rec["flags"] & ALIAS_FLAG:
                continue
            where = os.path.basename(v.path) + ":" + v.path_of(rec["parent"], rec["name"])
            fork = v.resource_fork(rec, fid)
            try:
                alis = [d for t, _, _, _, d in fat_cfm.read_rsrc(fork) if t == b"alis"]
            except (struct.error, IndexError):
                alis = []
            if not alis:                    # not an alias: pre-System 7 software set that bit freely
                continue
            target = parse_alias(alis[0])
            if not target:
                out.append({"alias": where, "state": "unreadable"})
                continue
            named = [(w, ps) for w, ps in index if w.name == target["volume"]]
            same = ([(w, ps) for w, ps in named if w is v]          # its own volume first,
                    or [(w, ps) for w, ps in named if w.created == target["volume_date"]] or named)
            if not same:
                state = "external"
            elif any(target["path"] in ps for _, ps in same):
                state = "ok"
            elif target["id"] and any(target["id"] in w.files or target["id"] in w.dirs for w, _ in same):
                state = "by-id"
            else:
                state = "missing"
            out.append({"alias": where, "state": state, **target})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("images", nargs="*")
    ap.add_argument("--all", action="store_true", help="every image in storage/images")
    ap.add_argument("--show", action="append", default=[], help="states to list besides missing, unreadable, by-id")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    import bake
    if args.all:
        paths = sorted(os.path.join(bake.IMAGES, f) for f in os.listdir(bake.IMAGES) if f.endswith(".img"))
    else:
        paths = [p if os.sep in p or p.endswith(".img") and os.path.exists(p) else os.path.join(bake.IMAGES, p + ".img")
                 for p in args.images]
    if not paths:
        ap.error("name images, or --all")
    results = scan(paths)
    if args.json:
        for r in results:
            print(json.dumps(r, ensure_ascii=False))
        return 0
    show = {"missing", "unreadable", "by-id"} | set(args.show)
    for r in results:
        if r["state"] in show:
            print(f"{r['state']:10} {r['alias']}  ->  {r.get('path', '?')}")
    counts = {}
    for r in results:
        counts[r["state"]] = counts.get(r["state"], 0) + 1
    print(f"{len(results)} aliases on {len(paths)} images: " + ", ".join(f"{n} {s}" for s, n in sorted(counts.items())))
    return 1 if counts.get("missing") or counts.get("unreadable") else 0


if __name__ == "__main__":
    sys.exit(main())
