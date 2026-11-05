#!/usr/bin/env python3
"""The site's library: boot disks and application disks for Executor's tab.

    tools/web/make_library.py [OUT_DIR] [--images DIR] [--only NAME...] [--force]

Makes every disk in DISKS below from the provisioned images (provisioning/
build.py's, in ~/storage/images) into OUT_DIR (default ~/storage/web-library),
each with its record, NAME.json: the title, tags and note tools/standalone's
library shows, "items", what is on the disk (disk_contents.py: name, kind,
version, publisher, year, architecture, size, applications, from the disk
itself; a description and notes from provisioning/library_items.json), and
"launch", the applications on it, which the site's cells
(web/tests/tab_site.mjs) open on each of their systems. A disk is made again
only when an image it comes from is newer, or with --force.

The boot disks are make_boot_disk.py's cuts of 7.6.1.img and 8.6.img; the
application disks make_disk.py's copies of whole folders of the provisioned
disks, so an application has its folder as it was installed. Point the
server's [library] images at OUT_DIR (docs/executor/WebSite.md).

What the applications' installers put in the System Folder (preferences,
registration, shared libraries, fonts) goes on their disk too, in its
"System Folder Additions" (make_disk.py --system-from): the System Folder
parts of the snapshots placed in the folders a disk takes (the source
image's definition, provisioning/images/NAME.json, says which went where),
in the definition's order. The tab copies them into its System when the
disk goes in; the record's "system_additions" lists them
(docs/executor/Disks.md). "additions": false on a disk leaves them out.

The CDs (provisioning/cds.json: magazine cover discs, pinned in media.json,
checked and given their records by provisioning/cds.py) go in as they are,
hard-linked, each with its cover scan cut down to COVER_PX for the site:
the record's "cover", and "items", their top-level folders and files.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFINITIONS = HERE.parent.parent / "provisioning" / "images"
sys.path.insert(0, str(HERE.parent / "diskaudit"))
import tidy  # noqa: E402  (disk_audit.py's checks put right)
sys.path.insert(0, str(HERE.parent / "macdecode"))
from rsrc import Disk  # noqa: E402
import disk_contents  # noqa: E402
CDS = HERE.parent.parent / "provisioning" / "cds.json"
COVER_PX = 480

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
     "note": "AppleWorks 5.0.3 (word processing, drawing, painting, spreadsheet, database), "
             "ClarisWorks 4.0v4, ClarisDraw 1.0v4, Corel WordPerfect 3.5e, Microsoft Excel 4.0, "
             "FileMaker Pro 4.0, Aldus Persuasion 3.0, Microsoft Works 4.0b: "
             "for Mac OS 7.6.1 and 8.6. Microsoft Office 98 (Word, Excel, PowerPoint; PowerPC: "
             "Mac OS 8.6 with PowerPC ticked) asks for its CD key at first launch.",
     "take": [("productivity-system-7.img", ["AppleWorks 5.0.3 ƒ", "ClarisWorks 4.0v4 ƒ", "ClarisDraw 1.0v4 ƒ",
                                              "Corel WordPerfect 3.5e ƒ", "Microsoft Excel 4.0 ƒ",
                                              "Microsoft Office 98 ƒ", "FileMaker Pro 4.0 ƒ",
                                              "Aldus Persuasion 3.0 ƒ", "Microsoft Works 4.0b ƒ"])],
     "launch": [
         {"app": "AppleWorks 5.0.3 ƒ:AppleWorks", "name": "AppleWorks"},
         {"app": "ClarisWorks 4.0v4 ƒ:ClarisWorks", "name": "ClarisWorks"},
         {"app": "ClarisDraw 1.0v4 ƒ:ClarisDraw 1.0v4", "name": "ClarisDraw 1.0v4"},
         {"app": "Corel WordPerfect 3.5e ƒ:Corel WordPerfect", "name": "Corel WordPerfect"},
         {"app": "Microsoft Excel 4.0 ƒ:Microsoft Excel", "name": "Microsoft Excel"},
         {"app": "Microsoft Office 98 ƒ:Microsoft Word", "name": "Microsoft Word", "systems": ["8.6"]},
         {"app": "FileMaker Pro 4.0 ƒ:FileMaker Pro", "name": "FileMaker Pro"},
         {"app": "Aldus Persuasion 3.0 ƒ:Aldus Persuasion 3.0", "name": "Aldus Persuasion 3.0"},
         {"app": "Microsoft Works 4.0b ƒ:Microsoft Works 4.0", "name": "Microsoft Works 4.0"},
     ]},
    {"name": "creative.img", "label": "Creative",
     "title": "Creative (68k)",
     "tags": ["creative", "68k"],
     "note": "Kid Pix 2.5 (painting, stamps, sounds), Adobe Photoshop 4.0, Adobe Illustrator 6.0, "
             "Macromedia FreeHand 5.5, Deneba Canvas 5.0.2. For Mac OS 7.6.1 and 8.6.",
     "take": [("creative-system-7.img", ["Kid Pix 2.5 ƒ", "Adobe Photoshop 4.0 ƒ", "Adobe Illustrator 6.0 ƒ",
                                          "Macromedia FreeHand 5.5 ƒ", "Deneba Canvas 5.0.2 ƒ"])],
     "launch": [
         {"app": "Kid Pix 2.5 ƒ:Kid Pix", "name": "Kid Pix"},
         {"app": "Adobe Photoshop 4.0 ƒ:Adobe Photoshop® 4.0", "name": "Adobe Photoshop® 4.0"},
         {"app": "Adobe Illustrator 6.0 ƒ:Adobe Illustrator® 6.0", "name": "Adobe Illustrator® 6.0"},
         {"app": "Macromedia FreeHand 5.5 ƒ:FreeHand 5.5", "name": "FreeHand 5.5"},
         {"app": "Deneba Canvas 5.0.2 ƒ:Canvas™ 5", "name": "Canvas™ 5"},
     ]},
    {"name": "creative-8.img", "label": "Creative 8",
     "title": "Creative 8 (PowerPC, Mac OS 8.6)",
     "tags": ["creative", "ppc"],
     "note": "Adobe Illustrator 8.0, Adobe Photoshop 5.5 (with ImageReady 2.0), Adobe PageMaker 6.5 "
             "(with Acrobat 3.0): PowerPC, for Mac OS 8.6.",
     "systems": ["8.6"],
     "take": [("creative-macos-8.img", ["Adobe Illustrator 8.0 ƒ", "Adobe Photoshop 5.5 ƒ",
                                        "Adobe PageMaker 6.5 ƒ", "Adobe Acrobat 3.0 ƒ"])],
     "launch": [
         {"app": "Adobe Illustrator 8.0 ƒ:Adobe Illustrator® 8.0", "name": "Adobe Illustrator® 8.0"},
         {"app": "Adobe Photoshop 5.5 ƒ:Adobe® Photoshop® 5.5", "name": "Adobe® Photoshop® 5.5"},
         {"app": "Adobe PageMaker 6.5 ƒ:Adobe® PageMaker® 6.5", "name": "Adobe® PageMaker® 6.5"},
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
    {"name": "tools-8.img", "label": "Tools",
     "title": "Tools (Mac OS 8)",
     "tags": ["development", "ppc"],
     "note": "BBEdit Lite 6.1.2, ResEdit 2.1.3, Resorcerer 2.0, Disk Copy 6.3.3, StuffIt Expander 6.0.1, "
             "ZTerm 1.0.1, MacsBug 6.6.3: the Mac OS 8/9 development disk's tools. For Mac OS 8.6.",
     "systems": ["8.6"],
     "take": [("dev-tools-macos-8-9.img", ["Tools ƒ"])],
     "launch": [
         {"app": "Tools ƒ:BBEdit Lite 6.1.2 ƒ:BBEdit Lite 6.1", "name": "BBEdit Lite 6.1"},
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
             "SimCity 2000, Wolfenstein 3D, Ferazel's Wand, Slithereens, Mars Rising; "
             "GameBox Classic, a library to launch them from.",
     "systems": ["8.6"],
     "take": [("games.img", ["Marathon 1.2 ƒ", "Marathon 2 ƒ", "Escape Velocity 1.0.5 ƒ",
                             "EV Override 1.0.2 ƒ", "Bubble Trouble 1.0.1 ƒ", "Barrack 1.0.4 ƒ",
                             "Chiral 1.0.4 ƒ", "Flashback 1.0 ƒ", "SimCity 2000® Collection ƒ",
                             "Wolfenstein 3D 1.0.2 ƒ"]),
              ("games-ppc.img", ["EV Nova 1.0.10 ƒ", "Ferazel's Wand 1.0.3 ƒ", "Slithereens 1.0.1 ƒ",
                                 "Mars Rising 1.0.2 ƒ", "GameBox Classic 1.2.0 ƒ"])],
     "launch": [
         {"app": "Marathon 2 ƒ:Marathon 2", "name": "Marathon 2"},
         {"app": "Escape Velocity 1.0.5 ƒ:Escape Velocity", "name": "Escape Velocity"},
         {"app": "Bubble Trouble 1.0.1 ƒ:Bubble Trouble", "name": "Bubble Trouble"},
         {"app": "EV Nova 1.0.10 ƒ:EV Nova", "name": "EV Nova", "source": "games-ppc.img"},
         {"app": "Ferazel's Wand 1.0.3 ƒ:Ferazel's Wand", "name": "Ferazel's Wand", "source": "games-ppc.img"},
         {"app": "Slithereens 1.0.1 ƒ:Slithereens", "name": "Slithereens", "source": "games-ppc.img"},
         {"app": "GameBox Classic 1.2.0 ƒ:GameBox", "name": "GameBox", "source": "games-ppc.img"},
     ]},
    {"name": "icons.img", "label": "Icons",
     "title": "Icons",
     "tags": ["icons", "68k", "ppc"],
     "note": "Custom icons to paste onto folders and files (Get Info, then Paste): The Iconfactory's "
             "classic collection, MacUser's Custom Icon Pack IV (Mac models) and Nitrozac's icons from "
             "MacAddict #63's CD, with their Read Mes. Mac OS 7.6.1 and 8.6 (the Iconfactory's are "
             "Mac OS 8.5's 'icns' icons: they show on 8.6, not on 7.6.1).",
     "take": [("icons.img", ["Iconfactory Classic Icons ƒ", "MacUser Custom Icon Pack IV ƒ",
                             "Nitrozac Icons (MacAddict 63) ƒ"])],
     "launch": []},
    {"name": "internet.img", "label": "Internet",
     "title": "Internet",
     "tags": ["internet", "ppc"],
     "note": "MR Browser 0.39 (Macintosh Repository's own client, the 68k build: signing in takes "
             "an MR account's PIN, from MY ACCOUNT on the website), "
             "Hotline Connect Client 1.9.1 (the classic PowerPC build: trackers, servers, chat, "
             "files; on Mac OS 8.6 with PowerPC ticked; it writes into its folder, so let the disk be "
             "changed) and Serial MReader 0.03 with the Serial Box 2021-08 database (68k and PowerPC; "
             "an archive only). MR Browser's PowerPC build and Hotline's Carbon one need CarbonLib: "
             "not here.",
     "systems": ["8.6"],
     "take": [("internet-macos-8-9.img", ["MR Browser 0.39 ƒ", "Hotline Connect Client 1.9.1 ƒ",
                                          "Serial MReader 0.03 ƒ"])],
     "launch": [
         {"app": "MR Browser 0.39 ƒ:MR Browser 68K v0.39", "name": "MR Browser 68K v0.39"},
         {"app": "Hotline Connect Client 1.9.1 ƒ:HLclient19.Classic", "name": "HLclient19.Classic"},
         {"app": "Serial MReader 0.03 ƒ:Serial MReader v0.03 PPC", "name": "Serial MReader v0.03 PPC"},
         {"app": "Serial MReader 0.03 ƒ:Serial MReader v0.03 68K", "name": "Serial MReader v0.03 68K",
          "systems": ["7.6.1", "8.6"]},
     ]},
    {"name": "utilities.img", "label": "Utilities",
     "title": "Utilities: CPU and speed",
     "tags": ["utilities", "68k", "ppc"],
     "note": "What the Mac says it is, and how fast it runs: Gestalt.Appl 2.7.9 (every Gestalt "
             "selector), TattleTech 2.84 (machine, CPU, FPU, memory; Mac OS 8.6), MacBench 5.0 "
             "(PowerPC: Mac OS 8.6 with PowerPC ticked), Speedometer 4.0 and Newer Technology's "
             "Gauge Series (Clockometer waits for a timer emulators don't give it: it hangs).",
     "take": [("utilities.img", ["Gestalt.Appl 2.7.9 ƒ", "TattleTech 2.84 ƒ", "MacBench 5.0 ƒ",
                                 "Speedometer 4.0 ƒ", "Gauge Series 1997 ƒ"])],
     "launch": [
         {"app": "Gestalt.Appl 2.7.9 ƒ:Gestalt.appl 2.7.9", "name": "Gestalt.appl 2.7.9"},
         {"app": "TattleTech 2.84 ƒ:TattleTech 2.84", "name": "TattleTech 2.84", "systems": ["8.6"]},
         {"app": "MacBench 5.0 ƒ:MacBench® 5.0", "name": "MacBench® 5.0", "systems": ["8.6"]},
     ]},
]


def placed_at(p, baked):
    """The top-level folders a definition's "place" entry puts on its image."""
    if p.get("to"):
        return [p["to"].strip(":").split(":")[0]]
    if p.get("as"):
        return [p["as"]]
    if p.get("take"):
        return [p["take"].split(":")[-1]]
    disk = Disk(baked / (p["snapshot"] + ".img"), tempfile.mkdtemp(prefix="make-library-"))
    try:
        return [n.decode("mac_roman") for n in disk.folders(b"Items") + disk.files(b"Items")]
    finally:
        disk.close()


def snapshots(d, baked):
    """The snapshots whose System Folder parts disk d carries: those placed
    in the folders it takes, in their definitions' order."""
    if "boot" in d or d.get("additions") is False:
        return []
    out = []
    for img, folders in d["take"]:
        definition = DEFINITIONS / (Path(img).stem + ".json")
        if not definition.exists():
            continue
        with open(definition) as f:
            for p in json.load(f).get("place", []):
                tops = placed_at(p, baked)
                if any(t == w or t.startswith(w) for t in tops for w in folders) and p["snapshot"] not in out:
                    out.append(p["snapshot"])
    return [baked / (s + ".img") for s in out]


def sources(d, images, baked):
    if "boot" in d:
        return [images / d["boot"]]
    return [images / img for img, _ in d["take"]] + snapshots(d, baked)


def make(d, images, out, baked):
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
        snaps = snapshots(d, baked)
        if snaps:
            cmd += ["--system-from", *map(str, snaps)]
        cmd += ["--additions-record", str(additions_path(out, d))]
        boots = [out / SYSTEMS[s] for s in d.get("systems", list(SYSTEMS)) if (out / SYSTEMS[s]).exists()]
        if boots:                       # (the boot disks are made first: aliases into them checked)
            cmd += ["--with", *map(str, boots)]
    subprocess.run(cmd, check=True)
    if "boot" not in d:                 # make_boot_disk.py tidies its own
        done = tidy.tidy(str(tmp))
        print(f"{d['name']}: {done['icons']} icons placed in {done['folders']} folders")
    os.replace(tmp, img)


def additions_path(out, d):
    """make_disk.py's account of the disk's System Folder additions, kept
    in OUT_DIR/.additions (the record is written whether or not the disk is
    made; not beside it, where the library would take it for a record)."""
    (out / ".additions").mkdir(exist_ok=True)
    return out / ".additions" / (d["name"] + ".json")


def record(d, out, img=None):
    """The disk's record; its "items" read from img, the disk made."""
    r = {"title": d["title"], "tags": d["tags"], "note": d["note"],
         "label": d["label"], "made_by": "tools/web/make_library.py"}
    if "boot" in d:
        r["system"] = next(s for s, n in SYSTEMS.items() if n == d["name"])
        r["systems"] = [r["system"]]
    else:
        r["sources"] = [img for img, _ in d["take"]]
        systems = d.get("systems", list(SYSTEMS))
        r["systems"] = systems          # the Systems it is offered on (the Disks panel's column)
        r["launch"] = [{"app": f"{d['label']}:{a['app']}", "name": a["name"],
                        "source": a.get("source", d["take"][0][0]), "source_app": a["app"],
                        "systems": a.get("systems", systems)} for a in d["launch"]]
        if additions_path(out, d).exists():
            with open(additions_path(out, d)) as f:
                adds = json.load(f)
            if adds:
                r["system_additions"] = adds
    if img is not None:
        r["items"] = disk_contents.items(img, r["systems"], d["tags"], d.get("launch", []))
    return r


def make_cd(cd, images, out, force):
    """The CD image linked into out, its cover cut down, its record."""
    from PIL import Image
    src, img = images / cd["name"], out / cd["name"]
    if not (images / (cd["name"] + ".json")).exists():
        sys.exit(f"{cd['name']}: no record in {images} (provisioning/cds.py checks it and writes it)")
    if force or not img.exists() or not os.path.samefile(src, img):
        tmp = out / (cd["name"] + ".part")
        tmp.unlink(missing_ok=True)
        try:
            os.link(src, tmp)
        except OSError:
            subprocess.run(["cp", "--reflink=auto", str(src), str(tmp)], check=True)
        os.replace(tmp, img)
    cover = Path(cd["name"]).stem + ".cover.jpg"
    scan = images / cd["cover"]
    if force or not (out / cover).exists() or scan.stat().st_mtime > (out / cover).stat().st_mtime:
        with Image.open(scan) as im:
            im = im.convert("RGB")
            im.thumbnail((COVER_PX, COVER_PX), Image.LANCZOS)
            im.save(out / cover, "JPEG", quality=85, optimize=True)
    with open(images / (cd["name"] + ".json")) as f:
        r = json.load(f)
    rec = {k: r[k] for k in ("title", "tags", "note", "magazine", "issue", "date", "publisher", "contents")}
    rec.update({"label": r["volume"], "cover": cover, "source": r["source"],
                "made_by": "tools/web/make_library.py"})
    rec["items"] = disk_contents.items(img, tags=r["tags"], cd=True)
    new = json.dumps(rec, indent=1, ensure_ascii=False) + "\n"
    p = out / (cd["name"] + ".json")
    if not p.exists() or p.read_text() != new:
        p.write_text(new)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out", nargs="?", default="~/storage/web-library")
    ap.add_argument("--images", default="~/storage/images")
    ap.add_argument("--baked", default="~/storage/baked", help="the snapshots (provisioning/bake.py's)")
    ap.add_argument("--only", nargs="+", default=[])
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    out = Path(os.path.expanduser(args.out))
    images = Path(os.path.expanduser(args.images))
    baked = Path(os.path.expanduser(args.baked))
    out.mkdir(parents=True, exist_ok=True)

    for d in DISKS:
        if args.only and d["name"] not in args.only:
            continue
        img = out / d["name"]
        srcs = sources(d, images, baked)
        missing = [s for s in srcs if not s.exists()]
        if missing:
            sys.exit(f"{d['name']}: no {', '.join(map(str, missing))} (provisioning/build.py makes it)")
        rec = out / (d["name"] + ".json")
        stale = (args.force or not img.exists() or ("boot" not in d and not additions_path(out, d).exists())
                 or any(s.stat().st_mtime > img.stat().st_mtime for s in srcs))
        if stale:
            make(d, images, out, baked)
        else:
            print(f"{img}: up to date")
        new = json.dumps(record(d, out, img), indent=1, ensure_ascii=False) + "\n"
        if not rec.exists() or rec.read_text() != new:
            rec.write_text(new)

    with open(CDS) as f:
        for cd in json.load(f)["cds"]:
            if not args.only or cd["name"] in args.only:
                make_cd(cd, images, out, args.force)


if __name__ == "__main__":
    main()
