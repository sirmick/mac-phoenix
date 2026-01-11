# MMIO Implementation Plan for Unicorn (v2)
## Goal: MMIO Transport → Existing EmulOp Handlers

## Core Concept
**MMIO is just the transport mechanism. The actual EmulOp implementation remains unchanged.**

```
68k Code → MMIO Write → MMIO Handler → EmulOp(opcode, regs) → Existing Switch
```

## 1. Simple MMIO Memory Layout

```
Base Address: 0xFF000000
Size: 4KB (0x1000)

0xFF000000-0xFF0001FF : EmulOp Trigger Region (512 bytes = 256 opcodes)
0xFF000200-0xFF000FFF : Reserved for future use
```

### EmulOp Mapping
Each EmulOp opcode (0x7100-0x71FF) maps to a unique MMIO address:
```
MMIO Address = 0xFF000000 + ((opcode - 0x7100) * 2)

Examples:
0x7100 (EXEC_RETURN) → 0xFF000000
0x7101 (SHUTDOWN)    → 0xFF000002
0x7102 (BREAK)       → 0xFF000004
0x7103 (RESET)       → 0xFF000006
...
0x713F               → 0xFF00007E
```

## 2. Minimal Code Changes

### A. Create Simple MMIO Header
**File: `src/common/include/mmio_transport.h`** (NEW)
```c
#ifndef MMIO_TRANSPORT_H
#define MMIO_TRANSPORT_H

#include <stdint.h>

// MMIO Base for EmulOp transport
#define MMIO_EMULOP_BASE  0xFF000000UL
#define MMIO_EMULOP_SIZE  0x00001000UL  // 4KB

// Convert EmulOp opcode to MMIO address
#define EMULOP_TO_MMIO(opcode) (MMIO_EMULOP_BASE + (((opcode) - 0x7100) * 2))

// Convert MMIO address to EmulOp opcode
#define MMIO_TO_EMULOP(addr)   (0x7100 + (((addr) - MMIO_EMULOP_BASE) / 2))

// Helper macro for ROM patches to emit MMIO write
#define EMIT_MMIO_EMULOP(wp, opcode) \
    do { \
        uint32_t mmio_addr = EMULOP_TO_MMIO(opcode); \
        *wp++ = htons(0x23FC);  /* MOVE.L #1, abs.L */ \
        *wp++ = htons(0x0000);  /* immediate high */ \
        *wp++ = htons(0x0001);  /* immediate low (any value works) */ \
        *wp++ = htons(mmio_addr >> 16);  /* address high */ \
        *wp++ = htons(mmio_addr & 0xFFFF);  /* address low */ \
    } while(0)

#endif
```

### B. Modify `unicorn_wrapper.c` - Add MMIO Handler
**Changes needed:**

1. **Add MMIO handler that calls existing EmulOp function**
```c
#include "mmio_transport.h"
#include "emul_op.h"
#include "main.h"  // For M68kRegisters

// MMIO handler - just a transport to EmulOp
static void mmio_emulop_write(uc_engine *uc, uint64_t addr, unsigned size,
                              uint64_t value, void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;

    // Check if this is in the EmulOp region
    if (addr < MMIO_EMULOP_BASE || addr >= MMIO_EMULOP_BASE + MMIO_EMULOP_SIZE) {
        fprintf(stderr, "[MMIO] Write outside EmulOp region: 0x%08lx\n", addr);
        return;
    }

    // Convert MMIO address to EmulOp opcode
    uint16_t opcode = MMIO_TO_EMULOP(addr);

    // Debug logging
    fprintf(stderr, "[MMIO] EmulOp 0x%04x triggered via MMIO at 0x%08lx\n", opcode, addr);

    // Build M68kRegisters from current Unicorn state
    struct M68kRegisters regs;
    for (int i = 0; i < 8; i++) {
        uc_reg_read(uc, UC_M68K_REG_D0 + i, &regs.d[i]);
        uc_reg_read(uc, UC_M68K_REG_A0 + i, &regs.a[i]);
    }
    uc_reg_read(uc, UC_M68K_REG_SR, &regs.sr);

    // Call the existing EmulOp handler with the giant switch statement
    EmulOp(opcode, &regs);

    // Write back any register changes
    for (int i = 0; i < 8; i++) {
        uc_reg_write(uc, UC_M68K_REG_D0 + i, &regs.d[i]);
        uc_reg_write(uc, UC_M68K_REG_A0 + i, &regs.a[i]);
    }
    uc_reg_write(uc, UC_M68K_REG_SR, &regs.sr);
}

// MMIO read handler (for future expansion, returns 0 for now)
static uint64_t mmio_emulop_read(uc_engine *uc, uint64_t addr,
                                 unsigned size, void *user_data) {
    // Could be used to return status/results in the future
    return 0;
}
```

2. **Register MMIO region in unicorn_create()**
```c
// In unicorn_create(), after uc_open()
fprintf(stderr, "[UNICORN] Mapping MMIO EmulOp transport at 0x%08X-0x%08X\n",
        MMIO_EMULOP_BASE, MMIO_EMULOP_BASE + MMIO_EMULOP_SIZE - 1);

err = uc_mmio_map(cpu->uc, MMIO_EMULOP_BASE, MMIO_EMULOP_SIZE,
                  mmio_emulop_read, mmio_emulop_write, cpu);
if (err != UC_ERR_OK) {
    fprintf(stderr, "Failed to map MMIO EmulOp region: %s\n", uc_strerror(err));
    uc_close(cpu->uc);
    free(cpu);
    return NULL;
}
```

3. **Remove EmulOp detection from hook_block()**
```c
// DELETE this entire block:
if (opcode >= 0x7100 && opcode < 0x7140) {
    uc_emu_stop(uc);
    cpu->trap_ctx.in_emulop = true;
    return;
}
```

4. **Remove EmulOp checking from unicorn_execute_n()**
```c
// DELETE all code checking for (opcode & 0xFF00) == 0x7100
```

### C. Update ROM Patches (Gradual Migration)
**File: `src/core/rom_patches.cpp`**

For Unicorn backend, use MMIO transport:
```c
// Add at top of file
#include "mmio_transport.h"

// Add helper function
static inline bool using_unicorn_backend() {
    // Check if we're using Unicorn (you'll need to determine this)
    return strcmp(CPUBackend, "unicorn") == 0;
}

// Modify patch insertion
if (using_unicorn_backend()) {
    EMIT_MMIO_EMULOP(wp, M68K_EMUL_OP_PATCH_BOOT_GLOBS);
} else {
    *wp++ = htons(M68K_EMUL_OP_PATCH_BOOT_GLOBS);  // UAE still uses direct
}
```

Or better, create a wrapper:
```c
// Helper to emit EmulOp in backend-appropriate way
static void emit_emulop(uint16_t **wp, uint16_t opcode) {
    if (using_unicorn_backend()) {
        EMIT_MMIO_EMULOP(*wp, opcode);
    } else {
        **wp = htons(opcode);
        (*wp)++;
    }
}

// Usage:
emit_emulop(&wp, M68K_EMUL_OP_PATCH_BOOT_GLOBS);
```

## 3. Testing Approach

### Create Test ROM
```python
# build_mmio_emulop_test.py
import struct

def emit_mmio_emulop(code, opcode):
    """Emit MMIO write to trigger EmulOp"""
    mmio_addr = 0xFF000000 + ((opcode - 0x7100) * 2)
    # move.l #1, mmio_addr
    code.extend([0x23FC])  # MOVE.L #imm, abs.L
    code.extend([0x0000, 0x0001])  # immediate = 1
    code.extend([mmio_addr >> 16, mmio_addr & 0xFFFF])  # address

# Test different EmulOps
code = []

# Test 1: BREAK (0x7101)
emit_mmio_emulop(code, 0x7101)

# Test 2: RESET (0x7103)
emit_mmio_emulop(code, 0x7103)

# Test 3: READ_XPRAM (0x7105)
emit_mmio_emulop(code, 0x7105)

# Test 4: SHUTDOWN (0x7102)
emit_mmio_emulop(code, 0x7102)
```

## 4. Benefits of This Approach

1. **Minimal Code Changes** - Reuses ALL existing EmulOp code
2. **No Duplication** - The giant switch statement stays intact
3. **Clean Transport** - MMIO just triggers existing handlers
4. **Backward Compatible** - UAE backend unchanged
5. **Easy Testing** - Can test MMIO transport separately
6. **100% Reliable** - MMIO always traps in JIT

## 5. What We're NOT Changing

- ❌ EmulOp handler implementation (the big switch)
- ❌ EmulOp opcode definitions
- ❌ EmulOp business logic
- ❌ UAE backend behavior

## 6. What We ARE Changing

- ✅ How Unicorn detects EmulOps (MMIO instead of instruction checking)
- ✅ ROM patch emission for Unicorn (MMIO writes instead of opcodes)
- ✅ Remove unreliable UC_HOOK_BLOCK checking
- ✅ Remove EmulOp detection from execution loop

## 7. Implementation Order

1. **Phase 1: Add MMIO Transport**
   - Create mmio_transport.h
   - Add MMIO handlers to unicorn_wrapper.c
   - Map MMIO region on init

2. **Phase 2: Test Side-by-Side**
   - Keep EmulOp detection for now
   - Test that MMIO transport works
   - Verify EmulOp handlers execute correctly

3. **Phase 3: Remove Legacy Detection**
   - Remove hook_block EmulOp checking
   - Remove unicorn_execute_n EmulOp checking
   - Clean up trap context code

4. **Phase 4: Update ROM Patches**
   - Add backend detection
   - Use MMIO for Unicorn, direct for UAE
   - Test all ROM patches

This approach gives us the reliability of MMIO while keeping all the existing, well-tested EmulOp implementation!