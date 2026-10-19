# Provisioning: baked Mac software, from install media to disks

Agreed 2026-10-16. Every disk mac-phoenix boots or mounts is built from
**snapshots** of software that a Mac installed: Apple's installer for the
System, each product's own installer or StuffIt Expander for everything
else, all run in the emulator. Inputs are pinned by hash, recipes are
replayable, and building a disk is copying snapshots. The messy work
(finding software, picking versions, clicking through installers) is
done once, up front, and kept.

Full intent: System 6.0.8, the System 7 range, Mac OS 8.6 and 9.0.4.
**Current scope: the 7.x range (7.1, 7.5.5, 7.6.1).** Mac OS 8.1 is
skipped.

**Where it stands (2026-10-16, branch `provisioning-dev-disks`).** A
first version works end to end for development disks: `tools/macfetch`
(pinned media), `provisioning/guest_install.py` (installers and StuffIt
Expander in the guest, captured), `provisioning/build_disk.py`,
`provisioning/test_dev_disks.py`, and `Development.img` +
`dev-7.5.5.img` / `dev-7.6.1.img`, on which every app launches on the
68k and PowerPC machines. It still has paths specific to that disk
("Development" in snapshots and recipes), host-side build steps and
hand-made base Systems; this plan replaces those. See
[../Provisioning.md](../Provisioning.md) for the current tools.

## Principles

* **The Mac does the work.** Archives are expanded by StuffIt Expander,
  installers run as installers, files are copied by the guest. Linux
  never decodes or rearranges Mac software on its way to a disk. Host-side
  steps are limited to moving whole containers (an HFS image, a raw
  floppy rewritten as a Disk Copy 4.2 image so the guest can mount it).
* **Snapshots only.** A disk is assembled from snapshots and nothing else:
  no unpacking, merging or patching at build time.
* **Generic.** Snapshots are what a product adds to a Mac, not where it
  ends up; which disk and folder is the image definition's business.
* **Pinned and hashed.** Media by MD5; snapshots record their bake,
  recipe hash, media pins and base; images record their snapshots. A
  change rebuilds only what depends on it.
* **One snapshot per product**, for both machines and every 7.x System
  it targets. Bakes run on the 68k machine; a product whose installer
  writes machine-specific files is also baked on PPC and the two runs are
  merged: different files are all kept (each System ignores the other
  machine's extensions), the same file in two builds becomes one fat file.
  Other conflicts fail the bake. "More files" over more variants.

## Terms

| Term | What | Defined in | Stored in |
|---|---|---|---|
| **media** | a pinned input file: archive, installer, CD or floppy image | `provisioning/media.json` | `media/` |
| **bake** | a recipe turning media into a snapshot, run in the guest | `provisioning/bakes/<name>.json` | |
| **snapshot** | what one product adds to a Mac | made by a bake | `baked/<name>.img` (machine runs: `baked/parts/<name>@<machine>.img`) |
| **image** | a disk assembled from snapshots | `provisioning/images/<name>.json` | `images/` |

### Snapshot layout

```
:Items:            the installed items as they'd sit at the top of a volume
:System Folder:    files added or changed in the System Folder (new
                   preferences included; the System's own churn left out)
:Snapshot Info     manifest: bake, recipe hash, media pins, base snapshot,
                   OS/arch, target volume name, content listing (path,
                   type/creator, flags, MD5 per fork; apps marked, with
                   facts such as "needs_serial", "no_quit")
```

Two hashes: the image's (pins one bake) and the content's (ignores dates,
so a re-bake that produced the same software is recognised as the same).
Images are shrunk to fit.

During a bake the install target is a blank volume, `{target}` in
recipes (default name `Install`). Aliases an installer writes carry that
name; the manifest records it and the build warns when a snapshot with
aliases lands on a volume of another name.

### Bake kinds

| Kind | For | How |
|---|---|---|
| `os` | a System | Apple's installer from CD or floppies onto a blank disk, updaters, first boot; the result is a whole bootable volume |
| `install` | a product with its own installer | recipe: launch, clicks, keys, waits; then first-launch steps (licence, registration, personalisation) |
| `expand` | an archive (`.sit .hqx .bin .zip .sea .cpt`) | StuffIt Expander in the guest; can be followed by first-launch steps |
| `copy` | folders on a disk or CD image | the image is mounted in the guest; BridgeAgent `COPY` places the items (both forks, Finder info) |
| `derive` | a snapshot from snapshots | a SIZE fix, a first launch of something already baked (merging machine runs is automatic) |

A bake runs on a System (`"os"`, default 7.6.1) on the 68k machine;
`"machines": ["68k", "ppc"]` runs it on both and merges the two into the
product's one snapshot. Every bake except `os` runs on an `os` snapshot,
so "baked on 7.6.1" is exact.

### Images

```json
{"name": "Development", "size_mb": 1024,
 "place": [{"snapshot": "mpw-3.4.2", "to": ""},
           {"snapshot": "frontier-5.0.1", "to": "Languages"}]}
{"name": "dev-7.6.1", "base": "os-7.6.1", "system_from": ["codewarrior-pro-4", "macsbug-6.6.3"]}
```

A data image is a blank volume plus snapshots' `:Items:`; a boot image is
an `os` snapshot plus snapshots' `:System Folder:` parts (and optionally
items).

## Storage and media

### Today

Spread over three places with overlapping content:

* **mac-phoenix-assets** (private Git LFS repo, `/home/mick/src/mac-phoenix-assets`):
  `storage/images/` mixes OS install CDs, hand-made System disks and
  their `.orig` baselines, collection disks (MacPack, games, installers),
  tool-generated disks (`mpw-eto22`, `dtdb-7.5.5`) and test copies
  (`test-*.img`; several tracked images are modified by tests booting
  them). `downloads/` holds installers by category, CDs (`E.T.O. #22`,
  `CodeWarrior 8 Pro`, ADC Seed), dev tools. `~/storage/{roms,images}`
  link here.
* **`~/storage` (local)**: `downloads/dev/` (macfetch), `installed/`
  (today's captures), `eto/` (`tools/mpw` expects `eto22.toast` there),
  `aslm-kit/` (copied out of E.T.O. for tests), `work/` (scratch),
  `snapshots/` (the emulator's state snapshots), `executor*/`.
* **Inside other images**: `data.img` exists only to carry the E.T.O.
  `.toast` files.

### Target

Everything pinned or baked lives in the assets repo (LFS), linked into
`~/storage`; everything generated per run stays local.

```
mac-phoenix-assets/
  media/                     pinned inputs; a file's path is where it came from
    apple/                   Apple install media: legacy-recovery.iso,
                             macos-7.6-retail.iso, macos-8.6.iso, macos-9.0.4.iso
    garden/apps|games|manuals/<file>    the Garden's own paths
    archive/<item>/<file>    Internet Archive items
    infomac/<path>           Info-Mac paths
    other/                   one-offs with no public source (E.T.O. #22,
                             magazine CDs), named and described in media.json
  baked/                     snapshots: <name>.img (parts/: per-machine runs)
  storage/images/            built images only (until the switch-over, also
                             the hand-made ones being replaced)
  storage/roms/              unchanged
~/storage/
  media, baked, images, roms -> the assets checkout
  work/                      scratch (bakes, builds, test copies, test-*.img)
  snapshots/, executor*/     the emulator's own local state
```

`provisioning/media.json` is the one list of media: every entry pinned,
its location under `media/` derived from its source, so nothing decides
paths by hand. macfetch `provision` fills `media/` from public sources;
`media/other/` and the Apple CDs are local-only pins served by the assets
repo. `tools/setup.sh` gains the `media` and `baked` links.

### Sorting out (milestone P1)

| Now | Goes to |
|---|---|
| `images/Apple Legacy Recovery.iso`, `MacOS 7.6 Retail.iso`, `MacOS8.6.iso`, `Apple_MacOS_9.0.4_USA.iso` | `media/apple/` (the web UI's CD-ROM list scans `media/apple/` too) |
| `images/data.img` | its two `.toast` files to `media/other/`; retired |
| `downloads/cds/*` (E.T.O. #22 + Frameworks, CodeWarrior 8 Pro, ADC Seed April 2001) | `media/other/` |
| `downloads/installers/**`, `downloads/dev/**`, loose `downloads/*` | triaged: matched to a public source by MD5 → `media/garden|archive|infomac/`; otherwise `media/other/`; duplicates dropped |
| `~/storage/downloads/dev/` (macfetch) | `media/` by source |
| `~/storage/eto/eto22.toast`, `~/storage/aslm-kit/` | from `media/other/`; `tools/mpw` and the ASLM test read them there (or `aslm-kit` becomes a `copy` bake) |
| `~/storage/installed/` | `baked/`, once re-baked in the generic layout |
| `images/test-*.img`, `mpw-*.img`, `dtdb-7.5.5.img` | generated locally into `~/storage/work/` |
| `images/Development.img`, `dev-7.x.img` (built 2026-10-16, untracked) | rebuilt from `provisioning/images/`; built images are not tracked |
| `images/macos-*.img(.orig)`, `system-*.img(.orig)` | kept until their `os` bakes replace them (P5), then retired |
| `images/MacPack.img`, `HD20SC.img`, `games.img`, `installers.img`, `68k-`/`ppc-installers.img`, `system-5.1.img`, `macos-8.6.1.img` | out of scope for now: left as they are; their software becomes bakes when collections are tackled |
| tracked images modified by tests (`macos-7.5.5.img` …) | tests boot copies in `work/`, never the tracked file |

## Layers

### L1: OS (`os-<ver>`)

| System | Source | Baked on | Runs on | Scope |
|---|---|---|---|---|
| System 6.0.8 | Legacy CD `Mac OS:System 6.0.8` (800K / 1440K floppy images) | SE / IIci ROM | 68k (SE, Plus, IIci profiles) | later |
| **System 7.1** | Legacy CD `Mac OS:System 7.1` + `7.1-C/Q610, C/Q650, Q800 1.1` enabler (the Quadra 650 needs it) | Quadra 650 | 68k | **now** |
| **System 7.5.5** | Legacy CD `System 7.5, Version 7.5.3` + `System Updates:System 7.5.5 Update` | Quadra 650 (universal install) | 68k, PPC | **now** |
| **Mac OS 7.6.1** | Legacy CD `Mac OS 7.6` + `Mac OS 7.6.1` (Retail CD as a second source) | Quadra 650 (universal install) | 68k, PPC | **now** |
| Mac OS 8.6 | `MacOS8.6.iso` | G3 | PPC | later |
| Mac OS 9.0.4 | `Apple_MacOS_9.0.4_USA.iso` (9.0 + the 9.0.4 update on the disc) | G3 | PPC | later |

Legacy CD = the Apple Legacy Software Recovery CD, `media/apple/legacy-recovery.iso`:
every System from 1.0 to Mac OS 8.1 with updaters, machine enablers
(`System Software by CPU`) and Apple's add-ons (`Apple SW (A-D|E-M|N-Z)`,
`Disk Utilities`). If the Internet Archive has an identical copy (same
MD5) it becomes a public pin.

An `os` bake: boot the installer (CD with `--bootdriver -62`, or a boot
floppy image), Easy Install onto a blank disk from `create_hfs.py`, apply
updaters, restart, answer first-boot questions (8.6/9: Mac OS Setup
Assistant), set the defaults we want (no startup sound loops, virtual
memory off, etc.), shut down. 7.5.5 and 7.6.1 use Custom Install's "any
Macintosh" option: one universal System per version that boots on both
machines.

### L2: tools (`tools-<family>`)

The bare Mac every guest gets, so any software can be unpacked, mounted
and networked. Standard boot images are `os-<ver>` + `tools-<family>`
(`7.6.1.img` …), and are what tests and the web UI profiles boot.

| Item | 6.0.8 | 7.1 | 7.5.5 / 7.6.1 | 8.6 / 9 | Source |
|---|---|---|---|---|---|
| StuffIt Expander | 4.x (68000-safe) | 5.5 | 5.5 | 5.5 | Garden |
| Disk Copy | 4.2 | 6.3.3 | 6.3.3 | on the CD | Legacy CD `Disk Utilities` |
| Networking | MacTCP 2.0.6 | MacTCP 2.0.6 | Open Transport 1.1.2 (7.5.5; 7.6.1 has OT) | built in | Legacy CD `Apple SW` |
| Shared Library Manager | | 1.1.1 / 2.0 | 2.0 | built in | Legacy CD |
| AppleScript, QuickTime, Sound Manager 3.1 | | newest per System | newest per System | built in | Legacy CD |
| SimpleText 1.3.1, Disk First Aid, Apple System Profiler, PC Exchange | 6.x versions | ✓ | ✓ | built in | Legacy CD |
| Internet Config 1.4 | | ✓ | ✓ | ✓ | Garden |
| BBEdit Lite | | 4.1 | 4.1 | 4.1 / 6.1 | Garden |

### L3: development (`dev-*`)

Today's set, re-baked generically; arrangement is in `images/Development.json`.

| Group | Snapshots |
|---|---|
| Compilers / IDEs | MPW 3.4.2 + Interfaces&Libraries (E.T.O. #22, `copy`), CodeWarrior Pro 4 (baked on both machines: the union of their System Folder parts, plus StdCLibInit from the CD for 7.5.5 on PPC), THINK C 6, Symantec C++ 7, THINK Pascal 4.5 |
| Languages / scripting | MacPerl 5.2.0r4, MacPython 1.5.2 (baked on both machines: one fat PythonCore), HyperCard 2.4.1, Frontier 5.0.1 (first launch done) |
| Debug / resources | MacsBug 6.6.3 (System Folder), ResEdit 2.1.3, Resorcerer 2.0 (needs a serial) |
| Comms | ZTerm 1.0.1 (needs a serial port the emulator doesn't have yet) |
| 8.6 / 9 only (later) | Universal Interfaces 3.4.2 and MPW 3.5 (HFS+), a later CodeWarrior for Carbon (`media/other/CodeWarrior 8 Pro`) |

Images: `Development.img` (one volume for the 7.x Systems) and
`dev-<ver>.img` = `<ver>.img` + the dev snapshots' System Folder parts.

## Tools

| Tool | Does | From |
|---|---|---|
| `tools/macfetch` | media: find, pin, fetch, verify | exists; paths become source-derived |
| `provisioning/bake.py` | runs bakes; `--explore` (guest left up), `--record` (writes the recipe from what's done) | `guest_install.py` |
| `provisioning/build.py` | images from snapshots (`provisioning/images/<name>.json`): items placed (`take`/`to`/`as`), System Folder parts over a base, a record beside the image with the apps' paths, alias warnings | `build_disk.py`, much smaller |
| `provisioning/test_image.py` | boots images on each machine, launches every app their records list (`no_quit` apps by restarting) | `test_dev_disks.py` |
| `/api/ui` | the guest's windows, controls, dialog items and menus as JSON, read from guest memory: recipes click by name (`{"click": {"button": "Install"}}`, `{"popup": …, "choose": …}`, `{"wait_for": …}`) or by drawn text (`/api/text`), and `check` what a click did; no screen coordinates | new |
| BridgeAgent `COPY src dst` | in-guest copy, both forks and Finder info | new |
| `ndif.py`, `fat_cfm.py` | NDIF decoding for media prep (floppy sets); fat CFM merge of machine runs | exist |

## Milestones

| | What | Status |
|---|---|---|
| **P0** | Groundwork: snapshot layout + manifest + content hash, `bake.py` (generic `{target}`, the five kinds, `--record`), BridgeAgent `COPY`, `build.py`, `test_image.py`, dependency tracking | done 2026-10-16: 16 snapshots; Development + dev-7.5.5/7.6.1 built from them; 15/15 apps on uae and kpx × 7.5.5 and 7.6.1; aliases rewritten and resolved in the guest |
| **P1** | Storage: `media/` and `baked/` in the assets repo, `media.json`, the sorting-out table above, `setup.sh` links, tests on copies | |
| **P2** | `os` bakes: 7.1 (68k), 7.5.5 and 7.6.1 (68k + PPC) from the Legacy CD | 7.6.1 and 7.5.5 done 2026-10-17 (both boot on uae and kpx); 7.1 to do: see [P2 status](#p2-status) |
| **P3** | `tools-7` and the 7.x standard boot images: StuffIt Expander and Disk Copy baked on `os-7.6.1` first (expand and floppy bakes need them in the guest), `tools-7.6.1` = `os-7.6.1` + those, then MacsBug and the rest of the tools on it | done for 7.6.1 and 7.5.5 2026-10-17: StuffIt Expander, Disk Copy (baked on `os-7.6.1`) → `tools-7.x` (bake bases) → MacsBug (baked on `tools-7.6.1`) → standard boot disks `7.6.1.img`, `7.5.5.img` (apps launch on uae and kpx; MacsBug loads). 7.6.1 already has Open Transport, AppleScript, ASLM and QuickTime; 7.5.5 has AppleScript, ASLM and QuickTime, and gets Open Transport 1.1.2 (`open-transport-1.1.2`, baked on `tools-7.5.5`). Its "all Macintoshes" install adds fat libraries but leaves 7.5.3's 1.1 68K libraries behind, and with those 7.5.5 bombs at startup on the PPC machine (illegal instruction, both cores); the bake lists them in `system_remove` and the build deletes them. `7.5.5.img` (OT 1.1.2) passes on uae and kpx |
| **P3b** | Backport the driving techniques to the GUI recipes (named clicks, drawn text for app-drawn option lists, `wait_for` instead of fixed waits, no per-machine steps): MacPerl and MacPython first (per-machine drags), then THINK C, Symantec C++, HyperCard, StuffIt Expander, Frontier's and Disk Copy's first launches; replayed with someone watching | done 2026-10-17: MacPerl, MacPython (one recipe for both machines now), THINK C, Symantec C++, HyperCard (its System upgrades unticked by name), Frontier's first launch, StuffIt Expander, Disk Copy: named controls, drawn text, `until` repeats; no pixels, no per-machine steps. 2026-10-17, later: no screen coordinates in any recipe (step classification: 0 left, from 5 popups at fixed points and 8 clicks offset from drawn text): popups pressed where their value is drawn (`{"popup": {"text": "Easy Install"}}`); the Apple Installer's and VISE's option lists tick a row when its label is clicked; 7.5.5's System Software group opened by `{"click": {"row": ...}}` (the list's user item, left edge); every option click followed by a `check` (Selected size / Approximate disk space needed, HyperCard's checkbox values). Everything re-baked unattended; snapshot content hashes unchanged except run noise (installer log dates, preferences, aliases, Frontier.root; os captures now drop BridgeAgent's `Trash:Old-BA-<time>` copies). bake.py compares every re-bake's install with the snapshot it replaces (`snapshot.install_diff`: aliases, installer logs and a bake's `volatile` paths by name; for os bakes the System's preferences, and fonts and control panels by size); the next full re-bake reported the same install for all 22 |
| **P4** | `dev-*`: the development set re-baked on the `os` snapshots (the P3b recipes; the rewrapped expand-only snapshots just re-baked) (until then: the first captures of 2026-10-16, rewrapped into the generic layout); `Development.img`, `dev-7.5.5/7.6.1.img`; the four-cell app matrix | done 2026-10-17: every dev snapshot re-baked on `tools-7.6.1` (none rewrapped any more); `dev-7.x` = the standard 7.x disks + the dev System Folder parts; 18/18 (17 apps + aliases) on uae and kpx × 7.5.5 and 7.6.1 |
| **P5** | Switch-over: tests, the boot matrix and web UI profiles boot baked images; the hand-made `.orig` disks retired | tests switched 2026-10-17: they boot copies (tests/lib/refresh_test_disk.sh) of `tests-7.5.5` / `tests-7.6.1` (standard disk + classic 68k MacPerl, Frontier, MPW), `tools/mpw` uses the built `mpw.img`; 6.0.8 stays hand-made; 9.0.4 switched 2026-10-19 (`tests-9.0.4` = the standard 9.0.4 disk: guest_suite_ppc{,_mame}, bridge_trap_ppc and the network suite pass on it). Of the 48 guest-booting ctests all pass except `boot_uae_jit` (known UAE JIT stalls). The five Executor tests on the mame-68k core that failed on the baked disks (guest/finder/keyboard suites, aslm, threads) were Executor's `PrOpen`: it read STR -8192 (the printer chosen in the Chooser) without checking it, and a baked System has never had a printer chosen; a NULL handle reads guest address 0, mapped on the UAE core, a host null pointer on mame-68k (fixed 2026-10-17). Web UI profiles switched 2026-10-19: `68k` 7.6.1, `ppc` 9.0.4, new `ppc-8.6`, `68k-dev` (dev-7.6.1 + Development), `ppc-dev` (dev-9.0.4 + Development-9); the hand-made disks only as fallbacks |
| **P6** | System 6.0.8 (`os`, `tools-6`) | done 2026-10-19: `os-6.0.8` baked on the IIci (machine `iici`: colour, 68030, 24-bit; `se` is the SE) from the Legacy CD's 800K set, all four floppies attached as disks: the Installer's Customize, Shift-clicked: System software for any Macintosh, software for all Apple printers, AppleShare (workstation), AppleTalk Responder, 32-Bit QuickDraw (the rest of the list is per-machine and Minimal variants the universal System covers), so Color, Monitors, Portable and the printer drivers are in and the disk boots the SE and the IIci; then the floppies' utilities copied in the Finder to the top of the disk (Apple HD SC Setup, Disk First Aid, Apple File Exchange, Font/DA Mover with Fonts and Desk Accessories, MacroMaker, TeachText; CloseView and Map, System Folder additions, not yet). No BridgeAgent before System 7: `launch`/`open` go through the Finder (names, Command-O), `copy` drags onto the target's icon, a `menu` step picks items by drawn text, shutdown is Special > Shut Down. `tools-6.0.8`: StuffIt Expander 4.0.2 and Disk Copy 4.2 in Utilities; `6.0.8.img` is built on it (the SE and IIci boot cells, the web UI's `se` and `iici` profiles). test_image: both apps on the SE; on the IIci Disk Copy 4.2 says it "will not run on this kind of Macintosh" (test_image now fails apps that refuse) |
| **P7** | Mac OS 8.6 and 9.0.4 (`os`, `tools-8/9`, dev additions) | done 2026-10-18, PowerPC only: `os-8.6` (the 8.6 CD's Mac OS Install run from 7.6.1) and `os-9.0.4` (Mac OS 9 Install, then the 9.0.4 Update, both from the 9 CD, run from 8.6; its version is the PowerPC Enabler's, the System file says 9.0), standard install, Apple driver update off, the Setup Assistant quit at first boot. `tools-8.6`/`tools-9.0.4` add CarbonLib 1.6 (HFS+ self-mounting image, mounted by the guest: media `as_is`), StuffIt Expander 6.0.1 and Disk Copy 6.3.3; `8.6.img` adds DHCP networking (9.0.4 is on DHCP as installed) and MacsBug. `Development-9.img` (shared by `dev-8.6`/`dev-9.0.4`): MPW-GM, MacPerl 5.6.1r2, MacPython-OS9 2.3.5 (needs CarbonLib 1.5+), BBEdit Lite 6.1.2, pinned with macfetch, plus HyperCard, Frontier, ResEdit, Resorcerer, ZTerm, MacsBug, Disk Copy; CodeWarrior Pro 7.2 (2026-10-19: the newest that runs on 9.0.4, needing CarbonLib 1.3.1+; Pro 8 needs 9.2.2): the 7.1 Tools CD's installer and update, then the 7.2 PowerPC compiler and linker plugins by COPY replace, first launch done. test_image: all apps launch on 8.6 and 9.0.4 (kpx). Not yet: 9.2.2 (and with it CodeWarrior 8) |
| P8 | Collections (apps, games) from the existing collection disks' software | later |

## P2 status

As of 2026-10-16:

* **Done:** the `os` bake kind in `bake.py` (installer from a running
  bootstrap System onto a blank "Macintosh HD", then a first boot from it
  alone, `first_boot` steps, the whole volume captured with the bridge's
  files removed and the System file's `vers` checked; manifest as an
  invisible `Snapshot Info`). Media pinned (local): the Legacy Recovery CD
  and the 7.6 Retail CD. BridgeAgent opens a document by launching its app
  with the `odoc` in the launch parameters (older apps such as Apple's
  Installer only take startup documents that way). `guest.py`: clicks
  relative to drawn text (`{"text": ..., "offset": [dx, dy]}`), text
  limited to the front window (`"front": true`), `"until"` repeats.
* **7.6.1** (`bakes/os-7.6.1.json`): baked 2026-10-17, unattended, as
  `baked/os-7.6.1.img` (System file `vers` 7.6.1; boots on the 68k and
  PPC machines). The System module's own Installer (launched plainly: it
  finds the script beside it), Custom Install "Universal system for any
  supported computer", a reboot, the 7.6.1 Update floppies through Disk
  Copy, first boot. BridgeAgent quits itself while an Installer runs
  (`agent_exit`) and the guest reboots after (`reboot`).
* **On top of it:** `disk-copy-6.3.3` and `stuffit-expander-5.5` baked on
  `os-7.6.1` (StuffIt's Standard File dialog by name: `outside_controls`,
  `exact` text), `images/tools-7.6.1.json` built (bases.json
  `tools-7.6.1`), `macsbug-6.6.3` baked on it. BridgeAgent finds an app for
  a creator by catalog search when the desktop database doesn't know it (a
  built System's starts empty). `until` retries a click whose target isn't
  up yet.
* **BridgeAgent dying under Apple's Installer (solved 2026-10-17):**
  system error 3 / "unimplemented trap" in BridgeAgent while or after an
  Installer ran. The cause was the agent's own `_WaitNextEvent` patch,
  whose saved trap address lived at a fixed address in ordinary RAM;
  removed (docs/CommandBridge.md, Bridge v2). A test bake of os-7.6.1 with
  no `agent_exit` and no reboot ran through; the same bake with the old
  agent bombed. The recipes still have their `agent_exit`/`reboot` steps:
  dropping them changes every bake key (a full re-bake), left for the next
  cascade.
* **Disk Copy's licence:** on the bootstrap System, Disk Copy stops at its
  licence on first launch, so the 7.6.1 Update step must click Agree
  before the floppies mount.
* **7.5.5** (`bakes/os-7.5.5.json`): baked 2026-10-17, unattended: the
  16-floppy System 7.5.3 set through Disk Copy, Custom Install "Universal
  System for any Macintosh" (inside the System Software group); the 7.5.5
  Update only updates the startup disk, so it runs at the first boot from
  the new disk, its floppies attached as disks (`first_boot_disks`), and
  ends in its own Restart.
* **7.1** (`bakes/os-7.1.json`): parked 2026-10-17 (the install worked,
  the first boot bombed at the boot-blocks stage with an empty bomb box);
  solved 2026-10-19: the model ID. Our Quadra profile reports 14 (Quadra
  900); Basilisk II's README: Systems before 7.5 may only run with 5 (IIci).
  With `--model-id 5` the installed 7.1 (System Enabler 040 and all) boots
  to the Finder. The bake's `"args"` give every guest `--model-id 5`; bases
  `os-7.1`/`standard-7.1` carry it to what boots them (test_image, bakes on
  it; the web UI's `68k-7.1` profile sets `model_id`). 7.1 has no File
  System Manager, so no Host volume and no BridgeAgent: the first boot runs
  without it (`"first_boot_agent": false`: Finder and menus), as System 6.

### Driving friction

Fixed 2026-10-16: the explore control server is threaded and up before
the guest boots (`--abort` stops even a replay or a long wait: waits check
a cancel flag); `--do` reloads `guest.py` into the running session;
`--port N` fixes the guest's web UI port and every run prints a `watch:`
URL (LAN address, `?view`: the web UI sends no input); each step is posted
to `/api/annotate` and shown over the screen; every wait fails at once on a
system error alert (`bomb.png`). Still open: long waits have no progress
or idle signal (deferred: the idle signal).

## Decisions

* Taken: snapshots only; generic layout; 8.1 skipped; 7.x first; MCL
  dropped from the dev set (PowerPC-only).
* Taken: the bake target stays `Install`: `build.py` rewrites aliases
  into the snapshot to where their targets land (name, creation date,
  path, the new catalog IDs), so the name an installer saw doesn't matter.
* Open: ResEdit and MacsBug in tools or dev only; Resorcerer (supply a serial
  for its first launch, or drop it).

## Known facts the bakes rely on

* Volume menus list disks in a different order on the PPC machine (the
  Host volume second): choose by name (`popup` + `choose`); steps can
  also be limited to a backend.
* Installers that "quit other applications" quit BridgeAgent; it comes
  back after the restart. On the PPC machine the boot phase stays at
  desktop through a restart, so `wait_finder` also waits for the agent's
  heartbeat, and a bridge command the agent doesn't pick up within 15 s
  fails instead of holding the HTTP server.
* Installers that install for the running machine: CodeWarrior (MathLib,
  StdCLibInit on PPC; CFM-68K, NuDragLib on 68k), MacPython (PythonCore).
* Apple Installer and Installer VISE refuse network volumes (Host).
* THINK C 6 and Symantec C++ 7 install into a folder named Development;
  THINK Project Manager asks for a project on every launch.
* Resorcerer 2.4 needs the Appearance Manager (Mac OS 8+); 2.0 doesn't.
* `/api/ui` is the windows of the process running at that tick; the
  Finder's desktop (kind 20, full screen) reports hilited even in the
  background, so `guest.py` only believes a desktop "front window" when it
  repeats.
* Several emulators can bake at once (BridgeAgent installed with a
  private hfsutils `HOME` since 2026-10-16).
