# Executor stragglers — where we are (2026-10-09)

The list agreed after the Finder scripting work. Speech, QuickTime and
networking are punted until later; everything else is in scope.

| # | Item | State |
|---|------|-------|
| 1 | `tell application "Finder"` (aliases, HLE sessions, folder listing) | done — 07dce3db; Finder suite 25/25 on UAE and Executor |
| 2 | Keyboard test, SimpleText | done — 56200715 (test repaired, in ctest), da6ac62b (save: an open file can't be deleted) |
| 3 | Dates in Script Editor ("program error") | done — f6793d47: Script Manager variables, IULDate/TimeString, StringToDate/Time, number formats; matches 7.5.5 |
| 4 | Extension stragglers: Date & Time (menu clock), General Controls (`OSDispatch $5E`, `WriteXPRam`), Find File (`SetGestaltValue`), Apple Guide (`GetFrontProcess`) | done (2026-10-10): Date & Time loads and draws its menu bar clock (`ScrnBitMap`, MenuDispatch -4..-1 as MBDF message 14, `AppPacks` holding the System's PACKs, INITs in the System heap); `GestaltValueDispatch` ($ABF1: New/Replace/Set/DeleteGestaltValue) for Find File; OSDispatch $5E stored (`SetHideDesktopInBackground`, a guess) for General Controls, whose panel opens; `GetFrontProcess` is a plain multiversal entry now, so the status lookup sees it. Apple Guide: see Parked |
| 5 | Redraw leftovers behind Script Editor's windows | done (2026-10-10): `CloseWindow` and a visible `NewWindow` skipped `CalcVisBehind` when nothing was behind in their own layer, so the layers behind (Finder's windows) kept visible regions that excluded the closed window and drew nothing there |
| 6 | `BeginSystemMode` (only a counter) | done (2026-10-10): a resource file opened in system mode goes just above the System file in the opener's chain and is linked there in every other process's chain and the new-process template; it outlives the opener (process exit and launch leave it) and `CloseResFile` takes it out of every chain |
| 7 | Color Picker (`RegisterComponentResourceFile`) | done (2026-10-10): Color Picker 2.0 loads by default and `GetColor` (Color control panel, "Other…") shows Apple's dialog with the HSL picker, as 7.5.5 does. Needed: the System routines at $668 (detached-package call) and $7B0 (selector-table dispatch) as 68k code (`sysroutines.cpp`); `GetResource('PACK')` looked up for real before Executor's ALRT hack; an open resource file answered again for `fsCurPerm`; a file's `thng` resources registered in their order; `ModalDialogMenuSetup` ($AA67) as a no-op. Open: item 17 |
| 8 | Startup Items: Finder's real rule (Executor's Process Manager stands in) | open. Where to look (2026-10-10): on the reference snapshot (`~/storage/snapshots/7.5.5-finder-idle`) Finder's one `LaunchApplication` of the boot is made from its routine at RAM $3F860F4 (`link a6,#-$2FE`), which calls jump-table entry A5+$D8A with (spec, 0, item record, two buffers); the Finder's CODE resources are compressed in the file, so disassemble from the snapshot (`sysroutines`-style, `snapdis`) to find the caller's condition. Under Executor, Finder's `FindFolder('strt')` at startup is only its folder inventory |
| 9 | Desktop DB: icons and rebuild | done (2026-10-10): an HFS image's "Desktop DB" is read whole: icon records (type 1) with their bitmaps from "Desktop DF", application records (2), comments (3, keyed by catalog ID, a Pascal string in the DB itself); bundles only fill in applications the database lacks. Comments set under Executor on an image stay in memory (no B*-tree writes) |
| 10 | Apple Event Manager list wire format | done for the default setup (2026-10-10): with AppleScript loaded, Apple's PACK 8 is the Apple Event Manager and reads its own wire format; `/api/launch` with `open` (an `'odoc'` whose direct parameter is a list of aliases) opens "About System 7.5" in SimpleText. Executor's own reader (`appleevent/AE.cpp`, used without AppleScript) still skips list and record parameters |
| 11 | System heap growth | open |
| 12 | DRVR unit clash | done (2026-10-10): opening a `DRVR` resource first looks for a driver of that name in the unit table, takes the resource ID's unit if free, else the first free unit from 48 on; `UnitNtryCnt` is the table's size (96, as 7.5.5), so extensions find free units themselves (General Controls renumbers and opens `.GCDriver`); `DrvrInstall`/`DrvrRemove` work (a pointer-installed 68k driver dispatches too); `GetNextEvent` calls `SystemTask`, which gives every open driver with `dNeedTime` its `accRun` |
| 13 | ctest port 18108 used twice | done — 56200715 |
| 14 | `command_bridge_executor` flake | open (not seen recently) |
| 15 | Executor's own host files visible in Finder | done — 07dce3db |
| 17 | Color Picker dialog background: green (RGB picker) or yellow-green (HSL) instead of white. The dialog's `dctb` says white and other white areas stay white, so the dialog was erased with a color-table index that the picker's palette (`pltt` 2660/3660, tolerant and animated entries) later changed; a port's cached `bkColor` index is not re-resolved when the device color table changes (`qColorMgr.cpp`, `qPaletteMgr.cpp`) | open |
| 16 | Date & Time control panel: the date fields and the hour draw empty | done (2026-10-10): the panel checks `AppPacks[7]` before each `NumToString`; PACK 4, 5 and 7 are ROM packages, so Executor fills those slots with stand-ins (Apple's header, code = the package's trap) |

Also fixed along the way: Finder type-to-select crash (61ef0717), host
file dates in local time, `ioVLsMod`, folder moves keeping their
contents' paths (07dce3db).

## Parked

### Apple Guide

Allowed by a per-system `extension-policy.txt`, its INIT 128 runs without
harm: `NewGestalt('reno')`, four `GetTrapAddress` probes ($AA6E, which it
later takes with `SetTrapAddress` $A647 as its own dispatcher; InitGraf;
Unimplemented; $03CF), the startup icon, and then it returns without
installing anything (no patches, no `ReplaceGestalt('help')`, no
`NewGestalt('ag_v')`, which its install code at +$21c2 does). Before the
install it looks for its own file by type and creator (`INIT`/`reno`, INIT
+$f28) with `PBGetFInfo` over a folder; what that search sees through the
host-folder System is the first thing to check. The Help menu keeps
Finder's three items. Phase 4 in `inits.yaml`; "as is, last, if ever" in
PLAN.md.

### Heap corruption in the Executor guest suite

`guest_suite_executor` (both cores) crashes at the start of its network
section: MacPerl's heap is corrupt by the time `require Socket` runs, and
the CPU ends up executing data (UAE core: guest pc `$03cd74cc`; Musashi:
pc 0). Facts so far:

- It needs the whole suite: disk + shared folder + audio + the AppleScript
  date/number checks + network. Dropping any one of disk, extfs or audio
  avoids it; so does dropping the AppleScript block. Not reproduced with
  smaller scripts.
- It happens without f6793d47's code too (the AppleScript checks then fail
  but still run), so it is older than the date work; heavy AppleScript use
  in MacPerl exposes it. Before the AppleScript checks were added, the
  suite ran to the end (with the punted network failures only).
- `--debug memcheck` (Executor's heap checker; pass it by adding
  `--debug memcheck` to the args in
  `romlib/config/front-ends/phoenix/executor_host.cpp`, and make
  `mm_fatal` in `mman/mmansubr.cpp` report instead of crash) finds an
  earlier problem: at tick 14 of boot, in Finder's application heap (zone
  `0x3df7bc4`), REL block `0x3e08398`'s handle `0x3df7c6c` doesn't point
  back to it. Whether that is the same bug, or Finder doing something the
  checker doesn't expect, isn't known yet.
- Likely class: host code holding a raw pointer into a relocatable block
  across a call that moves memory.

### 68k Toolbox test app (Retro68)

`tests/test_toolbox_suite.sh` builds Executor's test sources into a Mac
app and runs it on UAE (ground truth) or Executor. Infrastructure works;
the app crashes depending on code layout, and on Executor another Retro68
app's segment-loader patches are active when it launches. Details in the
script's header. Not in ctest.

## Environment

Executor on the UAE core needs `vm.mmap_min_addr=0`; it is kept by
`/etc/sysctl.d/99-mac-phoenix.conf` (another project's script runs
`sysctl --system`, which reset it to Ubuntu's 65536).

The reference snapshot `~/storage/snapshots/7.5.5-finder-idle` (UAE,
`--trace-atraps`, Finder idle on the 7.5.5 test image) was taken again on
2026-10-10; `tools/macdecode` reads it. The macdecode venv is
`~/.venvs/macdecode` (capstone, rsrcfork); a linear-sweep 68k
disassembler over a resource or a snapshot is a few lines on capstone with
the A-line words named from `base/trapname.cpp`.
