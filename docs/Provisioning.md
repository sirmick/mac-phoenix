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
| `dev-tools-system-7.img` | 1 GB HFS volume "Development": MPW 3.4.2 and Interfaces&Libraries (E.T.O. #22), CodeWarrior Pro 4, THINK C 6, Symantec C++ 7; under `Languages`: THINK Pascal 4.5, MacPerl 5.2, MacPython 1.5.2, HyperCard 2.4.1, Frontier 5.0.1; under `Tools`: ResEdit 2.1.3, Resorcerer 2.0, BBEdit Lite 4.1, Disk Copy 6.3.3, StuffIt Expander 5.5, ZTerm, MacsBug 6.6.3 |
| `dev-7.5.5.img`, `dev-7.6.1.img` | the base Systems (`provisioning/bases.json`) plus the snapshots' System Folder parts: MetroNub, CFM-68K and PowerPC libraries, MacsBug 6.6.3, PythonCore, ... |
| `7.1.img` | System 7.1 for the Quadra 650 (`os-7.1`): boots with `--model-id 5` (bases.json `args`; web UI profile `7.1-68k`); no BridgeAgent (no File System Manager, so no Host volume) |
| `6.0.8.img` | System 6.0.8, universal (boots the SE and the IIci: Color, Monitors, AppleShare, AppleTalk Responder, all printer drivers, 32-Bit QuickDraw), installed from the Legacy CD's floppies by Apple's Installer on the IIci (`os-6.0.8`), with the floppies' utilities at the top and StuffIt Expander 4.0.2 and Disk Copy 4.2 in Utilities (`tools-6.0.8`) |
| `8.6.img`, `9.0.4.img` | Mac OS 8.6 and 9.0.4 for the PowerPC machine (G3 ROM): installed from their CDs (`os-8.6`, `os-9.0.4`), with CarbonLib 1.6, StuffIt Expander 6.0.1, Disk Copy 6.3.3, MacsBug, TCP/IP on DHCP |
| `dev-tools-macos-8-9.img` | the 8.6 / 9 development volume: CodeWarrior Pro 7.2 (7.1 Tools CD + the 7.2 PowerPC plugins; needs CarbonLib 1.3.1+), MPW-GM; `Languages`: MacPerl 5.6.1r2, MacPython-OS9 2.3.5, HyperCard 2.4.1, Frontier 5.0.1; `Tools`: ResEdit, Resorcerer, BBEdit Lite 6.1.2, Disk Copy, StuffIt Expander 6.0.1, ZTerm, MacsBug |
| `dev-8.6.img`, `dev-9.0.4.img` | the 8.6 / 9.0.4 disks plus those tools' System Folder parts; boot with `dev-tools-macos-8-9.img` |

All three run on the 68k machine (`--backend uae`) and the PowerPC one
(`--backend kpx`).

```bash
python3 provisioning/bake.py --list            # bakes and their snapshots
python3 provisioning/bake.py --all --jobs 3    # every bake not up to date
python3 provisioning/build.py --all            # images not up to date
./build/mac-phoenix --disk ~/storage/images/dev-7.6.1.img --disk ~/storage/images/dev-tools-system-7.img ~/roms/quadra.rom

# every app the snapshots list launches, on each machine:
python3 provisioning/test_image.py dev-7.6.1 dev-tools-system-7
python3 provisioning/test_image.py dev-9.0.4 dev-tools-macos-8-9      # PowerPC only (the base's "machines")
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
Machines: `68k` (Quadra 650), `ppc` (G3), `se` (Mac SE) and `iici` (Mac IIci, colour; both System 6, so no
BridgeAgent; `launch`/`open` go through the Finder by name and Command-O,
`copy` drags the item onto the target's desktop icon, `quit` is
Command-Q, shutdown through Special > Shut Down; labels the Finder drew
before the text feed, or a window clips, are found in the draw log or
redrawn with Select All). A bake without `"machines"` runs on its base's (`bases.json`: 8.6 and
9.0.4 are PowerPC only), else on the 68k machine. A medium with `"as_is":
true` goes onto the Media volume as the MacBinary file it is, not opened on
the host: a self-mounting HFS+ image (CarbonLib 1.6) that only the guest
can mount. An `os` bake run from a System whose volume has the target's
name has its boot disk renamed `Bootstrap`. Scripted guests are capped at
640x480 (`--max-resolution`): 8.6 and later take the driver's largest mode
at their first boot.

Steps (see `provisioning/guest.py`) click by name where they can: `/api/ui`
gives the front process's windows, controls, dialog items and open menus
from guest memory (the front process as BridgeAgent reports it). The text
feed sees 68k code's drawing only: a PowerPC-native installer's text (VISE
in MacPython-OS9) isn't there, and its recipe works from controls alone.

```json
{"launch": "Install 1:Installer"},
{"wait_for": {"button": "Continue…"}, "timeout": 90},
{"popup": {"text": "Easy Install"}, "choose": "Custom Install"},
{"click": {"text": "Open Transport for 68K Macintoshes", "front": true}},
{"check": {"text": "Selected size: 1,933K"}},
{"click": {"row": "System Software", "exact": true}},
{"check": {"button": "AppleScript 1.1.1", "value": 0}},
{"click": {"button": "Install"}}
```

What an app draws itself is found by its drawn text (`/api/text`):

* `{"popup": {"text": ...}, "choose": ...}` presses a popup where its
  current value is drawn (`{"button": ...}` for a popup control) and
  releases on the named item of the open menu.
* `{"click": {"text": ...}}` clicks a label; in the Apple and VISE
  installers' option lists that ticks the row's box.
* `{"click": {"row": ...}}` clicks at the start of the list row with that
  text: the left edge of the dialog user item holding it, plus `inset`
  (default 8). This is for a disclosure triangle, which has no text of
  its own.
* `{"check": ...}` makes a click prove itself. It fails the bake at that
  step unless the front window shows the text (`text`, exact or a
  prefix), doesn't start any string with `text_not`, or a control has a
  `value`. Recipes check after every option they set, using the
  installer's Selected size or the checkbox's value.

`offset` (relative to drawn text) and `[x, y]` points remain for
exploring. No recipe uses screen coordinates; an `offset` that is still
needed gets a `note` saying why.

**Watching.** Every bake and explore session prints a `watch:` URL: the
guest's web UI on this host's LAN address with `?view` (nothing you do in
the page reaches the guest). The step being run shows over the screen
(`/api/annotate`). `--port N` fixes the port, so the URL stays the same
between sessions. A system error alert on screen fails the step at once
(with a `bomb.png`), and `--abort` stops a session even mid-wait; edits to
`guest.py` apply to a running explore session from its next `--do`.

Working out a new bake:

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
| `install_bridge_agent.sh` | BridgeAgent into an image's Startup Items, by hand (the emulator launches it from the Host volume instead) |
| `build_retro68.sh` | the Retro68 cross toolchain (host side) |
| `tools/mpw/mpw` | MPW's 68k tools in a headless Executor session (docs/MPW.md) |
