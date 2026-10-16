# Provisioning

Guest software reaches the emulator's disks in three steps, each scriptable
and repeatable (the plan and its milestones: [provisioning/PLAN.md](provisioning/PLAN.md)):

1. **Media**: `tools/macfetch/macfetch.py` finds files on the Macintosh
   Garden (its mirror FTP and Internet Archive snapshot), Info-Mac and the
   Internet Archive, and pins them in `provisioning/media.json`: name, size,
   MD5 and every place the file can come from. A file is fetched from
   whichever source works and checked against its pin.
2. **Bake**: classic Mac software installs through its own installer
   (Installer VISE, Apple Installer, StuffIt InstallerMaker), which runs on
   the Mac. `provisioning/bake.py` runs a bake (`provisioning/bakes/*.json`)
   in a scratch guest and captures what it installed as a *snapshot*:
   `baked/<name>.img`, one per product. Archives are expanded the same way,
   by StuffIt Expander in the guest, and items on CDs and disk images are
   copied by the guest (BridgeAgent `COPY`), so nothing passes through a
   Linux unarchiver on its way to a disk.
3. **Build**: `provisioning/build.py` assembles images
   (`provisioning/images/*.json`) from snapshots.

Routine builds never drive a GUI: bakes run again only when their
definition, media or base System changes.

## Snapshots

An HFS image holding what one product adds to a Mac:

```
:Items:            the installed items, as they'd sit at the top of a volume
:System Folder:    files added or changed in the System Folder (new
                   preferences included; the System's own churn left out)
:Snapshot Info     JSON: bake, key, base, OS, machines, target volume name,
                   apps to launch (with facts: no_quit, needs_serial), and a
                   content listing (type/creator, flags, MD5 per fork)
```

`provisioning/snapshot.py` reads and writes them. The content hash ignores
dates and the resource fork's system-reserved bytes, so a re-bake that made
the same software is recognised as the same.

## The development disks

| Image | What |
|---|---|
| `Development.img` | 1 GB HFS volume "Development": MPW 3.4.2 and Interfaces&Libraries (E.T.O. #22), CodeWarrior Pro 4, THINK C 6, Symantec C++ 7; under `Languages`: THINK Pascal 4.5, MacPerl 5.2, MacPython 1.5.2, HyperCard 2.4.1, Frontier 5.0.1; under `Tools`: ResEdit 2.1.3, Resorcerer 2.0, BBEdit Lite 4.1, Disk Copy 6.3.3, StuffIt Expander 5.5, ZTerm, MacsBug 6.6.3 |
| `dev-7.5.5.img`, `dev-7.6.1.img` | the base Systems (`provisioning/bases.json`) plus the snapshots' System Folder parts: MetroNub, CFM-68K and PowerPC libraries, MacsBug 6.6.3, PythonCore, ... |

All three run on the 68k machine (`--backend uae`) and the PowerPC one
(`--backend kpx`).

```bash
python3 provisioning/bake.py --list            # bakes and their snapshots
python3 provisioning/bake.py --all --jobs 3    # every bake not up to date
python3 provisioning/build.py --all            # images not up to date
./build/mac-phoenix --disk ~/storage/images/dev-7.6.1.img --disk ~/storage/images/Development.img ~/roms/quadra.rom

# every app the snapshots list launches, on each machine:
python3 provisioning/test_image.py dev-7.6.1 Development
```

The E.T.O. #22 disc is a local-only pin: a machine without it gets
`local-only media missing; put it at ...`.

## bake.py

A bake runs in a guest booted from a copy of a base System, with a blank
volume (`{target}`, named `Install`) as the install target. Kinds:

| Kind | How |
|---|---|
| `install` | the product's installer, driven by `steps` |
| `expand` | `archive`: StuffIt Expander expands it in the guest (then any `steps`: a first launch) |
| `copy` | `steps` of `{"copy": {"from": ..., "to": "{target}:"}}` from attached CDs or disk images |
| `derive` | a snapshot made on the host from other snapshots |

`"machines": ["68k", "ppc"]` runs a bake on both machines
(`baked/parts/<name>@<machine>.img`) and merges the runs: files only one
has are kept, CFM code that differs becomes one fat file
(`provisioning/fat_cfm.py`), aliases keep the first, anything else that
differs fails unless `"prefer"` says which run's file to keep.

Steps (see `provisioning/guest.py`) click by name where they can: `/api/ui`
gives the front process's windows, controls, dialog items and open menus
from guest memory.

```json
{"launch": "CW Pro 4 Tools:CW Pro 4 Tools Installer"},
{"wait_for": {"button": "Install"}, "timeout": 60},
{"popup": [270, 367], "choose": "{target}"},
{"click": {"button": "Install"}},
{"click": [121, 162]}
```

Pixels are the fallback, for what an app draws itself. Working out a new
bake:

```bash
python3 provisioning/bake.py NAME --explore --record   # boot with its media, leave the guest up
python3 provisioning/bake.py --do '{"look": "front"}'   # the UI + a half-size shot of the front window
python3 provisioning/bake.py --do '{"click": {"button": "Continue"}}'
python3 provisioning/bake.py --finish                    # capture, and save the steps (--record)
```

Floppy sets go on a "Media" volume and are mounted through the guest's
Disk Copy, so installers can eject and ask for them as on real floppies
(raw floppy images are converted to Disk Copy 4.2 first: `"dc42": true`).
Several bakes can run at once; scratch space is `$MACEMU_STORAGE/work`.

## build.py

```json
{"label": "Development", "size_mb": 1024,
 "place": [{"snapshot": "mpw-3.4.2"},
           {"snapshot": "frontier-5.0.1", "to": "Languages"},
           {"snapshot": "think-c-6", "take": "Development", "as": "THINK C 6.0"}]}
{"base": "7.6.1", "system_from": ["codewarrior-pro-4", "macsbug-6.6.3"]}
```

A data image is a blank volume with snapshots' `:Items:` placed (`take` an
item, `to` a folder, `as` a new name); a boot image is a base System with
snapshots' System Folder parts, later ones replacing earlier ones and the
System's own files. Two snapshots placing the same file fail the build.
Beside each image, `<name>.img.json` records its snapshots and the apps to
launch as paths on the image (what `test_image.py` runs). Aliases point at
the bake's target volume; the build warns where they land elsewhere.

## macfetch

```bash
M=tools/macfetch/macfetch.py
$M search hypercard 2.4              # local index of file names (built once: $M update)
$M garden codewarrior pro 4          # one search on macintoshgarden.org; never crawled
$M page 1                            # a title page: year, publisher, files with MD5s
$M pin 3 -m LIST --dir downloads/x   # into a list, with size and MD5
$M pin --ia cwpro4/CW4MACTOOLS.ISO -m LIST
$M pin --file PATH -m LIST           # a file only you have: hash only
$M provision LIST                    # fetch + verify; idempotent; exit 1 on any failure
```

Files land under `$MACEMU_STORAGE` (default `~/storage`), at the list's
`"out"` plus each entry's `"dir"`. Sources, fastest first: the Internet
Archive, `old.mac.gdn`, oldapplestuff.com, the Garden FTP
(`ftp.macintosh.garden`, the public mirror account), the Info-Mac mirrors.

## Other tools

| Script | What |
|---|---|
| `containers.py`, `ndif.py` | read media: archives (unar), HFS images, CDs with partition maps, Disk Copy 4.2 and 6 (NDIF) |
| `create_hfs.py`, `create_hfs_plus.py` | blank HFS / HFS+ images |
| `populate_disk.py` | copy everything in `~/storage/installers` onto an image (unar, MacBinary) |
| `hfs_bless.py`, `fix_hfs_header.py` | bless a System Folder; repair a volume header |
| `install_bridge_agent.sh` | BridgeAgent into Startup Items (the emulator runs it with `--bridge`) |
| `build_retro68.sh` | the Retro68 cross toolchain (host side) |
| `tools/mpw/mpw` | MPW's 68k tools in a headless Executor session (docs/MPW.md) |
