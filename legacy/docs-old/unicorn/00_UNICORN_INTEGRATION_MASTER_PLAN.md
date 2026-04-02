# Unicorn Engine Integration - Master Plan
## Modern Mac Emulator with Validated CPU Replacement

**Project:** BasiliskII & SheepShaver - Next Generation
**Goal:** Replace legacy CPU emulation with Unicorn Engine
**Strategy:** Dual-CPU validation with comprehensive state tracking
**Timeline:** 8-10 weeks to production
**Created:** December 27, 2024

---

## Table of Contents

1. [Project Vision](#project-vision)
2. [Why Replace the CPU?](#why-replace-the-cpu)
3. [Why Unicorn Engine?](#why-unicorn-engine)
4. [Dual-CPU Validation Strategy](#dual-cpu-validation-strategy)
5. [New Build System Architecture](#new-build-system-architecture)
6. [Phase Breakdown](#phase-breakdown)
7. [Implementation Guide](#implementation-guide)
8. [Testing & Validation](#testing--validation)
9. [Timeline & Milestones](#timeline--milestones)

---

## Project Vision

### The Goal

Build a **modern, maintainable Mac emulator** that can run classic Macintosh software anywhere - from desktop to Raspberry Pi Zero to web browser.

**Core Modernization Goals:**

1. ✅ **Simplified Platform Support**
   - Linux, macOS, Windows only (remove BeOS, AmigaOS, legacy platforms)
   - Modern build system (Meson) replacing complex Autotools
   - Clean, logical project structure

2. ✅ **Universal CPU Emulation**
   - Unicorn Engine with cross-platform JIT (x86, ARM, RISC-V, etc.)
   - Same performance characteristics on all architectures
   - Works on Raspberry Pi, Apple Silicon, x86 servers

3. ✅ **Modern UI & Display**
   - IPC-based architecture for clean separation
   - Supports local display (SDL2) and remote display (WebRTC/VNC)
   - Web browser client for zero-install remote access

4. ✅ **Network-First Architecture**
   - Run Mac Classic headless on a Raspberry Pi Zero
   - Stream display to web browser at low latency
   - Full PPC or M68K emulation over the network
   - Remote desktop experience indistinguishable from local

5. ✅ **Proven Correctness**
   - Dual-CPU validation against original UAE/KPX
   - Comprehensive testing at every step
   - Zero regressions, full compatibility

### Use Cases

**Local Desktop:**
```bash
./BasiliskII --display local
# Runs Mac OS 7.5 on your Linux/Mac/Windows desktop
```

**Headless Server:**
```bash
# On Raspberry Pi Zero
./BasiliskII --display webrtc --port 8080

# In web browser
http://raspberrypi.local:8080
# Full Mac Classic desktop in browser, low latency
```

**Remote Work:**
```bash
# Office server running SheepShaver
./SheepShaver --display vnc --port 5900

# Connect from home via SSH tunnel + VNC
# Or via web browser with noVNC client
```

### What We're Building

```
┌─────────────────────────────────────────────────────────────┐
│         Modern Mac Emulator (BasiliskII/SheepShaver)       │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │         Unicorn Engine CPU Emulation                 │  │
│  │  • M68K (68000-68040) for BasiliskII                │  │
│  │  • PPC (G3/G4) for SheepShaver                      │  │
│  │  • TCG JIT compilation for performance              │  │
│  │  • Validated against legacy UAE/KPX                 │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │              IPC Driver Architecture                 │  │
│  │  • Video driver (IPC)                               │  │
│  │  • Disk driver (IPC)                                │  │
│  │  • Network driver (IPC)                             │  │
│  │  • Audio driver (IPC)                               │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │           ROM Patching & Mac OS Support              │  │
│  │  • Mac ROM patches (unchanged from BasiliskII)      │  │
│  │  • EmulOp system (0x71xx illegal instructions)      │  │
│  │  • System call interception                         │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Target Platforms

- **Linux** - Primary development platform
- **macOS** - Native macOS builds
- **Windows** - Cross-platform via Meson/MSVC

**Removed Platforms:**
- ❌ BeOS (obsolete)
- ❌ AmigaOS (niche)
- ❌ Other legacy platforms

**Focus:** Modern desktop operating systems only.

---

## Why Unicorn Engine?

BasiliskII and SheepShaver currently use legacy CPU emulators (UAE for M68K, KPX for PPC). These are unmaintained forks with platform-specific limitations.

**Unicorn Engine** is a modern CPU emulation framework that enables the project vision:

### What is Unicorn?

**Unicorn Engine** - Lightweight CPU emulation framework extracted from QEMU.

- **Based on:** QEMU 5.0.1 TCG (Tiny Code Generator)
- **Focus:** CPU-only emulation (no devices, clean library API)
- **License:** GPL v2 (compatible with BasiliskII/SheepShaver)
- **Website:** https://www.unicorn-engine.org/

### Why Unicorn Enables Modern Mac Emulation

1. **Universal JIT Support**
   - Works on x86-64, ARM, ARM64, RISC-V, MIPS, etc.
   - **Critical:** Enables Raspberry Pi Zero headless server use case
   - Same performance on Apple Silicon, x86 desktops, ARM SBCs

2. **Both M68K and PPC in One Library**
   ```bash
   cmake .. -DUNICORN_ARCH="m68k;ppc"
   # Builds ~1.5MB library with both architectures
   ```
   - BasiliskII uses M68K backend
   - SheepShaver uses PPC backend
   - Unified codebase, shared infrastructure

3. **Clean Embedding API**
   ```c
   // Initialize M68K CPU
   uc_engine *uc;
   uc_open(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, &uc);

   // Map memory (zero-copy)
   uc_mem_map_ptr(uc, 0x0, ram_size, UC_PROT_ALL, ram_ptr);

   // Execute
   uc_emu_start(uc, start_addr, end_addr, 0, 0);
   ```
   Simple, library-first design. No complex initialization.

4. **Active Upstream**
   - Regular releases, active community
   - Used by AFL++, Qiling, Triton, thousands of projects
   - Can contribute fixes and benefit from improvements

5. **Proven CPU Cores**
   - QEMU's M68K and PPC cores (used by millions)
   - Better accuracy than legacy UAE/KPX
   - Well-tested in production environments

---

## Dual-CPU Validation Strategy

### The Problem

**How do we know Unicorn produces identical results to UAE/KPX?**

We can't just replace the CPU and hope it works. We need **proof** that Unicorn executes identically.

### The Solution: Dual-CPU Lockstep Execution

Run **both** CPUs side-by-side:

1. Execute one instruction on UAE/KPX
2. Execute one instruction on Unicorn
3. **Compare CPU state** (registers, PC, SR, flags)
4. **Compare memory writes**
5. **Compare external I/O** (device access, interrupts)
6. If anything diverges → **ABORT** and report

```
┌──────────────────────────────────────────────────────────────┐
│                  Dual-CPU Validation Harness                  │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│   ┌─────────────────────┐       ┌─────────────────────┐    │
│   │   UAE/KPX CPU       │       │   Unicorn CPU       │    │
│   │  (Legacy Emulator)  │       │  (New Emulator)     │    │
│   ├─────────────────────┤       ├─────────────────────┤    │
│   │ Separate RAM        │       │ Separate RAM        │    │
│   │ Separate ROM        │       │ Separate ROM        │    │
│   │ Separate Devices    │       │ Separate Devices    │    │
│   └──────────┬──────────┘       └──────────┬──────────┘    │
│              │                              │                │
│              │    Execute 1 instruction     │                │
│              │─────────────────────────────▶│                │
│              │                              │                │
│              └──────────────┬───────────────┘                │
│                             ▼                                │
│                    ┌────────────────┐                        │
│                    │  State Logger  │                        │
│                    │                │                        │
│                    │  Record:       │                        │
│                    │  • Registers   │                        │
│                    │  • Memory ops  │                        │
│                    │  • I/O access  │                        │
│                    │  • Exceptions  │                        │
│                    └────────┬───────┘                        │
│                             ▼                                │
│              ┌──────────────────────────────┐               │
│              │  Binary Trace File           │               │
│              │  (uae_trace.bin)             │               │
│              │  (unicorn_trace.bin)         │               │
│              └──────────────┬───────────────┘               │
│                             ▼                                │
│                    ┌────────────────┐                        │
│                    │ Python Differ  │                        │
│                    │                │                        │
│                    │ • Decode traces│                        │
│                    │ • Compare      │                        │
│                    │ • Report diffs │                        │
│                    └────────────────┘                        │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### State Capture Format

We need to capture **everything** that defines CPU execution state.

#### CPU State Structure (Binary Format)

```c
/* Single instruction state snapshot */
struct CPUStateSnapshot {
    /* Sequence number */
    uint64_t seq;           // Instruction number (0, 1, 2, ...)

    /* Program counter */
    uint32_t pc;            // Before instruction execution
    uint32_t pc_next;       // After instruction execution

    /* Data registers (M68K: D0-D7, PPC: GPR0-GPR31) */
    uint32_t dregs[32];     // Data/general-purpose registers

    /* Address registers (M68K: A0-A7, PPC: SPRs) */
    uint32_t aregs[32];     // Address/special registers

    /* Status/Condition registers */
    uint32_t sr;            // Status register (M68K) or MSR (PPC)
    uint32_t ccr;           // Condition codes

    /* Instruction info */
    uint32_t opcode;        // Opcode bytes
    uint8_t  opcode_len;    // Instruction length (2, 4, 6 bytes)

    /* Flags */
    uint8_t exception;      // Exception raised? (0=no, 1=yes)
    uint8_t exception_num;  // Exception vector number
    uint8_t is_emulop;      // Is this an EmulOp? (0x71xx)
    uint16_t emulop_num;    // EmulOp selector

    /* Timing */
    uint64_t timestamp_ns;  // Nanosecond timestamp
} __attribute__((packed));
```

**Size:** 280 bytes per instruction

**Format:** Little-endian binary (for Python struct unpacking)

#### Memory Operation Structure

```c
/* Memory access record */
struct MemoryOperation {
    uint64_t seq;           // Instruction that caused this access
    uint8_t  type;          // 0=read, 1=write
    uint32_t address;       // Physical address
    uint8_t  size;          // Access size (1, 2, 4 bytes)
    uint32_t value;         // Value read/written
    uint64_t timestamp_ns;  // When it occurred
} __attribute__((packed));
```

**Size:** 26 bytes per memory operation

#### I/O Operation Structure

```c
/* External I/O access (device registers, etc.) */
struct IOOperation {
    uint64_t seq;           // Instruction that caused this
    uint8_t  type;          // 0=port read, 1=port write, 2=MMIO, 3=interrupt
    uint32_t address;       // Device address or port number
    uint32_t value;         // Value
    uint64_t timestamp_ns;
} __attribute__((packed));
```

**Size:** 25 bytes per I/O operation

### Binary Trace File Format

```
┌─────────────────────────────────────────┐
│         Trace File Header               │
│  Magic: "MACTRACE"                      │
│  Version: 1                             │
│  CPU Type: 0=M68K, 1=PPC                │
│  Start Time: Unix timestamp             │
│  Total Instructions: uint64_t           │
└─────────────────────────────────────────┘
         ▼
┌─────────────────────────────────────────┐
│    CPUStateSnapshot #0                  │
│    (280 bytes)                          │
├─────────────────────────────────────────┤
│    MemoryOperation[] for seq=0          │
│    (variable count)                     │
├─────────────────────────────────────────┤
│    IOOperation[] for seq=0              │
│    (variable count)                     │
├─────────────────────────────────────────┤
│    Record Separator (0xFFFFFFFF)        │
└─────────────────────────────────────────┘
         ▼
┌─────────────────────────────────────────┐
│    CPUStateSnapshot #1                  │
│    (280 bytes)                          │
├─────────────────────────────────────────┤
│    MemoryOperation[] for seq=1          │
├─────────────────────────────────────────┤
│    IOOperation[] for seq=1              │
├─────────────────────────────────────────┤
│    Record Separator (0xFFFFFFFF)        │
└─────────────────────────────────────────┘
         ▼
        ...
```

**Advantages:**

- **Binary format** → Fast writes (no formatting overhead)
- **Fixed-size records** → Easy seeking/indexing
- **Append-only** → Can write while executing
- **Self-describing** → Header contains metadata

**Files Generated:**

- `uae_trace.bin` - UAE CPU execution trace
- `unicorn_trace.bin` - Unicorn CPU execution trace

### Trace Comparison Tool (Python)

```python
#!/usr/bin/env python3
"""
Dual-CPU Trace Comparison Tool

Compares execution traces from UAE/KPX and Unicorn CPUs,
reporting any divergences in CPU state, memory, or I/O.
"""

import struct
import sys
from dataclasses import dataclass
from typing import List, Optional

@dataclass
class CPUState:
    seq: int
    pc: int
    pc_next: int
    dregs: List[int]
    aregs: List[int]
    sr: int
    ccr: int
    opcode: int
    opcode_len: int
    exception: bool
    exception_num: int
    is_emulop: bool
    emulop_num: int
    timestamp_ns: int

@dataclass
class MemoryOp:
    seq: int
    type: str  # 'read' or 'write'
    address: int
    size: int
    value: int
    timestamp_ns: int

class TraceReader:
    """Read and parse binary trace files"""

    MAGIC = b"MACTRACE"

    def __init__(self, filename):
        self.file = open(filename, 'rb')
        self._read_header()

    def _read_header(self):
        magic = self.file.read(8)
        if magic != self.MAGIC:
            raise ValueError(f"Invalid trace file: {magic}")

        version, cpu_type, start_time, total_insns = struct.unpack('<IIQI',
                                                                   self.file.read(20))
        self.version = version
        self.cpu_type = 'M68K' if cpu_type == 0 else 'PPC'
        self.start_time = start_time
        self.total_instructions = total_insns

        print(f"Trace: {self.cpu_type}, {self.total_instructions} instructions")

    def read_cpu_state(self) -> Optional[CPUState]:
        """Read next CPU state snapshot"""
        data = self.file.read(280)
        if len(data) < 280:
            return None

        # Unpack binary structure
        seq, pc, pc_next = struct.unpack('<QII', data[0:16])
        dregs = struct.unpack('<32I', data[16:144])
        aregs = struct.unpack('<32I', data[144:272])
        sr, ccr, opcode, opcode_len, exc, exc_num, is_emulop, emulop_num, ts = \
            struct.unpack('<IIIBBBHQ', data[272:280])

        return CPUState(
            seq=seq,
            pc=pc,
            pc_next=pc_next,
            dregs=list(dregs),
            aregs=list(aregs),
            sr=sr,
            ccr=ccr,
            opcode=opcode,
            opcode_len=opcode_len,
            exception=(exc != 0),
            exception_num=exc_num,
            is_emulop=(is_emulop != 0),
            emulop_num=emulop_num,
            timestamp_ns=ts
        )

    def read_memory_ops(self) -> List[MemoryOp]:
        """Read memory operations for current instruction"""
        ops = []
        while True:
            marker = self.file.read(4)
            if struct.unpack('<I', marker)[0] == 0xFFFFFFFF:
                # End of record
                break

            # Rewind and read MemoryOp
            self.file.seek(-4, 1)
            data = self.file.read(26)

            seq, op_type, addr, size, value, ts = struct.unpack('<QBIIBQ', data)

            ops.append(MemoryOp(
                seq=seq,
                type='read' if op_type == 0 else 'write',
                address=addr,
                size=size,
                value=value,
                timestamp_ns=ts
            ))

        return ops

def compare_traces(uae_file, unicorn_file):
    """Compare two trace files and report divergences"""

    uae = TraceReader(uae_file)
    uni = TraceReader(unicorn_file)

    divergences = 0
    seq = 0

    while True:
        uae_state = uae.read_cpu_state()
        uni_state = uni.read_cpu_state()

        if uae_state is None or uni_state is None:
            break

        # Compare sequence numbers
        if uae_state.seq != uni_state.seq:
            print(f"ERROR: Sequence mismatch at {seq}")
            return False

        # Compare PC
        if uae_state.pc != uni_state.pc:
            print(f"\n╔════════════════════════════════════════════════════════╗")
            print(f"║  DIVERGENCE at instruction {seq}")
            print(f"╚════════════════════════════════════════════════════════╝")
            print(f"PC:  UAE=0x{uae_state.pc:08X}  Unicorn=0x{uni_state.pc:08X}")
            divergences += 1
            return False

        # Compare registers
        for i in range(8):  # D0-D7 / GPR0-GPR7
            if uae_state.dregs[i] != uni_state.dregs[i]:
                print(f"\nD{i} DIVERGENCE at instruction {seq}:")
                print(f"  UAE:     0x{uae_state.dregs[i]:08X}")
                print(f"  Unicorn: 0x{uni_state.dregs[i]:08X}")
                divergences += 1
                return False

        # Compare address registers
        for i in range(8):  # A0-A7
            if uae_state.aregs[i] != uni_state.aregs[i]:
                print(f"\nA{i} DIVERGENCE at instruction {seq}:")
                print(f"  UAE:     0x{uae_state.aregs[i]:08X}")
                print(f"  Unicorn: 0x{uni_state.aregs[i]:08X}")
                divergences += 1
                return False

        # Compare status register
        if uae_state.sr != uni_state.sr:
            print(f"\nSR DIVERGENCE at instruction {seq}:")
            print(f"  UAE:     0x{uae_state.sr:04X}")
            print(f"  Unicorn: 0x{uni_state.sr:04X}")
            divergences += 1
            return False

        # Read and compare memory operations
        uae_mem = uae.read_memory_ops()
        uni_mem = uni.read_memory_ops()

        if len(uae_mem) != len(uni_mem):
            print(f"\nMEMORY OP COUNT DIVERGENCE at instruction {seq}:")
            print(f"  UAE: {len(uae_mem)} operations")
            print(f"  Unicorn: {len(uni_mem)} operations")
            divergences += 1
            return False

        for uae_op, uni_op in zip(uae_mem, uni_mem):
            if (uae_op.type != uni_op.type or
                uae_op.address != uni_op.address or
                uae_op.value != uni_op.value):
                print(f"\nMEMORY OP DIVERGENCE at instruction {seq}:")
                print(f"  UAE:     {uae_op.type} 0x{uae_op.address:08X} = 0x{uae_op.value:08X}")
                print(f"  Unicorn: {uni_op.type} 0x{uni_op.address:08X} = 0x{uni_op.value:08X}")
                divergences += 1
                return False

        seq += 1

        # Progress indicator
        if seq % 10000 == 0:
            print(f"\r  Compared {seq} instructions... ✓ ", end='', flush=True)

    print(f"\n\n✅ SUCCESS: {seq} instructions executed identically!")
    print(f"   UAE and Unicorn are 100% compatible.\n")
    return True

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: compare_traces.py <uae_trace.bin> <unicorn_trace.bin>")
        sys.exit(1)

    success = compare_traces(sys.argv[1], sys.argv[2])
    sys.exit(0 if success else 1)
```

### Instrumentation Implementation

#### UAE/KPX Instrumentation

```cpp
// In UAE's m68k_do_execute() or similar main loop
void UAE_ExecuteOne() {
    CPUStateSnapshot before, after;

    // Capture state BEFORE instruction
    capture_cpu_state(&before);
    before.pc = m68k_getpc();

    // Hook memory operations
    memory_ops.clear();
    enable_memory_logging();

    // Execute ONE instruction
    uint16_t opcode = GET_OPCODE;
    regs.pc += 2;
    (*cpufunctbl[opcode])(opcode);

    // Capture state AFTER instruction
    capture_cpu_state(&after);
    after.pc_next = m68k_getpc();

    // Write to trace file
    write_cpu_state(trace_file, &after);
    write_memory_ops(trace_file, memory_ops);
    write_record_separator(trace_file);

    disable_memory_logging();
}
```

#### Unicorn Instrumentation

```c
// Unicorn execution with hooks
void Unicorn_ExecuteOne() {
    CPUStateSnapshot state;

    // Capture state BEFORE
    state.seq = instruction_count++;
    uc_reg_read(uc, UC_M68K_REG_PC, &state.pc);

    // Capture all registers
    for (int i = 0; i < 8; i++) {
        uc_reg_read(uc, UC_M68K_REG_D0 + i, &state.dregs[i]);
        uc_reg_read(uc, UC_M68K_REG_A0 + i, &state.aregs[i]);
    }
    uc_reg_read(uc, UC_M68K_REG_SR, &state.sr);

    // Hook memory access
    uc_hook mem_hook;
    uc_hook_add(uc, &mem_hook, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                memory_hook_callback, &memory_ops, 1, 0);

    // Execute ONE instruction
    uc_emu_start(uc, state.pc, 0xFFFFFFFF, 0, 1);

    // Capture state AFTER
    uc_reg_read(uc, UC_M68K_REG_PC, &state.pc_next);

    // Write to trace
    write_cpu_state(trace_file, &state);
    write_memory_ops(trace_file, memory_ops);
    write_record_separator(trace_file);

    uc_hook_del(uc, mem_hook);
}
```

### Trace Analysis Tools

Beyond simple comparison, we need tools to:

#### 1. Trace Viewer (Python)

```bash
./trace_view.py uae_trace.bin --start 1000 --count 100
```

Pretty-prints instructions 1000-1100:

```
Instruction 1000: PC=0x00401234
  D0=0x12345678  D1=0x00000042  D2=0x00000000  D3=0x00000000
  A0=0x00400000  A1=0x00410000  A7=0x0001FFE0  SR=0x2700
  Opcode: 0x2040  MOVEA.L D0,A0
  Memory: -

Instruction 1001: PC=0x00401236
  D0=0x12345678  D1=0x00000042  D2=0x00000000  D3=0x00000000
  A0=0x12345678  A1=0x00410000  A7=0x0001FFE0  SR=0x2700
  Opcode: 0x52A1  ADDQ.L #1,(A1)
  Memory: WRITE 0x00410000 = 0x00000001 (4 bytes)
```

#### 2. Divergence Finder

```bash
./find_divergence.py uae_trace.bin unicorn_trace.bin
```

Binary search to find first divergence point:

```
Checking instruction 50000... ✓ match
Checking instruction 75000... ✓ match
Checking instruction 87500... ✓ match
Checking instruction 93750... ✓ match
Checking instruction 96875... ✓ match
Checking instruction 98437... ✓ match
Checking instruction 99218... ✗ DIVERGENCE!

First divergence at instruction 99218:

PC: UAE=0x00405A3C  Unicorn=0x00405A3C  ✓ MATCH
D0: UAE=0x00000001  Unicorn=0x00000002  ✗ DIVERGENCE!

Instruction before divergence (99217):
  PC=0x00405A3A  Opcode=0x5240  ADDQ.W #1,D0

Instruction at divergence (99218):
  PC=0x00405A3C  Opcode=0x51C8FFFA  DBRA D0,$00405A38
```

#### 3. Trace Statistics

```bash
./trace_stats.py uae_trace.bin
```

```
Trace Statistics
================

Total Instructions: 1,234,567
Total Memory Ops:   4,567,890
  Reads:  3,456,789 (75.7%)
  Writes: 1,111,101 (24.3%)

Total I/O Ops: 12,345
  Port Reads:  6,789
  Port Writes: 4,567
  Interrupts:  989

Execution Time: 45.67s
Average IPS: 27,032

Instruction Distribution:
  MOVE*:   234,567 (19.0%)
  ADD*:    123,456 (10.0%)
  CMP*:    111,222 (9.0%)
  JMP/JSR:  98,765 (8.0%)
  RTS:      87,654 (7.1%)
  ...
```

### Why This Approach Works

**Advantages:**

1. ✅ **Complete Validation** - Every instruction, every register, every memory access compared
2. ✅ **Binary Traces** - Fast writes, compact storage, easy parsing
3. ✅ **Offline Analysis** - Record once, analyze many times
4. ✅ **Python Tools** - Easy to write analysis scripts
5. ✅ **Separate Memory** - Each CPU has own memory (prevents false passes from shared state)
6. ✅ **Deterministic** - Same input always produces same trace (can reproduce bugs)

**What We Catch:**

- ✅ Register calculation errors
- ✅ Flag computation differences
- ✅ Memory access ordering
- ✅ Exception handling differences
- ✅ EmulOp interception issues
- ✅ Timing-sensitive bugs (via timestamps)

---

## New Build System Architecture

### Why Rebuild from Scratch?

Current BasiliskII/SheepShaver build system:

- **Autotools** (configure.ac, Makefile.am) - complex, hard to maintain
- **Platform-specific Makefiles** - fragmented across Unix/Windows/macOS
- **Deeply nested** - files scattered across many directories
- **Legacy cruft** - BeOS, AmigaOS, other obsolete platforms
- **Tightly coupled** - hard to extract just what we need

### New Build System Goals

1. ✅ **Meson-based** - Modern, fast, cross-platform
2. ✅ **Clean structure** - Logical file organization
3. ✅ **IPC-first** - All drivers via IPC (no in-process device emulation)
4. ✅ **Three platforms only** - Linux, macOS, Windows
5. ✅ **Incremental migration** - Pull files over one-by-one, not big bang

### New Project Structure

```
macemu-next/
├── meson.build                 # Root build file
├── meson_options.txt           # Build options
│
├── src/
│   ├── common/                 # Shared code (both BasiliskII & SheepShaver)
│   │   ├── meson.build
│   │   ├── rom_patches.cpp     # ROM patching system
│   │   ├── emulop.cpp          # EmulOp handler
│   │   ├── memory.cpp          # Memory management
│   │   └── prefs.cpp           # Preferences
│   │
│   ├── cpu/                    # CPU emulation
│   │   ├── meson.build
│   │   ├── unicorn_wrapper.c   # Unicorn API wrapper
│   │   ├── unicorn_m68k.c      # M68K-specific
│   │   ├── unicorn_ppc.c       # PPC-specific
│   │   ├── emulop_hooks.c      # EmulOp illegal instruction hooks
│   │   └── cpu_state.c         # State capture for dual-CPU validation
│   │
│   ├── drivers/                # IPC-based drivers
│   │   ├── meson.build
│   │   ├── video_ipc.cpp       # Video driver (IPC)
│   │   ├── disk_ipc.cpp        # Disk driver (IPC)
│   │   ├── network_ipc.cpp     # Network driver (IPC)
│   │   ├── audio_ipc.cpp       # Audio driver (IPC)
│   │   └── ipc_protocol.h      # IPC message definitions
│   │
│   ├── basilisk/               # BasiliskII-specific
│   │   ├── meson.build
│   │   ├── main.cpp
│   │   ├── macos_util.cpp
│   │   └── ...
│   │
│   ├── sheepshaver/            # SheepShaver-specific
│   │   ├── meson.build
│   │   ├── main.cpp
│   │   ├── macos_util.cpp
│   │   └── ...
│   │
│   └── platform/               # Platform-specific code
│       ├── linux/
│       │   ├── meson.build
│       │   └── sys_unix.cpp
│       ├── macos/
│       │   ├── meson.build
│       │   └── sys_darwin.cpp
│       └── windows/
│           ├── meson.build
│           └── sys_win32.cpp
│
├── external/
│   ├── unicorn/                # Unicorn Engine (subproject)
│   │   └── meson.build         # Wrap file
│   └── ...
│
├── tests/
│   ├── meson.build
│   ├── cpu/
│   │   ├── test_unicorn_m68k.c
│   │   ├── test_unicorn_ppc.c
│   │   ├── test_emulop.c
│   │   └── test_dualcpu.c
│   ├── instructions/
│   │   ├── test_m68k_move.c
│   │   ├── test_m68k_arithmetic.c
│   │   └── ...
│   └── integration/
│       ├── test_rom_boot.c
│       └── ...
│
├── tools/
│   ├── trace_compare.py        # Dual-CPU trace comparison
│   ├── trace_view.py           # Trace viewer
│   ├── find_divergence.py      # Divergence finder
│   └── trace_stats.py          # Trace statistics
│
└── docs/
    ├── architecture.md
    ├── building.md
    ├── dual_cpu_validation.md
    └── unicorn_integration.md
```

### Meson Build Definition

**Root `meson.build`:**

```meson
project('macemu-next', ['c', 'cpp'],
  version: '2.0.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=2',
    'buildtype=debugoptimized'
  ]
)

# Options
cpu_backend = get_option('cpu_backend')  # 'unicorn', 'uae', 'dualcpu'
build_basilisk = get_option('basilisk')
build_sheepshaver = get_option('sheepshaver')

# Dependencies
unicorn_dep = dependency('unicorn', required: (cpu_backend in ['unicorn', 'dualcpu']))
sdl2_dep = dependency('sdl2', required: true)
threads_dep = dependency('threads', required: true)

# Subdirectories
subdir('external')
subdir('src/common')
subdir('src/cpu')
subdir('src/drivers')

if build_basilisk
  subdir('src/basilisk')
endif

if build_sheepshaver
  subdir('src/sheepshaver')
endif

# Platform-specific
if host_machine.system() == 'linux'
  subdir('src/platform/linux')
elif host_machine.system() == 'darwin'
  subdir('src/platform/macos')
elif host_machine.system() == 'windows'
  subdir('src/platform/windows')
endif

# Tests
if get_option('tests')
  subdir('tests')
endif
```

**`meson_options.txt`:**

```meson
option('cpu_backend',
  type: 'combo',
  choices: ['unicorn', 'uae', 'dualcpu'],
  value: 'dualcpu',
  description: 'CPU emulation backend'
)

option('basilisk',
  type: 'boolean',
  value: true,
  description: 'Build BasiliskII'
)

option('sheepshaver',
  type: 'boolean',
  value: true,
  description: 'Build SheepShaver'
)

option('tests',
  type: 'boolean',
  value: true,
  description: 'Build test suite'
)

option('ipc_only',
  type: 'boolean',
  value: true,
  description: 'Use IPC drivers only (no in-process devices)'
)
```

**CPU subdirectory `src/cpu/meson.build`:**

```meson
cpu_sources = []
cpu_deps = []

if cpu_backend == 'unicorn' or cpu_backend == 'dualcpu'
  cpu_sources += [
    'unicorn_wrapper.c',
    'unicorn_m68k.c',
    'unicorn_ppc.c',
    'emulop_hooks.c',
  ]
  cpu_deps += unicorn_dep
endif

if cpu_backend == 'uae' or cpu_backend == 'dualcpu'
  cpu_sources += [
    'uae/newcpu.cpp',
    'uae/cpuemu.cpp',
    # ... UAE sources
  ]
endif

if cpu_backend == 'dualcpu'
  cpu_sources += [
    'dualcpu_harness.c',
    'cpu_state.c',
    'trace_writer.c',
  ]
endif

cpu_lib = static_library('cpu',
  cpu_sources,
  dependencies: cpu_deps,
  include_directories: include_directories('..')
)

cpu_dep = declare_dependency(
  link_with: cpu_lib,
  include_directories: include_directories('.')
)
```

### Build Commands

```bash
# Initial setup
meson setup build --buildtype=debug -Dcpu_backend=dualcpu

# Build
meson compile -C build

# Run tests
meson test -C build

# Install
meson install -C build

# Clean rebuild
rm -rf build && meson setup build
```

### Build Configurations

**1. Dual-CPU Validation Mode:**

```bash
meson setup build -Dcpu_backend=dualcpu -Dtests=true
```

- Builds both UAE and Unicorn
- Enables trace recording
- Runs side-by-side

**2. Unicorn Only (Production):**

```bash
meson setup build -Dcpu_backend=unicorn -Dbuildtype=release
```

- Only Unicorn CPU
- Optimized build
- Smallest binary

**3. UAE Only (Fallback):**

```bash
meson setup build -Dcpu_backend=uae
```

- Legacy UAE CPU
- For comparison/fallback

### Migration Strategy

**Phase 1: Set up new build system**

1. Create new `macemu-next/` directory
2. Set up Meson build files
3. Copy over Unicorn wrapper code
4. Build minimal test harness

**Phase 2: Copy common code**

1. Copy `rom_patches.cpp` from BasiliskII
2. Copy `emulop.cpp`
3. Copy `prefs.cpp`
4. Copy `memory.cpp`
5. Adapt each file for new structure

**Phase 3: Copy platform code**

1. Copy `sys_unix.cpp` (Linux)
2. Copy `sys_darwin.cpp` (macOS)
3. Copy `sys_win32.cpp` (Windows)
4. Remove BeOS, AmigaOS, other platforms

**Phase 4: Copy BasiliskII-specific**

1. Copy `main.cpp`
2. Copy `macos_util.cpp`
3. Copy other BasiliskII files
4. Adapt for IPC drivers

**Phase 5: Build and test**

1. Build BasiliskII with Unicorn
2. Run dual-CPU validation
3. Fix any divergences
4. Validate with real Mac ROMs

**Phase 6: Repeat for SheepShaver**

1. Copy SheepShaver-specific files
2. Adapt PPC support
3. Validate with dual-CPU
4. Test with Mac OS 9

### Advantages of This Approach

1. ✅ **Clean slate** - No legacy cruft
2. ✅ **Modern build system** - Meson is fast, cross-platform
3. ✅ **Logical structure** - Easy to navigate
4. ✅ **Incremental** - Copy files one-by-one, validate each step
5. ✅ **Testable** - Tests built alongside code
6. ✅ **Flexible** - Easy to switch CPU backends
7. ✅ **Maintainable** - Clear dependencies, no hidden coupling

---

## Phase Breakdown

### Phase 1: Foundation Setup (Week 1)

**Goal:** Set up new project structure and build Unicorn

**Tasks:**

1. Create `macemu-next/` directory structure
2. Set up Meson build system
3. Build Unicorn Engine with M68K + PPC
4. Create basic test harness
5. Validate Unicorn works standalone

**Deliverables:**

- ✅ Project structure created
- ✅ Meson builds successfully
- ✅ Unicorn library built
- ✅ Simple M68K test passes

**Testing:**

```bash
cd macemu-next
meson setup build
meson compile -C build
./build/tests/test_unicorn_m68k
# Expected: ✅ TEST PASSED
```

**Commit:** `Phase 1: Set up project structure and build Unicorn Engine`

---

### Phase 2: Unicorn Wrapper API (Week 2)

**Goal:** Create clean C API wrapper around Unicorn

**Tasks:**

1. Create `src/cpu/unicorn_wrapper.h`
2. Implement `src/cpu/unicorn_wrapper.c`
3. Create M68K-specific functions
4. Create PPC-specific functions
5. Test register access and execution

**Files Created:**

```
src/cpu/
├── unicorn_wrapper.h      # Public API
├── unicorn_wrapper.c      # Core implementation
├── unicorn_m68k.c         # M68K specifics
└── unicorn_ppc.c          # PPC specifics
```

**API Design:**

```c
// CPU lifecycle
UnicornCPU* unicorn_create_m68k(void);
UnicornCPU* unicorn_create_ppc(void);
void unicorn_destroy(UnicornCPU* cpu);

// Memory mapping (zero-copy)
bool unicorn_map_ram(UnicornCPU* cpu, uint64_t addr, void* host_ptr, uint64_t size);
bool unicorn_map_rom(UnicornCPU* cpu, uint64_t addr, void* host_ptr, uint64_t size);

// Execution
bool unicorn_execute_one(UnicornCPU* cpu);
bool unicorn_execute_until(UnicornCPU* cpu, uint32_t end_pc);

// Registers (M68K)
uint32_t unicorn_get_dreg(UnicornCPU* cpu, int reg);  // D0-D7
uint32_t unicorn_get_areg(UnicornCPU* cpu, int reg);  // A0-A7
uint32_t unicorn_get_pc(UnicornCPU* cpu);
uint16_t unicorn_get_sr(UnicornCPU* cpu);

// Registers (PPC)
uint32_t unicorn_get_gpr(UnicornCPU* cpu, int reg);   // GPR0-GPR31
uint32_t unicorn_get_spr(UnicornCPU* cpu, int spr);   // Special regs
```

**Testing:**

```bash
./build/tests/test_unicorn_wrapper
# Tests: Create CPU, map memory, execute, read registers
```

**Commit:** `Phase 2: Implement Unicorn wrapper API for M68K and PPC`

---

### Phase 3: EmulOp Hook System (Week 2-3)

**Goal:** Implement illegal instruction hooks for BasiliskII/SheepShaver EmulOps

**Tasks:**

1. Study BasiliskII's EmulOp system (0x71xx opcodes)
2. Implement `UC_HOOK_INSN_INVALID` handler
3. Forward to BasiliskII's `EmulOp()` function
4. Test with actual EmulOp instructions

**Implementation:**

```c
// src/cpu/emulop_hooks.c

static void (*g_emulop_handler)(uint16_t opcode) = NULL;

static bool hook_invalid_insn(uc_engine *uc, void *user_data) {
    uint32_t pc;
    uint16_t opcode;

    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_mem_read(uc, pc, &opcode, sizeof(opcode));
    opcode = SWAP16(opcode);  // Big-endian

    // Check if EmulOp (0x71xx for M68K)
    if ((opcode & 0xFF00) == 0x7100) {
        if (g_emulop_handler) {
            g_emulop_handler(opcode);
            pc += 2;  // Advance past EmulOp
            uc_reg_write(uc, UC_M68K_REG_PC, &pc);
            return true;  // Handled
        }
    }

    return false;  // Not an EmulOp, raise exception
}

void unicorn_set_emulop_handler(UnicornCPU* cpu, EmulOpHandler handler) {
    g_emulop_handler = handler;

    uc_hook hook;
    uc_hook_add(cpu->uc, &hook, UC_HOOK_INSN_INVALID,
                (void*)hook_invalid_insn, NULL, 1, 0);
}
```

**Testing:**

```bash
./build/tests/test_emulop
# Tests: Execute 0x710a (VIDEO_OPEN), verify handler called
```

**Commit:** `Phase 3: Implement EmulOp illegal instruction hook system`

---

### Phase 4: State Capture & Trace Recording (Week 3)

**Goal:** Implement binary trace recording for dual-CPU validation

**Tasks:**

1. Define binary formats (`CPUStateSnapshot`, `MemoryOperation`, etc.)
2. Implement trace writer (`trace_writer.c`)
3. Add instrumentation points to execution
4. Test trace recording

**Files:**

```
src/cpu/
├── cpu_state.h            # State structure definitions
├── cpu_state.c            # State capture functions
├── trace_writer.h         # Binary trace file writer
└── trace_writer.c         # Implementation
```

**Implementation:**

```c
// cpu_state.h
typedef struct {
    uint64_t seq;
    uint32_t pc;
    uint32_t pc_next;
    uint32_t dregs[32];
    uint32_t aregs[32];
    uint32_t sr;
    uint32_t ccr;
    uint32_t opcode;
    uint8_t opcode_len;
    uint8_t exception;
    uint8_t exception_num;
    uint8_t is_emulop;
    uint16_t emulop_num;
    uint64_t timestamp_ns;
} __attribute__((packed)) CPUStateSnapshot;

// trace_writer.h
TraceFile* trace_open(const char* filename, CPUType cpu_type);
void trace_write_state(TraceFile* tf, const CPUStateSnapshot* state);
void trace_write_memory_op(TraceFile* tf, const MemoryOperation* op);
void trace_close(TraceFile* tf);
```

**Testing:**

```bash
./build/tests/test_trace_writer
# Tests: Write trace, read it back, verify correctness
```

**Commit:** `Phase 4: Implement binary trace recording for CPU state`

---

### Phase 5: Dual-CPU Harness (Week 3-4)

**Goal:** Run UAE and Unicorn side-by-side with state comparison

**Tasks:**

1. Adapt QEMU dual-CPU harness for Unicorn
2. Implement separate memory for each CPU
3. Add lockstep execution loop
4. Implement state comparison
5. Test with simple programs

**Files:**

```
src/cpu/
├── dualcpu_harness.h      # Public API
└── dualcpu_harness.c      # Implementation (~500 lines)
```

**Key Functions:**

```c
bool DualCPU_Init(void);
void DualCPU_Exit(void);
bool DualCPU_ExecuteOne(void);              // Execute 1 instruction on both
uint64_t DualCPU_ExecuteN(uint64_t count);  // Execute N instructions
void DualCPU_EnableTracing(const char* uae_file, const char* uni_file);
void DualCPU_GetStats(DualCPUStats* stats);
```

**Testing:**

```bash
./build/tests/test_dualcpu
# Tests: Execute simple M68K program, verify states match
```

**Commit:** `Phase 5: Implement dual-CPU validation harness`

---

### Phase 6: Python Trace Analysis Tools (Week 4)

**Goal:** Create Python tools to analyze and compare traces

**Tasks:**

1. Implement trace reader (`tools/trace_lib.py`)
2. Create comparison tool (`tools/compare_traces.py`)
3. Create trace viewer (`tools/trace_view.py`)
4. Create divergence finder (`tools/find_divergence.py`)
5. Create statistics tool (`tools/trace_stats.py`)

**Files:**

```
tools/
├── trace_lib.py           # Common trace reading/parsing
├── compare_traces.py      # Main comparison tool
├── trace_view.py          # Pretty-print traces
├── find_divergence.py     # Binary search for divergences
└── trace_stats.py         # Statistics and analysis
```

**Testing:**

```bash
# Run dual-CPU test to generate traces
./build/tests/test_dualcpu

# Compare traces
./tools/compare_traces.py uae_trace.bin unicorn_trace.bin
# Expected: ✅ SUCCESS: 1000 instructions executed identically!
```

**Commit:** `Phase 6: Add Python tools for trace analysis and comparison`

---

### Phase 7: Copy BasiliskII Common Code (Week 4-5)

**Goal:** Migrate BasiliskII common code to new structure

**Tasks:**

1. Copy `rom_patches.cpp` → `src/common/rom_patches.cpp`
2. Copy `emulop.cpp` → `src/common/emulop.cpp`
3. Copy `prefs.cpp` → `src/common/prefs.cpp`
4. Copy `memory.cpp` → `src/common/memory.cpp`
5. Adapt for new build system
6. Fix includes, dependencies
7. Build and test each file

**Migration Process (per file):**

```bash
# 1. Copy file
cp BasiliskII/src/rom_patches.cpp macemu-next/src/common/

# 2. Add to meson.build
# src/common/meson.build:
# common_sources += 'rom_patches.cpp'

# 3. Fix includes
# Change: #include "../include/sysdeps.h"
# To:     #include "common/sysdeps.h"

# 4. Build
meson compile -C build

# 5. Fix compilation errors

# 6. Test
./build/tests/test_rom_patches

# 7. Commit
git add src/common/rom_patches.cpp
git commit -m "Migrate rom_patches.cpp to new build system"
```

**Commit Strategy:**

One commit per file:
- `Migrate rom_patches.cpp to new build system`
- `Migrate emulop.cpp to new build system`
- `Migrate prefs.cpp to new build system`
- etc.

---

### Phase 8: Instruction Validation Tests (Week 5-6)

**Goal:** Validate all M68K/PPC instructions produce identical results

**Tasks:**

1. Create M68K instruction test suite
2. Test all instruction categories:
   - Data movement (MOVE, MOVEA, MOVEQ, LEA, PEA)
   - Arithmetic (ADD, SUB, MUL, DIV, NEG)
   - Logic (AND, OR, XOR, NOT)
   - Shift/Rotate (LSL, LSR, ASL, ASR, ROL, ROR)
   - Bit manipulation (BSET, BCLR, BTST, BCHG)
   - Comparison (CMP, TST)
   - Branching (BRA, Bcc, BSR, DBcc)
   - Subroutines (JSR, JMP, RTS, RTR, RTE)
   - Stack (LINK, UNLK, MOVE USP)
   - Exceptions (TRAP, TRAPV, CHK, ILLEGAL)
3. Run in dual-CPU mode
4. Fix any divergences
5. Document instruction coverage

**Test Structure:**

```
tests/instructions/
├── test_m68k_move.c           # MOVE* instructions
├── test_m68k_arithmetic.c     # ADD, SUB, MUL, DIV
├── test_m68k_logic.c          # AND, OR, XOR, NOT
├── test_m68k_shift.c          # LSL, LSR, ASL, ASR, ROL, ROR
├── test_m68k_bit.c            # BSET, BCLR, BTST, BCHG
├── test_m68k_compare.c        # CMP, TST
├── test_m68k_branch.c         # BRA, Bcc, BSR, DBcc
├── test_m68k_subroutine.c     # JSR, JMP, RTS, RTR, RTE
├── test_m68k_stack.c          # LINK, UNLK, MOVE USP
└── test_m68k_exception.c      # TRAP, TRAPV, CHK
```

**Testing:**

```bash
meson test -C build --suite instructions
# Expected: All tests pass, zero divergences
```

**Commit:** `Phase 8: Validate all M68K instruction categories`

---

### Phase 9: ROM Boot Validation (Week 6)

**Goal:** Boot real Mac ROM in dual-CPU mode

**Tasks:**

1. Load Mac ROM file
2. Copy to both UAE and Unicorn memory
3. Set up initial CPU state (PC, SP, SR)
4. Execute first 10,000 instructions in lockstep
5. Compare traces
6. Fix any divergences
7. Increase to 100,000 instructions
8. Full boot to "happy Mac" icon

**Testing:**

```bash
./build/tests/test_rom_boot --rom ~/roms/MacII.ROM --count 100000
# Expected: ✅ ROM boot validation PASSED (100,000 instructions identical)
```

**Commit:** `Phase 9: Validate Mac ROM boot sequence in dual-CPU mode`

---

### Phase 10: IPC Driver Architecture (Week 7)

**Goal:** Implement IPC-based driver system

**Tasks:**

1. Design IPC protocol (`src/drivers/ipc_protocol.h`)
2. Implement video driver IPC
3. Implement disk driver IPC
4. Implement network driver IPC
5. Implement audio driver IPC
6. Test each driver independently

**Architecture:**

```
┌─────────────────────────────────────────────────────┐
│            BasiliskII Main Process                  │
│                                                     │
│  ┌───────────────────────────────────────────────┐ │
│  │  Unicorn CPU Emulation                        │ │
│  └──────────────────┬────────────────────────────┘ │
│                     │                               │
│                     ▼                               │
│  ┌───────────────────────────────────────────────┐ │
│  │  Device Emulation Layer                       │ │
│  │  (VIA, SCC, SCSI, etc.)                      │ │
│  └──────────────────┬────────────────────────────┘ │
│                     │                               │
│                     ▼                               │
│  ┌───────────────────────────────────────────────┐ │
│  │  IPC Message Queue                            │ │
│  └──────────────────┬────────────────────────────┘ │
└────────────────────┼────────────────────────────────┘
                     │
        ┌────────────┼────────────┐
        │            │            │
        ▼            ▼            ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ Video Driver │ │ Disk Driver  │ │ Audio Driver │
│   (IPC)      │ │   (IPC)      │ │   (IPC)      │
│              │ │              │ │              │
│ SDL2/Vulkan  │ │ File I/O     │ │ PulseAudio   │
└──────────────┘ └──────────────┘ └──────────────┘
```

**IPC Protocol:**

```c
// Message types
enum IPCMessageType {
    IPC_VIDEO_UPDATE,      // Framebuffer update
    IPC_DISK_READ,         // Read sectors
    IPC_DISK_WRITE,        // Write sectors
    IPC_NETWORK_TX,        // Send packet
    IPC_NETWORK_RX,        // Receive packet
    IPC_AUDIO_PLAY,        // Play audio buffer
};

// Message structure
typedef struct {
    uint32_t type;
    uint32_t size;
    uint8_t data[];
} IPCMessage;
```

**Commit:** `Phase 10: Implement IPC driver architecture`

---

### Phase 11: Full BasiliskII Integration (Week 7-8)

**Goal:** Complete BasiliskII build with Unicorn CPU

**Tasks:**

1. Copy remaining BasiliskII files
2. Integrate with IPC drivers
3. Build complete BasiliskII
4. Test with Mac OS 7.5
5. Run dual-CPU validation during boot
6. Fix any remaining divergences

**Testing:**

```bash
# Build with dual-CPU validation
meson configure build -Dcpu_backend=dualcpu
meson compile -C build

# Run BasiliskII
./build/BasiliskII --config basilisk.conf

# Should boot Mac OS 7.5 with continuous validation
# Logs: ✓ 1,234,567 instructions validated, zero divergences
```

**Commit:** `Phase 11: Complete BasiliskII integration with Unicorn`

---

### Phase 12: Production Build (Week 8)

**Goal:** Create production Unicorn-only build

**Tasks:**

1. Switch to Unicorn-only mode
2. Remove UAE code (optional, can keep as fallback)
3. Optimize build
4. Performance benchmarking
5. Documentation
6. Release

**Build:**

```bash
meson setup build-release \
  -Dcpu_backend=unicorn \
  -Dbuildtype=release \
  -Dstrip=true

meson compile -C build-release
```

**Benchmarking:**

```bash
./tools/benchmark.py --uae ./BasiliskII-uae --unicorn ./BasiliskII-unicorn
# Compare boot time, application performance, etc.
```

**Commit:** `Phase 12: Production Unicorn build with optimizations`

---

## Timeline & Milestones

### Overview

```
Week 1:   Phase 1  - Foundation Setup
Week 2:   Phase 2  - Unicorn Wrapper API
Week 2-3: Phase 3  - EmulOp Hook System
Week 3:   Phase 4  - State Capture & Trace Recording
Week 3-4: Phase 5  - Dual-CPU Harness
Week 4:   Phase 6  - Python Trace Analysis Tools
Week 4-5: Phase 7  - Copy BasiliskII Common Code
Week 5-6: Phase 8  - Instruction Validation Tests
Week 6:   Phase 9  - ROM Boot Validation
Week 7:   Phase 10 - IPC Driver Architecture
Week 7-8: Phase 11 - Full BasiliskII Integration
Week 8:   Phase 12 - Production Build

Total: 8-10 weeks to production-ready BasiliskII
```

### Milestones

**Milestone 1: Unicorn Executes Code (End of Week 2)**
- ✅ Unicorn wrapper complete
- ✅ Can execute M68K instructions
- ✅ Register access works
- ✅ EmulOp hooks functional

**Milestone 2: Dual-CPU Validation Works (End of Week 4)**
- ✅ Both CPUs execute side-by-side
- ✅ State comparison working
- ✅ Binary traces generated
- ✅ Python tools can compare traces

**Milestone 3: Instruction Tests Pass (End of Week 6)**
- ✅ All M68K instruction categories tested
- ✅ Zero divergences in instruction tests
- ✅ ROM boot sequence validates
- ✅ Confidence in Unicorn accuracy

**Milestone 4: BasiliskII Boots (End of Week 8)**
- ✅ Full BasiliskII integrated
- ✅ Boots Mac OS 7.5
- ✅ IPC drivers working
- ✅ Dual-CPU validation passes

**Milestone 5: Production Release (End of Week 10)**
- ✅ Unicorn-only build optimized
- ✅ Performance benchmarked
- ✅ Documentation complete
- ✅ Ready for public release

---

## Success Criteria

### Phase Success

Each phase must meet:
- ✅ All code compiles without errors
- ✅ All tests pass
- ✅ Git commit created
- ✅ Documentation updated

### Overall Success

Project is complete when:
- ✅ BasiliskII boots Mac OS 7.5 with Unicorn CPU
- ✅ SheepShaver boots Mac OS 9 with Unicorn CPU
- ✅ All instruction validation tests pass (zero divergences)
- ✅ ROM boot validation passes (100,000+ instructions)
- ✅ Performance is equal or better than UAE/KPX
- ✅ Builds on Linux, macOS, Windows
- ✅ IPC drivers work for video, disk, network, audio
- ✅ Documentation complete
- ✅ CI/CD pipeline working

---

## Conclusion

This master plan provides a **complete roadmap** for building a modern Mac emulator with:

- ✅ **Accurate CPU emulation** via Unicorn Engine
- ✅ **Proven correctness** via dual-CPU validation
- ✅ **Cross-platform support** (Linux, macOS, Windows)
- ✅ **Modern build system** (Meson)
- ✅ **Clean architecture** (IPC drivers)
- ✅ **Comprehensive testing** (instruction tests, ROM validation)

**Timeline:** 8-10 weeks to production

**Next Step:** Begin Phase 1 - Foundation Setup 🚀

---

**End of Master Plan**
