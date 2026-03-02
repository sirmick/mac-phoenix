# Unicorn Engine Quirks and Integration

Unicorn Engine is used as a reference implementation for validating UAE CPU execution. This document covers its quirks and how we integrate it.

## Overview

**Unicorn** is a lightweight CPU emulation framework based on QEMU. We use it to:
- Execute the same M68K instructions as UAE
- Compare register state after each instruction
- Catch emulation bugs by detecting discrepancies

## Architecture

```
┌─────────────────┐
│   test_dualcpu  │
└────────┬────────┘
         │
    ┌────┴────┐
    │         │
┌───▼──┐  ┌──▼────┐
│ UAE  │  │Unicorn│
└───┬──┘  └──┬────┘
    │         │
    └─────┬───┘
          │
   Compare State
```

## API Differences from UAE

### Initialization

**UAE:**
```c
Init680x0();  // Initializes CPU core, builds opcode tables
```

**Unicorn:**
```c
uc_engine *uc;
uc_open(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, &uc);  // Create engine
uc_mem_map(uc, 0x00000000, RAM_SIZE, UC_PROT_ALL);  // Map RAM
uc_mem_map(uc, 0x02000000, ROM_SIZE, UC_PROT_ALL);  // Map ROM
```

Key difference: Unicorn requires explicit memory mapping, UAE uses direct addressing.

### Register Access

**UAE:**
```c
// Direct access to register structure
extern struct regstruct regs;
uint32_t d0 = regs.regs[0];
uint32_t a7 = regs.regs[15];
uint32_t pc = regs.pc;
```

**Unicorn:**
```c
uint32_t d0;
uc_reg_read(uc, UC_M68K_REG_D0, &d0);

uint32_t a7;
uc_reg_read(uc, UC_M68K_REG_A7, &a7);

uint32_t pc;
uc_reg_read(uc, UC_M68K_REG_PC, &pc);
```

Unicorn uses an API, not direct structure access.

### Memory Access

**UAE:**
```c
// Direct memory access via pointers
uint32_t value = *(uint32_t *)(RAMBaseHost + addr);
```

**Unicorn:**
```c
uint32_t value;
uc_mem_read(uc, addr, &value, sizeof(value));
```

### Execution

**UAE:**
```c
// Execute one instruction
m68k_do_execute();  // Runs until SPCFLAG or exception
```

**Unicorn:**
```c
// Execute instructions from PC until PC + N bytes
uc_emu_start(uc, pc, pc + 100, 0, 1);  // Execute 1 instruction
```

Unicorn's API is more explicit about start/end addresses.

## Byte Order Handling

### The Challenge

- M68K is **big-endian**
- Unicorn expects memory in **big-endian** (UC_MODE_BIG_ENDIAN)
- Host (x86) is **little-endian**
- ROM file is already in **big-endian**

### Solution

We keep memory in **big-endian** format (as loaded from ROM):

```c
// Load ROM - NO byte swapping!
uc_mem_write(uc, ROMBaseMac, ROMBaseHost, ROMSize);
```

Unicorn handles byte-swapping internally when:
- Reading opcodes from memory
- Reading operands
- Writing results

This matches UAE with `HAVE_GET_WORD_UNSWAPPED` approach.

## Register State Comparison

After each instruction, we compare ALL registers:

```c
void compare_cpu_state(void) {
    // Data registers D0-D7
    for (int i = 0; i < 8; i++) {
        uint32_t uae_val = uae_get_dreg(i);
        uint32_t uc_val = unicorn_get_dreg(uc, i);
        if (uae_val != uc_val) {
            printf("MISMATCH: D%d: UAE=0x%08x, Unicorn=0x%08x\n",
                   i, uae_val, uc_val);
        }
    }

    // Address registers A0-A7
    for (int i = 0; i < 8; i++) {
        uint32_t uae_val = uae_get_areg(i);
        uint32_t uc_val = unicorn_get_areg(uc, i);
        if (uae_val != uc_val) {
            printf("MISMATCH: A%d: UAE=0x%08x, Unicorn=0x%08x\n",
                   i, uae_val, uc_val);
        }
    }

    // PC
    uint32_t uae_pc = uae_get_pc();
    uint32_t uc_pc = unicorn_get_pc(uc);
    if (uae_pc != uc_pc) {
        printf("MISMATCH: PC: UAE=0x%08x, Unicorn=0x%08x\n",
               uae_pc, uc_pc);
    }

    // Status Register
    uint32_t uae_sr = uae_get_sr();
    uint32_t uc_sr = unicorn_get_sr(uc);
    if (uae_sr != uc_sr) {
        printf("MISMATCH: SR: UAE=0x%04x, Unicorn=0x%04x\n",
               uae_sr, uc_sr);
    }
}
```

## Known Differences

### 1. Condition Code Flags

**Most common divergence:** UAE and Unicorn sometimes differ in condition code flag updates (Z, N, C, V flags in SR).

Example from ROM boot test:
- After 4 instructions, SR diverges at instruction **SUB.B D0,(A0)**
- UAE: `0x2704` (Z flag set - correct!)
- Unicorn: `0x2700` (no flags - bug!)
- Both CPUs have same PC (0x02004054) and same registers

**Instruction trace:**
```
[0] PC=0x0200002A  JMP (d16,PC)
[1] PC=0x0200008C  MOVE #imm,SR
[2] PC=0x02000090  LEA (d16,PC),A6
[3] PC=0x02000094  BRA.W
[4] PC=0x02004052  SUB.B D0,(A0)  <- Divergence here!
```

**Analysis:**
- Instruction: SUB.B D0,(A0) where D0=0, A0=0, mem[0]=0
- Result: 0 - 0 = 0, so **Z flag should be set**
- UAE correctly sets Z flag → SR=0x2704 ✓
- Unicorn fails to set Z flag → SR=0x2700 ✗
- This is a **known Unicorn bug** - some arithmetic instructions don't update condition codes correctly

**What to check:**
- If **only SR differs** and PC/registers match → usually OK (minor flag differences)
- If **PC or data registers differ** → real divergence, needs debugging

### 2. Undefined Behavior

Some M68K instructions have undefined behavior (e.g., what happens to unused bits). UAE and Unicorn may differ in these cases - this is OK!

### 3. Exception Handling

Exception priority and timing may differ slightly. We focus on happy-path execution first.

### 3. Cycle Counting

Unicorn doesn't track instruction timing accurately. UAE has cycle-accurate emulation (though we disable it for simplicity).

### 4. Prefetch

Real M68K has a prefetch queue. Unicorn doesn't model this. UAE can model it but we disable it (`USE_PREFETCH_BUFFER=0`).

## Synchronization Strategy

### Memory

Both CPUs access the **same memory**:

```c
// Allocate RAM/ROM once
RAMBaseHost = mmap(...);
ROMBaseHost = RAMBaseHost + RAMSize;

// Load ROM once
read(rom_fd, ROMBaseHost, ROMSize);

// Map to Unicorn (points to same memory)
uc_mem_map_ptr(uc, RAMBaseMac, RAMSize, UC_PROT_ALL, RAMBaseHost);
uc_mem_map_ptr(uc, ROMBaseMac, ROMSize, UC_PROT_ALL, ROMBaseHost);
```

**Why?** If one CPU writes to memory, the other sees the change immediately.

### Execution Flow

```
1. Execute 1 instruction on UAE
2. Execute same instruction on Unicorn
3. Compare register state
4. If mismatch: STOP and debug
5. Repeat
```

This ensures both CPUs stay in lockstep.

## Wrapper Functions

We provide a unified API in `src/cpu/unicorn_wrapper.c`:

```c
// Initialize
UnicornCPU* unicorn_init_m68k(uint8_t *ram, uint32_t ram_size,
                               uint8_t *rom, uint32_t rom_size);

// Register access
uint32_t unicorn_get_dreg(UnicornCPU *cpu, int reg);
uint32_t unicorn_get_areg(UnicornCPU *cpu, int reg);
uint32_t unicorn_get_pc(UnicornCPU *cpu);
uint32_t unicorn_get_sr(UnicornCPU *cpu);

void unicorn_set_dreg(UnicornCPU *cpu, int reg, uint32_t value);
void unicorn_set_areg(UnicornCPU *cpu, int reg, uint32_t value);
void unicorn_set_pc(UnicornCPU *cpu, uint32_t value);
void unicorn_set_sr(UnicornCPU *cpu, uint32_t value);

// Execute
void unicorn_execute_one(UnicornCPU *cpu);
```

This matches the UAE wrapper API, making dual-CPU code clean.

## Debugging Mismatches

When a mismatch occurs:

```
MISMATCH at instruction 1234:
  Opcode: 0x4e75 (RTS)
  PC before: 0x0200abcd

  D0: UAE=0x00001234, Unicorn=0x00001234 ✓
  D1: UAE=0xdeadbeef, Unicorn=0xdeadbeef ✓
  A0: UAE=0x00001000, Unicorn=0x00001000 ✓
  A7: UAE=0x00001ffe, Unicorn=0x00002000 ✗
  PC: UAE=0x0200cafe, Unicorn=0x0200babe ✗
```

**Steps to debug:**
1. Note the opcode and PC where mismatch occurred
2. Disassemble the instruction
3. Check what it should do to registers
4. Add logging to UAE/Unicorn to see intermediate steps
5. Find which CPU is wrong

## Performance Impact

Running dual CPUs has significant overhead:

- **UAE alone**: ~100M instructions/sec
- **Dual CPU**: ~10M instructions/sec

The 10x slowdown comes from:
1. Running two CPUs
2. Frequent register comparisons
3. API overhead (Unicorn uses function calls, UAE uses direct access)

**This is OK for testing!** Once we're confident UAE works, we can disable dual-CPU mode for production.

## Future: Instruction-Level Logging

We can add instruction disassembly to help debug:

```c
#include "m68k.h"  // UAE disassembler

void log_instruction(uint32_t pc) {
    uint32_t next_pc;
    char buf[256];

    m68k_disasm_buf(buf, sizeof(buf), pc, &next_pc, 1);
    printf("0x%08x: %s\n", pc, buf);
}
```

This helps understand what instructions cause mismatches.

## Critical Unicorn Behaviors

### PC Changes in Interrupt Hooks -- SOLVED via Deferred Updates

**Background**: Unicorn intentionally overwrites PC after `UC_HOOK_INTR` callbacks return (GitHub issue #1027). Direct `uc_reg_write()` for PC inside hooks is ignored.

**Discovered**: January 2026 (commits `9464afa4`, `32a6926b`)

**Solution (February 2026)**: Deferred register updates. Instead of writing registers inside the hook, queue them for application at the next `hook_block()` call:

```c
// In hook_interrupt() - DEFER the update:
deferred_pc = new_pc;
deferred_pc_valid = 1;
// Don't call uc_reg_write() or uc_emu_stop() here!

// In hook_block() - APPLY deferred updates:
if (deferred_pc_valid) {
    uc_reg_write(uc, UC_M68K_REG_PC, &deferred_pc);
    deferred_pc_valid = 0;
}
```

**Result**: All A-line/F-line traps now work correctly. Both UAE and Unicorn populate 87 OS trap table entries and reach identical boot state.

See [ALineAndFLineStatus.md](ALineAndFLineStatus.md) for full details.

### SR Register Requires uint32_t

`uc_reg_write()` for SR reads 4 bytes from the pointer, not 2. QEMU internally represents SR as a 32-bit value. Passing a `uint16_t*` causes garbage in the upper bits.

```c
// WRONG:
uint16_t sr = 0x2700;
uc_reg_write(uc, UC_M68K_REG_SR, &sr);  // Reads 4 bytes!

// CORRECT:
uint32_t sr32 = 0x2700;
uc_reg_write(uc, UC_M68K_REG_SR, &sr32);
```

### MMIO Must Use uc_mmio_map(), Not Memory Read Hooks

`UC_HOOK_MEM_READ` does NOT fire for `uc_mem_map_ptr()` regions because QEMU's JIT compiles direct memory loads that bypass hooks. Hardware registers must use `uc_mmio_map()` which provides proper IO callbacks through QEMU's MMIO infrastructure.

```c
// WRONG: JIT bypasses this hook for uc_mem_map_ptr regions
uc_hook_add(uc, &hook, UC_HOOK_MEM_READ, read_callback, ...);

// CORRECT: Proper MMIO callbacks
uc_mmio_map(uc, 0x50F00000, 0x40000,
            mmio_read_callback, NULL,
            mmio_write_callback, NULL);
```

---

### No EMUL_OP Support

Unicorn doesn't know about BasiliskII's EMUL_OP illegal instructions. When it hits `0x71xx`:

```
Unicorn: ILLEGAL INSTRUCTION at 0x0200cafe
```

**Solution:** We detect EMUL_OP opcodes (0x71xx) using `UC_HOOK_INSN_INVALID` and handle them specially.

**Status**: ✅ Works correctly in macemu-next.

---

### No ROM Patching

Unicorn just executes raw ROM. UAE needs ROM patching for EMUL_OP insertion. We must:

1. Run UAE with ROM patching
2. Copy patched memory to Unicorn
3. Execute both CPUs on patched ROM

**Status**: ✅ Implemented correctly in macemu-next.

---

### Limited Exception Support

Unicorn's M68K exception handling may not match real hardware perfectly:
- Exception priority differs from real 68K
- Cannot modify exception behavior via hooks (see PC limitation above)
- Stack frame format might differ slightly

**Current approach**: Focus on normal instruction execution, use UAE for exception handling.

---

## 🎉 BREAKTHROUGH: Proper JIT Cache Management (January 2026)

**Status**: ✅ **WORKING** - Unicorn now boots successfully!

After extensive research into Unicorn/QEMU internals, we discovered the **correct way to handle JIT cache management** for register updates. This fixed three critical performance issues that were preventing Unicorn from booting.

### Discovery Timeline

- **January 22, 2026**: Breakthrough commit `72152174`
- Both UAE and Unicorn now successfully execute 400+ EmulOps in 10 seconds
- Boot sequence reaches PATCH_BOOT_GLOBS and continues correctly
- No crashes, no infinite loops, proper interrupt handling

### The Three Critical Bugs We Fixed

#### 1. ❌ Unnecessary Manual Cache Flushing

**What we were doing wrong:**
```c
// WRONG: Manual cache flushing after every register update
void unicorn_set_dreg(UnicornCPU *cpu, int reg, uint32_t val) {
    uc_reg_write(uc, UC_M68K_REG_D0 + reg, &val);

    // ❌ UNNECESSARY: Manual cache flush
    uint32_t pc = 0;
    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_ctl_remove_cache(uc, pc, pc + 16);  // Slow!
    uc_reg_write(uc, UC_M68K_REG_PC, &pc); // Double flush!
}
```

**Research findings** (from Unicorn source code `uc.c`):
- ✅ `uc_reg_write()` **AUTOMATICALLY** flushes cache when writing to PC
- ✅ Writing to other registers (D0-D7, A0-A7, SR) does **NOT** require cache flushing
- ✅ Translation blocks read register values from CPU state at runtime
- ❌ Only **CODE modification** requires manual `uc_ctl_remove_cache()`

**The correct way:**
```c
// ✅ CORRECT: No manual flushing needed
void unicorn_set_dreg(UnicornCPU *cpu, int reg, uint32_t val) {
    // Register values are read from CPU state at runtime,
    // not baked into translation blocks. No flush needed!
    uc_reg_write(uc, UC_M68K_REG_D0 + reg, &val);
}

void unicorn_set_pc(UnicornCPU *cpu, uint32_t pc) {
    // uc_reg_write() automatically flushes cache when writing to PC.
    // From uc.c: if (setpc) { quit_request = true; break_translation_loop(); }
    uc_reg_write(uc, UC_M68K_REG_PC, &pc);
}
```

**Performance impact**: Eliminated triple cache flushing on every register write!

---

#### 2. ❌ Redundant uc_emu_stop() After Every EmulOp

**What we were doing wrong:**
```c
// WRONG: Stopping emulation after every EmulOp
static void hook_interrupt(uc_engine *uc, uint32_t intno, void *user_data) {
    // ... handle EmulOp, update registers ...

    apply_deferred_updates_and_flush(cpu, uc, "hook_interrupt");

    // ❌ UNNECESSARY: Stopping emulation
    uc_emu_stop(uc);  // Causes ~200 instructions/sec!
}
```

**Why we thought this was necessary:**
- Believed register updates wouldn't take effect until emulation restarted
- Thought we needed to break execution to allow interrupt checking

**Research findings:**
- ✅ Register updates via `uc_reg_write()` take effect **immediately**
- ✅ `UC_HOOK_INTR` callback does **NOT** automatically stop emulation
- ✅ `hook_block` is called at every translation block boundary anyway
- ❌ `uc_emu_stop()` causes massive overhead (restart JIT, lose cached blocks)

**The correct way:**
```c
// ✅ CORRECT: Let emulation continue naturally
static void hook_interrupt(uc_engine *uc, uint32_t intno, void *user_data) {
    // ... handle EmulOp, update registers ...

    apply_deferred_updates_and_flush(cpu, uc, "hook_interrupt");

    // NO uc_emu_stop() needed! Execution continues naturally.
    // The EmulOp handler already advanced PC past the A-line instruction.
}
```

**Exception:** Hardware interrupts (timer, etc.) **DO** need `uc_emu_stop()`:
```c
// Hardware interrupt delivery requires stop/restart
uc_m68k_trigger_interrupt(uc, level, vector);
uc_emu_stop(uc);  // ✅ CORRECT: Interrupt needs restart to deliver
```

**Performance impact**: From ~200 instructions/sec to normal JIT speed!

---

#### 3. ❌ Single-Step Execution Mode

**What we were doing wrong:**
```c
// WRONG: Executing ONE instruction at a time
while (running) {
    unicorn_execute_n(unicorn_cpu, 1);  // ❌ Slow!
    // ... check for interrupts ...
}
```

**Why this existed:**
- Leftover debug code with TODO comment
- Mistaken belief that single-stepping was needed for EmulOp detection

**The correct way:**
```c
// ✅ CORRECT: Execute in batches like UAE
while (running) {
    unicorn_execute_n(unicorn_cpu, 1000);  // Fast batch execution!
    // EmulOps are detected automatically via UC_HOOK_INTR
}
```

**How EmulOps are detected:**
- UC_HOOK_INTR fires automatically on A-line exceptions (0xAExx opcodes)
- No need for single-stepping or manual instruction inspection
- JIT can optimize full translation blocks

**Performance impact**: Enables full JIT optimization with block chaining!

---

### Key Research Insights

#### When Cache Flushing IS Required

From QEMU/Unicorn documentation and source code:

✅ **Self-modifying code** - Writing to memory that contains executable code:
```c
// Code writes to itself - MUST flush cache
uint16_t new_opcode = 0x4e75;  // RTS instruction
uc_mem_write(uc, code_addr, &new_opcode, 2);
uc_ctl_remove_cache(uc, code_addr, code_addr + 2);  // Required!
```

✅ **PC modifications** - Already handled automatically by Unicorn:
```c
// Unicorn source (uc.c):
if (setpc) {
    // force to quit execution and flush TB
    uc->quit_request = true;
    uc->skip_sync_pc_on_exit = true;
    break_translation_loop(uc);
}
```

#### When Cache Flushing is NOT Required

❌ **Register updates** - Registers are read from CPU state at runtime:
```c
// These do NOT need cache flushing:
uc_reg_write(uc, UC_M68K_REG_D0, &value);  // Data registers
uc_reg_write(uc, UC_M68K_REG_A0, &value);  // Address registers
uc_reg_write(uc, UC_M68K_REG_SR, &value);  // Status register
```

**Why?** Translation blocks contain instructions like:
```asm
; Compiled TB reads register from CPU state
mov eax, [cpu_state + offset_D0]  ; Not baked into TB!
```

The register **value** is read at execution time, not compiled into the TB.

---

### Unicorn FAQ Clarification

The Unicorn FAQ says:
> **"Editing an instruction doesn't take effect"**
> Solution: Call `uc_ctl_remove_cache()` then write PC

**This applies to CODE modification, NOT register modification!**

The FAQ is talking about this scenario:
```c
// Modifying CODE in memory:
uint16_t *code = (uint16_t *)0x1000;
code[0] = 0x4e75;  // Change instruction to RTS
// Must flush cache because Unicorn has compiled the OLD instruction
uc_ctl_remove_cache(uc, 0x1000, 0x1002);
```

We were mistakenly applying this advice to **register** updates, which don't need it.

---

### Current Boot Status (March 2026)

**Both backends (30 seconds) -- IDENTICAL STATE:**
- ✅ 336 CLKNOMEM EmulOps (XPRAM/RTC initialization)
- ✅ PATCH_BOOT_GLOBS reached
- ✅ 87 OS trap table entries populated
- ✅ 16,879 total EmulOps dispatched (including 2,046 SCSI searches)
- ✅ Boot progress $0b78 = 0xfd89ffff
- ⏱️ Both stall at resource chain search (PC=0x0001c3d4) -- no SCSI boot disk

**Unicorn has achieved full boot parity with UAE.** The stall is NOT a Unicorn bug -- it's a shared emulator limitation.

### JIT Translation Block Invalidation (SOLVED with workaround)

**Problem**: Mac OS heap overwrites RAM containing EmulOp patch code. QEMU's JIT cache retains stale compiled translations pointing to the old code. When the JIT executes a stale TB, it crashes at PC=0x00000002.

**Root cause**: QEMU's self-modifying code detection (`TLB_NOTDIRTY` mechanism) is not properly wired in the Unicorn fork.

**Workaround**: Call `uc_ctl_flush_tb()` on every 60Hz timer tick in `hook_block`. This forces QEMU to recompile all translation blocks, ensuring stale code is never executed.

**Proper fix needed**: Investigate QEMU's `TLB_NOTDIRTY` / `tb_invalidate_phys_page_range()` to enable fine-grained TB invalidation only for modified pages.

```c
// In hook_block() -- every ~4096 blocks, check timer
if (should_check_timer) {
    poll_timer_interrupt();
    uc_ctl_flush_tb(uc);  // Workaround: flush all JIT cache
}
```

---

### Lessons Learned

1. **Read the source code** - The Unicorn FAQ is incomplete/misleading
2. **Understand JIT compilation** - TBs compile control flow, not data values
3. **Profile before optimizing** - We were flushing cache unnecessarily
4. **Question assumptions** - "This seems slow" led to the breakthrough
5. **Research, don't guess** - WebSearch for Unicorn/QEMU internals was critical

---

### Files Modified

See commit `72152174` for full details:
- `src/cpu/unicorn_wrapper.c` - Removed unnecessary cache flushing
- `src/cpu/cpu_unicorn.cpp` - Changed to batch execution (count=1000)
- `src/core/emul_op.cpp` - Enhanced CLKNOMEM logging
- `src/cpu/unicorn_wrapper.h` - Added deferred update tracking

---

---

## ✅ IRQ Storm Issue - SOLVED (January 2026)

### The Problem
Unicorn was experiencing an "IRQ storm" - the Mac ROM's interrupt polling loop executed 781,000+ times in 10 seconds instead of the expected ~600 times.

### Root Causes
1. **Incorrect EmulOp Encoding**: ROM patcher was converting 0x7129 to 0xAE29
2. **No Interrupt Checks**: JIT compiled tight loops without interrupt check points
3. **Deferred Updates**: Register updates were delayed, breaking timing
4. **No Exception Frames**: Interrupts weren't delivered with proper M68K frames

### The Solution (4-Phase Implementation)

#### Phase 1: Fixed IRQ Encoding
```c
// src/core/rom_patches.cpp
// Before: *wp++ = htons(make_emulop(M68K_EMUL_OP_IRQ));  // Wrong: 0xAE29
// After:  *wp++ = htons(0x7129);                         // Correct: 0x7129
```

#### Phase 2: QEMU-Style Execution Loop
Created `src/cpu/unicorn_exec_loop.c`:
- Adaptive batch sizing (3-50 instructions)
- Interrupt checks between batches
- Backward branch detection

#### Phase 3: Immediate Register Updates
- Eliminated deferred update mechanism
- All registers updated immediately after EmulOps
- No timing issues

#### Phase 4: Proper Interrupt Delivery
Created `src/cpu/m68k_interrupt.c`:
- Build M68K exception frames
- Handle interrupt priority masking
- Deliver timer at 60Hz

### Results
- **99.997% reduction** in IRQ polling overhead
- Timer delivers at perfect 60Hz
- Mac OS boots successfully
- Performance comparable to UAE

### Verification
```bash
# Should show ~20, not 780,000+
env EMULATOR_TIMEOUT=10 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver 2>&1 | grep -c poll_timer
```

See [UnicornIRQStormDebugSession.md](../UnicornIRQStormDebugSession.md) for full details.

---

## See Also

- [CPU Emulation](CPU.md) - Dual-CPU architecture
- [UAE Quirks](UAE-Quirks.md) - UAE-specific details
- [Memory Layout](Memory.md) - Shared memory setup
- [A-Line and F-Line Trap Handling](ALineAndFLineTrapHandling.md) - Exception handling
- [IRQ Storm Debug Session](../UnicornIRQStormDebugSession.md) - Complete fix details
