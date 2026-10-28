# Libraries in the definitions: ASLM, CFM and components

Executor's API definitions (`src/executor/multiversal/defs`) describe
Toolbox traps once and generate both sides from them: Executor's C++
declarations and trap registrations (`-G Executor`, `romlib/generated`)
and the Multiversal Interfaces, the C headers Retro68 programs use
(`-G CIncludes`; README.md, "The definitions as Retro68's headers").

Three other ways in which Mac code reaches system code have no
definitions: shared libraries loaded by ASLM, shared libraries loaded by
the Code Fragment Manager (PowerPC and CFM-68K), and Component Manager
calls. Executor answers each from hand-written tables in its own format,
and Retro68 gets no headers for them. This plan brings them into the
definitions, in a shape upstream multiversal can take.

## Where things are (2026-10-26)

| Route | Executor's table | Entries | Typed | Reaches |
|---|---|---|---|---|
| Traps | generated `PASCAL_TRAP`, `PASCAL_SUBTRAP`, `REGISTER_TRAP`, `NOTRAP_FUNCTION` (`base/traps.h`) | ~2,000 | from the C++ signature | the trap table; the same wrappers export each one from InterfaceLib for PowerPC and CFM-68K (`base/traps.impl.h`), through the trap's table entry once something has patched it (a subtrap: its dispatcher's entry, the selector in its register; Apple's PACK 8 under AppleScript), as InterfaceLib's glue does on a Mac; `library:` on a trap's entry exports it from that library too (`LIBRARY_ALSO`: SoundLib's Sound Manager calls, Mac OS 8/9's ThreadsLib, ObjectSupportLib, PrivateInterfaceLib); a `library:` list exports a library routine from the first and the others; on a subtrap without `executor:` it is glue through the dispatcher's table entry to the guest's own code (`TRAP_GLUE`: DragLib to Apple's 68k Drag Manager) |
| Open Transport over ASLM | `Def{set, name, Conv, "LLLLL", 'L', impl}` (`opentransport/ot_*.cpp`), index-to-name lists (`ot_names.inc`) — in the definitions since step 4 | ~130 of ~330 names | in the definitions (OT 1.1's types, since step 4); `impl(const Call&)` still reads its arguments | 68k; PowerPC (OTClientLib & co.) since step 4; C headers and PowerPC glue for Retro68 |
| ASLM's own set for PowerPC (`"!$fset"`) | in the definitions (`LibraryManager.yaml`, 2026-10-27): `ASLM_FUNCTION`/`ASLM_RAW` by set and index | 7 of 58 | yes (`callfromPPC::Invoker`) | PowerPC code bound through SLMGlobal+$88 (`opentransport/ot_slm_ppc.cpp`); Executor only; ASLM's classes (root manager, managers, pools, TProcNotifier) stay hand-written: the definitions have no form for objects |
| StdCLib, MathLib | `exportFn(LIB, "strlen", &c_strlen)`, `exportRaw` for variadic and hand-decoded routines, `exportData` (`clib/`) — in the definitions since step 2 | ~250 + ~70 raw + 25 data | yes (`callfromPPC::Invoker`) | PowerPC; StdCLib for CFM-68K too since step 2 |
| CFM glue | `exportGlue("c2pstr", &fn)` (`cfm68k_glue.cpp`) — in the definitions since step 3 | ~50 | yes | InterfaceLib, ObjectSupportLib, for CFM-68K and PowerPC |
| AppleScriptLib (OSA) | `{ "OSALoad", 0x0001, { 4, 4, 4 } }` (`cfm68k_glue.cpp`) | 42 | no: parameter sizes | CFM-68K, re-pushed to `_ComponentDispatch` |
| Component Manager | `switch` on the selector in `RAW_68K_IMPLEMENTATION(ComponentDispatch)` (`component.cpp`) | 35 selectors | no | 68k; `Components.yaml` declares no functions |

Left as they are: MacTCP's driver calls (a `switch` on `csCode`; Apple
declared no functions for them either, and the parameter blocks are in
`MacTCP.yaml`), the DNR code resource, the low-memory dispatchers at
`$7B0` and `$668`, and the System file's `'proc'` hooks.

What multiversal has already: a `cfm:` key (Navigation Services, 19
functions), used the other way round. It makes 68k glue that calls a
CFM library from a Multiverse-built 68k program (`cincludes.rb`
`generate_cfm_wrapper`, `custom/cfmwrapper.c`). `custom/libraries.yaml`
only groups glue objects into archives.

## The definitions

A function that is a library export says which library, under which
name, in which calling convention:

```yaml
- function:
    name: OTAllocMem
    return: void*
    args:
      - name: size
        type: OTByteCount
    library: OTUtilityLib        # CFM library: PowerPC, and CFM-68K where it exists
    aslm: { set: "OTClnt$LclUtils", name: OTAllocMemPriv }   # the 68k route
    callconv: C                  # C (default for libraries) or Pascal
    executor: C_
```

- `library:` names the CFM library that exports the symbol. Several
  architectures can share it; a routine that exists in only one says so
  (`only-for: ppc`, the existing filter key).
- `aslm: { set: "OTClnt$LclClnt", name: OTBind }` gives the function set
  68k glue binds it in through the Shared Library Manager, and the set's
  name for it (Open Transport's sets name their Pascal routines in
  capitals; the entry's `name` is Apple's, the PowerPC export's). Such a
  routine reads its arguments through `ot::Call`, so the generator emits
  `OT_FUNCTION` with a letter per argument from its type's size.
  Without `library:` the routine is no CFM export but one PowerPC code
  binds through ASLM itself (a stub record naming set and index, handed to
  SLMGlobal+$88): `aslm: { set: "!$fset", index: 21, name: Fail }`. The
  generator registers it with Executor's PowerPC binder by set and index,
  typed (`ASLM_FUNCTION`) or, with `raw: true`, only declared
  (`ASLM_RAW`; builtinlibs checks it against what the code registers).
  ASLM's own set is `LibraryManager.yaml`, Executor only: a client's C++
  interface is the SDK's.
- `callconv:` is for the 68k ASLM route (Open Transport has Pascal and
  C routines) and for the C side's `pascal`. It does not decide who pops
  in CFM-68K: there every routine with fixed arguments pops its own, C
  or Pascal (`c2pstr` too); only a variadic one leaves them to its
  caller (`c_routines68k` in `builtinlibs.cpp`), and a variadic routine
  is raw anyway. The name is upstream's: its `funptr` entries take
  `callconv: C` for native C function pointers (Driver Services).
- `raw: true` for routines Executor decodes by hand (variadic `printf`,
  MathLib's `l` routines on f1:f2): the C side is declared normally;
  Executor's code registers the routine itself (they are templates and
  lambdas the generated code can't name), and the generated code only
  records that it is there: builtinlibs reports one declared but not
  registered, or registered but not declared (`LIBRARY_RAW`).
- `executor:` names the C++ routine as for traps (`c_` prefix + name, or
  the whole name, so aliases share one: `_memcpy` is `c_memcpy`).
- Data exports (StdCLib's `errno`, `_iob`) are to be `- data:` entries
  with `library:` and a type; for now the sources register them (their
  storage is allocated when the library is set up).

A component call says its selector; its dispatcher is a component
type's (`selector-location: Component`, trap `$A82A`):

```yaml
- dispatcher:
    name: OSAComponent
    trap: 0xA82A
    selector-location: Component
- function:
    name: OSALoad
    return: OSAError
    args:
      - name: scriptingComponent
        type: ComponentInstance
      - name: scriptData
        type: const AEDesc*
      - name: modeFlags
        type: long
      - name: resultingScriptID
        type: OSAID*
    dispatcher: OSAComponent
    selector: 0x0001
```

The C side is what Apple's headers do:
`M68K_INLINE(0x2F3C, <params size>, <selector>, 0x7000, 0xA82A)`
(`move.l #size<<16|selector,-(sp); moveq #0,d0; _ComponentDispatch`),
and for PowerPC a call to `CallComponentUPP`. Executor's side registers
the selector with its component dispatch, typed from the C++ signature
like a trap: a function with `executor:` on such a dispatcher is one of
Executor's own components' calls (`COMPONENT_FUNCTION`), its arguments
read from the call's ComponentParameters (the last first, the instance
after them), and `ROMlib_register_native_component` names the
dispatcher whose calls a native component answers. The Component Manager's own calls (`OpenComponent`,
`CallComponentFunction`, negative selectors) become ordinary
`ComponentDispatch` subtraps in `Components.yaml`.

## What the generators make

**Executor (`executor.rb`).** A library function becomes a C++
declaration (`C_OTAllocMem`, `GUEST<>` types as for traps) and a
registration macro, `LIBRARY_FUNCTION(OTAllocMem, "OTUtilityLib",
Conv::C)`, in place of the hand-written tables. The macro registers the
same typed wrapper with `builtinlibs.cpp` for PowerPC and CFM-68K, and
with the ASLM binder (`ot.cpp`) when `aslm:` is given. The trap macros
already do this for InterfaceLib; this generalises their hard-coded
`"InterfaceLib"`.

**C headers (`cincludes.rb`).** Library functions are declared for both
architectures. On PowerPC they are ordinary imports; the program links
against the library's import stub (Retro68's `ImportLibraries`, or ones
the generator writes from the definitions). On 68k there are three
cases: a CFM-68K program imports them the same way; a classic 68k
program calls them through a wrapper over CFM (the existing `cfm:`
mechanism) or, for an ASLM library, through generated ASLM glue that
does what Apple's static glue does. Headers by Apple's names
(`OpenTransport.h`, `OSA.h`, `Components.h`, `fp.h`...).

Glue that is PowerPC code (Open Transport's public calls over its
`...Priv` exports) goes in `custom/` like the 68k glue and is listed
under `PowerPC:` in `custom/libraries.yaml`: the generator compiles it
into `libppc/` when `powerpc-apple-macos-gcc` is on its PATH. A
`funptr` with `upp: false` is a plain function pointer (`pascal` on 68k
unless `callconv: C`), as Open Transport's notifiers and task routines
are: PowerPC code passes them as TVectors, not routine descriptors.
A register-based `funptr` (`register:` on its arguments, D0-D3/A0-A3)
gets its Mixed Mode procInfo (`kRegisterBased`, the layout Executor's
Mixed Mode decodes): `uppTimerProcInfo` is $B802, as Apple's.

## Order of work

The whole plan (agreed 2026-10-26):

0. Land what's done: `executor-ppc-libs` and `multiversal-retro68` into
   main.
1. Upstream multiversal's definitions and C side — done 2026-10-26
   (README.md, "Upstream since the import"); its Executor-side generator
   rewrite stays out.
2. to 5., below: the library routes one by one.
6. Outward: the upstream change (only once seen and agreed), a Retro68
   with PowerPC to build the CFM side (done), Retro68's `AutomatedTests`
   on our Executor through `LaunchAPPL` (done 2026-10-27, below).

Throughout, the aim is MacPhoenix's own guest programs building against
the generated headers alone (BridgeAgent without Apple's UI 3.4 first:
done 2026-10-27, MacBrowser too, then every guest program but CLibTest,
and MacPhoenixSuite built with Retro68 runs; STRAGGLERS 63);
`multiversal_retro68` compiles each one that does.

The library routes:

2. **Schema and generators; StdCLib and MathLib on them** — done
   2026-10-26: `LIBRARY_FUNCTION`/`LIBRARY_RAW` (`base/traps.h`;
   `LibraryFunction` registers PowerPC and CFM-68K, logged like traps),
   `defs/StdCLib.yaml` (175 exports) and `defs/MathLib.yaml` (147),
   `only-for: Executor` (a guest's C headers come with its C library),
   the implementations in namespace Executor (their helper types and
   templates in anonymous namespaces). Then NuStdCLib and NuIntEnv
   (STRAGGLERS 60, done): the same StdCLib for CFM-68K programs, checked
   by CLibTest68K on Executor and with Apple's NuStdCLib
   (`clib_cfm68k_executor`, `clib_cfm68k`; PPCLibs.md). On the way:
   CFM-68K's double and float results (CFM68K.md).
3. **The CFM glue** (`cfm68k_glue.cpp`'s `exportGlue` list) — done
   2026-10-26: its 51 InterfaceLib exports are definitions. 22 were in
   them already for the C side only (no `executor:`): they got `library:
   InterfaceLib`, and the 17 whose glue only passed its arguments on now
   name Executor's own routine (`executor: EqualString`), the glue gone;
   29 are new entries, `only-for: Executor` until the C side has 68k glue
   for them (`c2pstr`, `ReadLocation`, `DirCreate`, the object
   specifier builders, `OpenComponent`, ...). Left in `cfm68k_glue.cpp`:
   the routines with bodies, `CallComponentUPP`, AppleScriptLib's table
   (step 5) and the CFM-68K overrides.
4. **Open Transport** — done for Executor and PowerPC, 2026-10-26: the
   130 functions are `OpenTransport.yaml` entries (`aslm:` and `library:`,
   argument sizes only, `only-for: Executor`), registered by `OT_FUNCTION`
   for the 68k binder and as PowerPC exports; the routines kept reading
   their arguments through `ot::Call`, which reads PowerPC registers too
   (OpenTransport.md, "The functions in the definitions"). The C side,
   PowerPC half, done the same day: OT 1.1's types and constants in
   `OpenTransport.yaml` and `OpenTptInternet.yaml`, the entries with real
   types (the `ot::Call` letters from the types' sizes, unchanged), and
   the public calls over the `...Priv` routines as glue
   (`custom/OpenTransportGlue.c`, `libppc/libOpenTransportApp.a`; the
   client record it hands OT as seen from outside, OpenTransport.md, "The
   C side"). OTTest built with Retro68 on them passes on Executor and
   matches Apple's glue on Mac OS 9.0.4. The 68k half (2026-10-27): the
   generator writes a wrapper per `aslm:` entry that gets its routine from
   ASLM's documented client interface (a TLibraryManager of the
   application's own, `GetFunctionPointer` by the set's index, now in the
   entries); OTTest for 68k passes on Executor and on the Quadra with
   Apple's ASLM and OT 1.1.1.
   As planned: the `Def` tables become definitions and the
   implementations take typed arguments instead of `Call`. By-products:
   `OpenTransportLib`, `OTUtilityLib`, `OpenTptInternetLib` for PowerPC
   (OpenTransport.md lists them as not written) and `OpenTransport.h`.
   Check: OTTest on 68k (the ASLM route) and PowerPC, the guest suite's
   network sections.
5. **Components** — done 2026-10-26 (the native component 2026-10-27).
   `Components.yaml` has the Component Manager's types, constants and 33
   calls as `ComponentDispatch` subtraps (`moveq #sel,d0`), Executor's
   routines typed (`component.cpp`; CallComponent and
   CallComponentFunction, which reshape the stack, raw handlers on the
   same dispatcher); InterfaceLib exports them as for any trap (the glue
   for three of them is gone). `OSA.yaml` has the Open Scripting
   Architecture's 42 calls on a `selector-location: Component`
   dispatcher: on the C side Apple's inline (`move.l
   #paramSize<<16|selector,-(sp); moveq #0,d0; _ComponentDispatch`, the
   parameter size summed from the arguments after the instance), on
   Executor's `COMPONENT_CALL` for those with `library: AppleScriptLib`
   (the CFM-68K export that makes the call; the hand-written table is
   gone, its 42 entries generated the same). Components.h and OSA.h are
   real headers now (mvtest.c calls both). Executor's own native
   component, the sound output device (2026-10-27): `SoundMgr.yaml` has
   Sound.h's sound component calls on a `SoundComponent` dispatcher (13,
   with `SoundComponentData`, `SoundParamBlock`, `SoundInfoList` and the
   `si...` selectors the device answers); the two the device implements
   have `executor: C_`, and `COMPONENT_FUNCTION` registers
   `C_SoundComponentGetInfo`/`SetInfo` (`sound/soundIMVI.cpp`, typed
   now) with the component type's dispatch (`component.cpp`), which
   answers open, close, register, version and can-do itself and logs the
   rest. The Sound control panel's Volumes and Sound Out pages read and
   set the device as before; mvtest.c calls both. PowerPC AppleScriptLib
   (2026-10-27): the same 42 `COMPONENT_CALL` entries are PowerPC exports
   too (the instance in r3, the rest after it, made into the 68k call to
   `_ComponentDispatch`). CFM still finds Apple's file first where the
   System has one: Apple's exports 78 routines (the `AS...` calls,
   properties and handlers, terminology, storage types, the debugger),
   these 42 answer where it isn't. Check: RetroTestPPC with Apple's file
   deleted from the run's System (`retro68_ppc_executor_asl`,
   `test_guest_suite.sh --delete-file`).

Each step: `ctest -L executor` and `multiversal_retro68`, which grows a
program per library (built against the generated headers alone).

## Upstream

The keys and generator changes go upstream as one change to
autc04/multiversal: the schema (`multiversal.schema.json`), both
generators, and the definitions. Upstream's generator has since been
rewritten on its Executor side (README.md, "Upstream since the
import"), so the Executor half needs porting there; the C half and the
definitions apply as they are. Nothing is sent without asking.

## Seeing where things stand

`tools/macdecode/libraries.py --scan` reads the import tables of every
PEF container (PowerPC and CFM-68K) on the disk images and the export
tables of Apple's libraries as MPW ships them (stub libraries; names
only), into `<storage>/macdecode/libraries.json`; without `--scan` it
prints a summary per library. The macdecode web view
(`tools/macdecode/macdecode.py SNAPSHOT --serve`, default port 8765) has a
**Libraries** page joining that with what the definitions say Executor
answers, read on every load: per library, Apple's exports, Executor's,
what programs import and what of that Executor lacks; then every symbol,
filtered by library, Executor has/lacks and imported or not ("work
list"), with the routes (PowerPC, CFM-68K, 68k ASLM set and name) and
the routine.

## Tools on the Retro68 side

- Retro68's `ImportLibraries` (89 PEF import stubs, generated in 2015
  from Apple's libraries) list every export of each Apple CFM library:
  the checklist for what a library's definitions still lack.
- The toolchain in `toolchain/retro68` is 68k only. A Retro68 with
  PowerPC (and 68k, Multiversal) is built at
  `~/opt/retro68/build-ppc/toolchain` (2026-10-26: `build-toolchain.bash
  --multiversal --prefix=...`, from the same source; `nproc` capped at 4).
  **RetroTest** (`tests/guest/retro68`, `build.sh`) is built with it for
  both processors against the headers generated from the definitions
  alone: traps, the Component Manager, OSA compiling and running
  `6 * 7` (68k inlines; InterfaceLib and AppleScriptLib imports on
  PowerPC). It passes on Executor (`retro68_executor`,
  `retro68_ppc_executor`) and on Apple's systems (`retro68_rom`: the
  Quadra with 7.5.5; `retro68_rom_ppc`: Mac OS 9.0.4), the binaries
  committed. Found on the way: 7.5.5's Component Manager has no
  `OpenAComponent` (Component Manager 3.0's; Executor answers it), and
  Retro68's PowerPC runtime imports `GetVRefNum` (an InterfaceLib export
  now).
- Retro68's `LaunchAPPL` has an Executor backend (`-e executor`: runs the
  application with `--executor-path`, resource fork as `%`AppleDouble).
  `tools/retro68/launchappl-executor` is that executor for MacPhoenix
  (`--backend executor --no-webserver`, the application's folder shared
  with `--extfs`, which is where the tests write `out`; mac-phoenix exits
  when the application quits), and `tools/retro68/automated_tests.py`
  runs Retro68's `AutomatedTests` through it, 68k and PowerPC, from the
  Retro68 build tree's own test list and expected output (the tree only
  read). All 32 pass on Executor's own System (2026-10-27; ctest
  `retro68_automated_executor`).
