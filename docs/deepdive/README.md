# Deep dives

Detailed technical notes on specific subsystems. The bird's-eye view is
in [`../Architecture.md`](../Architecture.md); these docs zoom into
quirks and design decisions that aren't obvious from reading the code.

## CPU

- [`cpu/UaeQuirks.md`](cpu/UaeQuirks.md) — UAE byte-swap dance, `regs.pc`
  vs `regs.pc_p`, `HAVE_GET_WORD_UNSWAPPED`, STOP semantics, prefetch.
- [`cpu/CpuBackendApi.md`](cpu/CpuBackendApi.md) — backend interface as
  Platform pointers (`cpu_execute_one` / `cpu_execute_fast`).
- [`cpu/CpuModelConfiguration.md`](cpu/CpuModelConfiguration.md) — UAE
  CPU level selection (now driven by machine profiles).

## Architecture

- [`MemoryArchitecture.md`](MemoryArchitecture.md) — direct addressing,
  endianness, the m68k Quadra layout (RAM / ROM / ScratchMem /
  framebuffer).
- [`PlatformAdapterImplementation.md`](PlatformAdapterImplementation.md) —
  null-driver-default + adapter pattern for video / audio / scsi /
  serial / ether / disk / platform.
- [`PlatformAPIInterrupts.md`](PlatformAPIInterrupts.md) — why
  `cpu_trigger_interrupt(level)` is a Platform pointer and how UAE
  delivers it.
