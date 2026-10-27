# Executor in MacPhoenix

MacPhoenix's `--backend executor` runs classic Mac applications on
[Executor 2000](https://github.com/autc04/executor), a reimplementation of
the Mac Toolbox in C++, without a Mac ROM. MacPhoenix took Executor's
Toolbox ("romlib"), put it on its own CPU cores and front end, and has since
grown it to run Apple's 7.5.5 Finder and System on top of it.

This page is the entry point: where the code came from, what MacPhoenix
changed, its licence, and where the rest is documented.

## Origin

* **Upstream:** [autc04/executor](https://github.com/autc04/executor),
  Wolfgang Thaller's revival of ARDI's Executor (Abacus Research &
  Development, 1986–2004; MIT since 2008).
* **Imported:** upstream commit `b2569eb1` (2026-10-06), its multiversal
  submodule at `1594ced0`, into `src/executor/`.
* **Reference checkout:** `executor/` at the repository root (gitignored),
  for comparisons and as a differential oracle.
  `tools/executor-upstream-diff.sh` clones it if missing.

| Upstream | MacPhoenix |
|---|---|
| `src/` | `src/executor/romlib/` |
| `multiversal/` (submodule) | `src/executor/multiversal/` (vendored) |
| `tests/` | `src/executor/tests/` |
| `syn68k/` (submodule): the 68k CPU | `src/executor/cpu/syn68k_common.cpp`: the syn68k API on a `cpu::Core` (MAME's 68000) |
| `PowerCore/` (submodule): the PowerPC CPU | `src/executor/cpu/PowerCore.*`: the same interface on MAME's PowerPC recompiler |
| `src/config/front-ends/{qt,x,sdl,sdl2,wayland,...}` | `romlib/config/front-ends/phoenix/` (video, cursor, sound to the web UI) and `src/executor/host/executor_child.cpp` |
| `cxmon/`, `lmdb/` | not imported |

## Licence

Executor is MIT ([src/executor/COPYING](../../src/executor/COPYING)).
MacPhoenix's changes to it, and the CPU glue, are MIT too
([src/executor/LICENSE-CONTRIBUTIONS](../../src/executor/LICENSE-CONTRIBUTIONS)),
so upstream or anyone else can take them on Executor's terms. MAME's CPU
cores are BSD-3-Clause. The rest of MacPhoenix's licensing:
[LICENSING.md](../../LICENSING.md).

## What MacPhoenix changed

`tools/executor-upstream-diff.sh` (today):

| | new files | removed | changed | lines |
|---|---|---|---|---|
| `romlib/` | 45 | 65 | 126 | +20,419 −10,873 |
| `multiversal/` | 3 | 0 | 28 | +2,548 −24 |
| `tests/` | 7 | 0 | 8 | +1,067 −33 |

The removals are the dropped front ends and the cxmon debugger. Changed
code is marked `MacPhoenix:` in comments (about 290 places in romlib). The changes
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
| Process Manager | `process.cpp` (257 → 3,412 lines), `include/rsys/process.h` | several processes side by side, each on its own host thread; partitions, layers, high-level events, the Thread Manager, desk accessories in DA Handler processes |
| Apple's System file | `res/resPolicy.cpp`, `res/resPrivate.cpp`, `sysroutines.cpp`, `extensions.cpp` | runs on Apple's 7.5.5 System file: chooses Apple's or Executor's version of each System resource, private ResourceDispatch selectors, the low-memory routines 7.5.5 expects, allowed INITs/cdevs at startup |
| Apple's packages in place of Executor's | `drag.cpp`, `helppkg.cpp`, `stdfilepkg.cpp`, `textservices.cpp`, `quickdraw/qSaveBits.cpp` | the Drag Manager, Help Manager, Standard File and Text Services from the System file, with the private calls they need |
| Component Manager | `component.cpp` | `$A82A`; System file, extension and `thng` components |
| Finder | `finder.cpp`, `menu/sysmenu.cpp`, `alias.cpp`, `icon.cpp`, `quickdraw/displays.cpp`, `desk.cpp` | the Desktop Manager, Apple/Help/Application menus, alias resolution, Icon Utilities and the Display Manager calls Finder makes |
| Networking | `mactcp/mactcp.cpp`, `mactcp/dnr.cpp`, `opentransport/` | MacTCP on host sockets (async calls, the resolver); Open Transport 1.1's 68k client API in C++ (on by default, `--no-executor-ot` to leave it out; [OpenTransport.md](OpenTransport.md)) |
| Devices | `hostdisk.cpp`, `vdriver/videodrvr.cpp`, `sound/snddispatch.cpp`, `config/front-ends/phoenix/phoenix_sound.cpp` | `.Disk` for host-folder volumes, `.Display`, the Sound Manager's dispatch groups and sound output |
| Debugging | `base/trap_history.*`, `mman/whatis.*` | the last traps and host memory moves for crash reports; what lives at a guest address |
| PowerPC | `cfm.cpp`, `base/builtinlibs.cpp`, `base/functions.impl.h`, `base/cpu.cpp` | the PowerCore wiring, unresolved-import logging, upstream's PPC parameter passing |

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
cd src/executor/multiversal
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

| Document | What |
|---|---|
| [PLAN.md](PLAN.md) | design (the CPU facades, "Apple's code or ours"), milestones M0–M4 and P, working notes, every change to imported code |
| [DIRECTION.md](DIRECTION.md) | where it goes: MAME cores, process switching, WebAssembly, PowerPC, A/UX |
| [STRAGGLERS.md](STRAGGLERS.md) | open problems and their fixes |
| [MEMORY_MAP.md](MEMORY_MAP.md) | the System 7 memory map Executor rebuilds, from snapshots of real boots |
| [OpenTransport.md](OpenTransport.md) | Open Transport on Executor |
| [LIBRARIES.md](LIBRARIES.md) | plan: ASLM and CFM libraries and Component Manager calls in the definitions |
| [../MPW.md](../MPW.md) | MPW and ToolServer running headless on Executor |

Running it: `./build/mac-phoenix --backend executor` (Executor's own
System), `--executor-system 7.5.5.img` (Apple's System and Finder),
`--core mame-68k`, `--executor-ppc`; tests: `ctest -L executor`.
