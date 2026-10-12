# Executor stragglers — where we are (2026-10-11)

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
| 8 | Startup Items: Finder's real rule (Executor's Process Manager stands in) | open; the rule is known (2026-10-12), the stand-in stays. **Finder's rule** (7.5.5 Finder, resident CODE 1 +$BDC, called from its init right after `SetResFileAttrs`): `OSEventAvail(0, &evt)`, then `gOpenStartupItems = (LM $B20 & $20) && !(evt.modifiers & shiftKey)`; the byte at $B20 is $32 on the real boot and $FF under Executor, so both pass, and the flag (A5-$9B6, read by the stub at A5+$9BA) is 1 in both idle snapshots. On the `'oapp'` Apple event (handler in the segment at table index $600: `'----'` absent, event ID `'oapp'`) the Finder posts its internal command `'opst'` to its command queue (A5-$E74, through A5+$1892); the command dispatcher (segment $1B60, a virtual method reached through CODE 3's thunk at +$1422) handles `'opst'` by finding the Startup Items folder object (`FindFolder(vRefNum, 'strt')` via A823 in CODE 2 +$10D0) and opening its items with `'sope'`; the launch itself is CODE segment $1BE0 (`_LaunchApplication` at +$F6/+$214) after a zoom-rectangle animation. **Under Executor** the `'oapp'` arrives (one `WaitNextEvent` returns what=23; Apple's PACK 8 does `AcceptHighLevelEvent` and `GetProcessSerialNumberFromPortName`), but no `FindFolder('strt')` ever follows, so the `'opst'` handler never runs: either PACK 8 does not reach the Finder's handler for this event, or the posted command is never drained. Not visible in the trap log because the Finder's Apple Event calls go to Apple's PACK 8 (not Executor's code) and much else through `GetTrapAddress`'d pointers. Next step: a `--watch` on the command list at `(A5-$E74)->$18` under Executor (A5 was $3E241BC in `~/storage/snapshots/executor-finder-nostartup`), or an Executor-side log in PACK 8's handler dispatch. **Side findings**: the Network Extension is a Finder extension (`fext`) whose own copy of the open-startup-items routine (the `$3F860F4` of the old note, in its segment at file offset $1E00, reached through its thunk table entry 2) is what the real boot's `atraps.tsv` caught; it is not the trigger (the ROM machine launches Startup Items with the file removed) and it aborts Executor with unimplemented trap $A08E when allowed, so it stays denied. Finder 7.5.5 keeps its code as Apple-compressed blobs read by file offset through its own segment loader into 59 slots of 1854 bytes (loader at A5+$5BA/$5B2, table at A5-$40); `tools/macdecode`'s resource parser decompresses them correctly. Fixed on the way: Executor answered `PBDTOpenInform` with "just created" for an HFS volume whose `Desktop DB` it had read, so the Finder rebuilt the mounted image's desktop database on every boot (progress bar, every file visited); it now reports it as existing |
| 9 | Desktop DB: icons and rebuild | done (2026-10-10): an HFS image's "Desktop DB" is read whole: icon records (type 1) with their bitmaps from "Desktop DF", application records (2), comments (3, keyed by catalog ID, a Pascal string in the DB itself); bundles only fill in applications the database lacks. Comments set under Executor on an image stay in memory (no B*-tree writes) |
| 10 | Apple Event Manager list wire format | done for the default setup (2026-10-10): with AppleScript loaded, Apple's PACK 8 is the Apple Event Manager and reads its own wire format; `/api/launch` with `open` (an `'odoc'` whose direct parameter is a list of aliases) opens "About System 7.5" in SimpleText. Executor's own reader (`appleevent/AE.cpp`, used without AppleScript) still skips list and record parameters |
| 11 | System heap growth | open |
| 12 | DRVR unit clash | done (2026-10-10): opening a `DRVR` resource first looks for a driver of that name in the unit table, takes the resource ID's unit if free, else the first free unit from 48 on; `UnitNtryCnt` is the table's size (96, as 7.5.5), so extensions find free units themselves (General Controls renumbers and opens `.GCDriver`); `DrvrInstall`/`DrvrRemove` work (a pointer-installed 68k driver dispatches too); `GetNextEvent` calls `SystemTask`, which gives every open driver with `dNeedTime` its `accRun` |
| 13 | ctest port 18108 used twice | done — 56200715 |
| 14 | `command_bridge_executor` flake | open (not seen recently) |
| 15 | Executor's own host files visible in Finder | done — 07dce3db |
| 17 | Color Picker dialog background: green (RGB picker) or yellow-green (HSL) instead of white. The dialog's `dctb` says white and other white areas stay white, so the dialog was erased with a color-table index that the picker's palette (`pltt` 2660/3660, tolerant and animated entries) later changed; a port's cached `bkColor` index is not re-resolved when the device color table changes (`qColorMgr.cpp`, `qPaletteMgr.cpp`) | open |
| 18 | Balloon for another process's window names the front process ("This window belongs to the application "Finder"" over BridgeAgent's window): Apple's Help Manager finds the window's layer (`GetWindowLayer`) and asks the Process Manager whose it is with a selector Executor lacks, then falls back to the current process | open |
| 19 | After a balloon over a desktop icon goes away, the icon's label is missing its left part until the Finder next redraws the desktop (the next balloon or window change brings it back; 7.5.5 shows it whole at once): the desktop update Executor hands the Finder for the uncovered region is redrawn clipped to it while the Finder erases the label's whole rectangle. Transient | open |
| 16 | Date & Time control panel: the date fields and the hour draw empty | done (2026-10-10): the panel checks `AppPacks[7]` before each `NumToString`; PACK 4, 5 and 7 are ROM packages, so Executor fills those slots with stand-ins (Apple's header, code = the package's trap) |
| 20 | Executor on the Musashi core aborts at boot (2026-10-10, main after the Help Manager/Speech Manager commits cb911107, d517963b): right after "Help Manager: the System's PACK 14", `syn68k: host pointer 0x800 is not guest-addressable (outside every window)` and `abort()` from `syn68k_common.cpp`. 0x800 is a guest address handed over as a host pointer; on the UAE core guest and host addresses are the same, so the new code (written after the syn68k facade, assuming UAE addressing) runs there and only the Musashi core's window check catches it. Takes down the four `*_executor_musashi` ctest cells (command bridge, guest suite, Finder suite, keyboard) | done (2026-10-11): `gestalt.cpp`'s static table took `(intptr_t)tooltraptable` and `ostraptable` at static initialization, before `traps::init` had set them (null since 14f6aa98 moved the tables into guest memory), so the first `Gestalt` call ran `US_TO_SYN68K` over the host "pointers" 0, 0 and 0x800 (`tooltraptable + 0x200` entries). On UAE that passed and answered `gestaltOSTable`/`gestaltToolboxTable`/`gestaltExtToolboxTable` as 0/0/0x800; on the window layouts it aborted. The three selectors are the fixed guest addresses now ($400, $E00, $1600) and the patch step is gone. Command bridge, Finder suite and keyboard pass on `--core musashi`; `guest_suite_executor_musashi` stops where the UAE cell does (the parked heap corruption) |
| 21 | `guest_suite_musashi` (ROM Mac, `--core musashi`) flakes: `/api/script` is acknowledged, but MacPerl never comes to the front (`/api/app` stays Finder) and the results file never appears, so the suite times out. Failed in both full ctest runs on 2026-10-10 and 1 of 6 manual runs (`tests/test_guest_suite.sh --core musashi`, with and without `--no-idlewait`; the passing runs finish in 7–20 s). Not seen on the UAE core. Same shape as item 14 | moot (2026-10-11): the vendored core is retired and the cell is `guest_suite_mame68k` now; reopen if the flake shows there |
| 22 | Executor on mame-68k: MacPerl dies right after launch with `Fatal error: unhandled trap 11 at pc 0xC0000000 (FPU)`: a line-F word the core traps on (on a 68040, the unimplemented FPU instructions go to the FPSP that the ROM installs; Executor has none), so `guest_suite_executor_mame68k` gets no results. The UAE core implements those instructions. Seen 2026-10-11, older than the MoveHHi fix | done (2026-10-11), two things. The facade asked for a 68040 without an FPU, which on mame-68k is an LC040 (every line-F word a trap 11), while UAE's 68040 opcode table has the FPU instructions whatever the flag says; the core has its FPU now (Gestalt still answers no FPU, as Executor always has). Behind that, `callcomp` (device.cpp) ran a driver call's completion routine at its *host* pointer truncated to 32 bits, right only on UAE's identity layout: MacPerl's first asynchronous MacTCP `_Control` never completed and it sat in its event loop with two menus up (the trap 11 at `0xC0000000` was the quit path's). Now runs the whole suite and stops where UAE does (`as_date_time_string`, item 23; the MacTCP gaps). `--executor-logtraps` no longer aborts on a parameter block's uninitialised string pointer (the logger converted it back before validating it) |
| 23 | `as_date_time_string` in the guest suite: `(date "Friday, October 9, 2026 4:11:52 PM") as string` answers today's date with that time, so a date with a weekday and a time loses its date part. Fails the same way before the 2026-10-11 work | done (2026-10-12): every date string with a time lost its date, weekday or not. AppleScript parses a literal with `StringToDate` and then `StringToTime`, both over the whole text and each into its own record, and takes a time whose status has no `leftOverChars` as today's at that time; Executor's `StringToTime` only looked for text after the time, so the date before it counted as consumed. Both parsers now report text on either side of their part as left over, and `StringToDate` skips a time that comes first (`4:11 PM October 9, 2026`). `guest_suite_executor` fails on the MacTCP gaps (M3h: `dns_resolve`, `udp_send`) alone; gtests `DateTime.TimeAlone`, `DateTime.TimeThenDate` |

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

### ~~Heap corruption in the Executor guest suite~~ (fixed 2026-10-11)

`guest_suite_executor` (every 68k core) crashed after its AppleScript
section with MacPerl executing data. It was not corruption. Executor's
`MoveHHi` was a no-op, so the code segments MacPerl moves high and locks
stayed wherever they were allocated and split its heap: a 64 KB
`NewHandle` failed with 241 KB free in 32 pieces (largest 52 KB), the
only failed allocation of the run. MacPerl's out-of-memory exit calls its
exit hook, which calls exit again once no script is running; that
recursion ran the stack down through all of memory to address 0, and
the snapshots taken afterwards looked randomly corrupt (the `$AAAAAAAA`
longs and the bad exit-path operands were the runaway stack's damage).
On a real 7.5.5 (`MACTEST_SNAPSHOT=... tests/test_guest_suite.sh` on the
ROM machine) the same heap keeps its segments together and at the top
(CODE 4, 15 and 22 in the last 2%), and the 64 KB handle is there.

Fixed by `ROMlib_movehhi` (`mman/mmansubr.cpp`): MoveHHi moves the block
up until a nonrelocatable or locked block or the top of the zone, the
unlocked relocatable blocks in between sliding down under it; and
`C_LoadSeg` (`segment.cpp`) moves a segment that isn't locked already
high before `HLock`, as 7.5.5's Segment Loader does. On the UAE core the
suite now gets through to the network section, where the punted
networking fails (MacTCP `dnrp`, UDP send, the TCP connect timeout); one
64 KB `NewHandle` still fails there (MacPerl copes); at that point
Executor's heap holds four more nonrelocatable blocks (75, 1.67 MB) than
the real one does at the end of the run (71, 1.51 MB). The mame-68k cell
does the same since item 22.
Side fixes from the hunt: ReallocateHandle frees before it allocates
(31ffbc00).

How it was found, for next time:

- `--executor-debug memcheck` checks every zone at every Memory Manager
  call and reports once (trap, last passing check, master pointer value,
  68k context, host stack), then switches off. Two Finder habits are
  known and only warned about: a locked block whose master pointer it
  points at SysZone for a while (`movea.l $A78,a1; movea.l 4(a1),a1;
  move.l a0,(a1)`, the real 7.5.5 does the same at `$1C0FBA`), and
  `TheZone` set to a temp handle around `ReallocHandle`. Clean from boot
  to the crash, it ruled out heap corruption.
- `--watch ADDR[:LEN]` (`src/common/guest_watch.cpp`) reports every write
  to guest memory with the 68k context; it showed MacPerl's
  script-running flag cleared just before the fatal allocation.
- A failed `NewHandle`/`NewPtr` prints the zone: free bytes, largest
  free block, a heap map (`N` nonrelocatable, `L` locked, `R`, `P`, `.`),
  every locked block and free gap with its owner, and the sizes of the
  nonrelocatable blocks. `--executor-debug allocs` logs every allocation
  of 4 KB or more with the code that asked (`mman/whatis.cpp` names a
  guest address: zone, block, resource type/ID/file).
- `MACTEST_SNAPSHOT=NAME tests/test_guest_suite.sh` snapshots guest RAM
  when the results arrive (ROM machine too), for `tools/macdecode`.

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

`--trace-atraps` works for the Executor backend too (`/api/snapshot`
then writes `atraps.tsv` with one row per distinct trap, caller PC and
parent, first `seq` and a count); `--executor-logtraps-nesting N` makes
`--executor-logtraps` log calls nested in callbacks (UPP calls from C++
are not traps, so they show only through what they call). In a UAE
trace, `A072` (`DoVBLTask`) rows are the VBL interrupt firing inside
whatever trap is named in their parent column.

The reference snapshot `~/storage/snapshots/7.5.5-finder-idle` (UAE,
`--trace-atraps`, Finder idle on the 7.5.5 test image) was taken again on
2026-10-10; `tools/macdecode` reads it. The macdecode venv is
`~/.venvs/macdecode` (capstone, rsrcfork); a linear-sweep 68k
disassembler over a resource or a snapshot is a few lines on capstone with
the A-line words named from `base/trapname.cpp`.
