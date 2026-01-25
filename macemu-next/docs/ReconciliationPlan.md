# macemu-next Architecture Reconciliation Plan

## Executive Summary

After analyzing the macemu-next architecture, the A-line trap/EmulOp overloading, timer interrupt handling, and comparing with QEMU's Quadra 800 implementation, I've identified the core issues and propose a comprehensive reconciliation plan to address the differences between UAE and Unicorn backends while maintaining the EmulOp system.

## Current Architecture Analysis

### 1. The Platform API Design (Successful)

The Platform API abstraction is **working well**:
- Clean separation between backends
- Runtime backend selection
- Function pointer dispatch for all operations
- No direct coupling between core emulation and CPU backends

**Recommendation**: Keep this design unchanged.

### 2. The A-Line Trap Overloading Issue

**The Problem**:
macemu-next uses two incompatible mechanisms on A-line traps (0xAxxx):

1. **Mac OS A-line traps** (0xAxxx): System calls that need exception handling
   - Must trigger M68K exception (vector 10)
   - Push exception frame
   - Jump to OS trap handler
   - Examples: 0xA247 (SetToolTrap), 0xA9FF (OpenDriver)

2. **BasiliskII EmulOps** (0xAExx repurposed as 0x71xx):
   - ROM patches convert 0xAExx → 0x71xx at patch time
   - BUT: IRQ polling loop patches insert 0xAE29 directly!
   - This creates 0xAE29 as both:
     - An A-line trap that should trigger exception
     - An EmulOp that should execute IRQ polling

### 3. The IRQ Storm in Unicorn

**Root Cause**: The Mac ROM's 60Hz interrupt handler is patched with:
```asm
0x0200a29a: AE29    ; IRQ EmulOp (should be 0x7129!)
0x0200a29c: 4A80    ; TST.L D0
0x0200a29e: 67FA    ; BEQ.S *-4 (back to 0x0200a29a)
```

This tight polling loop:
1. Gets compiled into a single Translation Block by Unicorn's JIT
2. The TB loops internally without returning to check real interrupts
3. Results in millions of EmulOp executions without interrupt checking

### 4. Timer Architecture Issues

**Current Design**:
- Wall-clock based (POSIX `clock_nanosleep`)
- Non-deterministic relative to instruction count
- UAE checks interrupts per-instruction
- Unicorn checks at TB boundaries only

**QEMU's Approach** (from q800.c analysis):
- Uses `m68k_set_irq_level()` for clean interrupt injection
- Hardware interrupt controller simulation (GLUE)
- Proper interrupt priority handling
- Timer events scheduled in virtual time

## Reconciliation Plan

### Phase 1: Fix the A-Line Trap Confusion

#### Step 1.1: Clean ROM Patching
**Problem**: IRQ EmulOp uses 0xAE29 instead of 0x7129

**Solution**: Fix the ROM patcher to use proper EmulOp encoding:

```c
// In rom_patches.cpp line 1043 and 1696
// WRONG:
*wp++ = htons(make_emulop(M68K_EMUL_OP_IRQ));  // Creates 0xAE29

// CORRECT:
*wp++ = htons(0x7129);  // Explicit EmulOp encoding
```

**Impact**:
- Eliminates A-line trap confusion
- IRQ polling becomes pure EmulOp
- No exception handling overhead

#### Step 1.2: Separate A-Line and EmulOp Handling
**Current**: Both handled in same hook_insn_invalid

**Proposed**: Two distinct paths:
```c
// In unicorn_wrapper.c
static bool hook_insn_invalid(...) {
    uint16_t opcode = read_opcode();

    // EmulOps (0x71xx) - fast path, no exceptions
    if ((opcode & 0xFF00) == 0x7100) {
        handle_emulop(opcode);
        advance_pc(2);
        return true;
    }

    // A-line traps (0xAxxx) - trigger M68K exception
    if ((opcode & 0xF000) == 0xA000) {
        trigger_m68k_exception(10, opcode);  // Vector 10
        return true;
    }

    // F-line traps (0xFxxx) - trigger M68K exception
    if ((opcode & 0xF000) == 0xF000) {
        trigger_m68k_exception(11, opcode);  // Vector 11
        return true;
    }

    return false;  // Real illegal instruction
}
```

### Phase 2: Address the IRQ Storm

#### Step 2.1: Force TB Breaks on Polling Loops
**Approach**: Detect the IRQ polling pattern and force TB termination

```c
// In hook_insn_invalid after handling EmulOp
if (opcode == 0x7129) {  // IRQ EmulOp
    // Force TB exit to allow interrupt checking
    uc_emu_stop(uc);
    // Set flag to resume immediately
    cpu->force_resume = true;
}
```

#### Step 2.2: Alternative - Periodic TB Invalidation
**For tight loops that don't hit EmulOps**:

```c
// In hook_block
static int blocks_since_interrupt_check = 0;
if (++blocks_since_interrupt_check > 100) {
    blocks_since_interrupt_check = 0;
    // Force re-entry to execution loop
    uc_emu_stop(uc);
    cpu->check_interrupts = true;
}
```

### Phase 3: Improve Interrupt Architecture

#### Step 3.1: Use QEMU's Native Interrupt Mechanism
**Instead of manual stack frame building**, use Unicorn's built-in QEMU function:

```c
// Export from Unicorn (already exists in unicorn.c):
void uc_m68k_trigger_interrupt(uc_engine *uc, int level, uint8_t vector);

// In unicorn_wrapper.c timer polling:
if (timer_expired) {
    uc_m68k_trigger_interrupt(uc, 1, 25);  // Level 1, autovector 25
}
```

**Benefits**:
- Proper exception frame format
- Correct supervisor mode handling
- Automatic SR mask checking
- No manual stack manipulation

#### Step 3.2: Instruction-Count Based Timer Mode (Testing)
**Add optional deterministic mode for testing**:

```c
// Environment variable: TIMER_MODE=instruction_count
if (timer_mode == TIMER_MODE_INSN_COUNT) {
    if (++instruction_count >= next_timer_count) {
        trigger_timer_interrupt();
        next_timer_count += TIMER_PERIOD_INSNS;
    }
}
```

### Phase 4: JIT Control Mechanisms

#### Step 4.1: Expose TB Size Control
**Add Unicorn API to limit TB size**:

```c
// Proposed Unicorn modification
uc_ctl_set_tb_max_insns(uc, 10);  // Max 10 instructions per TB
```

**Use cases**:
- During IRQ polling sections
- For timing-critical code
- Debug mode

#### Step 4.2: Hook Optimization
**Current**: UC_HOOK_BLOCK called for every TB

**Proposed**: Add UC_HOOK_TIMER for periodic checks:
```c
// Register timer hook (e.g., every 1000 instructions)
uc_hook_add(uc, &hook, UC_HOOK_TIMER, timer_callback, NULL,
            1000);  // Check every 1000 instructions
```

### Phase 5: Validation and Testing

#### Step 5.1: IRQ Storm Test
```bash
# Test IRQ polling performance
EMULATOR_TIMEOUT=10 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver
# Should see <1000 IRQ EmulOps instead of 781,000
```

#### Step 5.2: DualCPU Validation
```bash
# Ensure UAE and Unicorn still match behavior
CPU_BACKEND=dualcpu TIMER_MODE=instruction_count ./build/macemu-next
```

#### Step 5.3: Mac OS Boot Test
```bash
# Full boot test with proper interrupts
CPU_BACKEND=unicorn ./build/macemu-next ~/quadra.rom ~/disk.img
```

## Implementation Priority

### Immediate (Fix Critical Issues)
1. **Fix ROM patcher** - Use 0x7129 instead of 0xAE29 for IRQ EmulOp
2. **Force TB exit on IRQ EmulOp** - Break polling loop
3. **Use native interrupt mechanism** - uc_m68k_trigger_interrupt()

### Short-term (Improve Architecture)
4. Separate A-line and EmulOp paths clearly
5. Add instruction-count timer mode for testing
6. Implement periodic TB invalidation

### Long-term (Optimal Solution)
7. Modify Unicorn to support TB size limits
8. Add specialized timer hooks
9. Consider switching to QEMU directly for better control

## Risks and Mitigations

### Risk 1: Breaking Existing Functionality
**Mitigation**: Keep changes behind feature flags initially
```c
if (getenv("USE_NEW_IRQ_HANDLING")) {
    // New code path
} else {
    // Original code
}
```

### Risk 2: Performance Regression
**Mitigation**: Measure and optimize:
- Profile TB exit overhead
- Tune invalidation frequency
- Cache hot paths

### Risk 3: Unicorn API Limitations
**Mitigation**:
- Work within current API where possible
- Submit patches upstream to Unicorn project
- Maintain fork if necessary

## Success Criteria

1. **IRQ Storm Eliminated**: <1000 IRQ EmulOps per second
2. **Boot Success**: Mac OS boots to desktop
3. **Performance**: Unicorn still 5x+ faster than UAE
4. **Compatibility**: All EmulOps function correctly
5. **Deterministic Testing**: DualCPU mode validates without divergence

## Conclusion

The core issue is the conflation of A-line traps with EmulOps, combined with Unicorn's JIT creating large translation blocks that prevent interrupt checking. The solution requires:

1. **Immediate**: Fix the ROM patcher to use proper EmulOp encoding
2. **Architectural**: Separate exception and EmulOp handling paths
3. **Tactical**: Force TB exits in polling loops
4. **Strategic**: Use native QEMU interrupt mechanisms

This plan addresses both the immediate IRQ storm issue and the longer-term architectural concerns, while maintaining backward compatibility and the benefits of the Platform API design.