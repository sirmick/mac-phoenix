# MMIO Integration Instructions

## Overview
This document provides step-by-step instructions for integrating MMIO EmulOp transport into unicorn_wrapper.c.

## Files Created
1. `src/common/include/mmio_transport.h` - Header with MMIO definitions
2. `src/cpu/mmio_emulop_transport.c` - Reference implementation (copy code from here)
3. `roms/build_mmio_transport_test.py` - Test ROM builder

## Integration Steps

### Step 1: Add Include to unicorn_wrapper.c

At the top of `src/cpu/unicorn_wrapper.c`, after the other includes (~line 35):
```c
#include "mmio_transport.h"
#include "emul_op.h"  // For EmulOp() function
#include "main.h"     // For M68kRegisters struct
```

### Step 2: Add MMIO Hook to UnicornCPU Structure

In the `UnicornCPU` struct (~line 77), add:
```c
struct UnicornCPU {
    // ... existing fields ...

    /* MMIO EmulOp transport hook */
    uc_hook mmio_emulop_hook;

    // ... rest of structure ...
};
```

### Step 3: Add MMIO Handler Function

Add this function before `unicorn_create()` (~line 560):
```c
/* MMIO handler for EmulOp transport */
static void mmio_emulop_handler(uc_engine *uc, uc_mem_type type,
                                uint64_t address, int size, int64_t value,
                                void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;

    /* Only handle writes */
    if (type != UC_MEM_WRITE) {
        return;
    }

    /* Check if this is in the MMIO EmulOp region */
    if (!IS_MMIO_EMULOP(address)) {
        return;  /* Not our region */
    }

    /* Convert MMIO address to EmulOp opcode */
    uint16_t opcode = MMIO_TO_EMULOP(address);

    /* Debug logging */
    static int mmio_count = 0;
    if (mmio_count++ < 20) {  /* Log first 20 for debugging */
        fprintf(stderr, "[MMIO EmulOp] Triggered 0x%04x via write to 0x%08lx\n",
                opcode, address);
    }

    /* Build M68kRegisters from current Unicorn state */
    struct M68kRegisters regs;
    memset(&regs, 0, sizeof(regs));

    /* Read registers */
    for (int i = 0; i < 8; i++) {
        uc_reg_read(uc, UC_M68K_REG_D0 + i, &regs.d[i]);
        uc_reg_read(uc, UC_M68K_REG_A0 + i, &regs.a[i]);
    }
    uc_reg_read(uc, UC_M68K_REG_SR, &regs.sr);

    /* Call the existing EmulOp handler - reuses all existing code! */
    EmulOp(opcode, &regs);

    /* Write back register changes */
    for (int i = 0; i < 8; i++) {
        uc_reg_write(uc, UC_M68K_REG_D0 + i, &regs.d[i]);
        uc_reg_write(uc, UC_M68K_REG_A0 + i, &regs.a[i]);
    }
    uc_reg_write(uc, UC_M68K_REG_SR, &regs.sr);
}
```

### Step 4: Setup MMIO in unicorn_create()

In `unicorn_create()` or `unicorn_create_with_model()`, after `uc_open()` succeeds (~line 600):
```c
    /* Map MMIO EmulOp region */
    fprintf(stderr, "[UNICORN] Setting up MMIO EmulOp transport at 0x%08X\n",
            MMIO_EMULOP_BASE);

    err = uc_mem_map(cpu->uc, MMIO_EMULOP_BASE, MMIO_EMULOP_SIZE,
                    UC_PROT_READ | UC_PROT_WRITE);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "Failed to map MMIO EmulOp region: %s\n",
                uc_strerror(err));
        uc_close(cpu->uc);
        free(cpu);
        return NULL;
    }

    /* Hook memory writes to MMIO region */
    err = uc_hook_add(cpu->uc, &cpu->mmio_emulop_hook,
                     UC_HOOK_MEM_WRITE,
                     mmio_emulop_handler,
                     cpu,
                     MMIO_EMULOP_BASE,
                     MMIO_EMULOP_BASE + MMIO_EMULOP_SIZE - 1);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "Failed to hook MMIO EmulOp region: %s\n",
                uc_strerror(err));
        uc_close(cpu->uc);
        free(cpu);
        return NULL;
    }

    fprintf(stderr, "[UNICORN] MMIO EmulOp transport ready\n");
```

### Step 5: Cleanup in unicorn_destroy()

In `unicorn_destroy()` (~line 690):
```c
void unicorn_destroy(UnicornCPU *cpu) {
    if (!cpu) return;

    /* Remove MMIO hook */
    if (cpu->mmio_emulop_hook) {
        uc_hook_del(cpu->uc, cpu->mmio_emulop_hook);
    }

    // ... rest of cleanup ...
}
```

### Step 6: Remove Old EmulOp Detection (CRITICAL)

#### In hook_block() (~line 216):
**DELETE** this entire block:
```c
/* CRITICAL: Check for EmulOps at block start */
if (cpu->arch == UCPU_ARCH_M68K) {
    uint16_t opcode = 0;
    if (uc_mem_read(uc, pc, &opcode, sizeof(opcode)) == UC_ERR_OK) {
        #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        opcode = __builtin_bswap16(opcode);
        #endif

        /* Check if it's an EmulOp (0x7100-0x713F) */
        if (opcode >= 0x7100 && opcode < 0x7140) {
            /* Stop execution to handle EmulOp */
            uc_emu_stop(uc);
            cpu->trap_ctx.saved_pc = pc;
            cpu->trap_ctx.in_emulop = true;
            return;
        }
    }
}
```

#### In unicorn_execute_n() (~line 870):
**DELETE** all code checking for:
```c
if ((opcode & 0xFF00) == 0x7100) {
    // DELETE all this EmulOp detection code
}
```

#### In hook_insn_invalid():
**KEEP** A-line and F-line trap handling, but **REMOVE** any 0x71xx EmulOp handling.

## Testing

1. Build the test ROM:
```bash
cd macemu-next/roms
python3 build_mmio_transport_test.py
```

2. Add test ROM magic to rom_patches.cpp (if not done):
```c
test_magic == 0x4D4D5452  // "MMTR" - MMIO Transport test
```

3. Run test with Unicorn:
```bash
env EMULATOR_TIMEOUT=2 CPU_BACKEND=unicorn ./build/macemu-next --rom roms/mmio_transport_test.rom --no-webserver
```

Expected output:
```
[MMIO EmulOp] Triggered 0x7102 via write to 0xFF000004
[MMIO EmulOp] Triggered 0x7101 via write to 0xFF000002
*** Breakpoint
[TEST SUCCESS] D0=0xDEADBEEF - Success marker found!
[TEST] D7=0x00000005 (test counter)
```

## ROM Patch Updates

For ROM patches that need to work with both UAE and Unicorn:

```c
// In rom_patches.cpp
static void emit_emulop(uint16_t **wp, uint16_t opcode) {
    extern const char *CPUBackend;
    if (strcmp(CPUBackend, "unicorn") == 0) {
        EMIT_MMIO_EMULOP(*wp, opcode);
    } else {
        **wp = htons(opcode);
        (*wp)++;
    }
}

// Usage:
emit_emulop(&wp, M68K_EMUL_OP_SHUTDOWN);
```

## Benefits

1. **100% Reliable** - MMIO always traps, no TB boundary issues
2. **Reuses Existing Code** - All EmulOp handlers remain unchanged
3. **Clean Separation** - Transport vs implementation
4. **Better Performance** - No instruction checking overhead
5. **JIT-Friendly** - Works naturally with translation blocks

## Troubleshooting

If MMIO isn't working:
1. Check that MMIO region is mapped (0xFF000000)
2. Verify hook is installed with UC_HOOK_MEM_WRITE
3. Ensure old EmulOp detection is removed
4. Check debug output for "[MMIO EmulOp]" messages
5. Verify EmulOp() is being called with correct opcode