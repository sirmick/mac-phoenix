# Phased Implementation Plan for macemu-next Fixes

## Overview

This document provides a step-by-step implementation plan to fix the interrupt handling and EmulOp issues in macemu-next by adapting QEMU's architectural patterns around Unicorn.

Each phase is designed to be implementable in a single session, with clear test criteria and rollback points.

---

## Phase 1: Critical Bug Fix - IRQ EmulOp Encoding (30 minutes)

### Objective
Fix the incorrect A-line encoding (0xAE29) that should be EmulOp (0x7129).

### Files to Modify
1. `src/core/rom_patches.cpp`
   - Line 1043
   - Line 1696

### Implementation Steps

```c
// Step 1.1: Fix the IRQ EmulOp encoding
// OLD (WRONG):
*wp++ = htons(make_emulop(M68K_EMUL_OP_IRQ));

// NEW (CORRECT):
*wp++ = htons(0x7129);  // Direct EmulOp encoding
```

### Test Command
```bash
# Test that IRQ EmulOp is recognized correctly
EMULATOR_TIMEOUT=1 CPU_BACKEND=unicorn EMULOP_VERBOSE=1 ./build/macemu-next --no-webserver 2>&1 | grep "EmulOp 0x7129"
```

### Success Criteria
- Should see "EmulOp 0x7129" in output, not "opcode 0xae29"
- No A-line exception messages

### Rollback
```bash
git diff src/core/rom_patches.cpp > phase1_backup.patch
git checkout -- src/core/rom_patches.cpp
```

---

## Phase 2: Add QEMU-Style Execution Loop (1 hour)

### Objective
Wrap Unicorn execution in a control loop that checks interrupts between small instruction batches.

### Files to Create
1. `src/cpu/unicorn_exec_loop.c` (new file)

### Implementation

```c
// src/cpu/unicorn_exec_loop.c

#include "unicorn_wrapper.h"
#include <stdio.h>

// QEMU-inspired execution loop with interrupt checking
int unicorn_execute_with_interrupts(UnicornCPU *cpu, int max_total_insns) {
    int total_executed = 0;
    const int BATCH_SIZE = 20;  // Like a small TB in QEMU

    while (total_executed < max_total_insns) {
        uint32_t pc;
        uc_reg_read(cpu->uc, UC_M68K_REG_PC, &pc);

        // CRITICAL: Check interrupts BEFORE execution (QEMU pattern)
        if (poll_and_check_interrupts(cpu)) {
            // Interrupt was delivered, restart loop
            continue;
        }

        // Execute small batch
        int to_execute = (max_total_insns - total_executed);
        if (to_execute > BATCH_SIZE) {
            to_execute = BATCH_SIZE;
        }

        uc_err err = uc_emu_start(cpu->uc, pc, 0, 0, to_execute);
        total_executed += to_execute;

        // Handle errors
        if (err == UC_ERR_INSN_INVALID) {
            if (!handle_invalid_insn(cpu)) {
                return -1;  // Unhandled exception
            }
        } else if (err != UC_ERR_OK) {
            return -1;  // Other error
        }

        // Check if we hit a loop instruction (force interrupt check)
        if (detected_backward_branch(cpu)) {
            continue;  // Force interrupt check
        }
    }

    return total_executed;
}

// Helper: Check for backward branches (simplified)
static bool detected_backward_branch(UnicornCPU *cpu) {
    uint32_t pc;
    uc_reg_read(cpu->uc, UC_M68K_REG_PC, &pc);

    // Check if we're in the IRQ polling loop region
    if (pc >= 0x0200a29a && pc <= 0x0200a29e) {
        return true;  // Force interrupt check
    }

    return false;
}
```

### Files to Modify
2. `src/cpu/cpu_unicorn.cpp`

```c
// Replace unicorn_execute_n() calls with:
extern "C" int unicorn_execute_with_interrupts(UnicornCPU *cpu, int max_insns);

static CPUExecReturn unicorn_backend_execute(void) {
    // OLD:
    // unicorn_execute_n(unicorn_cpu, 1000);

    // NEW:
    int result = unicorn_execute_with_interrupts(unicorn_cpu, 1000);
    if (result < 0) {
        return CPU_EXEC_ABORTED;
    }
    return CPU_EXEC_OK;
}
```

### Test Commands
```bash
# Build
ninja -C build

# Test - should NOT see IRQ storm
EMULATOR_TIMEOUT=10 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver 2>&1 | grep -c "EmulOp 7129"
# Expected: <1000 (not 781000!)
```

### Success Criteria
- IRQ EmulOps reduced from ~781,000 to <1,000 in 10 seconds
- No crashes or hangs
- Timer interrupts still delivered

---

## Phase 3: Fix EmulOp Register Updates (1 hour)

### Objective
Handle EmulOps in the execution loop error path for immediate register updates.

### Files to Modify
1. `src/cpu/unicorn_exec_loop.c` (extend from Phase 2)

### Implementation

```c
// Add to unicorn_exec_loop.c

static bool handle_emulop_immediate(UnicornCPU *cpu, uint16_t opcode) {
    // Call the EmulOp handler
    if (cpu->emulop_handler) {
        cpu->emulop_handler(opcode, cpu->emulop_user_data);

        // CRITICAL: Update registers IMMEDIATELY
        // Don't use deferred updates!

        // Get updated registers from Platform API
        for (int i = 0; i < 8; i++) {
            uint32_t dreg = g_platform.cpu_get_dreg(i);
            uint32_t areg = g_platform.cpu_get_areg(i);
            uc_reg_write(cpu->uc, UC_M68K_REG_D0 + i, &dreg);
            uc_reg_write(cpu->uc, UC_M68K_REG_A0 + i, &areg);
        }

        // Update SR if needed
        uint32_t sr = g_platform.cpu_get_sr();
        uc_reg_write(cpu->uc, UC_M68K_REG_SR, &sr);

        // Advance PC past EmulOp
        uint32_t pc;
        uc_reg_read(cpu->uc, UC_M68K_REG_PC, &pc);
        pc += 2;
        uc_reg_write(cpu->uc, UC_M68K_REG_PC, &pc);

        return true;
    }
    return false;
}

// Modified error handling in main loop
if (err == UC_ERR_INSN_INVALID) {
    uint16_t opcode;
    uc_mem_read(cpu->uc, pc, &opcode, 2);
    opcode = bswap16(opcode);  // Fix endianness

    if ((opcode & 0xFF00) == 0x7100) {
        // It's an EmulOp!
        if (!handle_emulop_immediate(cpu, opcode)) {
            return -1;
        }
        total_executed++;  // Count the EmulOp
    } else {
        // Real illegal instruction
        return -1;
    }
}
```

### Remove Deferred Updates
2. `src/cpu/unicorn_wrapper.c`

```c
// Comment out or remove all deferred update code:
// - has_deferred_sr_update
// - deferred_dreg_value[]
// - apply_deferred_updates_and_flush()

// These are no longer needed with immediate updates
```

### Test Commands
```bash
# Test EmulOp register modifications
EMULATOR_TIMEOUT=2 CPU_BACKEND=unicorn EMULOP_VERBOSE=1 ./build/macemu-next --no-webserver 2>&1 | grep "CLKNOMEM"
# Should see CLKNOMEM operations completing successfully
```

### Success Criteria
- EmulOps modify registers immediately
- No deferred update messages
- CLKNOMEM and other EmulOps work correctly

---

## Phase 4: Add QEMU-Style Interrupt Delivery (2 hours)

### Objective
Copy QEMU's M68K interrupt delivery mechanism for proper exception handling.

### Files to Create
1. `src/cpu/m68k_interrupt.c` (new file)

### Implementation

```c
// src/cpu/m68k_interrupt.c
// Adapted from QEMU's target/m68k/op_helper.c

#include "unicorn_wrapper.h"
#include <stdint.h>

// QEMU's exception frame formats (from op_helper.c)
static void build_exception_frame(UnicornCPU *cpu, uint32_t *sp,
                                  int format, uint16_t sr,
                                  uint32_t addr, uint32_t pc) {
    // Format 0: Standard frame (most common)
    if (format == 0) {
        *sp -= 2;
        uc_mem_write_be16(cpu->uc, *sp, sr);      // Status Register
        *sp -= 4;
        uc_mem_write_be32(cpu->uc, *sp, pc);      // Program Counter
    }
    // Add other formats as needed
}

// QEMU-style interrupt delivery (adapted from m68k_interrupt_all)
void deliver_m68k_interrupt(UnicornCPU *cpu, int level, int vector) {
    uint32_t sr, pc, sp, vbr;

    // Read current state
    uc_reg_read(cpu->uc, UC_M68K_REG_SR, &sr);
    uc_reg_read(cpu->uc, UC_M68K_REG_PC, &pc);
    uc_reg_read(cpu->uc, UC_M68K_REG_A7, &sp);
    uc_reg_read(cpu->uc, UC_M68K_REG_VBR, &vbr);

    // Check interrupt priority (from QEMU's m68k_cpu_exec_interrupt)
    int current_ipl = (sr >> 8) & 7;
    if (level <= current_ipl) {
        return;  // Masked
    }

    // Save old SR
    uint16_t old_sr = sr;

    // Enter supervisor mode
    sr |= 0x2000;  // Set S bit

    // Update interrupt mask
    sr = (sr & 0xF8FF) | (level << 8);

    // Build exception frame (QEMU's format)
    build_exception_frame(cpu, &sp, 0, old_sr, 0, pc);

    // Update stack pointer
    uc_reg_write(cpu->uc, UC_M68K_REG_A7, &sp);

    // Update SR
    uc_reg_write(cpu->uc, UC_M68K_REG_SR, &sr);

    // Jump to interrupt handler (from vector table)
    uint32_t vector_addr = vbr + vector * 4;
    uint32_t handler_addr;
    uc_mem_read_be32(cpu->uc, vector_addr, &handler_addr);
    uc_reg_write(cpu->uc, UC_M68K_REG_PC, &handler_addr);
}

// Helper for timer interrupts
void deliver_timer_interrupt(UnicornCPU *cpu) {
    // Timer is level 1, autovector 25 (from QEMU's q800.c)
    deliver_m68k_interrupt(cpu, 1, 25);
}
```

### Files to Modify
2. `src/cpu/unicorn_exec_loop.c`

```c
// Update interrupt checking to use QEMU-style delivery
static bool poll_and_check_interrupts(UnicornCPU *cpu) {
    // Check timer
    if (timer_interrupt_pending()) {
        deliver_timer_interrupt(cpu);
        return true;
    }

    // Check other interrupt sources
    if (g_pending_interrupt_level > 0) {
        int level = g_pending_interrupt_level;
        int vector = 24 + level;  // Autovector
        g_pending_interrupt_level = 0;

        deliver_m68k_interrupt(cpu, level, vector);
        return true;
    }

    return false;
}
```

### Test Commands
```bash
# Test interrupt delivery
EMULATOR_TIMEOUT=5 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver 2>&1 | grep "timer"
# Should see timer interrupts being delivered
```

### Success Criteria
- Timer interrupts delivered at ~60Hz
- Proper exception frames on stack
- SR interrupt mask updated correctly

---

## Phase 5: Add TB Break Detection (1 hour)

### Objective
Detect instruction patterns that should force TB termination (like QEMU).

### Files to Modify
1. `src/cpu/unicorn_exec_loop.c`

### Implementation

```c
// Add instruction pattern detection
typedef enum {
    TB_CONTINUE,
    TB_BREAK_BRANCH,
    TB_BREAK_EXCEPTION,
    TB_BREAK_SYSTEM
} TBBreakReason;

static TBBreakReason check_tb_break_condition(UnicornCPU *cpu) {
    uint32_t pc;
    uint16_t opcode;

    uc_reg_read(cpu->uc, UC_M68K_REG_PC, &pc);

    // Read instruction we just executed
    if (pc >= 2) {
        uc_mem_read(cpu->uc, pc - 2, &opcode, 2);
        opcode = bswap16(opcode);

        // Check for backward branch (BRA.S, BEQ.S, etc.)
        if ((opcode & 0xFF00) == 0x6000) {  // BRA
            int8_t offset = opcode & 0xFF;
            if (offset < 0) {
                return TB_BREAK_BRANCH;
            }
        }

        // Check for BEQ.S and other conditional branches
        if ((opcode & 0xF000) == 0x6000) {
            int8_t offset = opcode & 0xFF;
            if (offset < 0 && offset != 0) {
                return TB_BREAK_BRANCH;
            }
        }

        // Check for RTE/RTS (system instructions)
        if (opcode == 0x4E73 || opcode == 0x4E75) {
            return TB_BREAK_SYSTEM;
        }

        // Check for A-line/F-line (exceptions)
        if ((opcode & 0xF000) == 0xA000 ||
            (opcode & 0xF000) == 0xF000) {
            return TB_BREAK_EXCEPTION;
        }
    }

    return TB_CONTINUE;
}

// In main loop, after execution:
TBBreakReason reason = check_tb_break_condition(cpu);
if (reason != TB_CONTINUE) {
    // Force interrupt check on next iteration
    continue;
}
```

### Dynamic Batch Sizing

```c
// Adaptive batch size based on code patterns
static int calculate_batch_size(UnicornCPU *cpu, uint32_t pc) {
    // Small batches for known hot loops
    if (pc >= 0x0200a000 && pc <= 0x0200b000) {
        return 3;  // IRQ polling region
    }

    // Medium batches for normal code
    if (pc >= 0x02000000 && pc <= 0x02100000) {
        return 20;  // ROM code
    }

    // Large batches for application code
    return 50;
}

// Use in main loop:
int batch_size = calculate_batch_size(cpu, pc);
```

### Test Commands
```bash
# Test with backward branch detection
UNICORN_DEBUG_BACKWARD_BRANCH=1 EMULATOR_TIMEOUT=2 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver
# Should see branch detection messages
```

### Success Criteria
- Backward branches cause TB breaks
- No infinite loops in single TBs
- Performance acceptable

---

## Phase 6: Optimization and Cleanup (1 hour)

### Objective
Remove old workarounds and optimize the new implementation.

### Files to Clean Up
1. `src/cpu/unicorn_wrapper.c`
   - Remove all deferred update code
   - Remove UC_HOOK_INTR handler
   - Simplify to just memory and basic hooks

2. `src/cpu/cpu_unicorn.cpp`
   - Remove single-instruction execution
   - Remove complex EmulOp handling in hooks
   - Use new execution loop

### Performance Optimizations

```c
// Cache frequently accessed addresses
typedef struct {
    uint32_t pc;
    bool is_loop;
    int batch_size;
} PCCache;

static PCCache pc_cache[256];  // Simple cache

static int get_cached_batch_size(uint32_t pc) {
    uint8_t hash = (pc >> 2) & 0xFF;
    if (pc_cache[hash].pc == pc) {
        return pc_cache[hash].batch_size;
    }

    // Calculate and cache
    int size = calculate_batch_size_for_pc(pc);
    pc_cache[hash].pc = pc;
    pc_cache[hash].batch_size = size;
    return size;
}
```

### Final Test Suite

```bash
#!/bin/bash
# test_all_phases.sh

echo "=== Phase 1: EmulOp Encoding ==="
./build/macemu-next --test-emulop 2>&1 | grep -q "0x7129" && echo "PASS" || echo "FAIL"

echo "=== Phase 2: IRQ Storm ==="
COUNT=$(EMULATOR_TIMEOUT=10 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver 2>&1 | grep -c "EmulOp 7129")
[ $COUNT -lt 1000 ] && echo "PASS ($COUNT IRQs)" || echo "FAIL ($COUNT IRQs)"

echo "=== Phase 3: Register Updates ==="
./build/macemu-next --test-register-update && echo "PASS" || echo "FAIL"

echo "=== Phase 4: Interrupt Delivery ==="
./build/macemu-next --test-interrupt && echo "PASS" || echo "FAIL"

echo "=== Phase 5: TB Breaks ==="
./build/macemu-next --test-tb-break && echo "PASS" || echo "FAIL"

echo "=== Phase 6: Performance ==="
time EMULATOR_TIMEOUT=10 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver
```

---

## Implementation Schedule

| Phase | Time | Priority | Risk | Dependencies |
|-------|------|----------|------|--------------|
| 1. Fix Encoding | 30 min | CRITICAL | Low | None |
| 2. Execution Loop | 1 hour | HIGH | Medium | Phase 1 |
| 3. Register Updates | 1 hour | HIGH | Low | Phase 2 |
| 4. Interrupt Delivery | 2 hours | MEDIUM | Medium | Phase 2 |
| 5. TB Detection | 1 hour | MEDIUM | Low | Phase 2 |
| 6. Optimization | 1 hour | LOW | Low | All phases |

**Total Time**: ~6.5 hours (1-2 sessions)

---

## Rollback Plan

Each phase can be rolled back independently:

```bash
# Create backup before each phase
git stash
git checkout -b phase-X-implementation

# If phase fails
git checkout main
git branch -D phase-X-implementation
git stash pop
```

---

## Success Metrics

1. **IRQ Storm Fixed**: <1,000 IRQ EmulOps per 10 seconds (vs 781,000)
2. **Boot Progress**: Mac OS boots further than before
3. **Performance**: Unicorn remains >5x faster than UAE
4. **Stability**: No crashes or hangs in 10-minute test
5. **Compatibility**: All existing EmulOps still work

---

## Next Steps After Completion

1. Consider full QEMU integration if more features needed
2. Upstream useful patches to Unicorn project
3. Document the new architecture for future maintainers
4. Profile and optimize hot paths
5. Add comprehensive test suite