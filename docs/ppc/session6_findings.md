# Session 6 — Findings & Status

> **Superseded.** The PPC_CHECK_INTERRUPTS=0 finding was incorrect — session 8
> proved that `compat/sysdeps.h` overrides the meson flag to 1, and the `-w`
> compiler flag suppressed the redefinition warning. The "fix" had no effect.

## Root cause found: PPC_CHECK_INTERRUPTS must be 0

Legacy SheepShaver uses `PPC_CHECK_INTERRUPTS=0`. With this setting:
- `trigger_interrupt()` is a no-op — tick thread interrupts never reach spcflags
- HandleInterrupt is never called via check_spcflags
- The nanokernel handles ALL interrupt delivery through its own OP_IRQ mechanism
- The tick thread's `SetInterruptFlag(INTFLAG_VIA)` sets flags that OP_IRQ reads

KPX had `PPC_CHECK_INTERRUPTS=1`, which added an extra HandleInterrupt path
that interfered with the nanokernel's mode transitions, limiting MODE_NATIVE
to 5-7/s instead of legacy's 55/s.

**Fix:** One line in `src/cpu/kpx/meson.build`: `PPC_CHECK_INTERRUPTS=0`

## ExtFS breaks mode engagement

Having a valid `--extfs` path causes the `InstallExtFS()` function to call
Execute68kTrap (Gestalt, FSM operations) during PatchAfterStartup_PPC.
These calls disrupt the MODE_NATIVE feedback loop, dropping peak from 55/s
to 6/s, which causes Mac OS to crash with error type 6768.

**Status:** `ppc::InstallExtFS()` is wired to the real `::InstallExtFS()`
(shared with m68k), but ExtFS paths should be omitted from PPC config
until the FSM timing interaction is resolved.

**Workaround:** Remove `extfs` from config, or point to nonexistent path.

## Boot status (without ExtFS)

- MODE_NATIVE: sustained peak 55/s (matches legacy)
- Boot phase: reaches "warm start" with 1000+ CHECKLOADs
- Does NOT yet reach Finder — stuck in warm start phase
- No crash or error when ExtFS is disabled
- Further investigation needed for boot completion

## Other fixes this session

- **Subprocess:** `--backend kpx` now passed to child process
- **Replay:** Guarded behind `#ifdef PPC_DETERMINISTIC_REPLAY` (disabled)
- **Debug cleanup:** 189 lines of instrumentation removed from hot paths
- **GPR save/restore in execute_macos_code:** Questionable, may be unnecessary

## Boot detection and shutdown dialog

Already implemented in `src/core/boot_progress.cpp`:
- Reads CurApName (Mac 0x0910) during OP_IRQ
- Detects "Finder" → PHASE_FINDER_LAUNCH
- IDLE_TIME after Finder → PHASE_DESKTOP
- `--dismiss-shutdown-dialog` auto-clicks through shutdown warning

This will work automatically once the boot reaches Finder.
