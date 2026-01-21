# Dual-CPU Validation Initialization Analysis

## Problem Statement

The dual-CPU validation mode **syncs UAE state to Unicorn on the first instruction** ([unicorn_validation.cpp:543-558](../src/cpu/unicorn_validation.cpp#L543-L558)), which masks initialization bugs that only appear when running each CPU standalone.

This was discovered when:
1. **Dual-CPU validation mode** - No divergence detected, emulator runs successfully
2. **Standalone Unicorn mode** - Shows different register states than standalone UAE mode from instruction #1
3. **Root cause** - The two CPUs initialize differently, but validation mode forces them to sync before testing begins

## Current Initialization Sequence

### UAE Initialization

**File**: [src/cpu/uae_cpu/newcpu.cpp:1232-1268](../src/cpu/uae_cpu/newcpu.cpp#L1232-L1268)

```cpp
void m68k_reset (void)
{
    m68k_areg (regs, 7) = 0x2000;           // ← A7 = 0x00002000
    m68k_setpc (ROMBaseMac + 0x2a);
    fill_prefetch_0 ();
    regs.s = 1;                             // ← Supervisor mode
    regs.m = 0;
    regs.stopped = 0;
    regs.t1 = 0;                            // ← Trace flags off
    regs.t0 = 0;
    SET_ZFLG (0);                           // ← All condition codes = 0
    SET_XFLG (0);
    SET_CFLG (0);
    SET_VFLG (0);
    SET_NFLG (0);
    SPCFLAGS_INIT( 0 );
    regs.intmask = 7;                       // ← All interrupts masked
    regs.vbr = regs.sfc = regs.dfc = 0;
    fpu_reset();
}
```

**Initial CPU state**:
- **A7**: 0x00002000
- **SR**: 0x0000 (should be 0x2700! Bug in trace capture, not actual SR)
  - S=1 (supervisor)
  - IntMask=7
  - XNZVC=00000
- **PC**: ROMBaseMac + 0x2a (typically 0x0040002a)
- **All other registers**: 0x00000000

### Unicorn Initialization

**File**: [src/cpu/unicorn_wrapper.c:265-349](../src/cpu/unicorn_wrapper.c#L265-L349)

```c
UnicornCPU* unicorn_create_with_model(UnicornArch arch, int cpu_model) {
    // ...
    uc_err err = uc_open(uc_arch, uc_mode, &cpu->uc);
    // ...
}
```

Unicorn uses default CPU reset state when `uc_open()` is called:

**Initial CPU state** (from Unicorn's M68K reset):
- **A7**: 0x00000000 (NOT 0x2000!)
- **SR**: 0x2700 (correct M68K reset value)
  - S=1 (supervisor)
  - IntMask=7
  - XNZVC=00000
- **PC**: 0x00000000 (NOT ROMBaseMac + 0x2a!)
- **All other registers**: 0x00000000

### Divergence at Instruction #1

When running standalone (without validation sync):

| Register | UAE          | Unicorn      | Match? |
|----------|--------------|--------------|--------|
| PC       | 0x0040002A   | 0x00000000   | ❌     |
| A7       | 0x00002000   | 0x00000000   | ❌     |
| SR       | 0x2700*      | 0x2700       | ✓      |
| D0-D7    | 0x00000000   | 0x00000000   | ✓      |
| A0-A6    | 0x00000000   | 0x00000000   | ✓      |

*Note: Trace showed SR=0x0000 for UAE, but this is because `uae_get_sr()` wasn't called before execution started. The actual SR internal flags were set correctly (s=1, intmask=7).

**Instruction #1 is EmulOp 0x7103 (M68K_EMUL_OP_RESET)**, which sets:
- A7 = 0x00010000
- SR = 0x2700
- PC = next instruction

After this EmulOp, both CPUs have identical state. However, **the fact that they started differently is important**.

## Current Dual-CPU Validation Behavior

**File**: [src/cpu/unicorn_validation.cpp:543-558](../src/cpu/unicorn_validation.cpp#L543-L558)

```cpp
// Sync state on first instruction (UAE CPU is initialized after reset)
if (validation_state.instruction_count == 0) {
    // IMPORTANT: Set PC and SR first, THEN registers
    // Setting PC clears A7 in Unicorn (bug or feature?), so A7 must be set after PC
    unicorn_set_pc(validation_state.unicorn, uae_get_pc());
    unicorn_set_sr(validation_state.unicorn, uae_get_sr());

    for (int i = 0; i < 8; i++) {
        unicorn_set_dreg(validation_state.unicorn, i, uae_get_dreg(i));
        unicorn_set_areg(validation_state.unicorn, i, uae_get_areg(i));
    }

    // Sync control registers (CACR, VBR, etc.)
    unicorn_set_cacr(validation_state.unicorn, uae_get_cacr());
    unicorn_set_vbr(validation_state.unicorn, uae_get_vbr());
}
```

This **forces Unicorn to match UAE's initial state** before any validation occurs.

**Why this exists**:
- The comment says "UAE CPU is initialized after reset"
- UAE's reset sets PC and A7 to specific values for Macintosh ROM boot
- Unicorn defaults to standard M68K reset (PC=0, A7=0)
- Without this sync, the first instruction would immediately diverge

**Problem**: This masks **any bugs in how either CPU initializes**, because we never actually validate the initialization itself.

## Why This Matters

### Real-World Bug Example

When tracing standalone UAE vs Unicorn at instruction #1000:
- **UAE**: A1 = 0x5F100000
- **Unicorn**: A1 = 0xEF700000

This divergence exists even though instruction #1 (EmulOp 0x7103) reset both CPUs to identical state.

**The divergence occurs somewhere between instruction #1 and #1000**, but dual-CPU validation mode **would never catch this** because:

1. Both CPUs execute instruction #1 (EmulOp 0x7103) identically
2. After EmulOp, both have A7=0x00010000, SR=0x2700
3. But at some point later, A1 diverges

The question is: **What instruction causes A1 to diverge?**

This is what we were in the middle of finding using the binary search approach with the new trace tools.

## Design Intent vs. Current Behavior

### Possible Design Intents

1. **Focus on runtime correctness, not initialization**
   - Assumption: Initialization differences don't matter as long as both CPUs start from the same state
   - Justification: Real Macintosh code starts executing after ROM boot, not from CPU reset
   - **Problem**: This assumes UAE's initialization is "correct" - but what if it's not?

2. **Work around Unicorn's generic reset**
   - Unicorn is a generic M68K emulator, doesn't know about Macintosh-specific boot
   - UAE has Macintosh-specific initialization (PC = ROMBaseMac + 0x2a)
   - **Problem**: Should we instead call Unicorn's reset, then set PC/A7 explicitly?

3. **Avoid false positives on first instruction**
   - Without sync, instruction #1 would always fail validation
   - This would make the validation output noisy
   - **Problem**: But now we have false negatives (bugs we don't catch)

### Current Behavior Issues

1. **Masks initialization bugs** - If UAE initializes incorrectly, Unicorn will inherit the bug
2. **Assumes UAE is "correct"** - Why sync UAE→Unicorn instead of the reverse?
3. **Hides divergences** - Real bugs that occur early in execution might be missed
4. **Makes debugging harder** - When standalone modes differ, can't trust dual-CPU validation

## Recommendations

### Option 1: Explicit Initialization (Preferred)

Instead of syncing UAE→Unicorn, **explicitly initialize both CPUs** to a known-good state:

```cpp
// Initialize both CPUs explicitly to Macintosh boot state
void validation_init_cpu_state() {
    // Set both CPUs to identical Macintosh boot state
    uint32_t initial_pc = ROMBaseMac + 0x2a;
    uint32_t initial_a7 = 0x2000;
    uint16_t initial_sr = 0x2700;

    // UAE
    uae_set_pc(initial_pc);
    uae_set_sr(initial_sr);
    uae_set_areg(7, initial_a7);
    for (int i = 0; i < 8; i++) {
        uae_set_dreg(i, 0);
        uae_set_areg(i, 0);
    }
    uae_set_areg(7, initial_a7);  // Restore A7 after loop

    // Unicorn
    unicorn_set_pc(cpu, initial_pc);
    unicorn_set_sr(cpu, initial_sr);
    unicorn_set_areg(cpu, 7, initial_a7);
    for (int i = 0; i < 8; i++) {
        unicorn_set_dreg(cpu, i, 0);
        unicorn_set_areg(cpu, i, 0);
    }
    unicorn_set_areg(cpu, 7, initial_a7);  // Restore A7 after loop

    // Control registers
    unicorn_set_cacr(cpu, 0);
    unicorn_set_vbr(cpu, 0);
}
```

**Benefits**:
- Both CPUs start from **known, identical state**
- Neither CPU's reset function is trusted - we set everything explicitly
- Clear documentation of what "Macintosh boot state" means
- Easy to test different initial states

**Implementation**:
- Call this function in `unicorn_validation_init()` after creating both CPUs
- Remove the sync code from `unicorn_validation_step()`
- Both standalone and dual-CPU modes would then start identically

### Option 2: Validate Initialization Too

Keep the current approach but **add a flag to disable sync**:

```cpp
// Environment variable: DUALCPU_VALIDATE_INIT=1
static bool validate_initialization = false;

void unicorn_validation_init() {
    // ...
    const char *validate_init_env = getenv("DUALCPU_VALIDATE_INIT");
    if (validate_init_env && strcmp(validate_init_env, "1") == 0) {
        validate_initialization = true;
        fprintf(stderr, "[DualCPU: Will validate initialization - no state sync]\n");
    }
}

bool unicorn_validation_step() {
    // ...
    // Only sync if not validating initialization
    if (validation_state.instruction_count == 0 && !validate_initialization) {
        // ... existing sync code ...
    }
}
```

**Benefits**:
- Backward compatible - default behavior unchanged
- Can opt-in to strict validation when debugging
- Useful for finding initialization bugs

**Drawbacks**:
- More complexity
- With validation enabled, would fail on first instruction (expected)
- Doesn't solve the root problem

### Option 3: Reset Both CPUs Identically

Have **both** CPUs call their reset functions, then validate that they match:

```cpp
bool unicorn_validation_init() {
    // Reset both CPUs
    uae_cpu_reset();
    // Unicorn reset via setting registers (no explicit reset function)
    unicorn_set_pc(validation_state.unicorn, 0);
    unicorn_set_sr(validation_state.unicorn, 0x2700);
    for (int i = 0; i < 8; i++) {
        unicorn_set_dreg(validation_state.unicorn, i, 0);
        unicorn_set_areg(validation_state.unicorn, i, 0);
    }

    // Then explicitly set Macintosh-specific boot state on BOTH
    // (so we're not testing generic M68K reset, but Mac boot state)
    uint32_t initial_pc = ROMBaseMac + 0x2a;
    uint32_t initial_a7 = 0x2000;

    uae_set_pc(initial_pc);
    unicorn_set_pc(validation_state.unicorn, initial_pc);

    uae_set_areg(7, initial_a7);
    unicorn_set_areg(validation_state.unicorn, 7, initial_a7);

    // Now validate they match
    if (uae_get_pc() != unicorn_get_pc(validation_state.unicorn)) {
        fprintf(stderr, "ERROR: PC mismatch after reset!\n");
        return false;
    }
    // ... validate other registers ...
}
```

**Benefits**:
- Tests that both reset functions work
- Then explicitly sets Macintosh-specific state
- Clear separation between "M68K reset" and "Macintosh boot state"

**Drawbacks**:
- More verbose
- Still doesn't validate initialization differences (they're overwritten)

## Proposed Implementation Plan

**Immediate fix** (Option 1):

1. Create `validation_init_cpu_state()` function with explicit initialization
2. Call it from `unicorn_validation_init()` after CPU creation
3. Remove sync code from `unicorn_validation_step()` at instruction_count==0
4. Test that dual-CPU validation still works
5. Test that standalone modes now produce identical traces

**Future enhancement** (Option 2):

1. Add `DUALCPU_VALIDATE_INIT` environment variable
2. When enabled, skip sync and validate from first instruction
3. Use this to ensure both CPUs have compatible initialization

## Questions to Answer

1. **Why does UAE initialize PC to ROMBaseMac + 0x2a?**
   - Is this correct for Macintosh boot?
   - Should Unicorn also do this, or should we set it explicitly?

2. **Why does UAE initialize A7 to 0x2000?**
   - Is this the correct initial stack pointer for Macintosh?
   - Where is this value documented?

3. **Should standalone modes also use explicit initialization?**
   - Currently UAE standalone uses `m68k_reset()`
   - Currently Unicorn standalone uses Unicorn's default reset
   - Should both call a common "set up Macintosh boot state" function?

4. **What instruction causes A1 to diverge between #1 and #1000?**
   - This is the active debugging task we were in the middle of
   - Need to use binary search with trace tools to find exact instruction

## Next Steps

1. ✅ **Document current behavior** (this file)
2. ⚠️ **Decide on fix approach** (discuss with user)
3. ⚠️ **Implement chosen fix**
4. ⚠️ **Test that dual-CPU validation still catches real bugs**
5. ⚠️ **Continue finding A1 divergence** (resume binary search debugging)
