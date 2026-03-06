# A-line/F-line Exception Handling for Unicorn Backend

## ⚠️ IMPORTANT: THIS DESIGN DOES NOT WORK

**This document describes a design that looked promising but was ultimately blocked by a fundamental Unicorn limitation.**

**Problem discovered** (January 2026): Unicorn cannot change PC from interrupt hooks. This makes it impossible to implement exception handling as designed below.

**Current status**: See [ALineAndFLineStatus.md](ALineAndFLineStatus.md) for actual implementation status and workarounds.

**Why keep this doc?** It explains how M68K exception handling *should* work and documents what was attempted.

---

## Overview (ORIGINAL DESIGN - DOES NOT WORK)

**Problem**: Unicorn CPU emulator throws `UC_ERR_EXCEPTION` when encountering A-line (0xAxxx) or F-line (0xFxxx) instructions, stopping execution. UAE handles these as Mac OS traps (exception vectors 10 and 11).

**Goal**: Make Unicorn handle A-line/F-line the same way UAE does, without modifying UAE or Unicorn source code, using an elegant "patch-in" approach.

**Why it failed**: Unicorn overwrites PC after `UC_HOOK_INTR` callbacks return, making it impossible to jump to exception handlers.

## Background: How Mac OS Traps Work

### A-line Traps (0xAxxx)
- **Purpose**: Mac OS system calls (Toolbox traps)
- **Examples**:
  - `0xA247` = SetToolTrap
  - `0xA9FF` = GetCursorAddr
  - `0xA9F4` = FindWindow
- **Exception Vector**: 10 (vector table offset 40)

### F-line Traps (0xFxxx)
- **Purpose**: FPU/coprocessor instructions (emulated by Mac OS)
- **Exception Vector**: 11 (vector table offset 44)

### How It Works in Hardware
1. CPU encounters 0xAxxx or 0xFxxx instruction
2. CPU raises exception (A-line = vector 10, F-line = vector 11)
3. CPU switches to supervisor mode
4. CPU pushes exception frame to stack
5. CPU reads handler address from vector table (VBR + vector * 4)
6. CPU jumps to handler
7. Handler executes, calls RTE (Return from Exception)
8. RTE pops exception frame and returns to user code

## Current Implementation: UAE

### UAE's op_illg Handler

Located in [newcpu.cpp:1286](../src/cpu/uae_cpu/newcpu.cpp#L1286):

```c
void REGPARAM2 op_illg (uae_u32 opcode)
{
    uaecptr pc = m68k_getpc ();

    if ((opcode & 0xF000) == 0xA000) {
        Exception(0xA,0);  // A-line trap
        return;
    }

    if ((opcode & 0xF000) == 0xF000) {
        Exception(0xB,0);  // F-line trap
        return;
    }

    // Actually illegal instruction
    write_log ("Illegal instruction: %04x at %08x\n", opcode, pc);
    Exception(4, 0);  // Illegal instruction exception
    return;
}
```

### UAE's Exception Function

Located in [newcpu.cpp:778](../src/cpu/uae_cpu/newcpu.cpp#L778):

```c
void Exception(int nr, uaecptr oldpc)
{
    uae_u32 currpc = m68k_getpc ();
    MakeSR();  // Update SR from internal flags

    // 1. Switch to supervisor mode if needed
    if (!regs.s) {
        regs.usp = m68k_areg(regs, 7);  // Save user stack pointer
        if (CPUType >= 2)
            m68k_areg(regs, 7) = regs.m ? regs.msp : regs.isp;
        else
            m68k_areg(regs, 7) = regs.isp;
        regs.s = 1;  // Set supervisor flag
    }

    // 2. Push exception frame for 68020+ (Quadra uses 68040)
    if (CPUType > 0) {
        // Most exceptions use simple format
        m68k_areg(regs, 7) -= 2;
        put_word (m68k_areg(regs, 7), nr * 4);  // Vector offset
    }

    // 3. Push PC and SR (common to all exception types)
    m68k_areg(regs, 7) -= 4;
    put_long (m68k_areg(regs, 7), currpc);  // PC
    m68k_areg(regs, 7) -= 2;
    put_word (m68k_areg(regs, 7), regs.sr);  // SR

    // 4. Jump to exception handler
    m68k_setpc (get_long (regs.vbr + 4*nr));

    // 5. Clear trace flags
    regs.t1 = regs.t0 = regs.m = 0;
}
```

### Exception Stack Frame Format (68020+)

For A-line/F-line exceptions on 68020/68030/68040:

```
[SP+0] = Status Register (word, 2 bytes)
[SP+2] = Program Counter (long, 4 bytes)
[SP+6] = Vector Offset (word, 2 bytes)
       = exception_number * 4
       = 40 for A-line (10 * 4)
       = 44 for F-line (11 * 4)

Total: 8 bytes
```

## Design: Unicorn Exception Handling

### Architecture Integration

Unicorn exception handling fits into the existing mac-phoenix architecture:

```
mac-phoenix/
├── src/cpu/
│   ├── unicorn_wrapper.c           # Low-level Unicorn API wrapper
│   ├── unicorn_exception.c         # NEW: Exception simulation logic
│   ├── cpu_unicorn.c               # CPUBackend implementation
│   ├── cpu_dualcpu.c               # DualCPU validation
│   └── cpu_backend.h               # Unified CPU backend API
```

### Key Principle: Separation of Concerns

1. **unicorn_wrapper.c**: Pure Unicorn API wrapper
   - Register read/write
   - Memory operations
   - Hook management
   - No Mac-specific logic

2. **unicorn_exception.c** (NEW): M68K exception simulation
   - A-line/F-line detection
   - Exception frame generation
   - Supervisor mode switching
   - Mac OS knowledge lives here

3. **cpu_unicorn.c**: CPUBackend interface implementation
   - Implements CPUBackend API
   - Coordinates between wrapper and exception handler
   - Integrates with platform API

### Implementation Strategy

#### Phase 1: Hook Integration

Extend the existing invalid instruction hook in [unicorn_wrapper.c:35](../src/cpu/unicorn_wrapper.c#L35):

```c
/* Invalid instruction hook for EmulOp and Exception handling */
static bool hook_invalid_insn(uc_engine *uc, void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;
    uint32_t pc;
    uint16_t opcode;

    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_mem_read(uc, pc, &opcode, sizeof(opcode));

    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    opcode = __builtin_bswap16(opcode);
    #endif

    // Check for EmulOp (0x71xx)
    if ((opcode & 0xFF00) == 0x7100) {
        if (cpu->emulop_handler) {
            cpu->emulop_handler(opcode, cpu->emulop_user_data);
            pc += 2;
            uc_reg_write(uc, UC_M68K_REG_PC, &pc);
            return true;  // Handled
        }
    }

    // Check for A-line trap (0xAxxx)
    if ((opcode & 0xF000) == 0xA000) {
        if (cpu->exception_handler) {
            cpu->exception_handler(cpu, 10, opcode);  // Exception 10
            return true;  // Handled
        }
    }

    // Check for F-line trap (0xFxxx)
    if ((opcode & 0xF000) == 0xF000) {
        if (cpu->exception_handler) {
            cpu->exception_handler(cpu, 11, opcode);  // Exception 11
            return true;  // Handled
        }
    }

    return false;  // Not handled, raise exception
}
```

#### Phase 2: Exception Simulation

Create new file `unicorn_exception.c`:

```c
/*
 * M68K Exception Simulation for Unicorn
 *
 * Simulates 68K exception handling mechanism that Unicorn doesn't provide.
 * Based on UAE's Exception() implementation.
 */

#include "unicorn_wrapper.h"
#include "unicorn_exception.h"
#include <unicorn/unicorn.h>
#include <stdio.h>

// Helper: Read 16-bit big-endian word from memory
static uint16_t read_word(UnicornCPU *cpu, uint32_t addr) {
    uint16_t value;
    unicorn_mem_read(cpu, addr, &value, 2);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap16(value);
    #else
    return value;
    #endif
}

// Helper: Write 16-bit big-endian word to memory
static void write_word(UnicornCPU *cpu, uint32_t addr, uint16_t value) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    value = __builtin_bswap16(value);
    #endif
    unicorn_mem_write(cpu, addr, &value, 2);
}

// Helper: Read 32-bit big-endian long from memory
static uint32_t read_long(UnicornCPU *cpu, uint32_t addr) {
    uint32_t value;
    unicorn_mem_read(cpu, addr, &value, 4);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(value);
    #else
    return value;
    #endif
}

// Helper: Write 32-bit big-endian long to memory
static void write_long(UnicornCPU *cpu, uint32_t addr, uint32_t value) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    value = __builtin_bswap32(value);
    #endif
    unicorn_mem_write(cpu, addr, &value, 4);
}

/*
 * Simulate M68K Exception
 *
 * This mimics the behavior of UAE's Exception() function.
 * See newcpu.cpp:778 for reference implementation.
 *
 * @param cpu        Unicorn CPU instance
 * @param vector_nr  Exception vector number (10 for A-line, 11 for F-line)
 * @param opcode     The instruction that triggered the exception (for logging)
 */
void unicorn_simulate_exception(UnicornCPU *cpu, int vector_nr, uint16_t opcode)
{
    // Enable verbose logging if EMULOP_VERBOSE is set
    static int exception_verbose = -1;
    if (exception_verbose == -1) {
        const char *env = getenv("EMULOP_VERBOSE");
        exception_verbose = (env && atoi(env) > 0) ? 1 : 0;
    }

    if (exception_verbose) {
        const char *exc_name = "UNKNOWN";
        if (vector_nr == 10) exc_name = "A-LINE";
        else if (vector_nr == 11) exc_name = "F-LINE";
        printf("[Exception] Vector %d (%s), Opcode 0x%04x\n",
               vector_nr, exc_name, opcode);
    }

    // 1. Read current state
    uint32_t pc = unicorn_get_pc(cpu);
    uint16_t sr = unicorn_get_sr(cpu);
    uint32_t a7 = unicorn_get_areg(cpu, 7);

    // 2. Check supervisor mode (bit 13 of SR)
    bool is_supervisor = (sr & (1 << 13)) != 0;

    if (!is_supervisor) {
        // Switch to supervisor mode

        // Save current A7 as User Stack Pointer
        uc_reg_write(cpu->uc, UC_M68K_REG_CR_USP, &a7);

        // Load Interrupt Stack Pointer into A7
        uint32_t isp;
        uc_reg_read(cpu->uc, UC_M68K_REG_CR_ISP, &isp);
        a7 = isp;
        unicorn_set_areg(cpu, 7, a7);

        // Set supervisor bit in SR
        sr |= (1 << 13);
        unicorn_set_sr(cpu, sr);
    }

    // 3. Build exception stack frame (68020+ format)
    // The Quadra 650 uses 68040, which is CPUType 4 in UAE

    // Push vector offset (word)
    a7 -= 2;
    write_word(cpu, a7, vector_nr * 4);

    // Push PC (long)
    a7 -= 4;
    write_long(cpu, a7, pc);

    // Push SR (word)
    a7 -= 2;
    write_word(cpu, a7, sr);

    // Update A7
    unicorn_set_areg(cpu, 7, a7);

    // 4. Read exception handler address from vector table
    uint32_t vbr;
    uc_reg_read(cpu->uc, UC_M68K_REG_CR_VBR, &vbr);

    uint32_t handler_addr = read_long(cpu, vbr + (vector_nr * 4));

    if (exception_verbose) {
        printf("  VBR=0x%08x, Handler=0x%08x, NewSP=0x%08x\n",
               vbr, handler_addr, a7);
    }

    // 5. Set PC to exception handler
    unicorn_set_pc(cpu, handler_addr);

    // 6. Clear trace flags (T1=bit 15, T0=bit 14)
    sr &= ~((1 << 15) | (1 << 14));
    unicorn_set_sr(cpu, sr);

    // Exception handled - execution will continue from handler
}
```

#### Phase 3: Wrapper Integration

Update [unicorn_wrapper.h](../src/cpu/unicorn_wrapper.h) to add exception handler:

```c
// Exception handler callback
typedef void (*ExceptionHandler)(UnicornCPU *cpu, int vector_nr, uint16_t opcode);

struct UnicornCPU {
    uc_engine *uc;
    UnicornArch arch;
    char error[256];

    /* Hooks */
    EmulOpHandler emulop_handler;
    void *emulop_user_data;

    ExceptionHandler exception_handler;  // NEW

    MemoryHookCallback memory_hook;
    void *memory_user_data;
    uc_hook mem_hook_handle;

    uc_hook invalid_insn_hook;
};

// New function to set exception handler
void unicorn_set_exception_handler(UnicornCPU *cpu, ExceptionHandler handler);
```

#### Phase 4: CPU Backend Integration

Update [cpu_unicorn.c](../src/cpu/cpu_unicorn.c) to register the exception handler during initialization:

```c
static bool unicorn_backend_init(void) {
    // ... existing initialization ...

    // Set exception handler for A-line/F-line traps
    unicorn_set_exception_handler(g_unicorn_cpu, unicorn_simulate_exception);

    return true;
}
```

## Testing Strategy

### Test 1: Single A-line Trap

Create minimal test that triggers A-line trap:

```c
// test_aline.c
uint16_t code[] = {
    0xA247,  // SetToolTrap (A-line trap)
    0x4E71   // NOP
};
```

Expected behavior:
1. Unicorn executes until 0xA247
2. Hook detects A-line pattern
3. Exception handler simulates exception
4. Execution continues from vector table handler

### Test 2: DualCPU Validation Past A-line

Run existing Quadra boot test with DualCPU:

```bash
CPU_BACKEND=dualcpu ./build/mac-phoenix ~/quadra.rom
```

Expected behavior:
- Validation should continue past instruction 23,250
- UAE and Unicorn should execute Mac OS trap handlers identically
- No divergence should occur

### Test 3: Exception Stack Frame Validation

Add validation to DualCPU that compares:
- Stack pointer after exception
- Stack frame contents (SR, PC, vector offset)
- PC after exception (should point to handler)

## Critical Design Decisions

### Decision 1: RTE (Return from Exception) Handling

**Question**: Does Unicorn automatically handle RTE instruction?

**Answer**: YES. RTE is a real 68K instruction that Unicorn emulates correctly:
- Pops SR from stack
- Pops PC from stack
- Pops vector offset (and discards it)
- Restores execution

**Implication**: We do NOT need to hook RTE. The exception mechanism is one-way: we simulate the exception entry, Unicorn handles the exit via RTE.

### Decision 2: PC Value During Exception

**Question**: Should PC point to the A-line instruction or the next instruction?

**Answer**: Point to the A-line instruction. The exception handler may want to:
- Read the instruction to determine which trap
- Decode parameters from the instruction bits
- Mac OS trap handlers expect this behavior

**Implication**: Do NOT advance PC like we do for EmulOps (which are fully handled and should skip).

### Decision 3: Memory Ordering

**Question**: Do we need to handle big-endian memory access?

**Answer**: YES. M68K is big-endian, x86 is little-endian. The helper functions (read_word, write_word, read_long, write_long) must handle byte swapping.

### Decision 4: CPUType Handling

**Question**: What CPU type does Unicorn emulate?

**Answer**: Check the CPU model at runtime:
- Quadra 650 uses 68040
- Exception stack frame format varies by CPU type
- For now, assume 68020+ format (all modern Macs)

**Future**: May need to query Unicorn's CPU model and adjust stack frame format.

## Future Enhancements

### Other Exceptions

Once A-line/F-line work, we may need to implement:

1. **Exception 3: Address Error** - Misaligned memory access
2. **Exception 5: Zero Divide** - Division by zero
3. **Exception 8: Privilege Violation** - User mode accessing supervisor
4. **Exception 24-31: Interrupts** - VBL, device interrupts

These can use the same exception simulation infrastructure.

### Exception Hook Optimization

Currently detects pattern in hook_invalid_insn. Could optimize by:
- Using Unicorn's code hook to pre-scan for A-line/F-line
- Building a cache of known trap addresses
- Reducing hook overhead for common cases

### Trace Mode Support

UAE's Exception() clears trace flags. We may need to:
- Implement trace exception (vector 9)
- Handle single-step debugging
- Support Mac debuggers (MacsBug, TMON)

## References

- UAE Exception implementation: [newcpu.cpp:778](../src/cpu/uae_cpu/newcpu.cpp#L778)
- UAE op_illg implementation: [newcpu.cpp:1286](../src/cpu/uae_cpu/newcpu.cpp#L1286)
- Unicorn wrapper: [unicorn_wrapper.c](../src/cpu/unicorn_wrapper.c)
- CPU backend API: [cpu_backend.h](../src/cpu/cpu_backend.h)
- M68K exception format: Motorola M68000 Family Programmer's Reference Manual

## Implementation Checklist

- [ ] Create `unicorn_exception.c` with exception simulation
- [ ] Add exception handler to `UnicornCPU` structure
- [ ] Update `hook_invalid_insn` to detect A-line/F-line
- [ ] Add `unicorn_set_exception_handler()` function
- [ ] Integrate exception handler in `cpu_unicorn.c` initialization
- [ ] Test with single A-line instruction
- [ ] Test with DualCPU validation (Quadra boot)
- [ ] Verify exception stack frame matches UAE
- [ ] Add verbose logging (EMULOP_VERBOSE)
- [ ] Document any divergences from UAE behavior
