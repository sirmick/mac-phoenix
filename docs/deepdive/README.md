# Deep dives

Detailed technical notes on specific subsystems. The bird's-eye view is
in [`../Architecture.md`](../Architecture.md); these docs zoom into
quirks and design decisions that aren't obvious from reading the code.

## CPU

- [`cpu/UaeQuirks.md`](cpu/UaeQuirks.md) — UAE byte-swap dance, `regs.pc`
  vs `regs.pc_p`, `HAVE_GET_WORD_UNSWAPPED`, STOP semantics, prefetch.
- [`cpu/UnicornQuirks.md`](cpu/UnicornQuirks.md) — register-write
  persistence inside hooks, SR-as-uint32, MMIO via `uc_mmio_map`,
  notdirty SMC handling, IRQ storm history.
- [`cpu/ALineAndFLineStatus.md`](cpu/ALineAndFLineStatus.md) — A-line /
  F-line trap dispatch via deferred register updates.
- [`cpu/CpuBackendApi.md`](cpu/CpuBackendApi.md) — backend interface as
  Platform pointers (`cpu_execute_one` / `cpu_execute_fast`).
- [`cpu/CpuModelConfiguration.md`](cpu/CpuModelConfiguration.md) — UAE
  CPU level + Unicorn `UC_CPU_M68K_*` selection (now driven by machine
  profiles).
- [`cpu/CpuTraceDebugging.md`](cpu/CpuTraceDebugging.md) — `CPU_TRACE`
  format, the `scripts/diff_cpus.sh` workflow.
- [`cpu/JitBlockSizeAnalysis.md`](cpu/JitBlockSizeAnalysis.md) — why
  Unicorn block sizes average 1.95 instructions and what that means for
  IRQ timing.

## Architecture

- [`MemoryArchitecture.md`](MemoryArchitecture.md) — direct addressing,
  endianness, the m68k Quadra layout (RAM / ROM / ScratchMem /
  framebuffer / NuBus stubs / MMIO).
- [`PlatformAdapterImplementation.md`](PlatformAdapterImplementation.md) —
  null-driver-default + adapter pattern for video / audio / scsi /
  serial / ether / disk / platform.
- [`PlatformAPIInterrupts.md`](PlatformAPIInterrupts.md) — why
  `cpu_trigger_interrupt(level)` is a Platform pointer, why Unicorn
  builds m68k exception frames manually instead of poking QEMU's
  `m68k_set_irq_level`.

## Analyses

- [`JitSmcDetectionAnalysis.md`](JitSmcDetectionAnalysis.md) — what
  Unicorn's QEMU fork stubbed out in `ram_addr.h` and why we still need
  the STALE-TB detector for Mac OS heap overwrites.
- [`InterruptTimingAnalysis.md`](InterruptTimingAnalysis.md) — why UAE
  and Unicorn-m68k diverge at instruction #29,518; expected
  non-determinism from wall-clock 60 Hz timers.
- [`UaeVsUnicornImplementationAnalysis.md`](UaeVsUnicornImplementationAnalysis.md) —
  feature-by-feature comparison of the two m68k backends.

For the perf breakdown of Unicorn-m68k vs UAE see
[`../UnicornPerformanceAnalysis.md`](../UnicornPerformanceAnalysis.md).
