# Deep dives

Detailed technical notes on specific subsystems. The bird's-eye view is
in [`../Architecture.md`](../Architecture.md); these docs zoom into
quirks and design decisions that aren't obvious from reading the code.

## CPU

- [`cpu/UaeQuirks.md`](cpu/UaeQuirks.md) — the `uae` core: byte-swapped
  opcode fetch (`HAVE_GET_WORD_UNSWAPPED`, `cft_map`), `regs.pc_p`, STOP
  semantics, CPU level selection.
- [`cpu/CpuBackendApi.md`](cpu/CpuBackendApi.md) — how a CPU core plugs in:
  `cpu::Core` and the Platform CPU entries.
- [`cpu/CpuModelConfiguration.md`](cpu/CpuModelConfiguration.md) — how the
  68k model (68000–68040, FPU, 24/32-bit) is chosen from the machine
  profile.

## Architecture

- [`MemoryArchitecture.md`](MemoryArchitecture.md) — the 68k ROM machine's
  guest memory: the host block, `GuestMemory`, endianness, 24-bit
  machines, ScratchMem and frame buffer.
- [`PlatformAdapterImplementation.md`](PlatformAdapterImplementation.md) —
  adapters, default drivers and real drivers for video / audio / scsi /
  serial / ether / disk / platform.
- [`PlatformAPIInterrupts.md`](PlatformAPIInterrupts.md) — how the 60 Hz
  tick reaches a core through `cpu_trigger_interrupt`.
