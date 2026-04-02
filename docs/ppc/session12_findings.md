# Session 12: Full PPC Audit — mac-phoenix vs legacy SheepShaver

## Summary

Line-by-line comparison of interrupts, drivers, threading, timer, EmulOps,
JIT config, and CPU config between `src/cpu/kpx/` (mac-phoenix) and
`legacy/SheepShaver/src/kpx_cpu/` (upstream kanjitalk755).

## Confirmed Non-Issues

| Area | Finding |
|------|---------|
| `interrupt()` signature | mac-phoenix 1-arg version sets `gpr(1) = KernelDataAddr` internally (line 868). Identical to legacy. |
| EmulOp dispatch chain | Complete: `execute_sheep` → `execute_emul_op` → `g_platform.ppc_emulop_handler` → `ppc::EmulOp`. Null-checked. Registered before CPU starts. |
| `InstallDrivers()` | PPC has its own `void ppc::InstallDrivers(void)` in `rom_patches_ppc.cpp`. No parameter mismatch. |
| JIT defines | Both use `PPC_ENABLE_JIT=1`, `PPC_REENTRANT_JIT=1`, `PPC_DECODE_CACHE=1`, `PPC_FLIGHT_RECORDER=1`, `REAL_ADDRESSING=1`. |
| PVR / clocks | `PVR=0x000c0000`, `TB=25MHz`, `Bus=100MHz`, `CPU=100MHz` — same. |
| EmulOp opcode values | `M68K_EMUL_BREAK = 0xfe43` — same enum, same derived constants. |
| Timer Manager traps | `InsTime/RmvTime/PrimeTime` all implemented in PPC EmulOp handler. |
| `PRECISE_TIMING` | Both legacy and mac-phoenix have `#if !PRECISE_TIMING` guard in `OP_IRQ`. The POSIX timer thread (`timer.cpp:596`) starts via `TimerInit()` and wakes when `PrimeTime()` schedules a task. Identical behavior. |

## Issues Fixed in This Session

### 1. Pervasive debug logging in emul_op_ppc.cpp

**Problem**: 15+ `fprintf(stderr, ...)` calls added to EmulOp handlers that
legacy doesn't have. Adds noise, changes timing, makes diff unreadable.

**Fix**: Stripped all debug logging to match legacy `emul_op.cpp` line-for-line.
Kept only `D(bug(...))` calls that legacy also has.

### 2. Stall detection in emul_op_ppc.cpp and tick_thread_func

**Problem**: `emulop_total`, `last_cl_count`, `stall_tracing`, `post_stall_log`
counters plus a 10-second stall detector with GPR/stack dumps. None in legacy.

**Fix**: Removed entirely.

### 3. HI-RATE / PASS-FAIL diagnostics in HandleInterrupt

**Problem**: ~60 lines of per-second rate counters, KernelData dumps, and
a 5-second PASS/FAIL verdict. Legacy HandleInterrupt has none of this.

**Fix**: Stripped to match legacy (just the IRQ nest check + mode switch).

### 4. Tick thread bloat (status, stall detector, mouse injection)

**Problem**: tick_thread_func had ~90 lines of stall detection, 5-second MIPS
status, and mouse position injection. Legacy tick_func is 30 lines.

**Fix**: Stripped to match legacy tick_func structure. Kept only: timing loop,
1Hz time update, 60Hz interrupt trigger, and video refresh.

### 5. Debug logging in timer.cpp

**Problem**: `timer_func()` and `TimerInterrupt()` had rate counters and
fire-count logging not present in legacy.

**Fix**: Stripped to match legacy.

### 6. Unnecessary setup_timer_interrupt() for PPC

**Problem**: `cpu_context.cpp:657` calls `setup_timer_interrupt()` which
initializes the polling timer. But PPC uses its own KPX tick thread
(`cpu_ppc_kpx.cpp:1119`), and `poll_timer_interrupt()` is never called
from the KPX execution loop. Unnecessary initialization.

**Fix**: Skipped `setup_timer_interrupt()` for PPC backend.

### 7. OP_NTRB_17_PATCH4 forces PatchAfterStartup

**Problem**: mac-phoenix calls `PatchAfterStartup_PPC()` from
`OP_NTRB_17_PATCH4` because accRun never fires. Legacy only calls
`PatchNativeResourceManager()`.

**Status**: KEPT as intentional delta. Without it, NQD acceleration and
ExtFS never install. Added clear comment explaining why.

## Architectural Differences (by design, not bugs)

| Area | Legacy | Mac-Phoenix | Why |
|------|--------|-------------|-----|
| Interrupt trigger | `pthread_kill(emul_thread, SIGUSR2)` | `ppc_cpu->trigger_interrupt()` (spcflags) | Polling replaces signals; works for emulated PPC |
| Tick thread | `tick_func` in `main_unix.cpp` | `tick_thread_func` in `cpu_ppc_kpx.cpp` | Same logic, different location |
| Timer interrupt | `timer_interrupt.cpp` polling | KPX tick thread | KPX has its own; polling timer unused |
| EmulOp dispatch | Direct `EmulOp()` call | Via `g_platform.ppc_emulop_handler` | Platform API abstraction |
| Boot progress | None | `boot_progress_report()` | Web UI needs boot phase tracking |
| `sigaltstack` | Set/restore around nanokernel | None | Not needed for emulated PPC (no signals) |
| Serial drivers | Runtime `gen_*_driver()` | Static byte arrays | Equivalent; different ROM offsets |
| ROM offsets | Dynamic `find_rom_resource()` | Hardcoded `0x34680` / `0x31bae` | Target-ROM specific |

## Files Modified

- `src/cpu/kpx/emul_op_ppc.cpp` — stripped debug logging, stall detection
- `src/cpu/kpx/cpu_ppc_kpx.cpp` — stripped HandleInterrupt diagnostics, tick thread bloat
- `src/core/timer.cpp` — stripped timer_func/TimerInterrupt logging
- `src/core/cpu_context.cpp` — skip setup_timer_interrupt for PPC
