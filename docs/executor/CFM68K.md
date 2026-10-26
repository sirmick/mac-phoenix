# CFM-68K on Executor

STRAGGLERS item 37. CFM-68K applications (CodeWarrior IDE 3.2, MacPython,
the CFM-68K halves of fat applications) need Apple's CFM-68K Runtime
Enabler on a 68k Mac. On Executor the Enabler's second INIT hangs the boot
(it hunts for Apple's level-1 interrupt handler next to vector `$64`), so it
stays blocked; Executor provides CFM-68K itself, in C++ on its Code Fragment
Manager, as it does Open Transport (decided 2026-10-26).

```bash
ctest -R guest_suite_cfm68k     # MacPhoenixSuite as a CFM-68K application:
                                #   _executor on Executor, the other on the ROM
                                #   machine with Apple's Enabler (dev-7.6.1)
tools/mpw/mpw --toolchain cw build tests/guest/suite   # MacPhoenixSuiteCFM68K
```

CodeWarrior IDE 3.2 (`dev-tools-system-7`) starts on Executor with
`dev-7.6.1`'s System, its CFM-68K plug-ins loaded, as on the ROM machine.

## What a CFM-68K application is

From CodeWarrior IDE 3.2 (`APPL`/`CWIE`), read with the PEF format in *Mac
OS Runtime Architectures*:

- `'cfrg'` 0 lists the fragments in the data fork: the application (`m68k`)
  and the libraries it carries (`MSL C.68K.DLL`, `MWCRuntime68K.DLL`), and
  the same for `pwpc`. Libraries in the application's own `'cfrg'` are found
  before any on disk.
- The `m68k` application fragment is a flat PEF container (`Joy!peffm68k`):
  code, pattern-initialized data, loader. Imports from InterfaceLib,
  DragLib, ThreadsLib, ObjectSupportLib, AppleScriptLib, AppearanceLib (the
  last two weak libraries) and the two Metrowerks libraries; `qd` is a data
  import. Main, init and term are transition vectors in the data section.
- `CODE` 0 holds a one-entry jump table to `CODE` 6 `'CFM Launch'`, the stub
  that calls the Enabler (`$AA5A`, private selectors -1, -2, -3, -19, `$44`).
  A 68k Mac without the Enabler runs only this stub. Metrowerks'
  `MWLink68K -xm application -model CFMflatdf` makes the same shape.

## Conventions (from the compiler's output in the applications)

- **Transition vector**: 8 bytes, the code address and the fragment's A5
  (its data section plus an offset the linker chose).
- **Call through a TV**: A1 = the TV, `MOVEA.L (A1)+,A0` / `JSR (A0)`; the
  callee starts with `MOVEA.L (A1),A5`. The caller saves its A5 in its frame
  and reloads it after the call. C function pointers are TV pointers.
- **Imports**: the importer's data holds a pointer to the exporter's TV (or
  to the data item).
- **Arguments**: pushed right to left (C order, for Pascal routines too),
  each in a **4-byte slot** (shorts and Booleans widened, the value at the
  slot's low end); the result in D0. A routine with fixed arguments **pops
  them itself** (`RTD`), Pascal or C (`c2pstr` too); only variadic ones
  (`CallUniversalProc`, `sprintf`) leave them to the caller. Seen as
  `MOVEM.L (A7)+` right after a call with no stack adjustment, in Apple's
  ObjectSupportLib and in the IDE (InterfaceLib calls: no caller pops in
  9597 call sites; variadic ones: always).
- A routine keeps D3-D7 and A2-A6.
- **Init**: one argument (the `CFragSystem7InitBlock` pointer), `OSErr` in
  D0. **Main**, **term**: none.
- Callbacks handed to the Toolbox are routine descriptors (`$AAFE`) with ISA
  `kM68kISA | kCFM68kRTA` and a TV for the procedure; the routine takes its
  arguments as above whatever its `procInfo` convention (a `pascal`
  Gestalt function: C order, `RTD #8`, result in D0).

## What Executor does

1. **Launch** (`launch.cpp`): an application whose `'cfrg'` has an `m68k`
   application fragment starts through Executor's CFM, like a PowerPC one:
   the fragment is prepared and its main TV called (`ROMlib_call_cfm68k`:
   A1 the TV + 4); `CODE` 0 and the stub never run. Fat applications take
   the 68k fragment unless `--executor-ppc`. Segmented CFM-68K (code in
   resources, the Enabler's segment loader) is refused as before.
2. **CFM core** (`cfm.cpp`, for PowerPC too):
   - `GetDiskFragment` reads the container at the offset and length given
     (it read the whole data fork, so a fat file's second container never
     loaded); `kFindLib`, `kLoadNewCopy` honoured.
   - Fragments prepared are listed per application zone, so a library is
     shared by its importers and found again by name or file and offset;
     the list goes with the application (`ROMlib_cfm_process_exit`).
   - Search: the application's own file, its folder, the Extensions folder,
     then Executor's libraries (the root and application folders were
     uninitialized TODOs). The application's resource file is not closed
     by the search any more.
   - `m68k` init routines run once the closure is bound, libraries first,
     with a `CFragSystem7InitBlock` (connection, closure, where the fragment
     came from, its name); term routines at `ExitToShell`, last prepared
     first.
   - A weak library's imports are weak; an unresolved 68k import is a TV
     that logs `[unimplemented] CFM-68K NAME` and answers 0 (it can't pop
     arguments it doesn't know: expect trouble after one).
3. **Executor's 68k libraries** (`base/builtinlibs.cpp`): every routine
   exported to InterfaceLib for PowerPC also has an 8-byte TV for `m68k`, its
   code a callback reading the convention above (`callfromCFM68K` in
   `functions.impl.h`). During the call A5 is `CurrentA5` (QuickDraw finds
   its globals through A5) and D3-D7/A2-A6 are restored afterwards; traps go
   through the trap table when an extension patched them. InterfaceLib's
   other names (DragLib, ThreadsLib, AppearanceLib, PrivateInterfaceLib, ...)
   answer from the same table when no file has them.
   `cfm68k_glue.cpp` adds what is glue on a Mac, not a trap Executor wraps
   (InterfaceLib has it by name): string conversions and comparisons, Dialog
   Manager glue, `DirCreate`, `ReadLocation`, the Object Support Library's
   record builders (`CreateObjSpecifier` & co.), component calls
   (`OpenDefaultComponent`, `CallComponentUPP` for a `ComponentParameters`
   block), and AppleScriptLib's OSA routines as component calls; the trap-
   and component-based ones run their trap through the trap table.
   **ObjectSupportLib** is Executor's even when Apple's file is there: the
   68k one runs `'proc'` resources against the AppleEvent Manager's private
   selectors.
4. **Mixed Mode** (`mixed_mode.cpp`): routine records with ISA
   `kM68kISA | kCFM68kRTA` are called through their TV;
   `CallUniversalProc`/`CallOSTrapUniversalProc` from 68k fragments (the
   routine's arguments after the procInfo, 4 bytes each); the dispatched
   conventions (`kD0DispatchedPascalStackBased` & co., the selector as the
   first parameter). Fixed on the way: a Pascal result read and dropped,
   register parameters and results mapped through the wrong table.
5. **Gestalt** `'cfrg'` stays unanswered on the 68k side: with it, 7.6.1's
   extensions take the Code Fragment Manager as callable from 68k code
   through `$AA5A`, which stays `_Unimplemented` there, and the boot stops
   after the Speech Manager. Applications launched as above don't need it.

Not done: segmented CFM-68K, MathLib's floating-point conventions (double
results land in D0:D1, unverified), Apple's NuStdCLib for MPW-built
applications on Executor's own System (it is in the Enabler's file), the
public `$AA5A` selectors for classic 68k callers (and then Gestalt
`'cfrg'`).

## Clean room

No Apple code is used. The conventions come from the PEF chapters of *Mac
OS Runtime Architectures* and from compiled callers and callees (prologues,
how a call site's stack is restored); routine selectors and parameter sizes
from the Universal Interfaces' inline code; the routines from Inside
Macintosh. The launch stub's private selectors are known only as numbers
and not implemented.
