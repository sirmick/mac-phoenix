#!/usr/bin/env python3
"""test_bake_exclude.py -- a bake's "exclude" paths ("Folder:Name": a serial
number file an archive came with) are left out of the Installers disk's
product folder (installers.bake_items) and match only that path and what is
under it (bake.excluded_path, which the capture uses too). office-98's
recipe leaves its archive's "Serial number" file out. No emulator."""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
import bake  # noqa: E402
import installers  # noqa: E402

failures = []


def expect(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


EX = ["Microsoft Office 98:Serial number", "Setup Archive.sea"]
expect(bake.excluded_path(b"Microsoft Office 98:Serial number", EX), "the path itself")
expect(bake.excluded_path(b"Microsoft Office 98:Serial number:inner", EX), "under it")
expect(not bake.excluded_path(b"Microsoft Office 98:Serial number 2", EX), "not a longer name")
expect(not bake.excluded_path(b"Microsoft Office 98", EX), "not its folder")
expect(not bake.excluded_path(b"Microsoft Office 98:Microsoft Word", EX), "not a sibling")
expect(not bake.excluded_path(b"Setup Archive.sea", EX), "top-level names: the capture's alone")
expect(bake.excluded_path("Foléder:Né".encode("mac_roman"), ["Foléder:Né"]), "MacRoman names")

# installers.bake_items with the media stood in for: the excluded file is gone,
# everything else kept, in order
items = [(b"Microsoft Office 98", None), (b"Microsoft Office 98:Microsoft Word", "w"),
         (b"Microsoft Office 98:Serial number", "s"), (b"Microsoft Office 98:Office", None)]
real = bake.media_items
bake.media_items = lambda m, work: list(items)
try:
    got = installers.bake_items({"media": [{"file": "x.sit"}], "exclude": EX}, "/nonexistent")
finally:
    bake.media_items = real
expect([r for r, _ in got] == [r for r, _ in items if r != b"Microsoft Office 98:Serial number"],
       "Installers disk: the serial number file left out, the rest kept")

expect("Microsoft Office 98:Serial number" in bake.load_bake("office-98").get("exclude", []),
       "office-98 excludes its archive's Serial number file")

print(f"{len(failures)} failure(s)" if failures else "all passed")
sys.exit(1 if failures else 0)
