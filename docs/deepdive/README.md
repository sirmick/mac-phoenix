# Deep Dive Documentation

Detailed technical documentation on specific subsystems.

---

## CPU Backend Documentation

All CPU-related docs are in [cpu/](cpu/).

### Essential CPU Docs

- **[cpu/UnicornQuirks.md](cpu/UnicornQuirks.md)** — Unicorn Engine quirks: PC changes in hooks, deferred updates, MMIO, SR uint32_t
- **[cpu/ALineAndFLineStatus.md](cpu/ALineAndFLineStatus.md)** — A-line/F-line trap handling status (WORKING via deferred updates)
- **[cpu/UaeQuirks.md](cpu/UaeQuirks.md)** — UAE CPU core quirks: byte-swapping, mem_banks, pc_p vs m68k_getpc()
- **[cpu/CpuBackendApi.md](cpu/CpuBackendApi.md)** — Unified CPU backend API (implemented via Platform struct)
- **[cpu/CpuModelConfiguration.md](cpu/CpuModelConfiguration.md)** — CPU model matching for dual-CPU validation (now driven by machine profiles)
- **[cpu/CpuTraceDebugging.md](cpu/CpuTraceDebugging.md)** — Trace debugging tools
- **[cpu/JitBlockSizeAnalysis.md](cpu/JitBlockSizeAnalysis.md)** — JIT translation block size analysis

---

## Analysis & Investigations

### [JitSmcDetectionAnalysis.md](JitSmcDetectionAnalysis.md)
Root cause analysis of Unicorn's broken self-modifying code detection. Guest-to-guest path works via `notdirty_write()`. STALE-TB detector catches remaining edge cases.

### [InterruptTimingAnalysis.md](InterruptTimingAnalysis.md)
Why UAE and Unicorn diverge at instruction #29,518. Timer interrupts fire at different instruction counts due to wall-clock timing. Not a bug — accept non-determinism.

### [UaeVsUnicornImplementationAnalysis.md](UaeVsUnicornImplementationAnalysis.md)
Comprehensive comparison of UAE vs Unicorn backends: feature matrix, deferred register updates solution, full boot parity.

---

## Architecture

### [MemoryArchitecture.md](MemoryArchitecture.md)
Direct addressing mode, memory layout (RAM at 0x0, ROM at 0x02000000), Unicorn extended map.

### [PlatformAdapterImplementation.md](PlatformAdapterImplementation.md)
Platform adapter pattern: all drivers use `g_platform` function pointers with null defaults. **All adapters complete.**

### [PlatformAPIInterrupts.md](PlatformAPIInterrupts.md)
Interrupt abstraction via platform API. Replaces global PendingInterrupt with backend-specific implementations.

### [RomPatchingRequired.md](RomPatchingRequired.md)
Why ROM patching is needed for dual-CPU testing.

---

## Recommended Reading Order

### For Understanding Current System
1. [../Architecture.md](../Architecture.md) — High-level overview
2. [MemoryArchitecture.md](MemoryArchitecture.md) — How memory works
3. [cpu/UaeQuirks.md](cpu/UaeQuirks.md) — UAE specifics
4. [cpu/UnicornQuirks.md](cpu/UnicornQuirks.md) — Unicorn specifics

### For Implementing New Features
1. [cpu/CpuBackendApi.md](cpu/CpuBackendApi.md) — Backend interface
2. [PlatformAdapterImplementation.md](PlatformAdapterImplementation.md) — Platform code

### For Debugging
1. [cpu/CpuTraceDebugging.md](cpu/CpuTraceDebugging.md) — Trace analysis
2. [InterruptTimingAnalysis.md](InterruptTimingAnalysis.md) — Timing issues

---

**Note**: For quick reference, see top-level docs:
- [../README.md](../README.md) — Quick start
- [../Architecture.md](../Architecture.md) — System overview
- [../Commands.md](../Commands.md) — Build and test commands
- [../ppc/](../ppc/) — PPC-specific documentation
