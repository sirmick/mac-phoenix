# Provisioning

Guest software reaches the emulator's disks in three steps, each scriptable
and repeatable:

1. **Fetch**: `tools/macfetch/macfetch.py` finds files on the Macintosh
   Garden (its mirror FTP and Internet Archive snapshot), Info-Mac and the
   Internet Archive, and pins them in a *list*: name, size, MD5 and every
   place the file can come from. `provision` fetches a list from whichever
   source works and checks each file against its pin.
2. **Install**: most classic Mac software only installs through its own
   installer (Installer VISE, Apple Installer, StuffIt InstallerMaker),
   which runs on the Mac. `provisioning/guest_install.py` runs it in a
   scratch guest and *captures* what it installed as a small HFS image.
   Archives are expanded the same way, by StuffIt Expander in the guest, so
   nothing passes through a Linux unarchiver on its way to a disk. Captures
   are pinned in the list too (local only: they are made, not downloaded).
3. **Build**: `provisioning/build_disk.py` copies a list's captures (and the
   few host-side items) onto HFS volumes.

Routine builds never drive a GUI: recipes are only run to make a capture
again.

## The development disks

`provisioning/lists/dev.json` makes three images that run on both the 68k
machine (`--backend uae`) and the PowerPC one (`--backend kpx`):

| Image | What |
|---|---|
| `Development.img` | 1 GB HFS volume "Development": MPW 3.4.2 and Interfaces&Libraries (E.T.O. #22), CodeWarrior Pro 4, THINK C 6, Symantec C++ 7, THINK Pascal 4.5, MacPerl 5.2, MacPython 1.5.2, HyperCard 2.4.1, Frontier 5.0.1, MCL 4.2 (PowerPC only), ResEdit 2.1.3, Resorcerer 2.0, BBEdit Lite 4.1, Disk Copy 6.3.3, StuffIt Expander 5.5, ZTerm, MacsBug 6.6.3 |
| `dev-7.5.5.img`, `dev-7.6.1.img` | the base Systems (`macos-7.x.img.orig`) plus the System Folder files the installers add: MetroNub, CFM-68K and PowerPC libraries, MacsBug 6.6.3, PythonCore, AppleScriptLib, ... |

```bash
O=~/storage/images
python3 provisioning/build_disk.py provisioning/lists/dev.json -o $O/Development.img
python3 provisioning/build_disk.py provisioning/lists/dev.json --no-provision --disk boot \
    --base $O/macos-7.6.1.img.orig --os 7.6.1 -o $O/dev-7.6.1.img
./build/mac-phoenix --disk $O/dev-7.6.1.img --disk $O/Development.img ~/roms/quadra.rom

# every app launches, on each machine:
python3 provisioning/test_dev_disks.py --boot $O/dev-7.6.1.img --dev $O/Development.img --backend uae
```

The list's E.T.O. #22 disc and the captures are local-only pins: a machine
without them gets `MISSING ... local-only, put it at ...` from `provision`.

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

## guest_install.py

```bash
# an installer, driven by a recipe (provisioning/recipes/*.json)
python3 provisioning/guest_install.py provisioning/recipes/cw-pro-4.json -o ~/storage/installed/cw-pro-4.img
# an archive, expanded by StuffIt Expander
python3 provisioning/guest_install.py --expand Frontier_5.0.1.sit -o ~/storage/installed/frontier.img
# working out a new recipe: boot with its media and leave the guest up
python3 provisioning/guest_install.py RECIPE --explore -o /dev/null
tools/macfetch/macfetch.py pin --file ~/storage/installed/X.img --dir installed -m provisioning/lists/dev.json
```

The guest boots a copy of `macos-7.6.1.img.orig` with an empty volume
named "Development" as the install target, so paths an installer records
already point at the right volume. Afterwards the capture holds
`:Development:` (everything installed there) and `:System Folder:` (files
added or changed, minus noise such as the bridge's agent and the System's
own preferences).
A recipe's steps are bridge launches, document opens (a Disk Copy image:
mounted), clicks and keys at 640x480 coordinates, and waits; `expect`
names what must be on Development afterwards, so a click that picked the
wrong volume fails the run. Floppy sets are put on a "Media" volume and
mounted through the guest's Disk Copy, so installers can eject and ask for
them as on real floppies; raw floppy images are converted to Disk Copy 4.2
first (`"dc42": true`).

**Both architectures.** Installers install for the machine they run on.
Where that matters, run the recipe on each (`--backend kpx`) and combine:
CodeWarrior's PowerPC-only extensions come from its CD; MacPython's
`PythonCore` is merged into one fat library from the two captures
(`fat_with`, `provisioning/fat_cfm.py`).

**First launch.** Steps after an installer finishes can launch the app and
answer its one-time questions (Frontier's "personalize your copy", Disk
Copy's licence), so the disks open cleanly. New preference files are
captured for this; the System's and the bridge's own are left out. A recipe
with `"expand": {"file": ...}` expands an archive and then runs its steps.

Several guests can run at once (each `--bridge` instance installs its agent
with a private hfsutils `HOME`).

## build_disk.py

Each list entry's `"install"` steps say what to take from the file and
where to put it: `take` (an HFS path in a capture or CD image, a folder in
an archive; `!` steps into a nested container), `to`, `as`, `skip`,
`disk` (`development` or `boot`), `os` (only for some System versions) and
`replace` (overwrite what the disk already has; by default a base System's
own copy wins), `size` (rewrite an app's SIZE: MCL 4.2's asks for 400 MB)
and `fat_with` (merge the other architecture's build of a CFM library). It
reads Disk Copy 4.2 and 6 (NDIF, `provisioning/ndif.py`) images, CD
images with partition maps and anything `unar` opens; hfsutils runs with a
private `HOME`; scratch space is `$MACEMU_STORAGE/work`.

## Other tools

| Script | What |
|---|---|
| `create_hfs.py`, `create_hfs_plus.py` | blank HFS / HFS+ images |
| `populate_disk.py` | copy everything in `~/storage/installers` onto an image (unar, MacBinary) |
| `hfs_bless.py`, `fix_hfs_header.py` | bless a System Folder; repair a volume header |
| `install_bridge_agent.sh` | BridgeAgent into Startup Items (the emulator runs it with `--bridge`) |
| `build_retro68.sh` | the Retro68 cross toolchain (host side) |
| `tools/mpw/mpw` | MPW's 68k tools in a headless Executor session (docs/MPW.md) |
