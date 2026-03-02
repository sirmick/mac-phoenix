# Deep Dive Documentation

Detailed technical documentation on specific subsystems.

---

## Active Investigations

### [InterruptTimingAnalysis.md](InterruptTimingAnalysis.md)
**Critical**: Explains why UAE and Unicorn diverge at instruction #29,518

**Key Finding**: Timer interrupts fire at different instruction counts because:
- Interrupts based on wall-clock time (not instruction count)
- UAE (interpreter) runs slower → interrupt fires earlier
- Unicorn (JIT) runs faster → interrupt fires later
- **Not a bug** - characteristic of wall-clock-based timing

**Recommendation**: Accept non-determinism, use functional testing instead of exact trace matching

---

## CPU Backend Documentation

**All CPU-related docs have been moved to [cpu/](cpu/) subdirectory for better organization.**

### Essential CPU Docs

#### [cpu/UnicornQuirks.md](cpu/UnicornQuirks.md)
**IMPORTANT - READ THIS FIRST**: Unicorn Engine quirks and solutions
- PC changes in hooks: solved via deferred register updates
- JIT TB invalidation: 60Hz flush workaround
- MMIO: must use `uc_mmio_map()` (JIT bypasses `UC_HOOK_MEM_READ`)
- SR requires `uint32_t*` for `uc_reg_write()`
- Hook types (UC_HOOK_BLOCK, UC_HOOK_INTR)

#### [cpu/ALineAndFLineStatus.md](cpu/ALineAndFLineStatus.md)
**Current Status**: A-line/F-line trap handling -- **WORKING** (March 2026)
- ✅ All A-line traps working via deferred register updates
- ✅ 87 OS trap table entries populated (matching UAE)
- ✅ Previous Unicorn PC limitation overcome

#### [cpu/UaeQuirks.md](cpu/UaeQuirks.md)
**Essential**: UAE CPU core quirks and gotchas
- Byte-swapping (RAM little-endian, ROM big-endian)
- `HAVE_GET_WORD_UNSWAPPED` flag
- `mem_banks[]` memory access
- `pc_p` pointer vs `m68k_getpc()` function

### CPU API and Architecture

#### [cpu/CpuBackendApi.md](cpu/CpuBackendApi.md)
CPU backend interface specification - how backends implement Platform API

#### [cpu/CpuModelConfiguration.md](cpu/CpuModelConfiguration.md)
CPU model selection (68020 vs 68030 vs 68040)

#### [cpu/DualCpuValidationInitialization.md](cpu/DualCpuValidationInitialization.md)
How dual-CPU validation mode initializes

### Trap and Exception Handling

#### [cpu/ALineAndFLineTrapHandling.md](cpu/ALineAndFLineTrapHandling.md)
**Detailed Design**: How A-line (0xAxxx) and F-line (0xFxxx) traps work
- M68K exception mechanism
- Vector table reading
- Handler execution
- **NOTE**: Implementation doesn't work on Unicorn due to PC limitation

### Debugging and Analysis

#### [cpu/CpuTraceDebugging.md](cpu/CpuTraceDebugging.md)
CPU trace debugging techniques and tools

#### [cpu/JIT_Block_Size_Analysis.md](cpu/JIT_Block_Size_Analysis.md)
JIT translation block size analysis for performance tuning

### Historical Bug Investigations

#### [cpu/UnicornBugSrLazyFlags.md](cpu/UnicornBugSrLazyFlags.md)
Unicorn bug with SR (Status Register) lazy flag evaluation

#### [cpu/UnicornEarlyCrashInvestigation.md](cpu/UnicornEarlyCrashInvestigation.md)
Investigation of early Unicorn crashes (now resolved)

#### [cpu/UnicornRTEQemuResearch.md](cpu/UnicornRTEQemuResearch.md)
Research into RTE instruction handling

#### [cpu/UnicornBatchExecutionRTEBug.md](cpu/UnicornBatchExecutionRTEBug.md)
RTE bug in batch execution mode

---

## Memory System

### [MemoryArchitecture.md](MemoryArchitecture.md)
**Essential**: Direct addressing mode, memory layout
- How Mac addresses map to host memory
- ROM at 0x40800000, RAM at 0x00000000
- `MEMBaseDiff` calculation
- Why byte-swapping is needed

---

## Platform and Configuration

### [PlatformArchitectureOld.md](PlatformArchitectureOld.md)
Old platform architecture doc (superseded by [../Architecture.md](../Architecture.md))

### [PlatformAdapterImplementation.md](PlatformAdapterImplementation.md)
Detailed platform adapter implementation notes

### [CpuModelConfiguration.md](CpuModelConfiguration.md)
CPU model selection (68020 vs 68030 vs 68040)

---

## ROM and Patching

### [RomPatchingRequired.md](RomPatchingRequired.md)
What ROM patches are needed and why

### [FullMontyPlan.md](FullMontyPlan.md)
Original "full monty" implementation plan

---

## Historical Investigations

### [QemuExtractionAnalysis.md](QemuExtractionAnalysis.md)
Analysis of extracting QEMU M68K code for potential use

### [CpuTraceDebugging.md](CpuTraceDebugging.md)
CPU trace debugging techniques and tools

### [DualCpuValidationInitialization.md](DualCpuValidationInitialization.md)
How dual-CPU validation mode initializes

### [UnicornEarlyCrashInvestigation.md](UnicornEarlyCrashInvestigation.md)
Investigation of early Unicorn crashes (now resolved)

---

## Recommended Reading Order

### For Understanding Current System
1. [../Architecture.md](../Architecture.md) - Start here (high-level overview)
2. [MemoryArchitecture.md](MemoryArchitecture.md) - How memory works
3. [UaeQuirks.md](UaeQuirks.md) - UAE backend specifics
4. [UnicornQuirks.md](UnicornQuirks.md) - Unicorn backend specifics
5. [InterruptTimingAnalysis.md](InterruptTimingAnalysis.md) - Current investigation

### For Implementing New Features
1. [CpuBackendApi.md](CpuBackendApi.md) - Backend interface
2. [ALineAndFLineTrapHandling.md](ALineAndFLineTrapHandling.md) - Trap mechanism
3. [PlatformAdapterImplementation.md](PlatformAdapterImplementation.md) - Platform code

### For Debugging
1. [CpuTraceDebugging.md](CpuTraceDebugging.md) - Trace analysis
2. [InterruptTimingAnalysis.md](InterruptTimingAnalysis.md) - Timing issues
3. [UnicornEarlyCrashInvestigation.md](UnicornEarlyCrashInvestigation.md) - Historical crashes

---

**Note**: Most docs here are detailed, technical deep-dives. For quick reference, see top-level docs:
- [../README.md](../README.md) - Quick start
- [../Architecture.md](../Architecture.md) - System overview
- [../Commands.md](../Commands.md) - Build and test commands
