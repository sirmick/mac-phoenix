# Executor core in MacPhoenix

Goal: run the real Finder on a Toolbox reimplemented in C++, inside the
MacPhoenix shell (web UI, WebRTC video, HTTP API, bridge), on the UAE
68k JIT instead of syn68k.

Source: Executor 2000 (autc04/executor), MIT licence. Imported from
upstream commit `b2569eb1` into `src/executor/`. The upstream checkout in
`executor/` stays outside git as the reference and differential oracle.

## Shape

```
mac-phoenix (parent: web UI, HTTP, WebRTC, bridge)
   │ exec `mac-phoenix --ipc --backend executor` + SHM/socket
   ▼
mac-phoenix (child, same binary)
   ├── host/executor_child.cpp   frames → SHM, control socket → input hooks
   ├── romlib                    Executor Toolbox, imported nearly unchanged
   ├── syn68k facade             syn68k_public.h implemented over UAE
   ├── PowerCore facade          stub now, KPX later
   └── front-ends/phoenix        render + cursor composite, executor_host API
```

The swap happens behind two facades so the 120k lines of Toolbox code
compile with minimal edits:

* **syn68k facade** (`src/executor/cpu/`). Same header name and API:
  `cpu_state`, `EM_Dn/EM_An`, `READ*/WRITE*`, `callback_install`,
  `trap_install_handler`, `interrupt_generate`, `CALL_EMULATOR`,
  `destroy_blocks`. Implemented on UAE:
  * **Addressing is identity.** Guest address == host address. Guest
    RAM is mapped at host 0 (needs `vm.mmap_min_addr=0`, same as KPX),
    the binary links below 4GB like `mac-phoenix`, the emulator thread
    stack and framebuffer are allocated below 4GB. UAE runs
    `DIRECT_ADDRESSING` with `MEMBaseDiff = 0`, which keeps the JIT
    usable. Executor's four 1GB windows collapse into one.
  * **Callbacks** are 2-byte cells in a guest-visible callback page,
    each holding one EmulOp opcode. The handler finds the slot from the
    PC, syncs UAE registers into `cpu_state`, calls the host function,
    syncs back and jumps to the returned address.
  * **A-line traps** use the real exception: vector 10 at `$28` points
    at a callback, which reads the trap PC from the 68040 frame and calls
    Executor's `alinehandler`. The guest runs in supervisor mode, as
    Mac OS does, so the frame lands on A7.
  * **Interrupts**: `interrupt_generate(4)` raises a UAE level-4
    interrupt; vector 28 points at a callback; `MAGIC_RTE_ADDRESS` is a
    real `RTE` in the callback page.
  * **Nested execution**: `CALL_EMULATOR` pushes the exit address (an
    EmulOp cell) and re-enters `m68k_execute`, the same path
    BasiliskII's `Execute68k` uses.
  * `destroy_blocks` flushes the JIT cache range.
* **PowerCore facade**. PowerCore has no licence and does not come
  across. A stub header keeps the PPC code paths compiling; executing
  PPC aborts with a clear message. KPX later implements this facade.

Dropped from the import: syn68k, PowerCore, cxmon debugger, the
qt/x/sdl/sdl2/wayland front ends, LMDB (its CNID mapper was already
disabled), the bundled Browser app stays as a resource for now.

Memory-map vocabulary for M1: [MEMORY_MAP.md](MEMORY_MAP.md).

**M1 approach (decided 2026-10-07):** no ROM. Executor's C++ builds a
System memory area that looks identical to a real one — vectors,
lowmem, trap tables at `$400`/`$E00`, System heap holding Apple System
file resources, Process Manager heap, A5 worlds. Snapshots of real
7.5.5 boots (`POST /api/snapshot`, `tools/macdecode/macdecode.py`) are the
reference; nothing from them is copied in at runtime, and no Apple
boot or `ptch`/`lpch` code runs.

**Known incompatibility (test case):** WorldBuilder's *Demo World*
(games.img, 1987) corrupts its app heap between `LoadSeg` and its first
`MoreMasters`; Executor then hits a fatal heap error. Same on upstream
Executor/syn68k, so not the UAE facade. Likely direct heap/lowmem
pokes by a pre-32-bit-clean app; revisit with the M1 heap work.

**Decoder:** `tools/macdecode/macdecode.py SNAPSHOT [--serve]` decodes a
snapshot using multiversal (`src/executor/multiversal/defs/*.yaml`) as
its database: lowmem globals by multiversal name and type, zones
(including the Process Manager heap and app partitions, found by
scanning), resource maps, and trap names. `--serve` (port 8765) shows the
decode next to multiversal as HTML and reloads when a YAML file changes.
Multiversal stays untouched for now: what we learn goes in the overlay
`tools/macdecode/learned.yaml` (lowmem names/types and trap names, each
with a status: known, guess or placeholder), which the decoder layers
on top. Unnamed nonzero lowmem becomes placeholder entries for it. First
entry: `$2B6` is `ExpandMem` (multiversal calls it `AE_info`).

Origins: the decoder matches the System heap against the resources of
the boot disk's System Folder (hfsutils), so trap entries and heap
addresses say where their code came from — ROM, `System 'lpch' 31`,
`'ptch'`, `'scod'` (Process Manager), `'gpch'`, or an INIT by file.
On 7.5.5 at Finder idle: 595 entries in ROM, 350 of 464 patched entries
traced, 114 unknown. Linked patches are relocated and compacted by the
loader, so a masked match (address fields as wildcards) backs up the
exact one.

**Diff:** the Executor child takes snapshots too (at its event loop,
`/api/snapshot`), and `macdecode.py REF --diff CANDIDATE` (or
`--serve --diff`, page `/diff`) scores Executor's memory against a real
boot. Baseline, 7.5.5 Finder idle vs Executor's Browser idle: lowmem 76
same + 19 same shape of 247 (100 differ, 52 unset); System resources
loaded 9 of 78; trap tables at `$400`/`$E00` 5 of 1059; no Process
Manager heap (one 61 MB ApplZone instead).

**Next (agreed 2026-10-07):** Executor sideloads Apple's System file and
its C++ builds the memory a real boot leaves, step by step, each checked
with the diff: (1) System heap contents and lowmem setup from Apple's
System file, with a mapping file choosing Apple's 68k code or
Executor's C++ per code resource; (2) our own Process Manager in C++
with Apple's layout (PM heap up to BufPtr, SIZE-sized partitions, A5
world at the top, per-process lowmem swap); (3) trap tables at
`$400`/`$E00` once extensions need to patch.

**Step 1 started:** `tools/macdecode/sideload_system.py IMAGE DIR` puts
Apple's System file into an Executor data directory, and
`--executor-data DIR` (config `executor_data_dir`) points Executor at it.
Apple's 68k definition procedures would replace Executor's native stubs
(Apple's MBDF calls unimplemented `$A81E` and recursed into a crash), so
a resource policy (`src/executor/res/resource-policy.txt`, embedded;
`<data_dir>/resource-policy.txt` overrides) serves Executor's version of
listed System resources from `ROMlib_mgetres2`; the map stays Apple's.
Executor's Browser now runs on Apple's 7.5.5 System file; System
resources loaded 21 of 78 (was 9). The rest come from components we
don't have yet: Process Manager (`scod`, `proc`, `lstr`, `lmem`),
Script Manager (`itl0-2`, `KSWP`), `PACK` 6/14, `dcmp` 0. The
Process Manager's per-process lowmem list is `lmem` -16458 (see
MEMORY_MAP.md): the spec for our context switch.

**Step 2, slice 1 (one process, real layout):** `ROMlib_InitZones`
makes the region above the System heap the Process Manager heap
(`BufPtr` at its top; the boot stack sits above `BufPtr`). At launch,
`process_layout_partition` peeks the app's SIZE and CODE 0, allocates a
locked partition (preferred + 16K) at the top of the PM heap, and lays
out `ApplZone`, `ApplLimit`, the stack, the A5 world and `MemTop` inside
it as 7.5.5 does (MEMORY_MAP.md, *Application partitions*); the app's
resource map then opens inside its heap. Diff against the Finder-idle
reference: lowmem same shape 19 → 33, differs 100 → 87; zones now show
a PM heap holding `ApplZone:Browser`. Slices next: process records and
the `lmem` swap (2), per-process host stacks and switching (3).

**What Finder needs (trace):** `mac-phoenix --trace-atraps` (UAE) records
every A-trap call site as a routine: trap, dispatcher selector
(`tools/macdecode/dispatch.py`: multiversal + Universal Interfaces glue +
`learned.yaml`), component instance or driver + csCode, the enclosing
routine, and the resource holding the caller. `tools/macdecode/atraps.py`
and the web view's `/needs` page classify each routine: needed if code
Executor runs calls it (Finder, System defprocs not in the resource
policy, optionally extensions), then done / basilisk (host code in the
Basilisk II core) / todo; struck out if only Apple ROM/patch code,
hardware/boot or replaced System code calls it. Compressed resources are
decompressed with `rsrcfork` (venv at `~/.venvs/macdecode`). 7.5.5 to
Finder: 16 todo, 2 basilisk (`.Disk` status/control), 171 done.
The tracer also records every trap install (`trap_installs.tsv`: slot,
old/new address, installer and target resources), shown as each slot's
patch history on `/traps`. `/entrypoints` lists all 4098 known entry
points (traps, selectors, drivers, components) with checkbox filters;
the work list (needed, not in Executor) is 28. Hardware/boot/debug tags
live in `learned.yaml` (`tags:`) for review.

**Where we left off:** snapshot API and a first reader (lowmem, heap
walk, trap classification) work; reference snapshot
`~/storage/snapshots/7.5.5-finder-idle`. Next: find the Process
Manager heap and partitions, tag System heap blocks with resource
type/ID, disassemble via Retro68 objdump, snapshot the Executor child,
then `diff-world`.

## Milestones

| | Milestone | Gate | Status |
|---|---|---|---|
| M0a | Core builds in-tree on the UAE facade | `mac-phoenix-executor` links | done |
| M0b | CPU parity | Executor's gtest suite on UAE matches syn68k | done: same two upstream failures, PPC tests skipped |
| M0c | Headless app run | built-in Browser reaches its event loop, screenshot | done: `executor.smoke.browser_screenshot` |
| M0d | Shell integration | `--backend executor` in the web UI shows video, takes input | done: one binary, UI option, `executor.smoke.web_backend` |
| M1 | Finder desktop | our boot phase, Apple System file as resource root, real Finder draws |
| M2 | Launch apps | Process Manager ours; Finder launches SimpleText, Kid Pix |
| M3 | Borrowed managers | QuickTime 1.6 Component Manager, Thread Manager, AE 1.0.1 |
| M4 | 7.5.5 + Drag Manager | |
| P | PPC | KPX behind the PowerCore facade, InterfaceLib to native |

Tools carried alongside:

* **Differential trap logs**: multiversal-driven logger in MacPhoenix
  (real ROM) versus Executor `--logtraps`.
* **Fault-on-ROM probe**: a 1MB faulting ROM range to enumerate every
  ROM address software reads.
* **Dual-mode tests**: Executor's Retro68 tests also run under real
  System 7 in MacPhoenix.

## Open questions

* Open Transport: parked. MacTCP plus a `dnrp` resolver first.
* Boot phase: reproduce the post-boot-2 state from a snapshot of real
  System 7.1 under MacPhoenix.

## Working notes

* Run: pick **Executor** as Emulator Mode or CPU in the web UI, or
  `./build/mac-phoenix --backend executor`. No ROM. Needs `vm.mmap_min_addr=0`.
* Executor's files live in `<storage>/executor/` (System Folder, prefs).
  That folder and the configured shared folder are the only host folders the
  guest sees; Executor's default of mounting `/` is disabled. Disk images
  from the config mount read-only.
* Status: `boot_phase` goes `Finder` at the first frame and `desktop` once
  the application first polls for events. `/api/app` reads `CurApName`.
* Input arrives on the IPC control socket and goes to Executor's EventSink
  through `IPCInputHooks`; the cursor is drawn into the frame by the front
  end, since Executor leaves cursors to the host.
* Executor menus are "sticky": releasing on a menu title leaves the menu
  open for a second click.
* Tests: `ctest --test-dir build -L executor`.
* Callbacks redirect PC by setting `next - 2` because UAE advances PC by 2
  after an EmulOp returns.
* UAE routes every memory access through `g_platform.mem_*` function
  pointers; the facade installs identity versions. Inlining them is an easy
  speed win. Interrupt delivery goes through `g_platform.m68k_intlev` and
  `m68k_poll_interrupts`.
* JIT is not enabled yet: every guest run is a nested call from host code,
  which uses UAE's interpreter loop.
* Never call `setenv` in the child once threads exist: Executor's path
  settings go through `ROMlib_path_overrides` instead.

## Changes to imported Executor code

Kept small so upstream fixes can be merged by hand:

* `mman/mman.cpp`: guest RAM via `syn68k_map_guest_ram()`; window setup removed.
* `vdriver/vdriver.cpp`: framebuffer via `syn68k_alloc_low()`.
* `base/logging.cpp`: pointer validity via `syn68k_is_guest_mapped()`.
* `main.cpp`: entry renamed `executor_main_entry`, low-stack emulator
  thread, no cxmon, phoenix front end.
* `file/fileMisc.cpp`: `ROMlib_path_overrides` before `getenv`.
* `file/localvolume/localvolume.cpp`: `ROMlib_local_volume_roots` instead of `/`.
* `hfs/hfsHelper.cpp`: `ROMlib_readonly_images`.
* `toolevent.cpp`: `ROMlib_app_polled_events` set by Get/WaitNextEvent.
* `mman/mmansubr.cpp`: `ROMlib_heap_death_dialog`; off in MacPhoenix, so a
  fatal heap error logs and `_exit(70)`s instead of crashing in the dialog.
* `file/localvolume/localvolume.cpp`: LMDB include dropped.
* `executor.cpp`: launches `FNDR` files (Finder) as well as `APPL`/`MPST`.
* `alias.cpp`: `FindFolder` with `kCreateFolder` creates the folder (was a
  `paramErr` stub).
* `res/resGetinfo.cpp`: `GetResourceSizeOnDisk` returns the on-disk length
  (compressed size), not the loaded handle's size.
* `mman/tempmem.cpp`: temporary memory comes from the Process Manager heap.
* `file/localvolume/item.cpp`: Mac/Unix epoch counts 1904's leap day
  (host file dates were a day early).
* `toolevent.cpp`: no `/tmp/testvol` probe (upstream posted its mount
  result as a `diskEvt` on the first `GetNextEvent`).
* `drag.cpp` (new): private `DragDispatch` selector Finder needs.
* `process.cpp`: private `OSDispatch` selectors Finder needs ($14 desk
  layer, $31/$32 Apple menu items, $42, $47, $55; names are guesses, see
  `tools/macdecode/learned.yaml`); every application gets an initialised
  scrap.
* `scrap.cpp`: `ScrapName` points at `ScrapTag` ($970) in low memory.
* `mman/mman.cpp`: `HandleZone` places an empty handle whose master pointer
  is in the Process Manager heap in that heap (temp memory).
* `gestalt.cpp`: `gestaltOSAttr` on 68k too, with bit 10.
* `iu.cpp`: Type Select (`Pack6` $28-$2E).
* `icon.cpp`: private `IconDispatch` $218/$220.
* `file/localvolume`: folders keep Finder info in `.finf` (Basilisk II
  layout) instead of failing `PBSetCatInfo` with `paramErr`.
* `base/traps.impl.h`: an unknown selector snapshots (fatal hook) before
  aborting.
* `file/localvolume/localvolume.cpp`: positioning uses `ioPosMode & 3`
  (flag bits such as `noCacheBit` were read as an unknown mode).
* `error/error.cpp`: `ROMlib_fatal_hook`, called first on a fatal error (the
  child snapshots RAM and registers as `executor-fatal`).
* `res/resPrivate.cpp` (new): private `ResourceDispatch` selectors;
  multiversal `ResourceDispatch` selector mask `D0<0xFF>`.
* `config/front-ends/phoenix/executor_host.cpp`: the data folder (and the
  shared folders) are the volumes, so the boot volume's root holds the
  System Folder; volumes are named after their folder.
* `mman/mman.cpp`: `HandleZone` keeps an empty handle in `TheZone` when that
  zone (nested in the Process Manager heap) holds its master pointer.
* `icon.cpp`: `GetLabel` takes nil outputs and label 0; Icon Utilities
  rewritten around one icon source (suite, cache, `PlotIconMethod` getter):
  alignment, label/selected/open/offline/disabled transforms, regions, hit
  tests, icon caches, `ForEachIconDo`; suite layout in `rsys/icon.h`;
  multiversal `IconGetterUPP`/`IconActionUPP` are typed callbacks.
* `alias.cpp`: `FindFolder` desktop/trash/temporary folders are "Desktop
  Folder", "Trash" and "Temporary Items" at the volume root (Apple's Folder
  Manager names), created invisible; were host `/tmp`.
* `finder.cpp`: the Desktop Manager (`PBDT*`), one database per volume, kept
  in `.desktopdb` at a host volume's root; host volumes report
  `bHasDesktopMgr` (`hfs/hfsXbar.cpp` `PBHGetVolParms`).
* `hostdisk.cpp` (new): `.Disk` driver (unit -63, as Basilisk II) and a
  fixed drive per host volume: driver gestalt, drive info, drive icon;
  multiversal `NDEVICES` 96 (the real unit table's size).
* `quickdraw/qIMVI.cpp`: `BitMapToRegion` builds the region in host memory
  and sizes the handle once (it wrote past a handle it failed to grow).
* `res/resInit.cpp`: the Fonts folder's suitcases are opened and chained
  below the System file (System 7.1+); `launch.cpp` keeps the System file
  and everything below it open across launches (it closed every file but
  the System file).
* `quickdraw/font.cpp`: a family whose first FOND points at a missing face
  (Chicago 12 is `FONT` 12 in a real ROM) tries the same family's FOND in
  the other open files.
* `menu/sysmenu.cpp` (new): the Help and Application system menus, kept at
  the end of the menu list with icon-suite titles; `menu.cpp` inserts
  application menus before them and re-adds them after `ClearMenuBar`/
  `SetMenuBar`; `stdmbdf.cpp` right-aligns and draws them, hit-tests by
  title width; `balloon.cpp` `HMGetHelpMenuHandle` returns the Help menu;
  `stdmdef.cpp` key equivalent $1C means a script code, not an icon.
* multiversal `umacdriver` starts like a `DRVR` header (name at 18).
* `gestalt.cpp`: `dply` 7, `dplv` $00020006, `hdwr`, `vm  ` 0, as on a real
  7.5.5 boot.
* `quickdraw/displays.cpp`: the Display Manager calls Finder makes (first/
  next screen device, draw desktop rect/region, desk region, notify procs,
  private -6/-9/-10 as 7.5.5 implements them); multiversal
  `DisplayDispatch` and `DialogDispatch` select on D0's low byte.
* `dial/dialDispatch.cpp`: `DialogDispatch` 7 (`IsCancelEvent`, a guess) and
  8 (`CheckEventQueueForUserCancel`).
* `appleevent/AE_hdlr.cpp`: ExpandMem+$1AE (System heap reserve) is 64K;
  multiversal `AE_info_t` names it.
* `file/localvolume/localvolume.cpp`: host volumes report a 2 GB HFS volume
  holding the folder's real contents (capped by host free space) and its
  file/folder counts (Finder only rebuilds the desktop database of a volume
  with files), and bless their System Folder (`vcbFndrInfo[0]`).
* `menu/sysmenu.cpp`, `process.cpp`: the Apple menu from Finder's Apple Menu
  Items list (OSDispatch $31/$32, 22-byte $31); `AppendResMenu('DRVR')`
  with Apple's System file marks the Apple menu instead of listing desk
  accessories and Executor's items; `MenuSelect`/`MenuKey` bring it up to
  date; `stdmdef.cpp` draws each entry's small icon (18-pixel rows) and
  sizes items as 7.5.5 does (10-pixel margin, command-key room only with a
  key).
* `menu/sysmenu.cpp`, `desk.cpp`: `OpenDeskAcc` of an Apple Menu Items entry
  posts its owner the raw 28-byte 'aevt'/'amis' event (key parameter) the
  7.5.5 Process Manager sends; Finder parses it itself and opens the item.
* `osevent/hle.cpp`, `process.cpp`: `AcceptHighLevelEvent` fills in the
  sender (the current process's port, no location; Finder drops events
  from other machines); `GetProcessSerialNumberFromPortName` and
  `GetPortNameFromProcessSerialNumber` for the current process.
* `version.cpp`: with Apple's System file (it has `lpch`), the system
  version comes from its `vers` 1 and its resources are left alone
  (`ROMlib_apple_system_file`); `gestalt.cpp` `mach` 20 as on the
  reference boot.
* `tests/`: low-stack test thread, PPC tests skip, fixture path, ctest names.
