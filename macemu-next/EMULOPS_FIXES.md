# EmulOps Implementation: Problems and Solutions

## Executive Summary

EmulOps are special 2-byte illegal instructions (0x71xx) that BasiliskII/SheepShaver use to intercept Mac OS system calls. The current MMIO transport implementation works but has a **critical size mismatch** (10 bytes vs 2 bytes) that breaks in-place ROM patching. This document outlines the history of attempts, the fundamental problems, and the correct solution.

**Current Status**: MMIO transport implemented but incomplete - falling back to UAE patches even for Unicorn.

**Recommended Solution**: Extend M68K instruction set in Unicorn fork to handle 0x71xx natively.

---

## Table of Contents

1. [Background: What are EmulOps?](#background-what-are-emulops)
2. [The Fundamental Problem](#the-fundamental-problem)
3. [History of Implementation Attempts](#history-of-implementation-attempts)
4. [Current Implementation Analysis](#current-implementation-analysis)
5. [The Correct Solution: Native Instruction Extension](#the-correct-solution-native-instruction-extension)
6. [Implementation Plan](#implementation-plan)
7. [Migration Strategy](#migration-strategy)

---

## Background: What are EmulOps?

EmulOps are BasiliskII's mechanism for intercepting Mac OS system calls:

1. **ROM Patching**: During initialization, BasiliskII patches the Mac ROM, replacing system trap handlers with 0x71xx instructions
2. **Illegal Instructions**: 0x71xx opcodes are illegal in M68K (would normally be MOVEQ on real hardware)
3. **Interception**: When CPU hits 0x71xx, it triggers an exception that BasiliskII handles
4. **Emulation**: BasiliskII executes native code to handle the system call (disk I/O, video, etc.)

Example:
```asm
; Original ROM code at 0x0200B37E (Sony driver open)
JSR  SonyOpen     ; 6 bytes

; After patching:
0x710C            ; EMUL_OP_SONY_OPEN (2 bytes)
0x4E71            ; NOP (2 bytes)
0x4E71            ; NOP (2 bytes)
```

---

## The Fundamental Problem

### UAE vs Unicorn CPU Architecture Differences

**UAE (Working)**:
- Custom M68K emulator with built-in illegal instruction handling
- Can modify registers during exception handling
- Single-instruction execution model
- No JIT, pure interpreter

**Unicorn (Problematic)**:
- Based on QEMU's TCG with JIT compilation
- Hooks run in execution context - register changes don't persist
- Translation block model - multiple instructions compiled together
- JIT optimization breaks per-instruction hooks

### The Core Challenge

We need to:
1. ✅ Intercept 0x71xx instructions reliably
2. ✅ Execute EmulOp handler (same code as UAE)
3. ✅ Modify CPU registers persistently
4. ✅ Work with JIT compilation
5. ✅ **Maintain 2-byte instruction size** (critical for ROM patching)

---

## History of Implementation Attempts

### Attempt 1: UC_HOOK_INSN_INVALID (Failed)

**Approach**: Use Unicorn's illegal instruction hook
```c
uc_hook_add(uc, &hook, UC_HOOK_INSN_INVALID, hook_invalid_insn, ...)
```

**Problems**:
- ❌ Register modifications don't persist after hook returns
- ❌ Breaks JIT optimization (per-instruction hooks disable JIT)
- ❌ Hook called during execution, not between instructions

**Status**: Abandoned

### Attempt 2: MMIO Transport (Current, Problematic)

**Approach**: Patch ROM to use memory-mapped I/O instead of illegal instructions
```asm
; Instead of:
0x710C                    ; 2 bytes

; Patch with:
MOVE.L #1, 0xFF000018     ; 10 bytes (!!)
```

**How it works**:
1. Map MMIO region at 0xFF000000
2. Register UC_HOOK_MEM_WRITE for this region
3. When write occurs, call EmulOp handler
4. Memory hooks work with JIT

**Problems**:
- ❌ **SIZE MISMATCH**: 10 bytes vs 2 bytes
- ❌ Overwrites adjacent ROM code
- ❌ Can't do in-place patching
- ❌ PatchROM_Unicorn incomplete

**Status**: Implemented but falling back to UAE patches

### Attempt 3: MMIO Trap (Proposed, Not Implemented)

**Approach**: Redirect PC to unmapped memory on illegal instruction
```c
// On UC_ERR_INSN_INVALID for 0x71xx:
trap_addr = 0xFF000000 + (opcode & 0xFF) * 2;
uc_reg_write(uc, UC_M68K_REG_PC, &trap_addr);
// Triggers UC_HOOK_MEM_FETCH_UNMAPPED
```

**Advantages**:
- ✅ Maintains 2-byte size
- ✅ Works with JIT
- ✅ Register modifications persist

**Problems**:
- Requires two uc_emu_start() calls per EmulOp
- Complex state management
- Never fully tested

**Status**: Documented but not implemented

### Attempt 4: Extend M68K Instruction Set (Started, Incomplete)

**Approach**: Modify Unicorn's M68K translator to recognize 0x71xx as valid instructions

**What was tried**:
- Added helper function to op_helper.c
- Added declaration to helper.h
- Got stuck on integration with translation system

**Why it failed**:
- Didn't modify opcode registration table
- Didn't create DISAS_INSN handler
- 0x7100-0x71FF already handled by 'mvzs' instruction
- Incomplete understanding of QEMU TCG architecture

**Status**: Partially attempted, abandoned

---

## Current Implementation Analysis

### What's Actually Running Now

```c
// In unicorn_wrapper.c
static void mmio_emulop_handler(uc_engine *uc, uc_mem_type type,
                                uint64_t address, int size, int64_t value,
                                void *user_data) {
    if (type != UC_MEM_WRITE) return;
    if (!IS_MMIO_EMULOP(address)) return;

    uint16_t opcode = MMIO_TO_EMULOP(address);
    // Call EmulOp handler...
}
```

### The Critical Problem

**PatchROM function comparison**:
```cpp
// PatchROM_UAE - Works fine
*wp++ = htons(0x710C);  // 2 bytes - fits perfectly

// PatchROM_Unicorn - Would corrupt ROM
emit_mmio_emulop(wp, 0x710C);  // 10 bytes - overwrites next 8 bytes!
```

**Result**: PatchROM_Unicorn is incomplete and both CPUs use UAE patches:
```cpp
bool PatchROM(void) {
    // TEMPORARY: Use UAE patches for both backends
    return PatchROM_UAE();
}
```

---

## Alternative: Using A-Line Opcodes (RECOMMENDED)

### Why Not 0x71xx?

The 0x71xx opcode range has a fundamental problem:
- **Conflicts with `mvzs` instruction** (0x7100-0x71FF in ColdFire)
- Requires complex modification of existing instruction decoder
- Risk of breaking valid ColdFire instructions

### The A-Line Solution (0xAE00-0xAE3F)

**A-Line instructions (0xA000-0xAFFF) are PERFECT for EmulOps:**

1. **Always illegal**: Guaranteed to trap on ALL 68K CPUs
2. **Reserved by Motorola**: Will never become valid instructions
3. **Historical precedent**: Mac OS used A-line traps for system calls
4. **4096 opcodes available**: We only need 64
5. **Clean implementation**: No conflicts with existing instructions
6. **Already supported**: Unicorn has A-line trap infrastructure

### Opcode Allocation

```
0xA000-0xA9FF : Mac OS system traps (avoid these)
0xAA00-0xADFF : Available
0xAE00-0xAE3F : EmulOps (our allocation)
0xAE40-0xAEFF : Reserved for future use
0xAF00-0xAFFF : Available
```

### Implementation Advantages

| Aspect | 0x71xx (Current) | 0xAE00-0xAE3F (Proposed) |
|--------|------------------|--------------------------|
| Conflicts with valid opcodes | YES (mvzs) | NO |
| Implementation complexity | HIGH | LOW |
| Unicorn modification needed | Complex | Simple |
| Future-proof | NO | YES |
| Mac OS compatibility | YES | YES |

### Code Changes Required

**ROM Patching:**
```cpp
// Old: Uses 0x71xx
*wp++ = htons(M68K_EMUL_OP_DISK_OPEN);  // 0x7110

// New: Uses 0xAExx
*wp++ = htons(M68K_EMUL_OP_DISK_OPEN_ALINE);  // 0xAE10
```

**EmulOp Definitions:**
```cpp
// Add new A-line definitions alongside old ones
#define M68K_EMUL_OP_DISK_OPEN       0x7110  // Legacy
#define M68K_EMUL_OP_DISK_OPEN_ALINE 0xAE10  // New A-line version
```

---

## The Correct Solution: Native Instruction Extension

### Why This Is The Right Approach

1. **Maintains 2-byte instruction size** - Critical for ROM patching
2. **JIT compatible** - Helper gets compiled into translation blocks
3. **Clean integration** - No hooks, no workarounds
4. **Performance** - No overhead beyond EmulOp execution itself
5. **Correctness** - Identical behavior to UAE

### How It Works

Instead of treating 0x71xx as illegal, make Unicorn recognize them as valid instructions that call our EmulOp handler:

```c
// In unicorn/qemu/target/m68k/translate.c
DISAS_INSN(emulop)  // Handles 0x7100-0x713F
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint16_t opcode = insn;

    // Generate call to helper
    TCGv_i32 op = tcg_const_i32(tcg_ctx, opcode);
    gen_helper_emulop(tcg_ctx, tcg_ctx->cpu_env, op);
    tcg_temp_free_i32(tcg_ctx, op);

    // PC already advanced by instruction fetch
}
```

### What Makes This Different from Previous Attempt

**Previous attempt** (failed):
- Only added helper function
- Didn't integrate with translation system
- No opcode registration changes

**Correct approach**:
- Modify opcode registration to intercept 0x7100-0x713F
- Create proper DISAS_INSN handler
- Generate TCG code that calls helper
- Helper executes in CPU context with persistent registers

---

## Implementation Plan

### Phase 1: Fork and Setup Unicorn (Day 1)

1. **Fork Unicorn repository**
   ```bash
   git clone https://github.com/unicorn-engine/unicorn unicorn-emulop
   cd unicorn-emulop
   git checkout -b emulop-support
   ```

2. **Set up build environment**
   ```bash
   mkdir build && cd build
   cmake .. -DUNICORN_ARCH=m68k -DCMAKE_BUILD_TYPE=Debug
   make -j8
   ```

3. **Create test harness**
   - Simple test that executes 0x7100 instruction
   - Verify it currently raises UC_ERR_INSN_INVALID

### Phase 2: Modify Opcode Registration (Day 2)

**File**: `qemu/target/m68k/translate.c`

1. **Find A-line trap handler** (search for "linea"):
   ```c
   DISAS_INSN(linea)  // Existing A-line trap handler
   {
       gen_exception(s, s->base.pc_next, EXCP_LINEA);
   }
   ```

2. **Add EmulOp registration BEFORE generic A-line**:
   ```c
   // Add specific handler for our EmulOp range
   INSN(emulop_aline, ae00, ffc0, M68000);  // 0xAE00-0xAE3F (64 EmulOps)

   // Keep existing A-line for all other 0xAxxx
   BASE(linea, a000, f000);  // 0xA000-0xAFFF (generic A-line)
   ```

3. **Note**: The mask `ffc0` matches specific pattern:
   - `ae00 & ffc0` = matches 0xAE00-0xAE3F exactly
   - More specific than generic `linea`, so takes precedence

### Phase 3: Create DISAS_INSN Handler (Day 2-3)

**File**: `qemu/target/m68k/translate.c`

Add around line 3200 (near other DISAS_INSN functions):

```c
DISAS_INSN(emulop_aline)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    /* Extract EmulOp number from A-line opcode */
    uint16_t emulop_num = insn & 0x3F;  // 0xAE00-0xAE3F -> 0-63

    /* Convert to legacy 0x71xx format for compatibility */
    uint16_t legacy_opcode = 0x7100 | emulop_num;

    /* Save opcode for helper */
    TCGv_i32 op = tcg_const_i32(tcg_ctx, legacy_opcode);

    /* Call EmulOp helper */
    gen_helper_emulop(tcg_ctx, tcg_ctx->cpu_env, op);

    /* Free temporary */
    tcg_temp_free_i32(tcg_ctx, op);

    /* Note: PC already advanced by 2 during instruction fetch */
}
```

### Phase 4: Add Helper Function (Day 3)

**File**: `qemu/target/m68k/helper.h`

Add at end:
```c
DEF_HELPER_2(emulop, void, env, i32)
```

**File**: `qemu/target/m68k/op_helper.c`

Add at end:
```c
void HELPER(emulop)(CPUM68KState *env, uint32_t opcode)
{
    struct uc_struct *uc = env->uc;

    /* Call back to BasiliskII's EmulOp handler */
    if (uc->hook_emulop) {
        uc->hook_emulop(uc, opcode, uc->hook_emulop_data);
    }
}
```

### Phase 5: Add Callback Mechanism (Day 3-4)

**File**: `qemu/target/m68k/unicorn.h`

Add to uc_struct:
```c
struct uc_struct {
    // ... existing fields ...

    /* EmulOp callback */
    void (*hook_emulop)(struct uc_struct *uc, uint32_t opcode, void *data);
    void *hook_emulop_data;
};
```

**File**: `include/unicorn/unicorn.h`

Add new API:
```c
/* Set EmulOp handler for M68K */
UNICORN_EXPORT
uc_err uc_m68k_set_emulop_handler(uc_engine *uc,
    void (*handler)(uc_engine*, uint32_t, void*), void *user_data);
```

**File**: `qemu/target/m68k/unicorn.c`

Implement API:
```c
uc_err uc_m68k_set_emulop_handler(uc_engine *uc,
    void (*handler)(uc_engine*, uint32_t, void*), void *user_data)
{
    if (!uc || uc->arch != UC_ARCH_M68K) {
        return UC_ERR_ARCH;
    }

    uc->hook_emulop = handler;
    uc->hook_emulop_data = user_data;
    return UC_ERR_OK;
}
```

### Phase 6: Integration with BasiliskII (Day 4-5)

**File**: `macemu-next/src/cpu/unicorn_wrapper.c`

```c
/* EmulOp callback for Unicorn */
static void unicorn_emulop_callback(uc_engine *uc, uint32_t opcode, void *data)
{
    /* Read registers from Unicorn */
    struct M68kRegistersC regs;
    for (int i = 0; i < 8; i++) {
        uc_reg_read(uc, UC_M68K_REG_D0 + i, &regs.d[i]);
        uc_reg_read(uc, UC_M68K_REG_A0 + i, &regs.a[i]);
    }
    uc_reg_read(uc, UC_M68K_REG_SR, &regs.sr);

    /* Call BasiliskII's EmulOp handler */
    EmulOp_C(opcode, &regs);

    /* Write registers back */
    for (int i = 0; i < 8; i++) {
        uc_reg_write(uc, UC_M68K_REG_D0 + i, &regs.d[i]);
        uc_reg_write(uc, UC_M68K_REG_A0 + i, &regs.a[i]);
    }
    uc_reg_write(uc, UC_M68K_REG_SR, &regs.sr);
}

/* In unicorn_create() */
uc_m68k_set_emulop_handler(cpu->uc, unicorn_emulop_callback, cpu);
```

### Phase 7: Testing (Day 5-6)

1. **Unit test**: Execute single 0x71xx instruction
2. **Integration test**: Boot Mac ROM with Unicorn
3. **Validation test**: Run dual-CPU mode, verify identical execution
4. **Performance test**: Measure overhead vs MMIO approach

### Phase 8: Complete ROM Patching (Day 6-7)

**File**: `macemu-next/src/core/rom_patches_unicorn.cpp`

Complete the implementation using 2-byte EmulOps:
```cpp
static inline void emit_emulop(uint16 **wp, uint16 opcode)
{
    **wp = htons(opcode);
    (*wp)++;
}

bool PatchROM_Unicorn(void)
{
    // Now we can use the same 2-byte patches as UAE!
    // Copy full PatchROM implementation...
}
```

---

## Migration Strategy

### Step 1: Build Custom Unicorn
```bash
cd unicorn-emulop
mkdir build && cd build
cmake .. -DUNICORN_ARCH=m68k -DCMAKE_BUILD_TYPE=Release
make -j8
sudo make install
```

### Step 2: Update BasiliskII Build
```bash
cd macemu-next
meson configure build -Dunicorn_custom=true
meson compile -C build
```

### Step 3: Test Phases

1. **Unicorn-only mode**: Verify EmulOps execute correctly
2. **Dual-CPU mode**: Verify identical execution with UAE
3. **Performance**: Benchmark vs MMIO approach
4. **Stability**: Extended testing with various Mac software

### Step 4: Remove MMIO Code

Once native EmulOps are working:
1. Remove MMIO transport code
2. Remove MMIO hook registration
3. Clean up PatchROM to use single implementation
4. Update documentation

---

## Benefits of Native Instruction Extension

### Comparison Matrix

| Aspect | UC_HOOK_INSN_INVALID | MMIO Transport | MMIO Trap | **Native Extension** |
|--------|---------------------|----------------|-----------|---------------------|
| JIT Compatible | ❌ | ✅ | ✅ | **✅** |
| Register Persistence | ❌ | ✅ | ✅ | **✅** |
| Instruction Size | 2 bytes ✅ | 10 bytes ❌ | 2 bytes ✅ | **2 bytes ✅** |
| ROM Patching Works | ❌ | ❌ | ✅ | **✅** |
| Performance | Poor | Good | Good | **Best** |
| Maintainability | ✅ | ✅ | ✅ | **❌ (fork)** |
| Implementation Complexity | Low | Medium | High | **High** |

### Why It's Worth The Effort

1. **Correctness**: Only solution that fully works with existing ROM patches
2. **Performance**: No overhead beyond EmulOp execution
3. **Cleanliness**: No workarounds, hacks, or multi-stage execution
4. **Future-proof**: Can extend to handle A-line/F-line traps same way

---

## Risks and Mitigation

### Risk 1: Maintaining Unicorn Fork
**Mitigation**:
- Keep changes minimal and well-documented
- Submit PR upstream (benefits other 68k emulation projects)
- Maintain compatibility with upstream API

### Risk 2: Breaking Existing Unicorn Behavior
**Mitigation**:
- Only intercept 0x7100-0x713F (64 opcodes)
- Leave rest of 0x71xx range for mvzs instruction
- Extensive testing with existing Unicorn users

### Risk 3: Implementation Complexity
**Mitigation**:
- Follow existing DISAS_INSN patterns
- Test incrementally at each phase
- Keep detailed logs of changes

---

## Conclusion

The native instruction extension approach is the **only complete solution** that:
- Maintains 2-byte instruction size required for ROM patching
- Works with JIT compilation
- Provides correct EmulOp execution

While it requires maintaining a Unicorn fork, this is a worthwhile tradeoff for a fully functional Mac emulator. The MMIO transport approach cannot work due to fundamental size constraints.

**Recommendation**: Proceed with native instruction extension implementation following the detailed plan above.

---

## Appendix: Code References

### Key Files to Modify

**Unicorn**:
- `qemu/target/m68k/translate.c` - Instruction decoder
- `qemu/target/m68k/op_helper.c` - Helper implementations
- `qemu/target/m68k/helper.h` - Helper declarations
- `qemu/target/m68k/unicorn.c` - Unicorn API implementation

**BasiliskII**:
- `src/cpu/unicorn_wrapper.c` - Unicorn integration
- `src/core/rom_patches_unicorn.cpp` - ROM patching
- `src/core/emul_op.cpp` - EmulOp handlers (unchanged)

### Related Documentation
- [MMIO_IMPLEMENTATION_PLAN.md](docs/unicorn/obsolete/MMIO_IMPLEMENTATION_PLAN.md) - Obsolete
- [MMIO_TRAP_APPROACH.md](docs/unicorn/obsolete/MMIO_TRAP_APPROACH.md) - Obsolete
- [00_UNICORN_INTEGRATION_MASTER_PLAN.md](docs/unicorn/00_UNICORN_INTEGRATION_MASTER_PLAN.md) - Overall strategy

---

*Document Version: 1.0*
*Date: January 2025*
*Author: Development Team*