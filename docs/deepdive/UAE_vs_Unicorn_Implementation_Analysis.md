# UAE vs Unicorn: Implementation Analysis and Gaps

**Date**: January 21, 2026
**Status**: Current analysis based on extensive testing and bug fixes
**Purpose**: Document the architectural differences, what works, what doesn't, and what needs fixing

---

## Executive Summary

**mac-phoenix** implements two M68K CPU backends:
- **UAE** (WinUAE M68K interpreter) - **Fully functional**, production-ready
- **Unicorn** (QEMU-based JIT compiler) - **Mostly functional** with known limitations

This document analyzes the differences, explains why certain things work in UAE but not Unicorn, and identifies what still needs to be fixed.

---

## Architecture Comparison

### UAE Backend Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ UAE M68K Interpreter (from WinUAE)                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐ │
│  │  Instruction │ →  │   Execute    │ →  │   Update     │ │
│  │    Fetch     │    │  Interpreter │    │   Registers  │ │
│  └──────────────┘    └──────────────┘    └──────────────┘ │
│         │                    │                    │        │
│         │                    │                    │        │
│         ▼                    ▼                    ▼        │
│  ┌──────────────────────────────────────────────────────┐ │
│  │         UAE Internal State Machine                   │ │
│  │  • Direct register access (regs.regs[])            │ │
│  │  • Exception handling (Exception() function)        │ │
│  │  • SPCFLAGS for interrupts/exceptions              │ │
│  │  • Prefetch queue emulation                        │ │
│  │  • Cycle-accurate timing (optional)                │ │
│  └──────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### Unicorn Backend Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Unicorn M68K JIT (from QEMU)                                │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐ │
│  │  Basic Block │ →  │  QEMU TCG    │ →  │  Host Code   │ │
│  │  Translation │    │  JIT Compile │    │  Execution   │ │
│  └──────────────┘    └──────────────┘    └──────────────┘ │
│         │                    │                    │        │
│         │                    │                    │        │
│         ▼                    ▼                    ▼        │
│  ┌──────────────────────────────────────────────────────┐ │
│  │           Unicorn Hook System                        │ │
│  │  • UC_HOOK_BLOCK (basic block boundaries)          │ │
│  │  • UC_HOOK_INTR (interrupt/exception trigger)      │ │
│  │  • UC_HOOK_INSN_INVALID (illegal instructions)     │ │
│  │  • API-based register access (uc_reg_read/write)   │ │
│  │  • ⚠️ LIMITED: Cannot change PC from interrupt hooks│ │
│  └──────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## Feature Comparison Matrix

| Feature | UAE | Unicorn | Notes |
|---------|-----|---------|-------|
| **Normal Instructions** | ✅ Full | ✅ Full | Both execute M68K correctly |
| **Performance** | 🐢 Interpreter | ⚡ JIT (~5-10x faster) | Unicorn's advantage |
| **EmulOps (0x71xx)** | ✅ Native | ✅ Via UC_HOOK_INSN_INVALID | Both work |
| **A-line EmulOps (0xAE00-0xAE3F)** | ✅ Native | ✅ Via UC_HOOK_INTR | Both work |
| **Mac OS A-line Traps (0xA000+)** | ✅ Full Support | ✅ **WORKING** (deferred updates) | Fixed March 2026 |
| **Mac OS F-line Traps (0xF000+)** | ✅ Full Support | ✅ **WORKING** (deferred updates) | Fixed March 2026 |
| **Interrupts (Detection)** | ✅ SPCFLAGS | ✅ UC_HOOK_BLOCK polling | Both work |
| **Interrupts (Execution)** | ✅ Native Exception() | ✅ Manual M68K frames | Both work |
| **Exception Simulation** | ✅ Full Control | ✅ Via deferred register updates | PC limitation overcome |
| **RTE Instruction** | ✅ Works | ✅ Works (patched) | Fixed in cpu-exec.c |
| **VBR Register** | ✅ Native | ✅ Added via custom API | Had to add UC_M68K_REG_CR_VBR |
| **SR Lazy Flags** | ✅ Correct | ⚠️ Some bugs | Known Unicorn issue |
| **Prefetch Queue** | ✅ Optional | ❌ Not modeled | Not critical for macemu |
| **Cycle Counting** | ✅ Accurate | ❌ Approximate | Not critical for macemu |
| **ROM Patching** | ✅ Direct | ✅ Via memory copy | Both work |
| **Memory Access** | ✅ Direct pointers | ✅ API-based | Different approach |

---

## Unicorn PC Change Issue -- SOLVED (March 2026)

### The Original Problem (January 2026)

Unicorn cannot change PC from `UC_HOOK_INTR` callbacks -- QEMU's `exception_next_eip` overwrites any PC changes after the hook returns. This was initially thought to be a fundamental blocker.

### The Solution: Deferred Register Updates

Instead of writing registers directly inside the hook, we **defer** all register updates and apply them at the next `hook_block()` boundary:

1. `hook_interrupt()` queues register changes (including PC) in deferred arrays
2. `hook_interrupt()` returns without calling `uc_emu_stop()` or `uc_reg_write()`
3. QEMU restores PC from `exception_next_eip` (harmless -- we'll overwrite it)
4. At the next basic block boundary, `hook_block()` fires
5. `apply_deferred_updates_and_flush()` applies all queued register writes
6. PC is now set correctly, execution continues from the right address

**Result**: All A-line/F-line traps work. Both UAE and Unicorn populate 87 OS trap table entries and reach identical boot state.

### Historical Attempts (That Failed)

From commits `9464afa4` and `32a6926b`:

1. ❌ `uc_ctl_remove_cache()` + `uc_reg_write()` inside hook
2. ❌ `uc_emu_stop()` to break execution
3. ❌ Skip instruction instead of jumping to handler

All failed because they tried to modify PC *inside* the hook. The deferred approach sidesteps the issue entirely.

---

## What Works in Both

### ✅ Normal M68K Instructions

Both UAE and Unicorn execute standard M68K instructions correctly:
- Arithmetic (ADD, SUB, MUL, DIV)
- Logic (AND, OR, XOR, NOT)
- Shifts/Rotates (ASL, LSR, ROL, ROR)
- Branches (BRA, BCC, BSR)
- Memory access (MOVE, LEA, PEA)
- Stack operations (LINK, UNLK, MOVEM)
- Control flow (JSR, RTS, JMP)

**Validation**: Dual-CPU mode has validated 514,000+ instructions with matching results.

### ✅ EmulOps (0x71xx Illegal Instructions)

Both backends handle BasiliskII EmulOps correctly:

**UAE**: Native support via `op_illg()` handler
**Unicorn**: Via `UC_HOOK_INSN_INVALID` hook

Implementation in [unicorn_wrapper.c:208-294](../src/cpu/unicorn_wrapper.c#L208-L294):
```c
static bool hook_insn_invalid(uc_engine *uc, void *user_data) {
    uint16_t opcode = read_opcode_at_pc();
    if ((opcode & 0xFF00) == 0x7100) {
        // Execute EmulOp
        extract_registers_from_unicorn();
        EmulOp_C(opcode, &regs);
        restore_registers_to_unicorn();
        advance_pc_by_2();
        return true;  // Handled
    }
    return false;  // Not handled
}
```

**Status**: ✅ Fully working in both backends

### ✅ A-line EmulOps (0xAE00-0xAE3F)

BasiliskII-specific A-line instructions for emulation services.

**UAE**: Native support via `op_illg()` handler
**Unicorn**: Via `UC_HOOK_INTR` hook

**Why these work in Unicorn** (unlike other A-line traps):
- Don't require PC changes
- Just read parameters from instruction/registers
- Execute emulation function
- Return control (PC auto-advances)

Implementation in [unicorn_wrapper.c:350-400](../src/cpu/unicorn_wrapper.c#L350-L400):
```c
static uint32_t hook_interrupt(uc_engine *uc, uint32_t int_no) {
    uint16_t opcode = read_opcode_at_pc();
    if ((opcode & 0xFFC0) == 0xAE00) {
        // A-line EmulOp
        uint8_t selector = opcode & 0x3F;
        execute_emulop_aline(selector);
        // PC auto-advances, no jump needed
        return 0;
    }
}
```

**Examples**:
- `0xAE00` - PATCH_BOOT_GLOBS
- `0xAE01` - Other BasiliskII-specific operations

**Status**: ✅ Fully working in both backends

### ✅ Interrupt Detection

Both backends can detect when interrupts should fire:

**UAE**:
- Polls timer via `SPCFLAG_INT`
- Checks every instruction
- Sets interrupt flag when timer expires

**Unicorn**:
- Polls timer in `UC_HOOK_BLOCK` (every ~100 instructions)
- Sets `g_pending_interrupt_level`
- Much more efficient than per-instruction polling

Implementation in [unicorn_wrapper.c:86-98](../src/cpu/unicorn_wrapper.c#L86-L98):
```c
static void hook_block(uc_engine *uc, uint64_t address, uint32_t size) {
    // Poll every 100 instructions (like UAE)
    if (total_instructions % 100 < size) {
        poll_timer_interrupt();  // Sets g_pending_interrupt_level
    }
}
```

**Status**: ✅ Both work, Unicorn is more efficient

### ✅ RTE (Return from Exception)

Both backends handle RTE correctly **after patching Unicorn**.

**Original problem**: Unicorn had a bug in batch execution mode:
- RTE would clear `exception_index`
- Then batch execution would immediately set it again
- Infinite loop!

**Fix** (commit `da1383a7`): Patched Unicorn's `cpu-exec.c`:
```c
// Handle EXCP_RTE *before* clearing exception_index
if (cpu->exception_index == EXCP_RTE) {
    // Process RTE
    restore_state_from_stack();
}
cpu->exception_index = -1;  // Clear AFTER handling
```

**Status**: ✅ Both work correctly

---

## What Only Works in UAE

### ❌ Mac OS A-line Traps (0xA000-0xAFFF, excluding EmulOps)

**Examples**:
- `0xA05D` - Various Mac OS Toolbox calls
- `0xA247` - SetToolTrap
- `0xA9FF` - GetCursorAddr

**Why UAE works**:
```c
void Exception(int vector_nr) {
    // Build stack frame
    push_word(vector_nr * 4);
    push_long(current_pc);
    push_word(sr);

    // Jump to handler
    uint32_t handler = read_long(vbr + vector_nr * 4);
    m68k_setpc(handler);  // ← This works!
}
```

**Why Unicorn fails**: PC change is ignored (see limitation above)

**Current status**:
- UAE: ✅ Full support
- Unicorn: ❌ Hangs on execution
- **Workaround**: Execute on UAE, sync to Unicorn

### ❌ Mac OS F-line Traps (0xF000-0xFFFF)

**Purpose**: FPU/coprocessor instructions (emulated by Mac OS)

**Same issue as A-line traps**:
- UAE: ✅ Works via Exception() mechanism
- Unicorn: ❌ Cannot change PC to handler

**Current status**:
- UAE: ✅ Full support
- Unicorn: ❌ Not functional standalone

---

## What Needs Fixing

### Priority 1: Critical Issues

#### 1. Unicorn PC Change Limitation

**Problem**: Cannot simulate M68K exceptions properly

**Possible solutions**:

**A. Fork and Patch Unicorn** (Most complete)
```c
// In Unicorn's cpu-exec.c, add:
if (env->allow_pc_override) {
    // Don't overwrite PC after interrupt hook
    // Let the hook's PC change persist
}
```

**Pros**: Would fix the issue completely
**Cons**: Maintenance burden, breaks Unicorn updates

**B. Use Unicorn's Native Exception Mechanism**
```c
// Instead of trying to simulate exceptions in hooks,
// use Unicorn's built-in exception handling:
uc_m68k_trigger_interrupt(uc, level, vector);
```

**Status**: Partially implemented but needs testing
**Issue**: May not match UAE's exact behavior

**C. Accept Limitation, Use Hybrid Approach**
```c
// Current workaround:
if (is_aline_trap(opcode) && !is_emulop(opcode)) {
    execute_on_uae_only();
    sync_state_to_unicorn();
}
```

**Pros**: Works today
**Cons**: Defeats dual-CPU validation purpose

**Recommendation**: Try option B (native Unicorn exceptions) first, fall back to C if needed.

---

### Priority 2: Performance and Correctness

#### 2. SR Lazy Flag Evaluation

**Problem**: Unicorn sometimes doesn't update condition codes correctly

**Evidence** (from early testing):
```
Instruction: SUB.B D0,(A0) where D0=0, A0=0, mem[0]=0
Expected: 0 - 0 = 0 → Z flag set (SR=0x2704)
UAE result: 0x2704 ✓
Unicorn result: 0x2700 ✗ (Z flag not set)
```

**Impact**: Minor - affects condition code flags, not data results

**Current status**:
- Known Unicorn bug (upstream issue)
- Doesn't significantly impact Mac OS execution
- Monitor for critical cases

**Recommendation**: Document but don't fix (upstream issue)

---

#### 3. Deferred SR Updates

**Problem**: Some EmulOps modify SR, but change must be deferred until after hook returns

**Example**: RESET EmulOp (0x7103)
```c
// RESET clears interrupt mask in SR
// But if we write SR immediately, Unicorn overwrites it
// Solution: Defer until UC_HOOK_BLOCK
```

**Implementation** [unicorn_wrapper.c:73-79](../src/cpu/unicorn_wrapper.c#L73-L79):
```c
void unicorn_defer_sr_update(void *cpu, uint16_t new_sr) {
    cpu->has_deferred_sr_update = true;
    cpu->deferred_sr_value = new_sr;
}

// Applied in hook_block() before interrupt check
```

**Status**: ✅ Implemented and working

**Recommendation**: Continue using deferred updates as needed

---

#### 4. Timer Interrupt Timing

**Problem**: UAE and Unicorn fire interrupts at different instruction counts

**Root cause**: Wall-clock time vs instruction count
- Timer fires based on **real time** (60 Hz)
- UAE is slow (interpreter) → 100 instructions takes longer → interrupt fires "earlier" (in instruction count)
- Unicorn is fast (JIT) → 100 instructions takes shorter time → interrupt fires "later" (in instruction count)

**Evidence** [InterruptTimingAnalysis.md](InterruptTimingAnalysis.md):
```
UAE interrupt fires at: ~29,518 instructions
Unicorn interrupt fires at: ~45,000+ instructions
Reason: JIT executes faster, reaches 16.67ms later in instruction count
```

**Impact**: Non-deterministic dual-CPU validation

**Status**: ✅ Understood, not a bug

**Recommendation**: Accept non-determinism, use functional testing instead of exact trace matching

---

### Priority 3: Nice to Have

#### 5. Cycle-Accurate Timing

**Feature**: UAE can emulate M68K cycle timing accurately

**Status**:
- UAE: ✅ Available (currently disabled for speed)
- Unicorn: ❌ Not available (approximate timing only)

**Impact**: Not critical for mac-phoenix (focus is on correctness, not timing accuracy)

**Recommendation**: Keep disabled for performance

---

#### 6. Prefetch Queue Emulation

**Feature**: Real M68K has instruction prefetch queue

**Status**:
- UAE: ✅ Can model it (currently disabled)
- Unicorn: ❌ Not modeled

**Impact**: Minimal - only affects self-modifying code edge cases

**Recommendation**: Keep disabled

---

## Implementation Gaps Summary

### Fully Implemented ✅

| Feature | UAE | Unicorn | Implementation |
|---------|-----|---------|---------------|
| Normal instructions | ✅ | ✅ | Core M68K execution |
| EmulOps (0x71xx) | ✅ | ✅ | UC_HOOK_INSN_INVALID |
| A-line EmulOps (0xAE00-0xAE3F) | ✅ | ✅ | UC_HOOK_INTR |
| Interrupt detection | ✅ | ✅ | UC_HOOK_BLOCK polling |
| RTE instruction | ✅ | ✅ | Patched cpu-exec.c |
| VBR register | ✅ | ✅ | Custom register API |
| ROM patching | ✅ | ✅ | Memory copy |

### Broken/Limited ❌

| Feature | UAE | Unicorn | Blocker |
|---------|-----|---------|---------|
| Mac OS A-line traps | ✅ | ❌ | Cannot change PC from hooks |
| Mac OS F-line traps | ✅ | ❌ | Cannot change PC from hooks |
| Exception simulation | ✅ | ❌ | Cannot change PC from hooks |
| Full ROM boot standalone | ✅ | ❌ | Needs A-line trap support |

### Not Critical ⚠️

| Feature | UAE | Unicorn | Impact |
|---------|-----|---------|--------|
| SR lazy flags | ✅ | ⚠️ | Minor - upstream bug |
| Cycle timing | ✅ | ❌ | Not needed |
| Prefetch queue | ✅ | ❌ | Not needed |
| Interrupt timing | ⏰ | ⏰ | Non-deterministic (expected) |

---

## Recommendations

### Short-Term (Current Workarounds)

1. ✅ **Continue using UAE as primary backend** for full Mac emulation
2. ✅ **Use Unicorn for validation** with UAE-execute workaround for traps
3. ✅ **Document limitations clearly** (done in this doc!)
4. ✅ **Focus on normal instruction validation** (514k+ already validated)

### Medium-Term (Improvements)

1. 🔧 **Try Unicorn native exception handling** (`uc_m68k_trigger_interrupt()`)
   - Test if it matches UAE behavior closely enough
   - May enable A-line/F-line without PC change hack

2. 🔧 **Investigate hybrid approach refinement**
   - Minimize state sync overhead
   - Optimize RAM sync (only dirty pages?)

3. 📚 **Add more validation tests**
   - Test specific instruction sequences
   - Validate more EmulOps
   - Test edge cases

### Long-Term (Architectural Options)

1. 🔨 **Fork and patch Unicorn** (if full standalone Unicorn is critical)
   - Add `allow_pc_override` flag
   - Submit upstream PR?

2. 🔄 **Alternative CPU engine** (if Unicorn limitations become blocking)
   - Musashi (pure interpreter, predictable)
   - QEMU directly (heavyweight)
   - Custom M68K JIT (huge effort)

3. ✅ **Accept hybrid architecture** as final design
   - UAE handles exceptions
   - Unicorn handles normal execution
   - Sync at exception boundaries
   - **This may be the optimal solution**

---

## Conclusion

**UAE** is fully functional and production-ready. It handles everything correctly.

**Unicorn** works well for:
- ✅ Normal M68K instructions (~5-10x faster than UAE)
- ✅ EmulOps (0x71xx)
- ✅ A-line EmulOps (0xAE00-0xAE3F)
- ✅ Interrupt detection and triggering

**Unicorn doesn't work for**:
- ❌ Mac OS A-line/F-line traps (PC change limitation)
- ❌ Full standalone ROM boot (depends on above)

**Current approach**: Hybrid execution with UAE handling traps works and may be the best long-term solution.

**Core validation goal** (dual-CPU validation of normal instructions) is **achieved** with 514,000+ validated instructions.

---

**See Also**:
- [cpu/UnicornQuirks.md](cpu/UnicornQuirks.md) - Detailed quirks
- [cpu/ALineAndFLineStatus.md](cpu/ALineAndFLineStatus.md) - Trap handling status
- [cpu/UaeQuirks.md](cpu/UaeQuirks.md) - UAE specifics
- [InterruptTimingAnalysis.md](InterruptTimingAnalysis.md) - Timing analysis
