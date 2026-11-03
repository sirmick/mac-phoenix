#!/usr/bin/env python3
"""test_disk_contents.py — tools/web/disk_contents.py (a library disk's
"items", tools/web/make_library.py) on a fixture HFS image it makes.

The fixture (hfsutils, as test_disk_audit.py makes its own): a game folder
with its application (CODE, 'vers' 1 with a copyright range) and a Read Me;
a group folder ("Tools ƒ") of two ƒ folders, one fat application (CODE and
a 'cfrg' with a PowerPC fragment) and one PowerPC-only; a document; a
System Folder with its System file; and what the list leaves out: a file
named in spaces (CDs line windows with them) and an invisible one. Checks
kind, arch, version, publisher and year, sizes, the group opened, the
launch list's systems, and a hand table's fields (null dropping one).
Skips (77) without hfsutils.
"""

import os
import shutil
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "web"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disk_contents  # noqa: E402
import fat_cfm  # noqa: E402
from test_disk_audit import Hfs, macbinary  # noqa: E402

FAILS = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def vers(short, long_):
    s, l = short.encode("mac_roman"), long_.encode("mac_roman")
    return bytes([1, 0x20, 0x80, 0, 0, 0, len(s)]) + s + bytes([len(l)]) + l


def cfrg(*archs):
    members = b""
    for a in archs:
        m = bytearray(44)
        m[0:4] = a
        struct.pack_into(">H", m, 40, len(m))
        members += bytes(m)
    head = bytearray(32)
    struct.pack_into(">H", head, 30, len(archs))
    return bytes(head) + members


def app(name, resources, data=b""):
    rsrc = fat_cfm.write_rsrc([(t, i, 0, None, d) for t, i, d in resources])
    return macbinary(name, b"APPL", b"TEST", data=data, rsrc=rsrc), len(data) + len(rsrc)


def main():
    if not shutil.which("hformat"):
        print("skip: no hfsutils")
        return 77
    work = tempfile.mkdtemp(prefix="disk-contents-")
    try:
        image = os.path.join(work, "lib.img")
        with open(image, "wb") as f:
            f.truncate(2 * 1024 * 1024)
        h = Hfs(image, work)
        h.run("hformat", "-l", "Library", image)
        sizes = {}

        def put(folder, mb, size=None):
            p = os.path.join(work, "f.bin")
            with open(p, "wb") as f:
                f.write(mb)
            h.run("hcopy", "-m", p, (":" + folder).encode("mac_roman"))
            return size

        with h:
            for d in ("Game 1.2 ƒ", "Tools ƒ", "Tools ƒ:Alpha ƒ", "Tools ƒ:Beta ƒ", "System Folder"):
                h.run("hmkdir", (":" + d).encode("mac_roman"))     # names as a Mac has them
            mb, sizes["game"] = app("Game 1.2", [(b"CODE", 0, b"\0" * 300), (b"CODE", 1, b"\x4e\x75"),
                                                 (b"vers", 1, vers("1.2", "1.2, ©1993-97 Example Soft, Inc."))])
            put("Game 1.2 ƒ:", mb)
            readme = macbinary("Read Me", b"TEXT", b"ttxt", data=b"x" * 1000)
            put("Game 1.2 ƒ:", readme)
            mb, _ = app("Alpha", [(b"CODE", 0, b"\0" * 64), (b"cfrg", 0, cfrg(b"pwpc")),
                                  (b"vers", 1, vers("Vers 2.0", "© Alpha Corp. 1996"))], data=b"\0" * 512)
            put("Tools ƒ:Alpha ƒ:", mb)
            mb, _ = app("Beta", [(b"cfrg", 0, cfrg(b"pwpc"))], data=b"\0" * 256)
            put("Tools ƒ:Beta ƒ:", mb)
            put("", macbinary("Notes", b"TEXT", b"ttxt", data=b"y" * 10))
            put("", macbinary(" \t ", b"TEXT", b"ttxt"))
            put("", macbinary("Hidden", b"TEXT", b"ttxt", flags=0x4000))
            system = fat_cfm.write_rsrc([(b"vers", 1, 0, None, vers("7.6.1", "7.6.1, © Apple Computer, Inc. 1983-1997"))])
            put("System Folder:", macbinary("System", b"zsys", b"MACS", rsrc=system))

        table = {"Game 1.2 ƒ": {"description": "A game.", "notes": "Shareware."},
                 "Alpha ƒ": {"year": 1999, "version": None}}
        launch = [{"app": "Tools ƒ:Beta ƒ:Beta", "name": "Beta", "systems": ["8.6"]}]
        items = disk_contents.items(image, ["7.6.1", "8.6"], ["games"], launch, table)
        by = {it["name"]: it for it in items}

        check([it["name"] for it in items] == ["Game 1.2 ƒ", "Notes", "System Folder", "Alpha ƒ", "Beta ƒ"],
              f"items, spacer and invisible left out, the group opened: {[it['name'] for it in items]}")
        g = by["Game 1.2 ƒ"]
        check(g["kind"] == "game", f"an application on a games disk is a game ({g['kind']})")
        check(g.get("arch") == "68k", f"CODE is 68k ({g.get('arch')})")
        check(g.get("version") == "1.2", f"version from 'vers' 1 ({g.get('version')})")
        check(g.get("publisher") == "Example Soft" and g.get("year") == 1997,
              f"publisher and the range's last year ({g.get('publisher')}, {g.get('year')})")
        check(g["size"] == sizes["game"] + 1000, f"size: both forks of everything inside ({g['size']})")
        check([a["name"] for a in g.get("apps", [])] == ["Game 1.2"], f"apps ({g.get('apps')})")
        check(g.get("description") == "A game." and g.get("notes") == "Shareware.", "the table's description, notes")
        check(g.get("systems") == ["7.6.1", "8.6"], f"the disk's systems ({g.get('systems')})")
        a = by["Alpha ƒ"]
        check(a.get("group") == "Tools ƒ", f"group ({a.get('group')})")
        check(a.get("arch") == "fat", f"CODE and a PowerPC fragment: fat ({a.get('arch')})")
        check("version" not in a and a.get("year") == 1999 and a.get("publisher") == "Alpha",
              f"the table's null drops the version, its year wins; name-first copyright ({a})")
        b = by["Beta ƒ"]
        check(b.get("arch") == "PowerPC", f"a PowerPC fragment only ({b.get('arch')})")
        check(b.get("systems") == ["8.6"], f"the launch list's systems ({b.get('systems')})")
        n = by["Notes"]
        check(n["kind"] == "documents" and n["size"] == 10 and "arch" not in n, f"a document ({n})")
        s = by["System Folder"]
        check(s["kind"] == "system" and s.get("version") == "7.6.1" and s.get("year") == 1997
              and "apps" not in s, f"a System Folder: the System file's version ({s})")

        cd = disk_contents.items(image, tags=["cd"], table={}, cd=True)
        cg = next(it for it in cd if it["name"] == "Game 1.2 ƒ")
        check(cg["kind"] == "folder" and "apps" not in cg and "version" not in cg,
              f"on a CD a folder is a folder, with its size ({cg})")
        check(disk_contents.publisher_year("Copyright 1998 UserLand Software, Inc.") == ("UserLand Software", 1998),
              "Copyright YEAR NAME")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(f"{len(FAILS)} failed" if FAILS else "all passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
