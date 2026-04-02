# MMIO Trap Approach for JIT-Compatible EmulOp Handling

## Problem Statement

BasiliskII patches the Mac ROM to replace Mac OS trap calls with illegal 0x71xx instructions (EmulOps). These need to be intercepted and handled by the emulator:

- **UAE CPU**: Has built-in illegal instruction handler that works fine
- **Unicorn CPU**: Uses `UC_HOOK_INSN_INVALID` hook, but:
  - ❌ Register modifications don't persist (Unicorn limitation)
  - ❌ May not work reliably with JIT mode
  - ❌ Hooks are called during execution, not between instructions

## Solution: MMIO Memory Trap Region

Instead of trying to execute illegal instructions, we **redirect PC to unmapped memory** which triggers a memory fetch hook that works in both interpreted and JIT modes.

### Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│ ROM Memory (after patching)                                  │
│   0x0200B37E: 0x710C  ← EmulOp (M68K_EMUL_OP_SONY_OPEN)     │
│   0x0200B380: 0x4E75  ← RTS (return)                        │
└──────────────────────────────────────────────────────────────┘
                    ↓ CPU tries to execute
┌──────────────────────────────────────────────────────────────┐
│ Unicorn raises UC_ERR_INSN_INVALID                          │
│   (0x710C is illegal instruction)                            │
└──────────────────────────────────────────────────────────────┘
                    ↓ Our handler catches error
┌──────────────────────────────────────────────────────────────┐
│ Save original PC (0x0200B37E) to context                     │
│ Calculate trap address:                                      │
│   opcode = 0x710C                                            │
│   emulop_num = 0x0C (12)                                     │
│   trap_addr = 0xFF000000 + (emulop_num * 2)                 │
│             = 0xFF000018                                     │
│ Redirect: PC ← 0xFF000018                                   │
└──────────────────────────────────────────────────────────────┘
                    ↓ Resume execution
┌──────────────────────────────────────────────────────────────┐
│ Unicorn tries to FETCH instruction from 0xFF000018          │
│   But trap region (0xFF000000-0xFF001000) is UNMAPPED!     │
│   Triggers UC_HOOK_MEM_FETCH_UNMAPPED                       │
│   ✅ This hook fires even in JIT mode!                      │
└──────────────────────────────────────────────────────────────┘
                    ↓ Hook callback
┌──────────────────────────────────────────────────────────────┐
│ Trap Handler Callback:                                       │
│   1. Calculate EmulOp: (0xFF000018 - 0xFF000000) / 2 = 12   │
│   2. Reconstruct opcode: 0x7100 + 12 = 0x710C              │
│   3. Read CPU registers from Unicorn                        │
│   4. Call EmulOp(0x710C, &regs)  ← Same handler as UAE!    │
│   5. Write modified registers back to Unicorn               │
│   6. Restore PC: original_pc + 2 = 0x0200B380              │
│   7. Return from hook                                       │
└──────────────────────────────────────────────────────────────┘
                    ↓ Execution continues
┌──────────────────────────────────────────────────────────────┐
│ Unicorn resumes at 0x0200B380 (RTS instruction)            │
│ EmulOp successfully executed! ✅                             │
└──────────────────────────────────────────────────────────────┘
```

### Memory Map

```
0x00000000 ┌─────────────────────┐
           │ RAM (32 MB)         │
           │                     │
0x02000000 ├─────────────────────┤
           │ ROM (1 MB)          │
           │ Contains 0x71xx     │
0x02100000 ├─────────────────────┤
           │ Dummy region (16MB) │
           │                     │
           │                     │
           │                     │
           │                     │
           │                     │
           │                     │
           │                     │
           │                     │
           │                     │
           │                     │
           │                     │
           │                     │
0xFF000000 ├─────────────────────┤ ← TRAP REGION BASE
           │ UNMAPPED!           │   (Intentionally no memory)
           │ 0xFF000000: EmulOp 0│
           │ 0xFF000002: EmulOp 1│
           │ 0xFF000004: EmulOp 2│
           │ ...                 │
           │ 0xFF000018: EmulOp12│ ← Sony Open (0x710C)
           │ ...                 │
           │ 0xFF000FFE: EmulOp2047
0xFF001000 └─────────────────────┘
```

### Implementation Details

#### Setup (at initialization):

```c
#define TRAP_REGION_BASE  0xFF000000
#define TRAP_REGION_SIZE  0x00001000  // 4KB = 2048 trap slots

// DON'T map this region - leave it unmapped!
// uc_mem_map(...) is NOT called for trap region

// Register hook for unmapped memory fetch
uc_hook trap_hook;
uc_hook_add(uc, &trap_hook,
            UC_HOOK_MEM_FETCH_UNMAPPED,
            trap_mem_fetch_handler,
            &trap_context,  // User data
            TRAP_REGION_BASE,
            TRAP_REGION_BASE + TRAP_REGION_SIZE - 1);

// Initialize trap context
trap_context.saved_pc = 0;
trap_context.in_emulop = false;
```

#### Execution Loop:

```c
typedef struct {
    uint32_t saved_pc;     // Original PC where 0x71xx was
    bool in_emulop;        // Are we handling an EmulOp?
} TrapContext;

int unicorn_execute_one(UnicornCPU *cpu) {
    uint32_t pc = unicorn_get_pc(cpu);

    uc_err err = uc_emu_start(cpu->uc, pc, 0xFFFFFFFFFFFFFFFFULL, 0, 1);

    if (err == UC_ERR_INSN_INVALID) {
        // Read opcode at PC
        uint16_t opcode;
        uc_mem_read(cpu->uc, pc, &opcode, sizeof(opcode));
        opcode = bswap16(opcode);  // Convert to host endian

        // Check if it's an EmulOp (0x71xx)
        if ((opcode & 0xFF00) == 0x7100) {
            // Save original PC
            trap_context.saved_pc = pc;
            trap_context.in_emulop = true;

            // Calculate trap address
            uint32_t emulop_num = opcode & 0xFF;
            uint32_t trap_addr = TRAP_REGION_BASE + (emulop_num * 2);

            // Redirect PC to trap region
            uc_reg_write(cpu->uc, UC_M68K_REG_PC, &trap_addr);

            // Resume - will trigger MEM_FETCH_UNMAPPED hook
            err = uc_emu_start(cpu->uc, trap_addr, 0xFFFFFFFFFFFFFFFFULL, 0, 1);

            // After hook completes, execution should continue normally
            return (err == UC_ERR_OK) ? 0 : -1;
        }

        // Not an EmulOp, real illegal instruction
        set_error(cpu, err);
        return -1;
    }

    return (err == UC_ERR_OK) ? 0 : -1;
}
```

#### Memory Fetch Hook Handler:

```c
static void trap_mem_fetch_handler(uc_engine *uc, uc_mem_type type,
                                   uint64_t address, int size,
                                   int64_t value, void *user_data) {
    TrapContext *ctx = (TrapContext *)user_data;

    // Verify this is our trap region
    if (address < TRAP_REGION_BASE ||
        address >= TRAP_REGION_BASE + TRAP_REGION_SIZE) {
        fprintf(stderr, "Unexpected unmapped fetch at 0x%08lx\n", address);
        return;
    }

    // Calculate which EmulOp this is
    uint32_t emulop_num = (address - TRAP_REGION_BASE) / 2;
    uint16_t opcode = 0x7100 + emulop_num;

    // Read CPU registers
    M68kRegisters regs;
    for (int i = 0; i < 8; i++) {
        uc_reg_read(uc, UC_M68K_REG_D0 + i, &regs.d[i]);
        uc_reg_read(uc, UC_M68K_REG_A0 + i, &regs.a[i]);
    }
    uc_reg_read(uc, UC_M68K_REG_SR, &regs.sr);

    // Execute EmulOp handler (same code as UAE uses!)
    EmulOp(opcode, &regs);

    // Write registers back
    for (int i = 0; i < 8; i++) {
        uc_reg_write(uc, UC_M68K_REG_D0 + i, &regs.d[i]);
        uc_reg_write(uc, UC_M68K_REG_A0 + i, &regs.a[i]);
    }
    uc_reg_write(uc, UC_M68K_REG_SR, &regs.sr);

    // Restore PC to instruction AFTER the 0x71xx
    uint32_t next_pc = ctx->saved_pc + 2;
    uc_reg_write(uc, UC_M68K_REG_PC, &next_pc);

    ctx->in_emulop = false;
}
```

### Key Advantages

1. ✅ **JIT Compatible**: Memory hooks fire even when code is JIT-compiled
2. ✅ **Register Persistence**: Modifications happen outside `uc_emu_start()` call
3. ✅ **100% Code Reuse**: Same `EmulOp()` handler as UAE
4. ✅ **Same ROM Patching**: Both UAE and Unicorn use identical ROM with 0x71xx
5. ✅ **Clean Separation**: EmulOp handling is explicit, not hidden in instruction execution

### Comparison with Other Approaches

| Approach | JIT? | Registers? | Code Reuse | ROM Same? |
|----------|------|------------|------------|-----------|
| UC_HOOK_INSN_INVALID (current) | ❌ | ❌ | ✅ | ✅ |
| MMIO Trap (this) | ✅ | ✅ | ✅ | ✅ |
| Modify Unicorn TCG | ✅ | ✅ | ✅ | ✅ |
| Different ROM patches | ✅ | ✅ | ✅ | ❌ |

### Trap Region Memory Layout

```c
// Each EmulOp gets 2-byte slot (even though it's unmapped)
// This allows us to encode which EmulOp by address

Address         EmulOp Number   Opcode      Handler
───────────────────────────────────────────────────────
0xFF000000      0               0x7100      M68K_EXEC_RETURN
0xFF000002      1               0x7101      M68K_EMUL_BREAK
0xFF000004      2               0x7102      M68K_EMUL_OP_SHUTDOWN
0xFF000006      3               0x7103      M68K_EMUL_OP_RESET
0xFF000008      4               0x7104      M68K_EMUL_OP_CLKNOMEM
...
0xFF000018      12              0x710C      M68K_EMUL_OP_SONY_OPEN
0xFF00001A      13              0x710D      M68K_EMUL_OP_SONY_PRIME
...
0xFF000FFE      2047            0x71FF      (unused)
```

### Error Handling

What if real unmapped memory fetch happens?

```c
static void trap_mem_fetch_handler(..., uint64_t address, ...) {
    // Check if this is in our trap region
    if (address < TRAP_REGION_BASE ||
        address >= TRAP_REGION_BASE + TRAP_REGION_SIZE) {
        fprintf(stderr, "ERROR: Unmapped memory fetch at 0x%08lx\n", address);
        fprintf(stderr, "This is NOT an EmulOp trap!\n");
        // Don't handle it - let Unicorn error out
        return;
    }

    // Also check if we're expecting this
    TrapContext *ctx = (TrapContext *)user_data;
    if (!ctx->in_emulop) {
        fprintf(stderr, "WARNING: Unexpected trap region access at 0x%08lx\n",
                address);
        return;
    }

    // Handle EmulOp...
}
```

### Performance Considerations

- **Two uc_emu_start() calls per EmulOp**: One fails with INSN_INVALID, second triggers trap
- **Memory hook overhead**: Minimal - only fires on unmapped fetch
- **JIT still compiles trap jumps**: The PC redirect happens in interpreter, but subsequent code after EmulOp gets JIT-compiled

### Future Enhancements

1. **A-line/F-line Traps**: Could use same approach for 0xAxxx and 0xFxxx traps
2. **Trap Statistics**: Track EmulOp call counts via trap region accesses
3. **Conditional Traps**: Could map some trap slots with memory for "fast path" EmulOps

## Testing Plan

1. Test each EmulOp individually in Unicorn-only mode
2. Verify dual-CPU validation still works
3. Benchmark performance vs current UC_HOOK_INSN_INVALID approach
4. Test with Unicorn JIT enabled (when available)
5. Verify ROM patching is identical between UAE and Unicorn

## References

- Unicorn hook documentation: https://github.com/unicorn-engine/unicorn/blob/master/docs/HOOK_MEMORY.md
- BasiliskII EmulOp implementation: `src/emul_op.cpp`
- BasiliskII ROM patching: `src/rom_patches.cpp`
