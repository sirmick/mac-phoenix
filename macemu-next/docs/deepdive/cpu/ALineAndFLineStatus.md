# A-line/F-line Exception Handling - Status

## Current State (March 2026)

### ✅ WORKING - Via Deferred Register Updates

A-line/F-line trap handling in Unicorn **now works correctly**. The previous limitation (Unicorn ignoring PC changes from interrupt hooks) was overcome by the deferred register update mechanism.

**Evidence**: Both UAE and Unicorn populate 87 identical OS trap table entries and dispatch 16,879 EmulOps in 30 seconds. All A-line traps (0xA000-0xAFFF) are handled successfully.

### How It Works Now

1. A-line (0xAxxx) or F-line (0xFxxx) instruction triggers `UC_ERR_EXCEPTION`
2. Unicorn calls `UC_HOOK_INTR` callback (`hook_interrupt()`)
3. Our code identifies the opcode and calls the appropriate EmulOp handler
4. Register updates (including PC) are **deferred** -- queued for later application
5. `hook_interrupt()` returns without calling `uc_emu_stop()`
6. At the next basic block boundary, `hook_block()` calls `apply_deferred_updates_and_flush()`
7. Deferred register writes are applied via `uc_reg_write()`, including the new PC
8. Execution continues from the correct address

### The Deferred Update Mechanism

```c
// In hook_interrupt() - register writes are DEFERRED:
deferred_dregs[reg] = value;
deferred_dregs_valid |= (1 << reg);

// In hook_block() - deferred writes are APPLIED:
void apply_deferred_updates_and_flush(UnicornCPU *cpu, uc_engine *uc, const char *caller) {
    for (int i = 0; i < 8; i++) {
        if (deferred_dregs_valid & (1 << i))
            uc_reg_write(uc, UC_M68K_REG_D0 + i, &deferred_dregs[i]);
        if (deferred_aregs_valid & (1 << i))
            uc_reg_write(uc, UC_M68K_REG_A0 + i, &deferred_aregs[i]);
    }
    if (deferred_pc_valid)
        uc_reg_write(uc, UC_M68K_REG_PC, &deferred_pc);
    if (deferred_sr_valid) {
        uint32_t sr32 = deferred_sr;  // Must be uint32_t, not uint16_t!
        uc_reg_write(uc, UC_M68K_REG_SR, &sr32);
    }
    // Clear all valid flags
}
```

### Why This Works (And Previous Attempts Failed)

**Previous approach (January 2026)**: Tried to write registers directly inside `UC_HOOK_INTR`:
- ❌ Unicorn's QEMU backend overwrites PC with `exception_next_eip` after hook returns
- ❌ `uc_emu_stop()` caused JIT restart overhead
- ❌ All direct PC modification attempts failed

**Current approach (February-March 2026)**: Defer all register writes:
- ✅ Don't fight Unicorn's hook behavior -- let it overwrite PC
- ✅ Queue register updates for application at the next `hook_block()` call
- ✅ `hook_block()` fires at every basic block boundary (before any JIT code runs)
- ✅ By the time deferred updates are applied, Unicorn has finished its post-hook PC restoration
- ✅ The deferred PC write at block boundary takes effect correctly

### Key Insight: SR Requires uint32_t

A subtle but critical detail: `uc_reg_write()` for SR requires a `uint32_t*`, not `uint16_t*`. QEMU internally represents SR as a 32-bit value. Passing a 16-bit pointer causes the upper bits to be garbage, corrupting the status register.

```c
// WRONG:
uint16_t sr = 0x2700;
uc_reg_write(uc, UC_M68K_REG_SR, &sr);  // Reads 4 bytes from &sr!

// CORRECT:
uint32_t sr32 = 0x2700;
uc_reg_write(uc, UC_M68K_REG_SR, &sr32);
```

## Historical Context

### The Original Problem (January 2026)

Unicorn cannot change PC from interrupt hooks -- this was a known architectural issue (Unicorn GitHub issue #1027). When we tried to set PC inside `UC_HOOK_INTR`, Unicorn's internal `exception_next_eip` mechanism would overwrite our PC value.

This affected:
- All non-EmulOp A-line traps (e.g., `0xA05D`, `0xA247`)
- ROM boot sequence after PATCH_BOOT_GLOBS
- Any Mac OS trap execution

### What We Tried (That Failed)

From commits `9464afa4` and `32a6926b`:
1. ✗ `uc_ctl_remove_cache()` + `uc_reg_write()` -- Unicorn still overwrites PC
2. ✗ `uc_emu_stop()` to break execution -- causes JIT restart overhead
3. ✗ Skipping instruction -- prevents infinite loop but doesn't execute handler

### The Breakthrough

The deferred register update mechanism was developed as part of the IRQ storm fix (January-February 2026). It was originally needed because EmulOp handlers run inside `UC_HOOK_INTR` callbacks where register writes don't persist. The same mechanism naturally solved the A-line/F-line trap problem.

## Current Boot Results

### Unicorn Backend (30 seconds)
- ✅ 87 OS trap table entries populated (A001-A0FF range)
- ✅ 16,879 EmulOps dispatched
- ✅ All A-line traps handled correctly
- ✅ Boot progress $0b78 = 0xfd89ffff
- ✅ Identical state to UAE at every checkpoint

### UAE Backend (30 seconds)
- ✅ 87 OS trap table entries populated (identical)
- ✅ Same EmulOp count
- ✅ Same boot progress

Both backends stall at the same resource chain search (PC=0x0001c3d4) because there is no SCSI boot disk providing system resources.

## Code Structure

```
src/cpu/
├── unicorn_wrapper.c       # Hook infrastructure, deferred updates, apply_deferred_updates_and_flush()
├── cpu_unicorn.cpp         # Backend interface, MMIO, memory mapping
├── unicorn_exec_loop.c     # QEMU-style execution loop
└── unicorn_validation.cpp  # DualCPU validation logic
```

## Testing

```bash
# Run Unicorn for 30s and check trap table count
EMULATOR_TIMEOUT=30 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver /home/mick/quadra.rom 2>&1 | grep "TRAP TABLE"

# Compare with UAE
EMULATOR_TIMEOUT=30 CPU_BACKEND=uae ./build/macemu-next --no-webserver /home/mick/quadra.rom 2>&1 | grep "TRAP TABLE"

# Both should show 87 RAM entries
```

---

*Last updated: March 1, 2026*
