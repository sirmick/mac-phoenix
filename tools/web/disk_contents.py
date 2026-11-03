#!/usr/bin/env python3
"""disk_contents.py — what is on a library disk, for its record's "items".

    disk_contents.py IMAGE [--systems 7.6.1,8.6] [--tags games,68k]

Reads an HFS or HFS+ image (read-only, tools/diskaudit/disk_audit.py's
reader) and lists its top-level items. A top-level folder that only groups
others ("Languages ƒ", "Tools ƒ": no application of its own, folders named
with ƒ inside) is opened: its folders are the items, "group" naming it.
Each item:

  name         the folder or file name
  kind         application, game, utility, documents, folder, or system
               (a System Folder: its version is the System file's)
  arch         68k, PowerPC or fat (its applications': 'CODE' is 68k, a
               'cfrg' lists its fragments' architectures)
  size         bytes, both forks, everything inside
  version      the main application's 'vers' 1 short version string
  publisher    \\ from the main application's 'vers' 1 long string
  year         /  ("©1997 Claris Corporation"), where it says
  apps         the applications inside: [{name, path, arch, version}]
  description  \\
  notes         } hand-written, from provisioning/library_items.json by
  ...          /  name (it may also set publisher, year, kind)
  systems      the Systems it is offered on (the disk's, or its launch
               entries')

The main application is the one the disk's launch list opens from the
item, or else the first one found, shallowest first.
"""

import argparse
import json
import os
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "diskaudit"))
sys.path.insert(0, str(HERE.parent.parent / "provisioning"))
import disk_audit  # noqa: E402
import fat_cfm  # noqa: E402

TABLE = HERE.parent.parent / "provisioning" / "library_items.json"
INVISIBLE = 0x4000
ROOT = 2
MAX_APPS = 8
APP_TYPES = {b"APPL", b"APPC", b"APPD"}
KIND_BY_TAG = {"games": "game", "utilities": "utility"}
HAND = ("description", "notes", "publisher", "year", "kind", "systems", "version")


def load_table(path=TABLE):
    with open(path) as f:
        return {k: v for k, v in json.load(f).items() if not k.startswith("//")}


def visible(vol, n):
    """Shown by the Finder, and named: CDs line their windows with files
    named in spaces and tabs."""
    return (not (n.flags or 0) & INVISIBLE and n.name not in disk_audit.FINDER_INVISIBLE
            and n.name.strip(" \t\r\n\xa0") != "")


def arch_of(res):
    """68k, PowerPC or fat for an application's resources (None: no code)."""
    archs = set()
    for typ, rid, _, _, data in res:
        if typ == b"cfrg" and rid == 0 and len(data) >= 32:
            try:
                members = fat_cfm.members(data)
            except struct.error:            # cut short (a damaged or odd 'cfrg')
                members = []
            for m in members:
                archs.add({b"pwpc": "PowerPC", b"m68k": "68k"}.get(bytes(m[0:4])))
        elif typ == b"CODE":
            archs.add("68k")
    archs.discard(None)
    if archs == {"68k", "PowerPC"}:
        return "fat"
    return archs.pop() if archs else None


def merge_arch(archs):
    """An item's arch from its applications': one kind, or "68k, PowerPC"
    when they differ (each runs on one side or both)."""
    archs = {a for a in archs if a}
    if len(archs) == 1:
        return archs.pop()
    sides = {s for a in archs for s in (("68k", "PowerPC") if a == "fat" else (a,))}
    return ", ".join(sorted(sides)) if sides else None


def parse_vers(data):
    """(short version, long string) of a 'vers' resource."""
    if len(data) < 7:
        return None, None
    n = data[6]
    short = data[7:7 + n].decode("mac_roman", "replace").strip()
    m = re.search(r"\d.*", short)              # "Vers 2.5", "Speedometer 4.0", "v1.0"
    short = m.group(0) if m else short
    rest = data[7 + n:]
    long_ = rest[1:1 + rest[0]].decode("mac_roman", "replace").strip() if rest else ""
    return short or None, long_ or None


COPYRIGHT = re.compile(r"(?:©|\(c\)|copyright)\s*(\d{4}(?:\s*[-–,]\s*(?:\d{4}|\d{2})\b)*)"
                       r"\s*,?\s*(?:by\s+)?([^,;.\r\n]*)", re.IGNORECASE)

NAME_FIRST = re.compile(r"(?:©|\(c\)|copyright)\s*([A-Za-z][^©\d\r\n]*?)[,.\s]+"
                        r"((?:19|20)\d\d(?:\s*[-–]\s*(?:\d{4}|\d{2})\b)?)", re.IGNORECASE)


def publisher_year(long_):
    """(publisher, year) from a 'vers' long string, where it says them."""
    if not long_:
        return None, None
    m = COPYRIGHT.search(long_)
    if not m:
        m = NAME_FIRST.search(long_)            # "© Apple Computer, Inc. 1997"
        if not m:
            return None, None
        return _company(m.group(1)), _last_year(m.group(2))
    year = _last_year(m.group(1))
    return _company(m.group(2)), year if 1984 <= year <= 2010 else None


def _last_year(s):
    """The last year of a range ("1993-97", "1990-1999")."""
    years = re.findall(r"\d+", s)
    return int(years[-1]) if len(years[-1]) == 4 else int(years[0][:2] + years[-1])


def _company(s):
    s = re.split(r"<| / ", s)[0]                # an e-mail address, "/ ©1994 Interplay"
    pub = re.sub(r"[,\s]+(Inc|Incorporated|Corp|Corporation|Ltd|Co)\.?$", "", s.strip()).strip(" ,") or None
    return None if pub and (pub[0].isdigit() or len(pub) < 3) else pub


class Contents:
    def __init__(self, image):
        self.vol = disk_audit.Volume(str(image))
        self.kids = {}
        for n in self.vol.nodes.values():
            if n.id != n.parent:
                self.kids.setdefault(n.parent, []).append(n)

    def children(self, did):
        return sorted(self.kids.get(did, []), key=lambda n: n.name.lower())

    def walk(self, node, depth=0):
        yield node, depth
        if node.is_dir:
            for c in self.children(node.id):
                yield from self.walk(c, depth + 1)

    def size(self, node):
        return sum(n.size or 0 for n, _ in self.walk(node) if not n.is_dir)

    def app(self, node, path):
        try:
            res = fat_cfm.read_rsrc(self.vol.resource_fork(node))
        except Exception:
            res = []
        a = {"name": node.name, "path": path, "arch": arch_of(res)}
        vers = next((d for t, i, _, _, d in res if t == b"vers" and i == 1), None)
        if vers:
            a["version"], long_ = parse_vers(vers)
            a["publisher"], a["year"] = publisher_year(long_)
        return {k: v for k, v in a.items() if v is not None}

    def apps(self, node):
        found = []
        for n, depth in self.walk(node):
            if not n.is_dir and n.type in APP_TYPES and visible(self.vol, n):
                found.append((depth, self.vol.path_of(n), n))
        found.sort(key=lambda t: (t[0], t[1].lower()))
        return [self.app(n, path) for _, path, n in found]

    def entries(self):
        """Top-level items, groups opened: (node, group name or None)."""
        out = []
        for n in self.children(ROOT):
            if not visible(self.vol, n):
                continue
            kids = [c for c in self.children(n.id) if visible(self.vol, c)] if n.is_dir else []
            sub = [c for c in kids if c.is_dir and c.name.endswith("ƒ")]
            if len(sub) >= 2 and not any(c.type in APP_TYPES for c in kids if not c.is_dir):
                out += [(c, n.name) for c in kids]
            else:
                out.append((n, None))
        return out


def _main_app(item, apps):
    """The application the item is named after (most words in common), else
    the shallowest."""
    words = set(re.findall(r"[a-z]{3,}", item.lower()))
    version = re.search(r"\d+(?:\.\d+)+\w*", item)

    def score(a):
        common = len(words & set(re.findall(r"[a-z]{3,}", a["name"].lower())))
        same = bool(version) and (version.group(0) in a["name"] or version.group(0) == a.get("version"))
        return (common > 0, same, common)
    return max(apps, key=score, default=None)


def items(image, systems=None, tags=(), launch=(), table=None, cd=False):
    """The record's "items" for a disk image (see the module's doc)."""
    table = load_table() if table is None else table
    c = Contents(image)
    out = []
    for node, group in c.entries():
        path = c.vol.path_of(node)
        rel = path.split(":", 1)[1] if ":" in path else node.name
        it = {"name": node.name}
        if group:
            it["group"] = group
        apps = c.apps(node) if node.is_dir else ([c.app(node, path)] if node.type in APP_TYPES else [])
        if node.is_dir:
            files = [n for n, _ in c.walk(node) if not n.is_dir]
            kind = "application" if apps else "documents" if files else "folder"
        else:
            kind = "application" if apps else "documents"
        if cd and node.is_dir:
            kind = "folder"                     # a CD's folders: what is in them is the note's
            apps = []
        if kind == "application" and not cd:
            kind = next((KIND_BY_TAG[t] for t in tags if t in KIND_BY_TAG), kind)
        system = None
        if node.is_dir and node.name == "System Folder":
            kind = "system"
            system = next((c.app(n, c.vol.path_of(n)) for n in c.children(node.id)
                           if not n.is_dir and n.type == b"zsys"), None)
        it["kind"] = kind
        arch = merge_arch(a.get("arch") for a in apps)
        if arch:
            it["arch"] = arch
        it["size"] = c.size(node)
        # The main application: the launch list's from this item, else the first
        mine = [l for l in launch if l["app"] == rel or l["app"].startswith(rel + ":")]
        main = None
        if mine:
            want = c.vol.name + ":" + mine[0]["app"]
            main = next((a for a in apps if a["path"] == want), None)
        main = system or main or _main_app(node.name, apps)
        if main:
            for k in ("version", "publisher", "year"):
                if k in main:
                    it[k] = main[k]
        if node.is_dir and apps and not system:
            base = path + ":"
            it["apps"] = [{k: (v[len(base):] if k == "path" else v) for k, v in a.items()
                           if k in ("name", "path", "arch", "version")} for a in apps[:MAX_APPS]]
            if len(apps) > MAX_APPS:
                it["more_apps"] = len(apps) - MAX_APPS
        if mine:
            it["systems"] = sorted({s for l in mine for s in l.get("systems", systems or [])})
        elif systems:
            it["systems"] = list(systems)
        for k, v in table.get(node.name, {}).items():
            if k in HAND:
                if v is None:
                    it.pop(k, None)             # "version": null: what the disk says is wrong
                else:
                    it[k] = v
        out.append(it)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image")
    ap.add_argument("--systems", default="")
    ap.add_argument("--tags", default="")
    ap.add_argument("--cd", action="store_true")
    args = ap.parse_args()
    r = items(args.image, [s for s in args.systems.split(",") if s], [t for t in args.tags.split(",") if t],
              cd=args.cd)
    print(json.dumps(r, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    main()
