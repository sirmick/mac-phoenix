# StdCLib and MathLib for PowerPC on Executor

Milestone P (PLAN.md). A Power Mac has Apple's StdCLib (the MPW C library)
and MathLib (fp.h, fenv.h, SANE's decimal records) in ROM; MPW-built
PowerPC applications import both, CodeWarrior's import MathLib (MSL formats
numbers with `num2dec`). Executor has no ROM, so it answers both itself in
C++, as it does InterfaceLib and Open Transport: `src/executor/romlib/clib/`.

```bash
ctest -R clib_executor             # CLibTest on Executor's libraries
ctest -R clib_rom_ppc              # the same on Apple's (Mac OS 9.0.4, mame-ppc): the reference
ctest -R clib_cfm68k               # CLibTest68K on Executor's (_executor) and Apple's NuStdCLib
ctest -R devtest_                  # DevTest, the device table: Executor's and Apple's (9.0.4)
ctest -R mpw_executor_86           # MPW Shell PowerPC on 8.6's System: its menus, redirection
ctest -R guest_suite_ppc_executor  # the fat MacPhoenixSuite's PowerPC code (--executor-ppc)
tools/mpw/mpw build tests/guest/clib   # CLibTest (MrC + PPCLink) and CLibTest68K (SC + ILink)
tools/mpw/mpw build tests/guest/devtest  # DevTest; copy build/DevTest.bin next to the source
```

## What is there

| File | What |
|------|------|
| `clib/clib.h` | guest memory access, a call's parameter words (`PPCArgs`; `CallArgs` for either processor), variadic arguments from a call or a `va_list` (`VarArgs`), export helpers |
| `clib/stdclib.cpp` | string.h, ctype.h (`__p_CType`), stdlib.h (numbers, the Memory Manager's heap for `malloc`, `qsort`/`bsearch` calling the program's comparison routines (`callClient`), `atexit`, `exit`), setjmp.h, signal.h, time.h (Mac seconds since 1904), locale.h, PLStringFuncs.h, IntEnv.h's path routines, `TrapAvailable`, the data an MPW `__start` imports (`_IntEnv`, `__C_phase`, `__target_for_exit`, `_exit_status`, `__NubAt3`, `StandAlone`), float.h's constants |
| `clib/stdio.cpp` | stdio.h on the File Manager (Mac paths, full or partial from the default directory), `_iob[40]` laid out as stdio.h's `FILE` (the `getc`/`putc` macros use `_cnt`/`_ptr`, then `_filbuf`/`_flsbuf`), the MPW low-level calls (`open`, `read`, `write`, `lseek`, `ioctl`, `faccess`) |
| `clib/format.cpp` | printf and scanf conversions |
| `clib/mathlib.cpp` | MathLib: double routines from the host's libm, the `l` routines on MathLib's double-double (a register pair, f1:f2), `fe*` on the guest's FPSCR, extended80 conversions, `num2dec`/`dec2num`/`str2dec`/`dec2str`, NaN codes |

What each library exports is in the definitions,
`multiversal/defs/StdCLib.yaml` and `MathLib.yaml` (docs/executor/
LIBRARIES.md): one entry per export, with `library:` and the C++ routine
that answers it (`executor: c_strlen`); the generator registers them for
PowerPC and CFM-68K, typed from the declaration, and they go through
`--executor-logtraps` like traps. Routines that read their own arguments
(variadic ones, MathLib's `l` routines on f1:f2) are `raw: true`: the
`exportRaw` calls in the sources register them, and builtinlibs reports a
raw export in either place but not the other. Data exports (`errno`,
`_iob`, ...) are registered by the sources. The entries are only for
Executor: a guest program's C headers come with its C library.

**CFM-68K** (STRAGGLERS 60): MPW's CFM-68K programs import StdCLib as
`NuStdCLib` and `NuIntEnv`; both are Executor's StdCLib, for 68k callers.
The printf and scanf family read their arguments from the 68k stack
(`CallArgs`: `PPCCallArgs`, `CFM68KCallArgs`; a variadic long double is 12
bytes there), `_iob`'s stride is the client's (`FILE` is 22 bytes in SC's
code, 24 in MrC's), and callbacks into the program (`callClient`) run on
its processor. CLibTest68K (the same `clib.c`, SC + ILink; build.mpw)
passes on Executor and on the Quadra with Apple's NuStdCLib, which answers
eleven things otherwise than Mac OS 9's StdCLib (printf's NaN codes and
`-0`, `%#x` of 0, `%.20f` exact, `%p` in strftime, `tm_isdst` -1): the
test takes either on 68k, and Executor's gives Mac OS 9's on both.

StdCLib's state (streams, `atexit`, signal handlers, `errno`) starts afresh
for each application that imports it and its streams are flushed and closed
when it ends (`ROMlib_cfm_terminate`).

## The device table

The low-level calls (`open`, `read`, `write`, `lseek`, `ioctl`, `close`,
`faccess`, `unlink`, `rename`) and stdio above them go through StdCLib's
device table, which a program can read and extend (`_getDevHandler`,
`_addDevHandler`); MPW Shell installs its own devices there for its
windows and pipes. `clib/stdio.cpp` keeps it as Apple's does on Mac OS 9.0.4
(DevTest, and DevProbe's logs of each handler call before it):

- Five slots: 0 `FSYS` (files, through the File Manager), 1 `CONS` (the
  console), 2 `SYST` (`Dev:Null`, `Dev:StdIn`/`StdOut`/`StdErr`), 3 and 4
  free. `_addDevHandler(slot, name, faccess, close, read, write, ioctl)`
  returns the slot, -1 past 4; `_getDevHandler` 0, or -1 for an empty slot.
  The handlers are Executor's own PowerPC or CFM-68K routines
  (`builtinlibs`), so a program can wrap them and call the originals.
- A name is offered to the devices from slot 4 down; `faccess` answers -1
  (not mine), 0, an errno or `0x4000 | OSErr`. `open` and `unlink` turn
  the OSErr into an errno (fnfErr ENOENT, nsvErr ENXIO: `dev:test`).
  After a 0, the descriptor's device is set if the handler left it 0.
- The descriptor a handler gets (20 bytes): flags (the `open` flags with
  the access mode plus 1), an error word, the device entry, the device's
  own word, the count left and the buffer; read and write move the last
  two on. A handler's nonzero result goes to `errno` as it is and the call
  returns -1.
- `lseek` is `FIOLSEEK` ({whence, offset}, the new position in the second
  word), `fcntl(F_DUPFD)` is `FIODUPFD` to the device, then the descriptor
  copied. `FSYS` answers `FIOINTERACTIVE`, `FIOBUFSIZE` and `FIOFNAME`
  with EINVAL (stdio asks the first two when it sets up a buffer),
  `FIOREFNUM` with the refnum, `FIOSETEOF` with the argument as the EOF.
  Its device word is a handle to {references, refnum, ...}; MPW reads the
  refnum at +2.
- `dev:stdout` and the others are dups of descriptors 0-2; the standard
  streams open `dev:console` at their first use.
- `MakeResolvedPath(vol, dir, path, resolveLeaf, buffer, ...)` puts the
  path in buffer as a Pascal string, also when it fails; MPW Shell finds
  its commands with it (`{Commands}`).

## Behaviour: Apple's, checked

CLibTest checks 290 things on both machines. Where the C standard leaves
room, Executor does what Apple's does on the Power Mac:

- `strerror`'s texts ("Error 0 - No error", "Math result not
  representable", ..., "Unknown error").
- printf: infinities `INF`/`-INF`, NaNs `NAN(017)` with SANE's NaN code,
  upper case for every conversion; `%s` of a null pointer prints nothing;
  `%#x` and `%p` of 0 print `0x0`.
- New files are `TEXT`/`ttxt` (SimpleText's). stdout is fully buffered,
  stderr line-buffered. The console streams have no window: what is
  written to them goes to Executor's log (`[stdout] ...`); stdin is at its
  end.
- strftime: `%p` is " AM"/" PM", `%X` "08:04:03 AM", `%x` "Monday, March 5,
  2001", `%c` the two together (the U.S. international resources' forms).
- `setlocale` answers every category's locale at once: `#0#0#1#1#1` for
  "C", `#0#0#2#2#2` for the native locale ("").
- `signal` takes a mask of the six signal bits; a mask with none answers
  `SIG_DFL`.
- `PLpos(sub, str)`: the substring first, as Pascal's `Pos`.
- NaN codes in bits 37-44 (`nan("17")` is `7FF8022000000000`; past 255,
  255); `sqrt`, `log` and `acos`/`asin` of an invalid argument give SANE's
  codes 1, 36 and 34. In a decimal record a NaN is `N` and four hex digits
  (`N4011`; `str2dec` of something that isn't a number gives `N0011`).
  `dec2str` puts a blank before a positive FLOATDECIMAL number (` 3.1416e+0`,
  ` INF`), not before a FIXEDDECIMAL one.

Where Apple's library is merely imprecise, Executor's is exact and the test
takes either: Apple's `sqrt` and `log` are an ulp off at times; its decimal
conversions (printf, `num2dec`) approximate past 17 significant digits
(`%.20f` of 0.1 gives `0.10000000000000000000`, `%f` of 1e20
`99999999999999987584.860160`) and `%g` of 1e-100 prints `0`; a
FIXEDDECIMAL conversion needing more than 36 digits gives 36 digits of an
approximation where Executor's gives `?`.

## Not done

- MathLib for CFM-68K programs: none needs it as a shared library (MPW
  links NuMathLib.o in); CLibTest68K's MathLib checks are PowerPC-only.
- `fe*` exception flags: what Executor's MathLib raises it records in the
  FPSCR; what the guest's own floating-point instructions raise depends on
  the core (mame-ppc keeps no FPSCR exception bits).
- NaN codes for other invalid operations (pow 37, trig 33, remainder 9)
  are SANE's numbers but not checked against Apple's.
- The native locale changes nothing but `setlocale`'s answer.
- Not matched (DevProbe): a failing `ioctl` on descriptor 0 leaves ENOENT
  in `errno` on Apple's (the console open's fnfErr kept in the
  descriptor), EINVAL on Executor's.
- `_RTInit`, `_RTExit`, `_DoExitProcs` and stdio's other internals not in
  the survey answer 0 and are logged (`[unimplemented] PowerPC NAME`).

## On the way

- CFM ran no PowerPC init or term routines (upstream's call worked only on
  a PowerPC host); it runs both now, as for CFM-68K, through `callPPC`
  (`base/cpu.cpp`), which also serves `qsort` and `atexit`.
- An exception out of a system call (ExitToShell's, from `exit`) can't
  unwind through the recompiler's native frames: `PowerCore` stops at the
  exit cell and rethrows from C++.
- A full path on a volume that isn't mounted gave the default volume's
  answer (fnfErr) where a Mac says nsvErr (hfsXbar.cpp `getVolume`).
- MPW Shell PPC on 8.6: a routine called through its transition vector
  gets the vector in r12 (`callPPC`, Mixed Mode), a call into a patched OS
  trap's table entry the trap word in D1, `fputs` returns the count.
- PowerPC imports nothing exports are logged and answer 0 instead of
  aborting; unresolved data imports get zeroed storage, not a routine.
- `CallComponentUPP` for PowerPC (AppleScriptLib's component calls); the
  glue copied the parameters without the component instance after them.
- AppleScriptLib's OSA routines are Executor's own PowerPC exports too
  (OSA.yaml, `COMPONENT_CALL`; LIBRARIES.md), for a System without Apple's
  AppleScriptLib file.
- SANE's `Fx2dec` overflowed two stack buffers for numbers from about
  1e170 up (MrC folding `num2dec(&f, 1e300, &d)` crashed the MPW session).
- mame-ppc reloads the rounding mode after a host op, so `fesetround`
  takes effect.

## Clean room

From the MPW C headers' declarations and constants (stdio.h's `FILE`,
fenv.h, fp.h's `decimal`, ...), the C standard, the Apple Numerics Manual,
and CLibTest's and DevTest's answers on the Power Mac (and CLibTest68K's on the Quadra
with the Enabler). No Apple library code was read. Which routines are in
NuStdCLib and which in NuIntEnv came from the export tables (names only) of
MPW's StdCLib stub library; the CFM-68K conventions from SC's code for our
own sources.
The survey of what applications import (StdCLib 138 PEF containers,
MathLib 181 on the provisioned images) read only PEF import tables.
