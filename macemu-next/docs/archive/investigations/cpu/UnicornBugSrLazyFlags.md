# Unicorn Bug: M68K SR Register Read Doesn't Flush Lazy Flags

## Summary

**File Modified**: `external/unicorn/qemu/target/m68k/unicorn.c`
**Function**: `reg_read_m68k()` case `UC_M68K_REG_SR`
**Status**: Fixed locally, should be submitted upstream

## The Bug

When reading the M68K Status Register (SR) via `uc_reg_read(UC_M68K_REG_SR)`, Unicorn returns **incorrect condition code flags** for instructions with lazy flag evaluation (ADD, SUB, CMP, etc.).

### Root Cause

Unicorn's M68K emulation uses **lazy flag evaluation** as an optimization:

1. Arithmetic instructions (SUB, ADD, etc.) set `env->cc_op` to indicate what operation was performed (e.g., `CC_OP_SUBB` for byte subtraction)
2. Intermediate values are stored in `env->cc_n`, `env->cc_v`, etc.
3. Actual condition code flags (N, Z, V, C) are computed **on-demand** when needed, using the `COMPUTE_CCR` macro

The bug is in [unicorn.c:75](../external/unicorn/qemu/target/m68k/unicorn.c#L75):

**Buggy code**:
```c
case UC_M68K_REG_SR:
    CHECK_REG_TYPE(uint32_t);
    env->cc_op = CC_OP_FLAGS;  // BUG: Discards lazy flag state!
    *(uint32_t *)value = cpu_m68k_get_sr(env);
    break;
```

By setting `env->cc_op = CC_OP_FLAGS`, it tells `COMPUTE_CCR` "flags are already computed, don't compute them". This is wrong when `env->cc_op` was `CC_OP_SUBB` - the flags haven't been computed yet!

## Impact on Dual-CPU Validation

Our dual-CPU harness executes instructions in lockstep:
```c
// Execute instruction on both CPUs
unicorn_execute_one(unicorn);  // Executes SUB.B, sets cc_op=CC_OP_SUBB
uae_cpu_execute_one();          // Executes SUB.B, updates SR immediately

// Compare state
uint16_t uae_sr = uae_get_sr();         // Correct: 0x2704 (Z flag set)
uint16_t unicorn_sr = unicorn_get_sr(); // WRONG: 0x2700 (no flags!)
```

Result: **False divergence** detected at every arithmetic instruction.

## The Fix

Replace `env->cc_op = CC_OP_FLAGS` with `helper_flush_flags(env, env->cc_op)`:

```c
case UC_M68K_REG_SR:
    CHECK_REG_TYPE(uint32_t);
    helper_flush_flags(env, env->cc_op);  // Properly compute lazy flags
    *(uint32_t *)value = cpu_m68k_get_sr(env);
    break;
```

`helper_flush_flags()` ([helper.c:862](../external/unicorn/qemu/target/m68k/helper.c#L862)):
- Calls `COMPUTE_CCR` with the current `cc_op` to compute flags
- **Then** sets `env->cc_op = CC_OP_FLAGS` to indicate flags are now computed
- Updates `env->cc_n`, `env->cc_v`, `env->cc_c`, `env->cc_z` with actual flag values

## Test Case

```c
// Example: SUB.B D0,(A0) where D0=0 and (A0)=0
// Expected result: Z flag should be set (result is zero)

// Setup
uc_reg_write(uc, UC_M68K_REG_D0, &zero);
uc_mem_write(uc, addr, &zero, 1);

// Execute SUB.B D0,(A0)
uc_emu_start(uc, pc, 0, 0, 1);

// Read SR
uint32_t sr;
uc_reg_read(uc, UC_M68K_REG_SR, &sr);

// BUG: sr == 0x2700 (no Z flag)
// FIX: sr == 0x2704 (Z flag set correctly)
```

## Verification

After fix, dual-CPU validation executes **7 ROM instructions** in perfect lockstep:

| # | PC | Opcode | Instruction | UAE SR | Unicorn SR | Status |
|---|------------|--------|-------------|--------|------------|--------|
| 0 | 0x0200002A | 0x4EFA | JMP 0x0200008C | 0x2700 | 0x2700 | ✅ |
| 1 | 0x0200008C | 0x46FC | MOVE #0x2700,SR | 0x2700 | 0x2700 | ✅ |
| 2 | 0x02000090 | 0x4DFA | LEA (PC+disp),A6 | 0x2700 | 0x2700 | ✅ |
| 3 | 0x02000094 | 0x6000 | BRA 0x02004052 | 0x2700 | 0x2700 | ✅ |
| 4 | 0x02004052 | 0x9080 | SUB.B D0,(A0) | 0x2704 | 0x2704 | ✅ |
| 5 | 0x02004054 | 0x08C0 | BSET #31,D0 | 0x2704 | 0x2704 | ✅ |
| 6 | 0x02004058 | 0x4E7B | MOVEC D0,CACR | 0x2704 | 0x2704 | ✅ |
| 7 | 0x0200405C | 0x4E7A | MOVEC CACR,D0 | - | - | ⚠️ Exception |

Execution stops at instruction 7 when Unicorn raises UC_ERR_EXCEPTION for
MOVEC CACR,D0 (reading Cache Control Register). This is expected - cache
register emulation is a separate task.

## Upstream Status

This fix should be submitted to Unicorn as a bug fix. The change:
- Has no performance impact (only affects register reads, not execution)
- Fixes correctness issue with lazy flag evaluation
- Matches expected M68K behavior

## Alternative Workarounds

If modifying Unicorn is undesirable, workarounds include:

1. **Context save/restore**: Use `uc_context_save()` which properly flushes state
2. **Single-step with sync**: Execute in single-step mode which may flush flags
3. **Wrapper flush**: Call helper function before every SR read (requires exposing internal API)

However, **none of these are as clean as fixing the bug properly**.

##References

- Unicorn lazy flag evaluation: [translate.c:set_cc_op()](../external/unicorn/qemu/target/m68k/translate.c#L247)
- Flag computation macro: [helper.c:COMPUTE_CCR](../external/unicorn/qemu/target/m68k/helper.c#L777)
- Helper flush function: [helper.c:helper_flush_flags()](../external/unicorn/qemu/target/m68k/helper.c#L862)
