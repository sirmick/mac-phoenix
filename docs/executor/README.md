# Executor in MacPhoenix

Executor runs classic Mac applications, 68k and PowerPC, on a
reimplementation of the Mac Toolbox in C++, without a Mac ROM. MacPhoenix
took [Executor 2000](https://github.com/autc04/executor)'s Toolbox
("romlib"), put it on MAME's CPU cores and its own front ends, and has
since grown it to run Apple's System 7.5.5, 7.6.1 and Mac OS 8.6 System
Folders and Finder (9.0.4 under way) as well as its own System.

This page is the technical index: where the code came from, what
MacPhoenix changed, its licence, and every document here. Running and
building it (the desktop application, the tab and its server,
`mac-phoenix --backend executor`): [executor/README.md](../../executor/README.md).

## Origin

* **Upstream:** [autc04/executor](https://github.com/autc04/executor),
  Wolfgang Thaller's revival of ARDI's Executor (Abacus Research &
  Development, 1986–2004; MIT since 2008).
* **Imported:** upstream commit `b2569eb1` (2026-10-06), its multiversal
  submodule at `1594ced0`, into `src/executor/`; since 2026-11-06 in
  `executor/`, the product's own top-level directory (what lives where:
  [executor/README.md](../../executor/README.md)).
* **Reference checkout:** `executor-upstream/` at the repository root
  (gitignored), for comparisons and as a differential oracle.
  `tools/executor-upstream-diff.sh` clones it if missing (or
  `EXECUTOR_UPSTREAM=DIR`).

| Upstream | MacPhoenix |
|---|---|
| `src/` | `executor/core/romlib/` |
| `multiversal/` (submodule) | `executor/core/multiversal/` (vendored) |
| `tests/` | `executor/tests/` |
| `syn68k/` (submodule): the 68k CPU | `executor/core/cpu/syn68k_common.cpp`: the syn68k API on a `cpu::Core` (MAME's 68000) |
| `PowerCore/` (submodule): the PowerPC CPU | `executor/core/cpu/PowerCore.*`: the same interface on MAME's PowerPC recompiler |
| `src/config/front-ends/{qt,x,sdl,sdl2,wayland,...}` | `executor/frontends/`: `headless/` (upstream's), `phoenix/` (video, cursor, sound, the `executor_host` API; `executor_child.cpp`, mac-phoenix's IPC child), `wasm/` (the tab's module), `desktop/` (the Qt 6 desktop application) |
| | ours alone: `executor/host/` (session code the tab and the desktop share), `executor/web/` (the tab), `executor/server/` (the Go server) |
| `cxmon/`, `lmdb/` | not imported |

## Licence

Executor is MIT ([executor/COPYING](../../executor/COPYING)).
MacPhoenix's changes to it, and the CPU glue, are MIT too
([executor/LICENSE-CONTRIBUTIONS](../../executor/LICENSE-CONTRIBUTIONS)),
so upstream or anyone else can take them on Executor's terms. MAME's CPU
cores are BSD-3-Clause. The rest of MacPhoenix's licensing:
[LICENSING.md](../../LICENSING.md).

## What MacPhoenix changed

`tools/executor-upstream-diff.sh` (2026-11-07):

| | new files | removed | changed | lines |
|---|---|---|---|---|
| `core/romlib/` | 397 | 7 | 210 | +97,946 −4,745 |
| `frontends/` | 43 | 61 | 2 | +88,249 −8,346 |
| `core/multiversal/` | 24 | 0 | 56 | +29,580 −119 |
| `tests/` | 74 | 0 | 11 | +8,370 −40 |

The front ends' removals are upstream's qt/x/sdl/wayland ones; their
additions include vendored single-header libraries (miniaudio). Changed
code is marked `MacPhoenix:` in comments (about 880 places in romlib). The changes
to upstream files are described in
[PLAN.md, "Changes to imported Executor code"](PLAN.md#changes-to-imported-executor-code);
`tools/executor-upstream-diff.sh --patch` prints the complete diff.

### The CPU side (not in upstream's tree)

* **68k:** the syn68k API (`cpu_state`, callbacks, A-line traps,
  interrupts, `CALL_EMULATOR`) implemented over a `cpu::Core`, MAME's
  68000 (`mame-68k`), in Executor's four address windows. UAE ran it until
  2026-10-23 (identity addressing, `vm.mmap_min_addr=0`). See PLAN.md,
  "Shape".
* **PowerPC:** `PowerCore` keeps upstream's interface (`PowerCoreState`,
  `execute()`, `syscall`, `handleInterrupt`) and runs on MAME's PowerPC
  recompiler over the same guest memory. Two PowerPC host-op instructions
  replace upstream's magic values (the `0xFFFFFFFC` return address and `sc`).
  PowerPC-only applications run; `--executor-ppc` runs fat applications'
  PowerPC code. Status and open items: PLAN.md, milestone P.

### New subsystems in romlib

| Area | Files | What |
|---|---|---|
| Process Manager | `process.cpp`, `include/rsys/process.h` | several processes side by side, each a minicoro coroutine on Executor's one host thread (DIRECTION.md, section 2); partitions, layers, high-level events, the Thread Manager, desk accessories in DA Handler processes |
| Apple's System file | `res/resPolicy.cpp`, `res/resPrivate.cpp`, `sysroutines.cpp`, `extensions.cpp` | runs on Apple's 7.5.5–8.6 System files: chooses Apple's or Executor's version of each System resource, private ResourceDispatch selectors, the low-memory routines the System expects, allowed INITs/cdevs at startup |
| Apple's packages in place of Executor's | `drag.cpp`, `helppkg.cpp`, `stdfilepkg.cpp`, `textservices.cpp`, `quickdraw/qSaveBits.cpp` | the Drag Manager, Help Manager, Standard File and Text Services from the System file, with the private calls they need |
| Component Manager | `component.cpp` | `$A82A`; System file, extension and `thng` components |
| Finder | `finder.cpp`, `menu/sysmenu.cpp`, `alias.cpp`, `icon.cpp`, `quickdraw/displays.cpp`, `desk.cpp` | the Desktop Manager, Apple/Help/Application menus, alias resolution, Icon Utilities and the Display Manager calls Finder makes |
| Networking | `mactcp/mactcp.cpp`, `mactcp/dnr.cpp`, `opentransport/` | MacTCP on host sockets (async calls, the resolver); Open Transport's client API in C++ for 68k (ASLM) and PowerPC clients (on by default, `--no-executor-ot` to leave it out; [OpenTransport.md](OpenTransport.md)); all through `rsys/hostnet.h`: host sockets natively, the server's relay in the tab |
| Devices | `hostdisk.cpp`, `vdriver/videodrvr.cpp`, `sound/snddispatch.cpp`, `frontends/phoenix/phoenix_sound.cpp` | `.Disk` for host-folder volumes, `.Display`, the Sound Manager's dispatch groups and sound output |
| Debugging | `base/trap_history.*`, `mman/whatis.*` | the last traps and host memory moves for crash reports; what lives at a guest address |
| PowerPC | `cfm.cpp`, `base/builtinlibs.cpp`, `base/functions.impl.h`, `base/cpu.cpp`, `clib/` | the PowerCore wiring, unresolved-import logging, upstream's PPC parameter passing; StdCLib and MathLib in C++ ([PPCLibs.md](PPCLibs.md)) |
| CFM-68K | `cfm68k_glue.cpp`, `cfm.cpp` | 68k code fragments on Executor's own Code Fragment Manager, no Enabler ([CFM68K.md](CFM68K.md)) |
| File systems | `file/hfsplusvolume.cpp`, `file/fsref.cpp`, `file/fsm.cpp`, `hfs/image_io.h` | HFS+ volumes and the FSRef calls ([HFSPlus.md](HFSPlus.md)); the File System Manager, so PC Exchange mounts DOS disks ([FSM.md](FSM.md)); disk images through an opener (overlays in the tab and the desktop) |
| Mac OS 8 | `appearance*.cpp`, `iconservices.cpp`, `collections.cpp`, `nameregistry.cpp`, `quickdraw/displays.cpp` | the Appearance Manager and the platinum look ([Appearance.md](Appearance.md)), Icon Services ([IconServices.md](IconServices.md)), the Collection Manager ([CollectionManager.md](CollectionManager.md)), the Name Registry, live display modes ([DisplayModes.md](DisplayModes.md)) |
| Drawing at 2x | `hidpi/` | text and chrome from outlines and shapes over the 1x screen, GWorld shadows ([HiDPI.md](HiDPI.md)) |
| Memory | `mman/fullheap.cpp` | Executor's host code on a full application heap ([FullHeap.md](FullHeap.md)) |
| Games | `inputsprocket.cpp` | InputSprocket |

### Application special cases

Upstream's per-application hacks (Illustrator's white back colour, Excel's
"no block moves before the first event call", the 'odoc' held back until
a handler is installed, ClarisWorks' VM answer, MacBench's `$FE09` no-op,
Excel's Standard File refCon and others) are gone, each replaced by what
a Quadra or the G3 was measured to do (November 2026). Nothing in romlib
looks at the application's creator any more. What stays:

* **MPLibrary declined** (`cfm.cpp`): there is no nanokernel MP; a Mac
  without MPLibrary answers `fragLibNotFound` too, so weak imports go nil.
* **Per-application `.ecf` files** (`prefs/prefs.cpp`): upstream's user
  mechanism, `<Configuration>/<app name or creator>.ecf`; none ship.
* **OSDispatch `$2`**: answered as measured on the Quadra; its name is a
  guess.
* **`_TranslationDispatch` `$24`/`$20` stand-ins** (`base/traps.cpp`): for a
  System where Easy Open isn't running (Executor's own); on Apple's 7.5.5
  and 7.6.1 Easy Open answers.

## Upstream since the import

Upstream has moved on (81 commits by 2026-10-20). Taken so far:
`b6290b62`'s `callfromPPC::ParameterPasser` (PowerPC calling conventions)
and its tests. Not merged: the rewrite of trap and UPP marshalling into
generated code (Phases 0–5, U1–U5, 2026-10-07 to 10-18), which touches the
same files as most of MacPhoenix's trap work.

Upstream's multiversal has moved on as well (50 commits by 2026-10-22, at
`580f330`). Taken (2026-10-26, as upstream's own commits, `git am`): the
definitions and the C side, 18 commits. Driver Services, Name Registry
and PCI with the generator features they need (`callconv: C` function
pointers, `align: power`, `define`); the high-level File Manager glue
fixes (`HOpen`'s `ioMisc`, `ioFVersNum`, `AppFile`); register-output
glue inlined; `GetGrayRgn` as a function (`C_GetGrayRgn` in
`windInit.cpp`); `ControlRef` as `ControlHandle`; the def-proc,
decompressor, `TEDoText` and sound modifier UPP types (and the `A3`,
`D3`, `D4`, `D7` register names in `base/functions.h`); the Apple Event
Manager's constants and the #327 declarations (`ioDirMask`,
`GetVolParmsInfoBuffer`, `bHasExtFSVol`: out of `file/file.h` now).
Kept ours: `GetFrontProcess`'s entry (the same fix, made here
independently, with more on the second argument), `AEArrayType` as a
byte (Apple's `SInt8`; upstream made it `int16_t`) and the private
`_AE_hdlr_*` entry points, whose new signatures go with upstream
Executor's Apple Event Manager rework (`1af5b3e2`, "PACK 8 spec
conformance"). Not taken: the Executor-side generator rewrite (above),
`Executor.yaml` (upstream Executor's `unixmount`), and upstream's fix to
`ROMlib_dotext`'s register convention (`bf0c04b0`), an Executor change
still to look at.

## The definitions as Retro68's headers

`multiversal/defs` is also the source of the Multiversal Interfaces, the
free C headers Retro68 offers instead of Apple's Universal Interfaces, so
what MacPhoenix adds there (215 functions since the import: the HFS Plus
APIs, the Display Manager, layers and other Process Manager calls, the
Thread Manager, ...) can reach Retro68 programs too:

```bash
cd executor/core/multiversal
ruby make-multiverse.rb -G CIncludes -o OUT    # OUT/CIncludes, OUT/lib68k (glue)
```

`ctest -R retro68_` runs RetroTest, a program built with Retro68 for 68k
and PowerPC against these headers alone, on Executor and on Apple's
systems (LIBRARIES.md, "Tools on the Retro68 side").
`ctest -R multiversal_retro68` (`tests/test_multiversal_retro68.sh`)
generates them, builds `tests/multiversal/mvtest.c` against them alone
(none of Apple's headers), and compiles Retro68's samples. Definitions
must be plain Mac types: `Handle*`, not `GUEST<Handle>*` (the Executor
generator wraps pointers itself; the C generator would write `GUEST`).
The generator names a header for each Universal Interfaces file the
definitions cover (`cincludes.rb`; each just includes `Multiverse.h`).

BridgeAgent and MacBrowser are built against them alone (CMake,
`tools/retro68/multiversal-headers.sh`: generated into the build tree,
with none of the toolchain's or Apple's interfaces; needs ruby), as are
RetroTest, OTTest and MacPhoenixSuite: `tests/guest/suite/retro68`
builds the suite with Retro68 for 68k and PowerPC from the same sources
as its MPW build, and it gives the same results on Apple's systems and
on Executor (`guest_suite_retro68*`); `multiversal_retro68` compiles
each, and with it every other guest program but CLibTest, which tests
Apple's C library and so is built with that library's own headers
(`fp.h`, `fenv.h`, `PLStringFuncs.h`: StdCLib and MathLib's definitions
are `only-for: Executor`, a guest's C headers come with its C library).
RetroTest also runs the high-level HFS glue (`DirCreate`, `HSetFInfo`,
`HSetFLock`, `CatMove`) on both processors, on Apple's systems and
Executor. Many parameters Apple declares `const T *` are plain
`T *` here (warnings in C). Some Executor signatures differ from Apple's
(`NewThread` takes `ProcPtr` and `LONGINT`s, not `ThreadEntryUPP`,
`ThreadStyle` and `ThreadOptions`); where the C side needs Apple's form
and Executor's code another, the definition is split (`only-for:
Executor` / `not-for: Executor`: `TMTask`'s `tmAddr`, `GetFrontProcess`,
`PBCatSearch`).

## Documentation

Plans and where things stand:

| Document | What |
|---|---|
| [PLAN.md](PLAN.md) | design (the CPU facades, "Apple's code or ours"), the core's milestones (M0–M4, P), the product milestones (M1, P1, P2 done; M2 next), working notes, every change to imported code |
| [DIRECTION.md](DIRECTION.md) | why a C++ Toolbox, and the architecture steps: MAME cores, minicoro processes, WebAssembly and the relay, PowerPC, A/UX |
| [M1.md](M1.md) | M1's audit: every target application × System, the tab cells that cover it, the last result |
| [SCOREBOARD.md](SCOREBOARD.md) | the target applications on Executor (7.6.1, 8.6; native and the tab) beside the ROM machines |
| [STRAGGLERS.md](STRAGGLERS.md) | numbered open problems and their fixes |
| [APPLE-DISKS.md](APPLE-DISKS.md) | every item on Apple's 7.5.5 and 7.6.1 disks opened on Executor: the fix list, the extension policy, the cut boot disks |
| [MacOS8-9.md](MacOS8-9.md) | Mac OS 8.6 and 9.0.4: what the Finder needed, the 8.6 grid, the work list; "9.0.4: what is open" |
| [Speed.md](Speed.md) | Executor 68k and PowerPC, native and in the tab, against the ROM machines; MacPhoenixBench; the MMU's cost |

The tab, its server and the desktop application:

| Document | What |
|---|---|
| [Standalone.md](Standalone.md) | `executor-standalone`: building, a release, flags, the claim, the API, start-up costs, tests |
| [WebSite.md](WebSite.md) | putting the site up: the tab's build, the library disks, systemd, HTTPS, backups, the site's cells |
| [Settings.md](Settings.md) | the settings document and its schema, the API, the dialog, how the page applies them |
| [Disks.md](Disks.md) | the tab's Disks panel, disks in and out live, System Folder additions |
| [DisplayModes.md](DisplayModes.md) | the screen's resolution changed live (Display Manager, fit to window) |
| [HiDPI.md](HiDPI.md) | drawing at 2x: the design, phases 1, 2 and 4 as built, in the tab too |
| [../../executor/frontends/desktop/README.md](../../executor/frontends/desktop/README.md) | the desktop application: use, data folder, command line, packaging (AppImage, Windows zip, macOS status) |

Subsystems:

| Document | What |
|---|---|
| [MEMORY_MAP.md](MEMORY_MAP.md) | the System 7 memory map Executor rebuilds, from snapshots of real boots |
| [FullHeap.md](FullHeap.md) | a full application heap: what the Mac does, Executor's rule |
| [OpenTransport.md](OpenTransport.md) | Open Transport in C++, 68k and PowerPC clients, the configuration database |
| [CFM68K.md](CFM68K.md) | CFM-68K applications on Executor's Code Fragment Manager |
| [PPCLibs.md](PPCLibs.md) | StdCLib and MathLib for PowerPC, in C++ |
| [LIBRARIES.md](LIBRARIES.md) | ASLM and CFM libraries and Component Manager calls in the definitions |
| [HFSPlus.md](HFSPlus.md) | HFS+ volumes and the FSRef calls |
| [FSM.md](FSM.md) | the File System Manager; PC Exchange and DOS disks |
| [Appearance.md](Appearance.md) | the Appearance Manager: themes, brushes, the platinum drawing |
| [IconServices.md](IconServices.md) | Icon Services: behavioural specification |
| [CollectionManager.md](CollectionManager.md) | the Collection Manager: behavioural specification |
| [cleanroom/README.md](cleanroom/README.md) | clean-room specifications for parts first worked out from Apple's code |

Tools:

| Document | What |
|---|---|
| [PPCTrace.md](PPCTrace.md) | PowerPC breakpoints and traces in a session (`--executor-ppc-break`) |
| [DiffTest.md](DiffTest.md) | differential probes: Apple's 8/9 libraries against Executor's, key by key |
| [../MPW.md](../MPW.md) | MPW and ToolServer running headless on Executor |
