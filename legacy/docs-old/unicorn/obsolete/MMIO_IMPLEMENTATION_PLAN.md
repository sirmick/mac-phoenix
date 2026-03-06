# MMIO Trap Implementation Plan

## Summary

We've successfully implemented:
1. ✅ Ringbuffer trace (last N instructions)
2. ✅ Immediate abort on divergence with detailed report
3. ✅ Separate RAM spaces for UAE and Unicorn
4. ✅ Documented MMIO trap approach

## Current Status

The validation system now works perfectly and detected a real Unicorn M68K bug:
- Instruction #514116: MOVE.B (0x2815)
- SR divergence: UAE=0x2010 vs Unicorn=0x2018
- D4 divergence: UAE=0x00FF00FF vs Unicorn=0xFF00FF00

## Next: MMIO Trap Implementation

### Files to Modify

1. **unicorn_wrapper.c**:
   - Add TRAP_REGION defines (0xFF000000, 4KB)
   - Add TrapContext to UnicornCPU struct
   - Add trap_hook handle
   - Modify unicorn_execute_one() to redirect PC on INSN_INVALID
   - Add trap_mem_fetch_handler() for UC_HOOK_MEM_FETCH_UNMAPPED
   - Register hook during unicorn_create()

2. **unicorn_wrapper.h**:
   - No changes needed (internal implementation)

### Implementation Steps

#### Step 1: Add Trap Region Constants

```c
/* MMIO Trap Region for JIT-compatible EmulOp handling */
#define TRAP_REGION_BASE  0xFF000000UL
#define TRAP_REGION_SIZE  0x00001000UL  /* 4KB = 2048 EmulOp slots */
```

#### Step 2: Extend UnicornCPU Struct

```c
/* Trap context for MMIO approach */
typedef struct {
    uint32_t saved_pc;     /* Original PC where 0x71xx was */
    bool in_emulop;        /* Currently handling EmulOp? */
} TrapContext;

struct UnicornCPU {
    // ... existing fields ...
    uc_hook trap_hook;     /* MMIO trap region hook */
    TrapContext trap_ctx;  /* Trap state */
};
```

#### Step 3: Add Trap Hook Handler

```c
static void trap_mem_fetch_handler(uc_engine *uc, uc_mem_type type,
                                   uint64_t address, int size,
                                   int64_t value, void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;

    // Verify address is in trap region
    if (address < TRAP_REGION_BASE ||
        address >= TRAP_REGION_BASE + TRAP_REGION_SIZE) {
        fprintf(stderr, "ERROR: Unexpected unmapped fetch at 0x%08lx\n", address);
        return;
    }

    if (!cpu->trap_ctx.in_emulop) {
        fprintf(stderr, "WARNING: Trap region access without INSN_INVALID\n");
        return;
    }

    // Calculate EmulOp number from address
    uint32_t emulop_num = (address - TRAP_REGION_BASE) / 2;
    uint16_t opcode = 0x7100 + emulop_num;

    // Call platform EmulOp handler (g_platform.emulop_handler)
    // This is the SAME handler UAE uses - 100% code reuse!
    if (g_platform.emulop_handler) {
        bool pc_advanced = g_platform.emulop_handler(opcode, false);

        // Restore PC to instruction AFTER the 0x71xx
        uint32_t next_pc = cpu->trap_ctx.saved_pc + 2;
        uc_reg_write(uc, UC_M68K_REG_PC, &next_pc);

        cpu->trap_ctx.in_emulop = false;
    }
}
```

#### Step 4: Modify unicorn_execute_one()

```c
bool unicorn_execute_one(UnicornCPU *cpu) {
    if (!cpu || !cpu->uc) return false;

    uint64_t pc;
    uc_reg_read(cpu->uc, UC_M68K_REG_PC, &pc);

    uc_err err = uc_emu_start(cpu->uc, pc, 0xFFFFFFFFFFFFFFFFULL, 0, 1);

    if (err == UC_ERR_INSN_INVALID && cpu->arch == UCPU_ARCH_M68K) {
        // Read opcode
        uint16_t opcode;
        if (uc_mem_read(cpu->uc, (uint32_t)pc, &opcode, sizeof(opcode)) == UC_ERR_OK) {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            opcode = __builtin_bswap16(opcode);
            #endif

            // Check if it's an EmulOp (0x71xx)
            if ((opcode & 0xFF00) == 0x7100) {
                // Save original PC
                cpu->trap_ctx.saved_pc = (uint32_t)pc;
                cpu->trap_ctx.in_emulop = true;

                // Calculate trap address
                uint32_t emulop_num = opcode & 0xFF;
                uint32_t trap_addr = TRAP_REGION_BASE + (emulop_num * 2);

                // Redirect PC to trap region
                uc_reg_write(cpu->uc, UC_M68K_REG_PC, &trap_addr);

                // Resume - will trigger UC_HOOK_MEM_FETCH_UNMAPPED
                err = uc_emu_start(cpu->uc, trap_addr, 0xFFFFFFFFFFFFFFFFULL, 0, 1);

                // Check if trap handler executed successfully
                return (err == UC_ERR_OK || cpu->trap_ctx.in_emulop == false);
            }

            // A-line/F-line traps can use same approach...
        }
    }

    if (err != UC_ERR_OK) {
        set_error(cpu, err);
        return false;
    }
    return true;
}
```

#### Step 5: Register Hook in unicorn_create()

```c
UnicornCPU* unicorn_create_with_model(UnicornArch arch, int cpu_model) {
    // ... existing initialization ...

    // Register MMIO trap hook for EmulOps
    // IMPORTANT: Don't map the trap region! Leave it unmapped!
    err = uc_hook_add(cpu->uc, &cpu->trap_hook,
                     UC_HOOK_MEM_FETCH_UNMAPPED,
                     trap_mem_fetch_handler,
                     cpu,  // user_data
                     TRAP_REGION_BASE,
                     TRAP_REGION_BASE + TRAP_REGION_SIZE - 1);

    if (err != UC_ERR_OK) {
        fprintf(stderr, "Failed to add MMIO trap hook: %s\n", uc_strerror(err));
        uc_close(cpu->uc);
        free(cpu);
        return NULL;
    }

    // Initialize trap context
    cpu->trap_ctx.saved_pc = 0;
    cpu->trap_ctx.in_emulop = false;

    return cpu;
}
```

### Testing Plan

1. Test Unicorn-only mode with EMULATOR_TIMEOUT=5
2. Verify EmulOps execute correctly
3. Check that ringbuffer trace still works
4. Verify dual-CPU validation still detects divergences
5. Test with JIT enabled (when Unicorn JIT is available)

### Benefits of This Approach

1. ✅ **JIT Compatible**: Memory hooks fire even in JIT mode
2. ✅ **Register Persistence**: Modifications happen outside uc_emu_start()
3. ✅ **100% Code Reuse**: Same EmulOp handler as UAE
4. ✅ **Same ROM**: Both CPUs use identical ROM patches (0x71xx)
5. ✅ **Clean**: No messy hook-based register modifications

### Known Limitations

1. **Two uc_emu_start() calls per EmulOp**: First fails with INSN_INVALID, second triggers trap
   - Minor performance overhead
   - Could optimize with custom Unicorn build

2. **No JIT for trap region jumps**: PC redirect happens in interpreter
   - But code AFTER EmulOp still gets JIT-compiled
   - Net effect: minimal impact

### Future Extensions

1. Use same approach for A-line/F-line traps
2. Add trap statistics/profiling
3. Consider conditional mapping for "fast path" EmulOps

## Current Bug: Unicorn MOVE.B Issue

The validation found a real Unicorn bug that needs to be reported upstream:

```
Instruction #514116: PC=0x0202E8E8, Opcode=0x2815 (MOVE.B (A5),D4)

UAE result:
  D4: 0x0000FFFF → 0x00FF00FF
  SR: 0x2018 → 0x2010 (Z flag cleared)

Unicorn result:
  D4: 0x0000FFFF → 0xFF00FF00  ❌ Wrong byte order!
  SR: 0x2018 → 0x2018  ❌ Z flag not updated!
```

This should be reported to: https://github.com/unicorn-engine/unicorn/issues
