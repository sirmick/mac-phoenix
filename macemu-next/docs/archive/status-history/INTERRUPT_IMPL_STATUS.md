# Interrupt Implementation Status

## What We've Done So Far

### 1. Moved Interrupt Infrastructure to Shared Code ✅
- Added `volatile bool PendingInterrupt` to `uae_wrapper.cpp`
- Moved `TriggerInterrupt()` from `basilisk_glue.cpp` to `uae_wrapper.cpp`
- Added declarations to `uae_wrapper.h`
- Made `InterruptFlags` volatile in `main.h`

### 2. Updated UAE Backend ✅
- UAE's `newcpu.cpp` now checks `PendingInterrupt` and sets `SPCFLAG_INT`
- Removed duplicate `TriggerInterrupt()` from `basilisk_glue.cpp`

### 3. Added Interrupt Support to Unicorn (NEEDS OPTIMIZATION)
- Currently checking interrupts in `UC_HOOK_CODE` (line 147-199 of `unicorn_wrapper.c`)
- **Problem**: This runs before EVERY instruction - very slow (10x overhead)
- **Solution**: Need to move to `UC_HOOK_BLOCK` (runs at basic block boundaries)

## What Needs To Be Done

### Move Interrupts from UC_HOOK_CODE to UC_HOOK_BLOCK

**Why**: UC_HOOK_BLOCK is much faster - only runs at basic block boundaries, not every instruction

**Changes needed in `unicorn_wrapper.c`:**

1. **Add block_hook handle** (line ~62):
```c
uc_hook code_hook;  // UC_HOOK_CODE for EmulOps/traps (allows register modification)
uc_hook block_hook; // UC_HOOK_BLOCK for interrupts (efficient)
```

2. **Create hook_block function** (insert before hook_code, around line 135):
```c
/**
 * Hook for basic block execution (UC_HOOK_BLOCK)
 * Called at the start of each basic block - much more efficient than per-instruction
 * Used for interrupt checking
 */
static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    (void)size;
    (void)user_data;

    uint32_t pc = (uint32_t)address;

    /* Check for pending interrupts */
    extern volatile bool PendingInterrupt;
    extern int intlev(void);

    if (PendingInterrupt) {
        PendingInterrupt = false;

        int intr_level = intlev();
        if (intr_level > 0) {
            uint32_t sr;
            uc_reg_read(uc, UC_M68K_REG_SR, &sr);
            int current_mask = (sr >> 8) & 7;

            if (intr_level > current_mask) {
                /* M68K interrupt exception handling */
                uint32_t sp;
                uc_reg_read(uc, UC_M68K_REG_A7, &sp);

                /* Push PC (long, big-endian) */
                sp -= 4;
                uint32_t pc_be = __builtin_bswap32(pc);
                uc_mem_write(uc, sp, &pc_be, 4);

                /* Push SR (word, big-endian) */
                sp -= 2;
                uint16_t sr_be = __builtin_bswap16((uint16_t)sr);
                uc_mem_write(uc, sp, &sr_be, 2);

                /* Update SR: set supervisor mode, set interrupt mask */
                sr |= (1 << 13);  /* S bit */
                sr = (sr & ~0x0700) | ((intr_level & 7) << 8);  /* I2-I0 */
                uc_reg_write(uc, UC_M68K_REG_SR, &sr);
                uc_reg_write(uc, UC_M68K_REG_A7, &sp);

                /* Read vector and jump */
                uint32_t vbr = 0;  /* TODO: Read VBR for 68020+ */
                uint32_t vector_addr = vbr + (24 + intr_level) * 4;
                uint32_t handler_addr_be;
                uc_mem_read(uc, vector_addr, &handler_addr_be, 4);
                uint32_t handler_addr = __builtin_bswap32(handler_addr_be);
                uc_reg_write(uc, UC_M68K_REG_PC, &handler_addr);

                /* Stop to apply changes */
                uc_emu_stop(uc);
            }
        }
    }
}
```

3. **Remove interrupt code from hook_code** (lines 147-199):
Delete the entire `PendingInterrupt` check block, keep only EmulOp/trap handling

4. **Register UC_HOOK_BLOCK** (around line 840):
```c
/* Register block hook for interrupts */
uc_hook_add(cpu->uc, &cpu->block_hook, UC_HOOK_BLOCK,
            (void*)hook_block, cpu, 1, 0);

/* Register code hook for EmulOps/traps */
uc_hook_add(cpu->uc, &cpu->code_hook, UC_HOOK_CODE,
            (void*)hook_code, cpu, 1, 0);
```

## Testing Plan

1. **Build**: `ninja -C build`
2. **Quick test**: `EMULATOR_TIMEOUT=5 CPU_BACKEND=unicorn ./build/macemu-next ~/quadra.rom`
3. **Trace test**: `./run_traces.sh` - compare UAE vs Unicorn at 250k instructions
4. **Expected**: Unicorn should now process interrupts and run much longer without crashing

## Performance Expectations

- **Before**: UC_HOOK_CODE every instruction = ~10x overhead
- **After**: UC_HOOK_BLOCK at basic blocks = minimal overhead
- **Result**: Unicorn should run at near-native JIT speed

## Files Modified

- ✅ `macemu-next/src/cpu/uae_wrapper.cpp` - Added PendingInterrupt, TriggerInterrupt()
- ✅ `macemu-next/src/cpu/uae_wrapper.h` - Added interrupt declarations
- ✅ `macemu-next/src/common/include/main.h` - Made InterruptFlags volatile
- ✅ `macemu-next/src/cpu/uae_cpu/newcpu.cpp` - Check PendingInterrupt
- ✅ `macemu-next/src/cpu/uae_cpu/basilisk_glue.cpp` - Removed duplicate TriggerInterrupt
- ⏳ `macemu-next/src/cpu/unicorn_wrapper.c` - NEEDS: Move to UC_HOOK_BLOCK

## Key Insight from Research

From Unicorn documentation:
- UC_HOOK_CODE has severe performance overhead (10x slower)
- UC_HOOK_BLOCK is recommended for periodic checks
- EmulOps must stay in UC_HOOK_CODE (illegal instructions need immediate handling)
- Interrupts can safely check at block boundaries (asynchronous events)

This hybrid approach gives us:
- **Fast**: Block-level interrupt checking
- **Correct**: Instruction-level EmulOp handling
