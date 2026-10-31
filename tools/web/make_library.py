#!/usr/bin/env python3
"""The site's library: boot disks and application disks for Executor's tab.

    tools/web/make_library.py [OUT_DIR] [--images DIR] [--only NAME...] [--force]

Makes every disk in DISKS below from the provisioned images (provisioning/
build.py's, in ~/storage/images) into OUT_DIR (default ~/storage/web-library),
each with its record, NAME.json: the title, tags and note tools/web/server's
library shows, and "launch", the applications on it, which the site's cells
(web/tests/tab_site.mjs) open on each of their systems. A disk is made again
only when an image it comes from is newer, or with --force.

The boot disks are make_boot_disk.py's cuts of 7.6.1.img and 8.6.img; the
application disks make_disk.py's copies of whole folders of the provisioned
disks, so an application has its folder as it was installed. Point the
server's [library] images at OUT_DIR (docs/executor/WebSite.md).
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# Each application: its path on the disk made here, its path on the source
# image (as docs/executor/SCOREBOARD.md's App column has it, which says
# whether it runs yet), the systems the site's cells open it on, and the
# application name Executor reports when it is in front (CurApName).
# "cell": false leaves an application out of the cells (games: a few of
# each disk's are opened, the rest only offered).
SYSTEMS = {"7.6.1": "boot-7.6.1.img", "8.6": "boot-8.6.img"}

DISKS = [
    {"name": "boot-7.6.1.img", "boot": "7.6.1.img", "label": "Macintosh HD",
     "title": "Mac OS 7.6.1",
     "tags": ["system", "68k"],
     "note": "Apple's System 7.6.1, cut for the tab (tools/web/make_boot_disk.py): the System Folder "
             "with what Executor runs, SimpleText. 68k code; fat applications run their 68k side."},
    {"name": "boot-8.6.img", "boot": "8.6.img", "label": "Macintosh HD",
     "keep": ["Applications:SimpleText"],
     "title": "Mac OS 8.6",
     "tags": ["system", "ppc"],
     "note": "Apple's Mac OS 8.6, cut for the tab: the System Folder with what Executor runs, "
             "SimpleText. Tick PowerPC for fat and PowerPC-only applications."},
    {"name": "productivity.img", "label": "Productivity",
     "title": "Productivity",
     "tags": ["productivity", "68k", "ppc"],
     "note": "AppleWorks 5.0.3 (word processing, drawing, painting, spreadsheet, database) and "
             "ClarisWorks 4.0v4. For Mac OS 7.6.1 and 8.6.",
     "take": [("productivity-system-7.img", ["AppleWorks 5.0.3 ƒ", "ClarisWorks 4.0v4 ƒ"])],
     "launch": [
         {"app": "AppleWorks 5.0.3 ƒ:AppleWorks", "name": "AppleWorks"},
         {"app": "ClarisWorks 4.0v4 ƒ:ClarisWorks", "name": "ClarisWorks"},
     ]},
    {"name": "development.img", "label": "Development",
     "title": "Development",
     "tags": ["development", "68k", "ppc"],
     "note": "MPW (Shell, SC, Rez, Interfaces&Libraries), CodeWarrior Pro 4 (IDE 3.2), THINK C 6.0, "
             "THINK Pascal 4.5, HyperCard 2.4.1, MacPerl 5.2, ResEdit 2.1.3, BBEdit Lite 4.1. "
             "For Mac OS 7.6.1 and 8.6.",
     "take": [("dev-tools-system-7.img", ["MPW", "Interfaces&Libraries", "CodeWarrior Pro 4 ƒ",
                                          "THINK C 6.0 ƒ", "Languages ƒ", "Tools ƒ"])],
     "launch": [
         {"app": "MPW:MPW Shell", "name": "MPW Shell"},
         {"app": "CodeWarrior Pro 4 ƒ:Metrowerks CodeWarrior:CodeWarrior IDE 3.2", "name": "CodeWarrior IDE 3.2"},
         {"app": "THINK C 6.0 ƒ:THINK C:THINK Project Manager", "name": "THINK Project Manager"},
         {"app": "Languages ƒ:THINK Pascal 4.5d4 ƒ:Think Pascal:THINK Pascal 4.5d4:THINK Pascal 4.5",
          "name": "THINK Pascal 4.5"},
         {"app": "Languages ƒ:HyperCard 2.4.1 ƒ:HyperCard", "name": "HyperCard"},
         {"app": "Languages ƒ:MacPerl 5.2.0r4 ƒ:MacPerl", "name": "MacPerl"},
         {"app": "Tools ƒ:ResEdit 2.1.3 ƒ:ResEdit 2.1.3:ResEdit", "name": "ResEdit"},
         {"app": "Tools ƒ:BBEdit Lite 4.1 ƒ:BBEdit Lite 4.1", "name": "BBEdit Lite 4.1"},
     ]},
    {"name": "games-68k.img", "label": "Games",
     "title": "Games (68k)",
     "tags": ["games", "68k"],
     "note": "Games that run on Executor's 68k side: A-Train, Apeiron, Avara, Bolo, Civilization, "
             "Exile I and II, Lemmings, Maelstrom, Master of Orion, Pathways into Darkness, Populous II, "
             "Prince of Persia, SimEarth, SimLife, Spectre, Swoop, The Secret of Monkey Island. "
             "Mac OS 7.6.1 (8.6 too).",
     "systems": ["7.6.1"],
     "take": [("games.img", ["A-Train ƒ", "Apeiron 1.0.2 ƒ", "Avara 1.0.2 ƒ", "Bolo 0.99.7 ƒ",
                             "Civilization 1.0.6 ƒ", "Exile 2.0.1 ƒ", "Exile II 2.0.3 ƒ",
                             "Lemmings 1.5.2 ƒ", "Maelstrom 1.4.2 ƒ", "Master of Orion ƒ",
                             "Pathways into Darkness 1.1 ƒ", "Populous II ƒ", "Prince of Persia ƒ",
                             "SimEarth 2.0 ƒ", "SimLife 1.0.1 ƒ", "Spectre 1.1 ƒ", "Swoop 1.0.2 ƒ",
                             "The Secret of Monkey Island ƒ"])],
     "launch": [
         {"app": "Lemmings 1.5.2 ƒ:Lemmings", "name": "Lemmings"},
         {"app": "Maelstrom 1.4.2 ƒ:Maelstrom", "name": "Maelstrom"},
         {"app": "SimEarth 2.0 ƒ:SimEarth Color", "name": "SimEarth Color"},
         {"app": "Prince of Persia ƒ:Prince of Persia", "name": "Prince of Persia"},
         {"app": "The Secret of Monkey Island ƒ:Monkey Island", "name": "Monkey Island"},
         {"app": "Master of Orion ƒ:Master of Orion", "name": "Master of Orion"},
     ]},
    {"name": "games-ppc.img", "label": "Games PPC",
     "title": "Games (PowerPC)",
     "tags": ["games", "ppc"],
     "note": "Fat and PowerPC games, on Mac OS 8.6 with PowerPC ticked: Marathon, Marathon 2, "
             "Escape Velocity, EV Override, EV Nova, Bubble Trouble, Barrack, Chiral, Flashback, "
             "SimCity 2000, Wolfenstein 3D, Ferazel's Wand, Slithereens, Mars Rising.",
     "systems": ["8.6"],
     "take": [("games.img", ["Marathon 1.2 ƒ", "Marathon 2 ƒ", "Escape Velocity 1.0.5 ƒ",
                             "EV Override 1.0.2 ƒ", "Bubble Trouble 1.0.1 ƒ", "Barrack 1.0.4 ƒ",
                             "Chiral 1.0.4 ƒ", "Flashback 1.0 ƒ", "SimCity 2000® Collection ƒ",
                             "Wolfenstein 3D 1.0.2 ƒ"]),
              ("games-ppc.img", ["EV Nova 1.0.10 ƒ", "Ferazel's Wand 1.0.3 ƒ", "Slithereens 1.0.1 ƒ",
                                 "Mars Rising 1.0.2 ƒ"])],
     "launch": [
         {"app": "Marathon 2 ƒ:Marathon 2", "name": "Marathon 2"},
         {"app": "Escape Velocity 1.0.5 ƒ:Escape Velocity", "name": "Escape Velocity"},
         {"app": "Bubble Trouble 1.0.1 ƒ:Bubble Trouble", "name": "Bubble Trouble"},
         {"app": "EV Nova 1.0.10 ƒ:EV Nova", "name": "EV Nova", "source": "games-ppc.img"},
         {"app": "Ferazel's Wand 1.0.3 ƒ:Ferazel's Wand", "name": "Ferazel's Wand", "source": "games-ppc.img"},
         {"app": "Slithereens 1.0.1 ƒ:Slithereens", "name": "Slithereens", "source": "games-ppc.img"},
     ]},
]


def sources(d, images):
    if "boot" in d:
        return [images / d["boot"]]
    return [images / img for img, _ in d["take"]]


def make(d, images, out):
    img = out / d["name"]
    tmp = out / (d["name"] + ".part")
    if "boot" in d:
        cmd = [sys.executable, str(HERE / "make_boot_disk.py"), str(images / d["boot"]), str(tmp),
               "--label", d["label"]]
        for k in d.get("keep", []):
            cmd += ["--keep", k]
    else:
        cmd = [sys.executable, str(HERE / "make_disk.py"), str(tmp), "--label", d["label"]]
        for src, folders in d["take"]:
            cmd += ["--take", str(images / src), *folders]
    subprocess.run(cmd, check=True)
    os.replace(tmp, img)


def record(d):
    r = {"title": d["title"], "tags": d["tags"], "note": d["note"],
         "label": d["label"], "made_by": "tools/web/make_library.py"}
    if "boot" in d:
        r["system"] = next(s for s, n in SYSTEMS.items() if n == d["name"])
    else:
        r["sources"] = [img for img, _ in d["take"]]
        systems = d.get("systems", list(SYSTEMS))
        r["launch"] = [{"app": f"{d['label']}:{a['app']}", "name": a["name"],
                        "source": a.get("source", d["take"][0][0]), "source_app": a["app"],
                        "systems": a.get("systems", systems)} for a in d["launch"]]
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out", nargs="?", default="~/storage/web-library")
    ap.add_argument("--images", default="~/storage/images")
    ap.add_argument("--only", nargs="+", default=[])
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    out = Path(os.path.expanduser(args.out))
    images = Path(os.path.expanduser(args.images))
    out.mkdir(parents=True, exist_ok=True)

    for d in DISKS:
        if args.only and d["name"] not in args.only:
            continue
        img = out / d["name"]
        srcs = sources(d, images)
        missing = [s for s in srcs if not s.exists()]
        if missing:
            sys.exit(f"{d['name']}: no {', '.join(map(str, missing))} (provisioning/build.py makes it)")
        rec = out / (d["name"] + ".json")
        new = json.dumps(record(d), indent=1, ensure_ascii=False) + "\n"
        stale = (args.force or not img.exists()
                 or any(s.stat().st_mtime > img.stat().st_mtime for s in srcs))
        if stale:
            make(d, images, out)
        else:
            print(f"{img}: up to date")
        if not rec.exists() or rec.read_text() != new:
            rec.write_text(new)


if __name__ == "__main__":
    main()
