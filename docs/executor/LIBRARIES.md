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
| Traps | generated `PASCAL_TRAP`, `PASCAL_SUBTRAP`, `REGISTER_TRAP`, `NOTRAP_FUNCTION` (`base/traps.h`) | ~2,000 | from the C++ signature | the trap table; the same wrappers export each one from InterfaceLib for PowerPC and CFM-68K (`base/traps.impl.h`) |
| Open Transport over ASLM | `Def{set, name, Conv, "LLLLL", 'L', impl}` (`opentransport/ot_*.cpp`), index-to-name lists (`ot_names.inc`) | ~130 of ~330 names | no: signature strings, `impl(const Call&)` reads its arguments | 68k only; no `OpenTransportLib` for PowerPC |
| StdCLib, MathLib | `exportFn(LIB, "strlen", &c_strlen)`, `exportRaw` for variadic and hand-decoded routines, `exportData` (`clib/`) — in the definitions since step 2 | ~250 + ~70 raw + 25 data | yes (`callfromPPC::Invoker`) | PowerPC; StdCLib for CFM-68K too since step 2 |
| CFM glue | `exportGlue("c2pstr", &fn)` (`cfm68k_glue.cpp`) | ~50 | yes | InterfaceLib, ObjectSupportLib, for CFM-68K and PowerPC |
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
    aslm: { set: utils, index: 12 }   # the 68k route through ASLM, if any
    callconv: C                  # C (default for libraries) or Pascal
    executor: C_
```

- `library:` names the CFM library that exports the symbol. Several
  architectures can share it; a routine that exists in only one says so
  (`only-for: ppc`, the existing filter key).
- `aslm:` gives the ASLM function set and index the 68k glue binds by
  (Open Transport's static glue: `__SLM11FuncDispatch`). The set's
  library name and version go in a `- aslm-set:` entry, once per set.
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

A component call says its selector; the component type is the
dispatcher's:

```yaml
- dispatcher:
    name: OSAComponent
    component: osa               # component type 'osa '
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
like a trap. The Component Manager's own calls (`OpenComponent`,
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

## Order of work

The whole plan (agreed 2026-10-26):

0. Land what's done: `executor-ppc-libs` and `multiversal-retro68` into
   main.
1. Upstream multiversal's definitions and C side — done 2026-10-26
   (README.md, "Upstream since the import"); its Executor-side generator
   rewrite stays out.
2. to 5., below: the library routes one by one.
6. Outward: the upstream change (only once seen and agreed), a Retro68
   with PowerPC to build the CFM side, Retro68's `AutomatedTests` on our
   Executor through `LaunchAPPL`.

Throughout, the aim is MacPhoenix's own guest programs building against
the generated headers alone (BridgeAgent without Apple's UI 3.4 first);
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
3. **The CFM glue** (`cfm68k_glue.cpp`'s `exportGlue` list).
4. **Open Transport.** The `Def` tables become definitions and the
   implementations take typed arguments instead of `Call`. By-products:
   `OpenTransportLib`, `OTUtilityLib`, `OpenTptInternetLib` for PowerPC
   (OpenTransport.md lists them as not written) and `OpenTransport.h`.
   Check: OTTest on 68k (the ASLM route) and PowerPC, the guest suite's
   network sections.
5. **Components.** `Components.yaml`'s own calls, then OSA
   (AppleScriptLib's table), then Executor's own native component (the
   sound output device, `sound/soundIMVI.cpp`).

Each step: `ctest -L executor` and `multiversal_retro68`, which grows a
program per library (built against the generated headers alone).

## Upstream

The keys and generator changes go upstream as one change to
autc04/multiversal: the schema (`multiversal.schema.json`), both
generators, and the definitions. Upstream's generator has since been
rewritten on its Executor side (README.md, "Upstream since the
import"), so the Executor half needs porting there; the C half and the
definitions apply as they are. Nothing is sent without asking.

## Tools on the Retro68 side

- Retro68's `ImportLibraries` (89 PEF import stubs, generated in 2015
  from Apple's libraries) list every export of each Apple CFM library:
  the checklist for what a library's definitions still lack.
- The toolchain in `toolchain/retro68` is 68k only (no
  `powerpc-apple-macos`); the PowerPC and CFM side of the C headers needs
  a Retro68 build with PowerPC.
- Retro68's `LaunchAPPL` has an Executor backend (`-e executor`: runs the
  application with `--executor-path`, resource fork as `%`AppleDouble)
  and its `AutomatedTests` run through it; pointed at MacPhoenix
  (`--backend executor --executor-app`), they would run Retro68's tests
  on our Executor.
