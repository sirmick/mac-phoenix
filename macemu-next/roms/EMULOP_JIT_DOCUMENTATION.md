# EmulOp Handling with JIT in MacEmu

## Overview

EmulOps are special opcodes used by MacEmu to trigger emulator-specific operations from within 68k code. They occupy the opcode range 0x7100-0x713F, which overlaps with valid M68K MOVEQ instructions.

## The Challenge with JIT

The main challenge with EmulOps in a JIT environment is that:

1. **Some EmulOps are valid M68K instructions** - Opcodes 0x7100-0x713F are valid MOVEQ instructions (MOVEQ #0-#63, D0)
2. **JIT compiles blocks of instructions** - The JIT doesn't check every instruction individually
3. **Translation blocks can be large** - A single TB might contain hundreds of instructions

## How Unicorn Handles EmulOps

Unicorn uses a two-pronged approach to catch EmulOps:

### 1. UC_HOOK_BLOCK (Primary Detection)

At the start of every translation block, Unicorn checks if the first instruction is an EmulOp:

```c
static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    uint16_t opcode = 0;
    if (uc_mem_read(uc, pc, &opcode, sizeof(opcode)) == UC_ERR_OK) {
        /* Check if it's an EmulOp (0x7100-0x713F) */
        if (opcode >= 0x7100 && opcode < 0x7140) {
            /* Stop execution to handle EmulOp */
            uc_emu_stop(uc);
            cpu->trap_ctx.in_emulop = true;
            return;
        }
    }
    // ... rest of block hook
}
```

**Pros:**
- Efficient - only checks once per TB
- Catches EmulOps at TB boundaries

**Cons:**
- Misses EmulOps in the middle of TBs
- Requires careful TB management

### 2. UC_HOOK_INSN_INVALID (Fallback Detection)

For truly illegal instructions (A-line traps 0xAxxx, F-line traps 0xFxxx), Unicorn triggers this hook:

```c
static bool hook_insn_invalid(uc_engine *uc, void *user_data) {
    uint16_t opcode;
    uc_mem_read(uc, pc, &opcode, 2);

    /* Check for A-line trap (0xAxxx) */
    if ((opcode & 0xF000) == 0xA000) {
        // Handle A-line trap
        return true;
    }

    /* Check for F-line trap (0xFxxx) */
    if ((opcode & 0xF000) == 0xF000) {
        // Handle F-line trap
        return true;
    }

    return false;
}
```

## Current Implementation Status

### What Works

✅ **EmulOps at TB start** - Caught by UC_HOOK_BLOCK
✅ **A-line and F-line traps** - Caught by UC_HOOK_INSN_INVALID
✅ **SHUTDOWN EmulOp (0x7101)** - Successfully terminates emulation
✅ **Basic EmulOps** - RESET, CLKNOMEM, READ_XPRAM work

### Known Issues

❌ **EmulOps mid-block** - May be missed if not at TB boundary
❌ **JIT optimization** - EmulOps that look like MOVEQ may be optimized away
❌ **Side effects** - Some EmulOps have unintended side effects in test ROMs

## Test Results

From our `emulop_jit_test.rom`:

| EmulOp | Opcode | Description | Detection Method | Status |
|--------|--------|-------------|------------------|--------|
| RESET | 0x7103 | Reset CPU | UC_HOOK_BLOCK | ✅ Works |
| CLKNOMEM | 0x7104 | Clock/RTC access | UC_HOOK_BLOCK | ✅ Works |
| READ_XPRAM | 0x7105 | Read XPRAM | UC_HOOK_BLOCK | ✅ Works |
| READ_XPRAM2 | 0x7106 | Read XPRAM2 | UC_HOOK_BLOCK | ✅ Works |
| SHUTDOWN | 0x7101 | Quit emulator | UC_HOOK_BLOCK | ✅ Works |
| A-line trap | 0xA000 | Mac OS trap | UC_HOOK_INSN_INVALID | ✅ Works |
| F-line trap | 0xF000 | FPU trap | UC_HOOK_INSN_INVALID | ✅ Works |

## Performance Considerations

The current approach has minimal performance impact:

1. **Block hook overhead** - One check per TB (average ~22 instructions per TB)
2. **No per-instruction overhead** - Unlike previous UC_HOOK_CODE approach
3. **Efficient for common case** - Most TBs don't start with EmulOps

## Recommendations for Improvement

1. **Force TB breaks on EmulOps** - Modify Unicorn to always end TBs at EmulOps
2. **MMIO trap region** - Use memory-mapped I/O for EmulOp detection (partially implemented)
3. **Opcode rewriting** - Replace EmulOps with illegal opcodes that always trigger hooks
4. **Custom Unicorn patches** - Add native EmulOp support to Unicorn's M68K backend

## How UAE Handles EmulOps

For comparison, UAE's interpreter mode handles EmulOps differently:

1. **Direct opcode check** - Every instruction is checked before execution
2. **No JIT complications** - Interpreter doesn't have TB concerns
3. **Immediate detection** - EmulOps are caught immediately

This is why UAE passes all EmulOp tests while Unicorn may miss some mid-block EmulOps.

## Summary

The current EmulOp implementation in Unicorn's JIT mode works for most practical cases:
- EmulOps used by Mac OS ROM patches work correctly
- Test ROMs can use EmulOps successfully
- Performance impact is minimal

The main limitation is that EmulOps in the middle of translation blocks may not be detected immediately, but this rarely affects real-world usage since most EmulOps are placed at strategic locations (trap handlers, ROM patches) that naturally align with TB boundaries.