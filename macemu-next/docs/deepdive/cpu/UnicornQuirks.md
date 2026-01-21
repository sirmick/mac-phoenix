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

## Critical Limitations of Unicorn

### ⚠️ CRITICAL: Cannot Change PC from Interrupt Hooks

**This is the most important Unicorn limitation to understand.**

**Problem**: Unicorn intentionally overwrites PC after `UC_HOOK_INTR` callbacks return.

**Discovered**: January 2026 (commits `9464afa4`, `32a6926b`)
**Unicorn Issue**: [GitHub #1027](https://github.com/unicorn-engine/unicorn/issues/1027)

**Impact**:
```c
// This DOES NOT WORK:
static uint32_t hook_interrupt(uc_engine *uc, uint32_t int_no, void *user_data) {
    uint32_t new_pc = 0x1234;
    uc_reg_write(uc, UC_M68K_REG_PC, &new_pc);  // ❌ IGNORED!
    return 0;
}
// After hook returns, Unicorn overwrites PC with exception_next_eip
```

**What We Tried**:
1. ✗ `uc_ctl_remove_cache()` + `uc_reg_write()` - Unicorn FAQ suggestion, doesn't work
2. ✗ `uc_emu_stop()` to break execution - causes other issues
3. ✗ Skipping the instruction - prevents loop but doesn't execute handler

**Root Cause** (from Unicorn source code):
- After interrupt hooks run, Unicorn restores PC from internal state
- The `exception_next_eip` value overwrites any PC changes
- This is by design and affects all architectures

**Consequences for macemu-next**:
- ❌ Cannot simulate M68K exception jumps (A-line, F-line, interrupts)
- ❌ Cannot implement Mac OS trap execution natively in Unicorn
- ❌ ROM boot hangs when encountering non-EmulOp A-line traps
- ✅ A-line EmulOps (0xAE00-0xAE3F) still work (don't need PC changes)

**Workaround**: Execute A-line/F-line traps on UAE, sync state to Unicorn.

See [ALineAndFLineStatus.md](ALineAndFLineStatus.md) for detailed analysis.

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

## See Also

- [CPU Emulation](CPU.md) - Dual-CPU architecture
- [UAE Quirks](UAE-Quirks.md) - UAE-specific details
- [Memory Layout](Memory.md) - Shared memory setup
