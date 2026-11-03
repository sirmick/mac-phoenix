#!/usr/bin/env python3
"""test_system_additions.py -- a library disk's System Folder additions
(tools/web/make_disk.py --system-from): two snapshots' System Folder parts
laid out in an invisible "System Folder Additions", the later one's file
winning, the System's own files and a boot cut's drops left out, the record
saying what is there and what takes a restart; make_library.py's mapping of
a definition's "place" entries to the folders they put at the top. hfsutils
only, no emulator."""
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "web"))
import make_library  # noqa: E402

failures = []


def expect(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


work = tempfile.mkdtemp(prefix="test-system-additions-")
env = dict(os.environ, HOME=work)


def hfs(*args, check=True):
    r = subprocess.run(list(args), env=env, capture_output=True)
    if check and r.returncode:
        sys.exit(f"SKIP: {args[0]}: {r.stderr.decode(errors='replace').strip()}")
    return r.stdout


def image(name, label, folders, files):
    """An HFS image with these folders and text files {path: text}."""
    path = os.path.join(work, name)
    with open(path, "wb") as f:
        f.truncate(2 << 20)
    hfs("hformat", "-l", label, path)
    hfs("hmount", path)
    for d in folders:
        hfs("hmkdir", d)
    for p, text in files.items():
        local = os.path.join(work, "file.txt")
        with open(local, "w") as f:
            f.write(text)
        hfs("hcopy", "-t", local, p)
    hfs("humount")
    return path


try:
    subprocess.run(["hformat"], capture_output=True)
except FileNotFoundError:
    print("SKIP: no hfsutils")
    sys.exit(77)

src = image("apps.img", "Apps", [":Thing Folder"], {":Thing Folder:Thing": "the app"})
snap1 = image("snap1.img", "snap1", [":Items", ":System Folder", ":System Folder:Preferences",
                                     ":System Folder:Fonts", ":System Folder:Help"],
              {":System Folder:Preferences:Thing Prefs": "first",
               ":System Folder:Preferences:Only First": "first",
               ":System Folder:Fonts:Thing Font": "a font",
               ":System Folder:Help:Thing Help": "left out (a boot cut's)",
               ":System Folder:Clipboard": "left out (the System's own)"})
snap2 = image("snap2.img", "snap2", [":Items", ":System Folder", ":System Folder:Preferences"],
              {":System Folder:Preferences:Thing Prefs": "second",
               ":System Folder:System": "left out"})
out = os.path.join(work, "disk.img")
rec = os.path.join(work, "additions.json")
subprocess.run([sys.executable, os.path.join(ROOT, "tools", "web", "make_disk.py"), out, "--label", "Apps",
                "--take", src, "Thing", "--system-from", snap1, snap2, "--additions-record", rec],
               check=True, env=env)

hfs("hmount", out)
top = hfs("hls", "-1a", ":").decode("mac_roman").split("\n")
expect("System Folder Additions" in top and "Thing Folder" in top, "the disk has the app's folder and the additions")
expect("System Folder Additions" not in hfs("hls", "-1", ":").decode("mac_roman").split("\n"),
       "the additions folder is invisible")
prefs = hfs("hls", "-1a", ":System Folder Additions:Preferences").decode("mac_roman").split("\n")
expect({"Thing Prefs", "Only First"} <= set(prefs), "both snapshots' preferences")
local = os.path.join(work, "out.txt")
hfs("hcopy", "-t", ":System Folder Additions:Preferences:Thing Prefs", local)
with open(local) as f:
    expect(f.read().strip() == "second", "the later snapshot's file wins")
rest = [n for n in hfs("hls", "-1a", ":System Folder Additions").decode("mac_roman").split("\n") if n]
expect(sorted(rest) == ["Fonts", "Preferences"], f"the System's own files and a boot cut's drops left out ({rest})")
hfs("humount")

with open(rec) as f:
    r = json.load(f)
expect(r["folder"] == "System Folder Additions" and r["files"] == 3, f"the record counts 3 files ({r['files']})")
expect(sorted(r["items"]) == ["Fonts:Thing Font", "Preferences:Only First", "Preferences:Thing Prefs"],
       "and lists them")
expect(r["restart"] == ["Fonts"], "the fonts take a restart")

baked = work
expect(make_library.placed_at({"snapshot": "x", "take": "A:B", "as": "B ƒ"}, baked) == ["B ƒ"], "place: as")
expect(make_library.placed_at({"snapshot": "x", "take": "A:B"}, baked) == ["B"], "place: take's last part")
expect(make_library.placed_at({"snapshot": "x", "take": "B", "to": "Tools ƒ:Sub"}, baked) == ["Tools ƒ"], "place: to")

shutil.rmtree(work, ignore_errors=True)
if failures:
    print(f"{len(failures)} failed")
    sys.exit(1)
print("all passed")
