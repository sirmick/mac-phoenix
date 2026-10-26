# StdCLib and MathLib for PowerPC on Executor

Milestone P (PLAN.md). A Power Mac has Apple's StdCLib (the MPW C library)
and MathLib (fp.h, fenv.h, SANE's decimal records) in ROM; MPW-built
PowerPC applications import both, CodeWarrior's import MathLib (MSL formats
numbers with `num2dec`). Executor has no ROM, so it answers both itself in
C++, as it does InterfaceLib and Open Transport: `src/executor/romlib/clib/`.

```bash
ctest -R clib_executor             # CLibTest on Executor's libraries
ctest -R clib_rom_ppc              # the same on Apple's (Mac OS 9.0.4, mame-ppc): the reference
ctest -R guest_suite_ppc_executor  # the fat MacPhoenixSuite's PowerPC code (--executor-ppc)
tools/mpw/mpw build tests/guest/clib   # CLibTest: MrC + PPCLink on StdCLib and MathLib
```

## What is there

| File | What |
|------|------|
| `clib/clib.h` | guest memory access, a call's parameter words (`PPCArgs`), variadic arguments from a call or a `va_list` (`VarArgs`), export helpers |
| `clib/stdclib.cpp` | string.h, ctype.h (`__p_CType`), stdlib.h (numbers, the Memory Manager's heap for `malloc`, `qsort`/`bsearch` calling PowerPC comparison routines, `atexit`, `exit`), setjmp.h, signal.h, time.h (Mac seconds since 1904), locale.h, PLStringFuncs.h, IntEnv.h's path routines, `TrapAvailable`, the data an MPW `__start` imports (`_IntEnv`, `__C_phase`, `__target_for_exit`, `_exit_status`, `__NubAt3`, `StandAlone`), float.h's constants |
| `clib/stdio.cpp` | stdio.h on the File Manager (Mac paths, full or partial from the default directory), `_iob[40]` laid out as stdio.h's `FILE` (the `getc`/`putc` macros use `_cnt`/`_ptr`, then `_filbuf`/`_flsbuf`), the MPW low-level calls (`open`, `read`, `write`, `lseek`, `ioctl`, `faccess`) |
| `clib/format.cpp` | printf and scanf conversions |
| `clib/mathlib.cpp` | MathLib: double routines from the host's libm, the `l` routines on MathLib's double-double (a register pair, f1:f2), `fe*` on the guest's FPSCR, extended80 conversions, `num2dec`/`dec2num`/`str2dec`/`dec2str`, NaN codes |

StdCLib's state (streams, `atexit`, signal handlers, `errno`) starts afresh
for each application that imports it and its streams are flushed and closed
when it ends (`ROMlib_cfm_terminate`).

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

- Executor's CFM-68K side has no NuStdCLib yet (STRAGGLERS 60): the same C++
  exported to `m68k` fragments.
- `fe*` exception flags: what Executor's MathLib raises it records in the
  FPSCR; what the guest's own floating-point instructions raise depends on
  the core (mame-ppc keeps no FPSCR exception bits).
- NaN codes for other invalid operations (pow 37, trig 33, remainder 9)
  are SANE's numbers but not checked against Apple's.
- The native locale changes nothing but `setlocale`'s answer.
- `_RTInit`, `_RTExit`, `_DoExitProcs` and stdio's other internals not in
  the survey answer 0 and are logged (`[unimplemented] PowerPC NAME`).

## On the way

- CFM ran no PowerPC init or term routines (upstream's call worked only on
  a PowerPC host); it runs both now, as for CFM-68K, through `callPPC`
  (`base/cpu.cpp`), which also serves `qsort` and `atexit`.
- An exception out of a system call (ExitToShell's, from `exit`) can't
  unwind through the recompiler's native frames: `PowerCore` stops at the
  exit cell and rethrows from C++.
- PowerPC imports nothing exports are logged and answer 0 instead of
  aborting; unresolved data imports get zeroed storage, not a routine.
- `CallComponentUPP` for PowerPC (AppleScriptLib's component calls); the
  glue copied the parameters without the component instance after them.
- SANE's `Fx2dec` overflowed two stack buffers for numbers from about
  1e170 up (MrC folding `num2dec(&f, 1e300, &d)` crashed the MPW session).
- mame-ppc reloads the rounding mode after a host op, so `fesetround`
  takes effect.

## Clean room

From the MPW C headers' declarations and constants (stdio.h's `FILE`,
fenv.h, fp.h's `decimal`, ...), the C standard, the Apple Numerics Manual,
and CLibTest's answers on the Power Mac. No Apple library code was read.
The survey of what applications import (StdCLib 138 PEF containers,
MathLib 181 on the provisioned images) read only PEF import tables.
