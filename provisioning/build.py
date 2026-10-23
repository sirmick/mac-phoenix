#!/usr/bin/env python3
"""build.py — images from snapshots (docs/provisioning/PLAN.md).

An image definition (provisioning/images/<name>.json) says which snapshots
go where:

  {"name": "dev-tools-system-7", "label": "Development", "size_mb": 1024,
   "place": [{"snapshot": "mpw-3.4.2"},
             {"snapshot": "frontier-5.0.1", "to": "Languages"},
             {"snapshot": "think-c-6", "take": "Development", "as": "THINK C 6.0"}]}

  {"name": "dev-7.6.1", "base": "7.6.1",
   "system_from": ["codewarrior-pro-4", "macsbug-6.6.3"]}

A data image is a blank volume ("label", default the name; "size_mb",
default sized to fit with room to spare) with snapshots' :Items: placed:
"take" an item of :Items: (default all of them; a path takes one inside a
folder: "CodeWarrior Pro 4:CodeWarrior MPW:MPW"), "to" a folder
(default the top), "as" a new name for the taken item, "launch" the apps to
launch (paths in the snapshot, for one baked without a "launch"),
"redirect" ({old: new}, paths in the snapshot: an alias target an updater
renamed after the alias was made), "external_aliases": "drop" (aliases to
anything not placed are left out: a CD's documentation). At a data
image's top there are only folders, each named "<Product> <version> ƒ"
(categories too: "Tools ƒ"), but for MPW and Interfaces&Libraries, which MPW
finds by name; the build fails otherwise. An Installers image (one per
category: installers-games, -productivity, -development, -system) holds
no snapshots but the installers bakes used, a folder per product:
"installers": [{"as": "Excel 4.0 ƒ", "bakes": ["excel-4.0"]}] (installers.py
says what goes in); every bake with media must be on one. A boot image starts
from a copy of a base System (provisioning/bases.json) and gets the
:System Folder: parts of the snapshots in "system_from", replacing the
System's own files (later snapshots win over earlier ones); it can "place"
items too, and "disable" System Folder items: moved to a "Disabled Items"
folder at its top, to put back by hand (6.0.8's AppleShare, whose INIT takes
high memory Silicon Beach's games won't share). Two snapshots placing the
same file fail the build.

Aliases a snapshot holds point at its bake's target volume. Those whose
target is among the items placed are rewritten to where it landed (volume
name and creation date, full path; the stale file and folder IDs cleared,
so the Alias Manager finds it by path). A boot image's System Folder
aliases point into a data image: "aliases_to" names it. Built volumes get a
creation date from their name, so aliases into them stay valid across
rebuilds. Aliases to anything else (a CD the product came on) are kept.

"title" (a short name) and "tags" are for the web UI's disk list: copied
to the record with "note", not part of what decides a rebuild. Tags: the
machines a boot image is for (68k, ppc), dev (development tools), test
(what the guest tests boot), build (a layer bakes start from), mpw-service
(tools/mpw's disks).

Built images go to storage/images/<name>.img with a record beside them
(<name>.img.json: the snapshots and their content hashes, and the apps to
launch, as paths on the image); an image is rebuilt when its definition, its
base or a snapshot's content changed.

  build.py NAME...            build these images (if not up to date)
  build.py --all              every image definition
  build.py --list             definitions and their state
"""

import argparse
import hashlib
import json
import os
import shutil
import struct
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bake  # noqa: E402
import fat_cfm  # noqa: E402
import installers  # noqa: E402
import snapshot  # noqa: E402
from containers import Volume, mr  # noqa: E402
from snapshot import Hfs  # noqa: E402

DEFS = os.path.join(HERE, "images")
FORMAT = 9                  # bumped when what build.py writes changes: images rebuild


def load_def(name):
    path = os.path.join(DEFS, name + ".json")
    if not os.path.exists(path):
        sys.exit(f"no image definition {name} ({path})")
    d = bake.load_json(path)
    d["name"] = name
    return d


def all_defs():
    return sorted(f[:-5] for f in os.listdir(DEFS) if f.endswith(".json"))


def out_path(d):
    return os.path.join(bake.IMAGES, d["name"] + ".img")


def snapshots_of(d):
    return list(dict.fromkeys([p["snapshot"] for p in d.get("place", [])] + d.get("system_from", [])))


def manifests(d):
    out = {}
    for s in snapshots_of(d):
        path = bake.snapshot_path(s)
        if not os.path.exists(path):
            sys.exit(f"{d['name']}: snapshot {s} not baked ({path}); bake.py {s}")
        out[s] = snapshot.manifest(path, bake.work_root())
    return out


META = ("title", "tags")         # for the web UI's disk list; copied to the record, not in the key


def image_key(d, mans):
    h = hashlib.md5(json.dumps([FORMAT, {k: v for k, v in d.items() if k not in META}], sort_keys=True).encode())
    if d.get("aliases_to"):                 # where its System Folder aliases point
        other = load_def(d["aliases_to"])
        try:
            h.update(bake.load_json(record_path(other)).get("key", "").encode())
        except (OSError, ValueError):
            pass
    if d.get("base"):
        h.update(bake.base_identity(d["base"]).encode())
    for s in sorted(mans):
        h.update(mans[s]["content_hash"].encode())
        h.update(json.dumps(mans[s].get("system_remove", [])).encode())   # not in the content hash
    for p in d.get("installers", []):                # what the bakes gave the guest
        for n in p["bakes"]:
            b = bake.load_bake(n)
            h.update(json.dumps(b, sort_keys=True).encode())
            for m in bake.media_names(b):
                h.update(bake.media_entry(m).get("md5", "").encode())
    return h.hexdigest()


def content_identity(d):
    """What a built image holds, for keys that must survive a rebuild (a
    bake's base): its definition but the descriptive fields, its snapshots'
    contents, its base's identity. Not FORMAT: how build.py writes an image
    doesn't change what is on it."""
    mans = manifests(d)
    h = hashlib.md5(json.dumps({k: v for k, v in d.items() if k not in META + ("note",)}, sort_keys=True).encode())
    if d.get("base"):
        h.update(bake.base_identity(d["base"]).encode())
    for s in sorted(mans):
        h.update(mans[s]["content_hash"].encode())
        h.update(json.dumps(mans[s].get("system_remove", [])).encode())
    return h.hexdigest()


def record_path(d):
    return out_path(d) + ".json"


def write_record(d, record):
    """<name>.img.json, with the definition's title, tags and note (the web
    UI's disk list shows them)."""
    for k in META + ("note",):
        record.pop(k, None)
        if d.get(k):
            record[k] = d[k]
    with open(record_path(d), "w") as f:
        json.dump(record, f, indent=1, ensure_ascii=False)


def up_to_date(d, key):
    """The record's key matches, and the image is still what was built (a
    web UI profile or a test booting it in place changes it)."""
    try:
        rec = bake.load_json(record_path(d))
        return (os.path.exists(out_path(d)) and rec.get("key") == key
                and rec.get("md5") in (None, bake.file_md5(out_path(d))))
    except (OSError, ValueError):
        return False


MAC_1997 = 2934835200          # 1997-01-01 in Mac time


def volume_date(label):
    """A built volume's creation date: fixed per name (aliases record it)."""
    return MAC_1997 + int(hashlib.md5(label.encode()).hexdigest(), 16) % (365 * 86400)


def mdb_date(image):
    with open(image, "rb") as f:
        f.seek(1024 + 2)
        return struct.unpack(">I", f.read(4))[0]


def set_mdb_date(image, date):
    """drCrDate in the master directory block and its copy at the end."""
    size = os.path.getsize(image)
    with open(image, "r+b") as f:
        for at in (1024, size - 1024):
            f.seek(at)
            if f.read(2) == b"BD":
                f.seek(at + 2)
                f.write(struct.pack(">I", date))


def mapper(place, label):
    """Path segments under a snapshot's :Items: -> segments on the image, or None."""
    to = [s for s in place.get("to", "").split(":") if s]
    take = place["take"].split(":") if place.get("take") else []

    def m(segs):
        if not segs or segs[:len(take)] != take:
            return None
        if not take:
            return [label] + to + segs
        return [label] + to + [place.get("as") or take[-1]] + segs[len(take):]
    return m


def redirected(place, where):
    """`where` after the place's "redirect": alias targets (paths in the
    snapshot) that moved after the alias was made (an updater renamed them)."""
    moves = place.get("redirect", {})

    def m(segs):
        path = ":".join(segs)
        for old, new in moves.items():
            if path == old or path.startswith(old + ":"):
                segs = (new + path[len(old):]).split(":")
                break
        return where(segs)
    return m if moves else where


def rewrite_alias(rec, target, where, date):
    """An 'alis' record pointing into volume `target`, rewritten by `where`
    (segments -> new segments, volume first); None if it points elsewhere."""
    vol = rec[11:11 + rec[10]].decode("mac_roman")
    tags, o = [], 150
    while o + 4 <= len(rec):
        tag, n = struct.unpack(">hH", rec[o:o + 4])
        if tag == -1:
            break
        tags.append((tag, rec[o + 4:o + 4 + n]))
        o += 4 + n + (n & 1)
    path = dict(tags).get(2)
    if vol != target or not path:
        return None
    new = where(path.decode("mac_roman").split(":")[1:])
    if not new:
        return None
    head = bytearray(rec[:150])
    name = mr(new[0])[:27]
    head[10:38] = bytes([len(name)]) + name.ljust(27, b"\0")
    struct.pack_into(">I", head, 38, date)
    struct.pack_into(">I", head, 46, 0)               # parent folder ID
    struct.pack_into(">I", head, 114, 0)              # file ID
    body = b""
    for tag, data in tags:
        if tag == 1:                                  # folder IDs along the path
            continue
        if tag == 2:
            data = mr(":".join(new))
        body += struct.pack(">hH", tag, len(data)) + data + b"\0" * (len(data) & 1)
    body += struct.pack(">hH", -1, 0)
    rec = bytes(head) + body
    return rec[:4] + struct.pack(">H", len(rec)) + rec[6:], new


def edit_alias(local, edit):
    """Apply edit(record) -> new record or None to the 'alis' resources of
    MacBinary file `local`; what the last edit returned besides, or None."""
    with open(local, "rb") as f:
        blob = f.read()
    header, data, rsrc = fat_cfm.macbinary(blob)
    res, changed = [], None
    for t, i, attrs, name, payload in fat_cfm.read_rsrc(rsrc):
        if t == b"alis":
            r = edit(payload)
            if r:
                payload, changed = r
        res.append((t, i, attrs, name, payload))
    if changed is not None:
        rsrc = fat_cfm.write_rsrc(res)
        header = bytearray(header)
        struct.pack_into(">II", header, 83, len(data), len(rsrc))
        header[124:126] = struct.pack(">H", fat_cfm.crc(bytes(header[:124])))
        with open(local, "wb") as f:
            f.write(bytes(header) + data + bytes((-len(data)) % 128) + rsrc + bytes((-len(rsrc)) % 128))
    return changed


def fix_alias(local, target, where, date):
    """Point the alias in `local` at where its target landed; the target's
    new segments (volume first), or None if it points elsewhere."""
    return edit_alias(local, lambda rec: rewrite_alias(rec, target, where, date))


def set_alias_ids(local, parent, cnid):
    """The target's parent folder ID and its own file or folder ID."""
    def edit(rec):
        rec = bytearray(rec)
        struct.pack_into(">I", rec, 46, parent)
        struct.pack_into(">I", rec, 114, cnid)
        return bytes(rec), True
    edit_alias(local, edit)


def catalog_id(hfs, path):
    out = hfs.run("hls", "-i", "-d", path).split()
    return int(out[0])


def is_alias(local):
    with open(local, "rb") as f:
        return bool(f.read(74)[73] & 0x80)


def launch_paths(place, man):
    """The snapshot's apps as paths on the image, given where its items went.
    A place's own "launch" (paths in the snapshot, as a bake's) stands in for
    a snapshot made without one."""
    out = []
    to = [s for s in place.get("to", "").split(":") if s]
    take = place["take"].split(":") if place.get("take") else []
    for entry in man.get("launch") or place.get("launch", []):
        e = dict(entry) if isinstance(entry, dict) else {"app": entry}
        segs = e["app"].split(":")
        if take:
            if segs[:len(take)] != take:
                continue
            segs = [place.get("as") or take[-1]] + segs[len(take):]
        e["app"] = ":".join(to + segs)
        e["snapshot"] = place["snapshot"]
        out.append(e)
    return out


class Builder:
    def __init__(self, d, work, log=print):
        self.d, self.work, self.log = d, work, log
        self.hfs = Hfs(work)
        self.placed = {}            # path on the image -> snapshot that put it there
        self.warnings = []
        self.fixed, self.kept, self.dropped = [], [], []    # aliases rewritten, left alone, left out
        self.ids = []               # (alias on the image, image its target is on, target segments)

    def stage(self, snap, path):
        st = tempfile.mkdtemp(prefix="stage.", dir=self.work)
        return Volume(bake.snapshot_path(snap), self.work).export(path, st), st

    def put(self, image, snap, items, dest, rename=None, replace=False, alias=None, drop_external=False):
        """Copy staged items into folder dest (b":A:B" or b"") of image.
        alias: (target volume, where, date) for rewriting aliases; with
        drop_external, aliases pointing anywhere else are left out."""
        replaced, dropped = [], set()
        for rel, local in items:
            if local and alias and is_alias(local):
                path = rel.decode("mac_roman")
                new = fix_alias(local, *alias[:3])
                if new:
                    segs = rel.split(b":")
                    if rename:
                        segs[0] = mr(rename)
                    self.ids.append((dest + b":" + b":".join(segs), alias[3], new))
                    self.fixed.append(f"{snap}: {path}")
                elif drop_external:
                    dropped.add(rel)
                    self.dropped.append(f"{snap}: {path}")
                else:
                    self.kept.append(f"{snap}: {path}")
        items = [(r, l) for r, l in items if r not in dropped]
        self.hfs.mount(image)
        try:
            listed = {}

            def exists(path):
                folder, _, leaf = path.rpartition(b":")
                if folder not in listed:
                    try:
                        listed[folder] = {n for n, _ in self.hfs.ls(folder + b":")}
                    except RuntimeError:
                        listed[folder] = set()
                return leaf in listed[folder]

            parts = [p for p in dest.split(b":") if p]
            for i in range(len(parts)):
                self.hfs.run("hmkdir", b":" + b":".join(parts[:i + 1]), check=False)
            for rel, local in items:
                segs = rel.split(b":")
                if rename:
                    segs[0] = mr(rename)
                target = dest + b":" + b":".join(segs)
                if local is None:
                    self.hfs.run("hmkdir", target, check=False)
                    continue
                key = target.decode("mac_roman")
                if exists(target):
                    if key in self.placed and self.placed[key] != snap and not replace:
                        raise SystemExit(f"{self.d['name']}: {key} from both {self.placed[key]} and {snap}")
                    if not replace and key not in self.placed:
                        raise SystemExit(f"{self.d['name']}: {key} from {snap} is already on the base")
                    replaced.append(key)
                    self.hfs.run("hdel", target)
                self.hfs.run("hcopy", "-m", local, target)
                self.placed[key] = snap
        finally:
            self.hfs.umount()
        return replaced

    def place(self, image, label, p, man):
        snap, take, as_ = p["snapshot"], p.get("take"), p.get("as")
        if as_ and not take:
            sys.exit(f"{self.d['name']}: {snap}: \"as\" needs \"take\"")
        items, st = self.stage(snap, ":Items:" + take if take else ":Items:*")
        to = b":" + mr(p.get("to", "")).strip(b":") if p.get("to") else b""
        self.put(image, snap, items, to, as_, alias=(man.get("target"), redirected(p, mapper(p, label)), self.date, image),
                 drop_external=p.get("external_aliases") == "drop")
        shutil.rmtree(st)
        files = sum(1 for _, l in items if l)
        where = ":".join(s for s in (p.get("to", "").strip(":"), as_) if s)
        self.log(f"  {snap}: {take or '(all)'} -> :{where}  ({files} files)")

    def alias_ids(self, image):
        """Fill in the IDs of rewritten aliases' targets, now that they exist."""
        ids = {}
        for where in {t for _, t, _ in self.ids}:
            self.hfs.mount(where)
            try:
                for _, t, segs in self.ids:
                    if t != where:
                        continue
                    path = mr(":" + ":".join(segs[1:]))
                    parent = path.rpartition(b":")[0]
                    try:
                        ids[(t, path)] = (catalog_id(self.hfs, parent + b":") if parent else 2,
                                          catalog_id(self.hfs, path))
                    except (RuntimeError, ValueError, IndexError):
                        self.warnings.append(f"alias target {':'.join(segs)} is not on {os.path.basename(t)}")
            finally:
                self.hfs.umount()
        st = tempfile.mkdtemp(prefix="alias.", dir=self.work)
        self.hfs.mount(image)
        try:
            for n, (alias, t, segs) in enumerate(self.ids):
                key = (t, mr(":" + ":".join(segs[1:])))
                if key not in ids:
                    continue
                local = os.path.join(st, f"{n}.bin")
                self.hfs.run("hcopy", "-m", alias, local)
                set_alias_ids(local, *ids[key])
                self.hfs.run("hdel", alias)
                self.hfs.run("hcopy", "-m", local, alias)
        finally:
            self.hfs.umount()

    def system(self, image, snap, man):
        sysf = b":System Folder"
        items, st = self.stage(snap, ":System Folder:*")
        alias = None
        if self.d.get("aliases_to"):
            data = load_def(self.d["aliases_to"])
            p = next((p for p in data.get("place", []) if p["snapshot"] == snap), None)
            if p:
                label = data.get("label", data["name"])
                alias = (man.get("target"), mapper(p, label), volume_date(label), out_path(data))
        replaced = self.put(image, snap, items, sysf, replace=True, alias=alias)
        removed = []
        if man.get("system_remove"):            # what the product supersedes but leaves behind
            self.hfs.mount(image)
            try:
                for rel in sorted(man["system_remove"], key=lambda r: -r.count(":")):   # deepest first
                    path = sysf + b":" + mr(rel)
                    if self.hfs.exists(path):
                        if self.hfs.run("hdel", path, check=False) == b"" and self.hfs.exists(path):
                            self.hfs.run("hrmdir", path, check=False)                     # a folder
                        removed.append(rel)
            finally:
                self.hfs.umount()
        shutil.rmtree(st)
        files = sum(1 for _, l in items if l)
        self.log(f"  {snap}: System Folder  ({files} files"
                 + (f"; replaced {', '.join(r.split(':', 2)[-1] for r in replaced)}" if replaced else "")
                 + (f"; removed {', '.join(removed)}" if removed else "") + ")")


PLAIN = {b"MPW", b"Interfaces&Libraries"}      # folders found by name: MPW looks for {MPW}::Interfaces&Libraries:


def check_names(hfs, image, name):
    """A data image's top level: one folder per product, its name ending in
    " ƒ" (the Finder convention), but for the folders software finds by name."""
    hfs.mount(image)
    try:
        bad = [n.decode("mac_roman") for n, is_dir in hfs.ls(b":")
               if n not in PLAIN and not (is_dir and n.endswith(mr(" ƒ")))]
    finally:
        hfs.umount()
    if bad:
        raise SystemExit(f"{name}: at the top, not a folder named \"... ƒ\": {', '.join(bad)}"
                         " (place with \"take\" + \"as\", or \"to\")")


def record_launch(d, mans, label):
    """The apps to launch, as paths on the image: its snapshots', and a built
    base's (its record's)."""
    launch = []
    base_info = bake.bases().get(d.get("base") or "", {})
    if "built" in base_info:
        try:
            launch = bake.load_json(os.path.join(bake.IMAGES, base_info["built"] + ".img.json")).get("launch", [])
        except (OSError, ValueError):
            pass
    for p in d.get("place", []):
        launch += [dict(e, app=f"{label}:{e['app']}") for e in launch_paths(p, mans[p["snapshot"]])]
    return launch


def build(d, force=False, log=print):
    if d.get("aliases_to"):
        build(load_def(d["aliases_to"]), log=log)       # its aliases need the IDs there
    mans = manifests(d)
    key = image_key(d, mans)
    out = out_path(d)
    if not force and up_to_date(d, key):
        log(f"{d['name']}: up to date ({out})")
        rec = bake.load_json(record_path(d))
        launch = record_launch(d, mans, rec["label"])     # launch facts: not in the key
        if any(rec.get(k) != d.get(k) for k in META + ("note",)) or launch != rec.get("launch"):
            write_record(d, dict(rec, launch=launch))
        return out
    work = tempfile.mkdtemp(prefix="build.", dir=bake.work_root())
    b = Builder(d, work, log)
    part = out + ".part"
    log(f"{d['name']}: building {out}")
    try:
        if d.get("base"):
            base, info = bake.base_image(d["base"])
            shutil.copyfile(base, part)
            label = bake.volume_name(part)
            b.date = mdb_date(part)
        else:
            label = d.get("label", d["name"])
            size = d.get("size_mb")
            products = []
            for p in d.get("installers", []):
                vols = set()
                products.append((p, installers.product_items(p, work, vols), vols))
            if not size and products:
                size = installers.volume_mb([i for _, items, _ in products for i in [(None, None)] + items])
            if not size:
                total = sum(os.path.getsize(bake.snapshot_path(s)) for s in mans)
                size = int(total * 1.5 / (1 << 20)) + 64
            if size > 2047:
                raise SystemExit(f"{d['name']}: {size} MB, over HFS's 2 GB: split it")
            snapshot.blank(b.hfs, part, label, size)
            b.date = volume_date(label)
            set_mdb_date(part, b.date)
            for p, items, vols in products:
                staged = {r for r, _ in items}
                for vol in vols:                    # a CD staged as a folder: its aliases into it
                    def where(segs, vol=vol, p=p):
                        rel = mr(":".join([vol] + segs))
                        return [label, p["as"], vol] + segs if rel in staged else None
                    for rel, local in items:
                        if local and is_alias(local):
                            new = fix_alias(local, vol, where, b.date)
                            if new:
                                b.ids.append((b":" + mr(p["as"]) + b":" + rel, part, new))
                                b.fixed.append(f"{p['as']}: {rel.decode('mac_roman')}")
                bake.put(b.hfs, part, [(mr(p["as"]), None)] + [(mr(p["as"]) + b":" + r, l) for r, l in items])
                log(f"  {p['as']}: {', '.join(p['bakes'])}  ({sum(1 for _, l in items if l)} files, "
                    f"{installers.staged_size(items) >> 20} MB)")
            if products:
                missing = installers.unlisted([load_def(n) for n in all_defs()])
                if missing:
                    b.warnings.append(f"bakes with media on no Installers disk: {', '.join(missing)}")
        for p in d.get("place", []):
            b.place(part, label, p, mans[p["snapshot"]])
        for s in d.get("system_from", []):
            if d.get("base") and mans[s].get("os") and mans[s]["os"] != d["base"]:
                log(f"  ({s} was baked on {mans[s]['os']})")
            b.system(part, s, mans[s])
        if d.get("disable"):                    # System Folder items set aside, restorable
            b.hfs.mount(part)
            try:
                b.hfs.run("hmkdir", b":Disabled Items", check=False)
                for name in d["disable"]:
                    src = b":System Folder:" + mr(name)
                    if not b.hfs.exists(src):
                        raise SystemExit(f"{d['name']}: no {name} in the System Folder to disable")
                    b.hfs.run("hrename", src, b":Disabled Items:" + mr(name))
                    log(f"  disabled: {name} (now in :Disabled Items)")
            finally:
                b.hfs.umount()
        b.alias_ids(part)
        if not d.get("base"):
            check_names(b.hfs, part, d["name"])
    except BaseException:
        if os.path.exists(part):
            os.unlink(part)
        raise
    finally:
        shutil.rmtree(work, ignore_errors=True)
    os.replace(part, out)
    if b.fixed:
        log(f"  aliases rewritten: {', '.join(b.fixed)}")
    if b.kept:
        b.warnings.append(f"aliases left pointing outside the image: {', '.join(b.kept)}")
    if b.dropped:
        log(f"  aliases pointing outside the image, left out: {', '.join(b.dropped)}")
    for w in b.warnings:
        log(f"  warning: {w}")
    launch = record_launch(d, mans, label)
    base_info = bake.bases().get(d.get("base") or "", {})
    if "built" in base_info:                # a built base: its aliases are this image's too
        try:
            b.ids_paths = bake.load_json(os.path.join(bake.IMAGES, base_info["built"] + ".img.json")).get("aliases", [])
        except (OSError, ValueError):
            pass
    record = {"name": d["name"], "label": label, "key": key, "base": d.get("base"),
              "made": time.strftime("%Y-%m-%d %H:%M:%S"),
              "snapshots": {s: {"content_hash": m["content_hash"], "os": m.get("os"),
                                "machines": m.get("machines", [m["arch"]] if m.get("arch") else ["68k"])}
                            for s, m in mans.items()},
              "launch": launch,
              "aliases": getattr(b, "ids_paths", []) + [label + a.decode("mac_roman") for a, _, _ in b.ids],     # rewritten; test_image.py resolves them
              "warnings": b.warnings}
    record["md5"] = bake.file_md5(out)
    write_record(d, record)
    log(f"{out}: {len(b.placed)} files from {len(mans)} snapshots, {os.path.getsize(out) >> 20} MB")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    names = all_defs() if args.all or args.list else args.names
    if not names:
        ap.error("name an image, or --all")
    for n in names:
        d = load_def(n)
        if args.list:
            missing = [s for s in snapshots_of(d) if not os.path.exists(bake.snapshot_path(s))]
            state = (f"missing snapshots {', '.join(missing)}" if missing else
                     "up to date" if up_to_date(d, image_key(d, manifests(d))) else "to build")
            print(f"{n:16} {len(snapshots_of(d)):3} snapshots  {state}")
            continue
        build(d, args.force)


if __name__ == "__main__":
    main()
