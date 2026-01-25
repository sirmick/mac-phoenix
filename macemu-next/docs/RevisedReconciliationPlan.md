# Revised macemu-next Architecture Reconciliation Plan

## Critical Discoveries from Deep Analysis

After thorough analysis of the documentation and code, several critical issues have been identified that fundamentally affect the reconciliation strategy:

### 1. EmulOps Cannot Directly Modify Registers in Unicorn

**The Core Problem**: When EmulOps are handled as 0x71xx illegal instructions in Unicorn:
- The `UC_HOOK_INSN_INVALID` callback fires
- EmulOp handler is called and modifies registers
- **BUT**: Register modifications don't persist after the hook returns
- This is because Unicorn's JIT has already compiled the translation block

**Current Workaround**: Deferred register updates
```c
// EmulOp handler defers register updates
unicorn_defer_sr_update(unicorn_cpu, new_sr);
unicorn_defer_dreg_update(unicorn_cpu, 0, new_d0);

// Updates are applied at next hook_block boundary
static void hook_block(...) {
    apply_deferred_updates_and_flush(cpu, uc, "hook_block");
}
```

**Why This Is Problematic**:
- Register updates are delayed until next TB boundary
- Tight loops checking register values may not see updates in time
- Requires complex deferred update tracking

### 2. A-Line Traps Are Overloaded and Broken

**The Confusion**:
1. **Mac OS A-line traps** (0xA000-0xAFFF): Should trigger M68K exception (vector 10)
2. **BasiliskII A-line EmulOps** (0xAE00-0xAE3F): Should execute emulator functions
3. **The IRQ EmulOp Bug**: ROM patcher creates 0xAE29 instead of 0x7129!

**Current State**:
- Unicorn treats 0xAExx as A-line exceptions, triggering `UC_HOOK_INTR`
- The hook tries to convert 0xAE29 → 0x7129 and handle as EmulOp
- This adds unnecessary overhead and complexity

### 3. Unicorn Cannot Change PC from Interrupt Hooks

**Fundamental Limitation** (Unicorn issue #1027):
- After `UC_HOOK_INTR` returns, Unicorn overwrites PC with saved value
- This makes it impossible to implement proper exception handling
- Mac OS traps cannot jump to exception handlers
- Result: ROM boot hangs on non-EmulOp A-line traps

### 4. IRQ Storm Due to JIT Translation Blocks

**The Tight Polling Loop**:
```asm
0x0200a29a: AE29    ; IRQ EmulOp (WRONG! Should be 0x7129)
0x0200a29c: 4A80    ; TST.L D0
0x0200a29e: 67FA    ; BEQ.S *-4 (loop back)
```

**The Problem**:
- This 3-instruction loop gets compiled into a single TB
- The TB executes millions of times internally
- No opportunity to check real timer interrupts
- Results in 781,000+ EmulOp executions per 10 seconds

## Revised Reconciliation Strategy

### Phase 1: Fix Critical Bugs (Immediate)

#### 1.1 Fix the IRQ EmulOp Encoding

**File**: `src/core/rom_patches.cpp`
**Lines**: 1043, 1696

**Current (WRONG)**:
```c
*wp++ = htons(make_emulop(M68K_EMUL_OP_IRQ));  // Produces 0xAE29
```

**Fixed**:
```c
*wp++ = htons(0x7129);  // Correct EmulOp encoding
```

**Impact**:
- Eliminates A-line exception overhead
- IRQ polling becomes pure illegal instruction (faster)
- Reduces confusion in hook handling

#### 1.2 Force TB Exit on IRQ EmulOp

**File**: `src/cpu/unicorn_wrapper.c`
**Location**: In `hook_interrupt` after handling 0x7129

```c
if (legacy_opcode == 0x7129) {  // IRQ EmulOp
    /* Force TB exit to allow interrupt checking */
    uc_emu_stop(uc);
    cpu->force_immediate_resume = true;  // Flag for outer loop
}
```

**Impact**:
- Breaks the tight polling loop
- Allows timer interrupt checking
- Should reduce IRQ EmulOps from 781K to <1K per 10 seconds

### Phase 2: Simplify EmulOp Handling

#### 2.1 Use 0x71xx Consistently

**Stop using A-line EmulOps (0xAExx) entirely**:
1. Audit all ROM patches
2. Convert all 0xAExx → 0x71xx at patch time
3. Remove A-line → EmulOp conversion in hooks

**Benefits**:
- Clean separation: 0x71xx = EmulOps, 0xAxxx = Mac traps
- Simpler hook logic
- Less overhead

#### 2.2 Handle EmulOps After uc_emu_start()

**Instead of using hooks**, handle EmulOps in the error path:

```c
while (running) {
    uc_err err = uc_emu_start(cpu->uc, pc, 0, 0, count);

    if (err == UC_ERR_INSN_INVALID) {
        uint16_t opcode;
        uc_mem_read(cpu->uc, pc, &opcode, 2);

        if ((opcode & 0xFF00) == 0x7100) {
            // Handle EmulOp
            platform.emulop_handler(opcode);

            // Update registers IMMEDIATELY (not deferred)
            for (int i = 0; i < 8; i++) {
                uint32_t d = platform.cpu_get_dreg(i);
                uc_reg_write(cpu->uc, UC_M68K_REG_D0 + i, &d);
            }

            // Advance PC and continue
            pc += 2;
            uc_reg_write(cpu->uc, UC_M68K_REG_PC, &pc);
            continue;
        }
    }
}
```

**Benefits**:
- Register updates happen immediately
- No deferred update complexity
- Clear control flow

### Phase 3: Accept Architectural Limitations

#### 3.1 A-Line/F-Line Traps Won't Work in Unicorn

**Reality**: Due to the PC modification limitation, true Mac OS trap handling is impossible in Unicorn.

**Implications**:
- Unicorn can only run ROM code that uses EmulOps
- Full Mac OS boot requires UAE
- DualCPU validation must use UAE for trap execution

**Decision**: Document this clearly and focus on what works.

#### 3.2 Use UAE as Primary Backend

**Recommendation**:
- UAE: Production backend for full Mac emulation
- Unicorn: Validation and performance testing only
- DualCPU: Development tool for finding bugs

### Phase 4: Alternative Approaches

#### 4.1 Consider QEMU Direct Integration

**Instead of Unicorn**, integrate QEMU's M68K emulation directly:

**Pros**:
- Full control over execution loop
- Can check interrupts between TBs properly
- Native `m68k_set_irq_level()` support
- Proper exception handling

**Cons**:
- Much more complex integration
- Larger codebase
- More maintenance

**How QEMU Does It Right** (from q800.c):
```c
// Hardware triggers interrupt
m68k_set_irq_level(cpu, level, vector);

// QEMU's main loop checks between TBs
while (!cpu->exit_request) {
    tb = tb_find(cpu, pc);
    cpu_exec_tb(tb);

    // Check interrupts HERE, between TBs
    if (cpu_handle_interrupt(cpu)) {
        // Interrupt delivered
    }
}
```

#### 4.2 Patch Unicorn Source

**Fork and modify Unicorn** to:
1. Not overwrite PC after interrupt hooks
2. Check interrupts between TB execution
3. Provide TB size control API

**Pros**: Would fix all issues
**Cons**: Maintenance burden, diverges from upstream

### Phase 5: Optimization Strategies

#### 5.1 Adaptive TB Size Control

**For different code sections**:
```c
// Tight polling loops: Small TBs
if (pc >= 0x0200a000 && pc <= 0x0200b000) {
    uc_ctl_set_tb_max_insns(uc, 3);  // Force small TBs
} else {
    uc_ctl_set_tb_max_insns(uc, 100);  // Normal TBs
}
```

#### 5.2 Periodic Forced Exits

**Every N blocks, force interrupt check**:
```c
static int blocks_since_check = 0;
if (++blocks_since_check > 50) {
    blocks_since_check = 0;
    uc_emu_stop(uc);
    cpu->check_interrupts = true;
}
```

## Implementation Priority

### Immediate (This Week)
1. ✅ Fix IRQ EmulOp encoding (0xAE29 → 0x7129)
2. ✅ Force TB exit on IRQ polling
3. ✅ Test IRQ storm reduction

### Short Term (This Month)
4. Audit all ROM patches for 0xAExx usage
5. Convert to consistent 0x71xx encoding
6. Simplify hook architecture

### Medium Term (Next Quarter)
7. Evaluate QEMU direct integration
8. Consider Unicorn fork if needed
9. Optimize TB management

### Long Term
10. Full architectural redesign if needed
11. Custom M68K JIT implementation (last resort)

## Success Metrics

### Must Have (Critical)
- ✅ IRQ storm eliminated (<1000 IRQ EmulOps/sec)
- ✅ EmulOps execute correctly
- ✅ Timer interrupts delivered

### Should Have (Important)
- ⚠️ Clean separation of EmulOps and Mac traps
- ⚠️ Simplified hook architecture
- ⚠️ Better performance than UAE

### Nice to Have (Future)
- ❌ Full Mac OS trap support in Unicorn
- ❌ Complete ROM boot in Unicorn standalone
- ❌ Perfect UAE/Unicorn parity

## Risk Mitigation

### Risk: Breaking Existing Functionality
**Mitigation**:
- Test each change in isolation
- Keep old code paths available via flags
- Extensive logging and debugging

### Risk: Performance Regression
**Mitigation**:
- Profile before and after changes
- Optimize hot paths
- Accept that correctness > speed

### Risk: Architectural Dead End
**Mitigation**:
- Keep UAE as fallback
- Document limitations clearly
- Consider alternative emulators

## Conclusion

The fundamental issues are:

1. **EmulOp register modification** requires workarounds (deferred updates or error path handling)
2. **A-line trap confusion** due to overloading (0xAExx used for both)
3. **IRQ storm** from incorrect encoding (0xAE29 vs 0x7129)
4. **Unicorn limitations** prevent proper exception handling

The immediate fix is simple: correct the IRQ EmulOp encoding and force TB exits. The longer-term solution requires accepting Unicorn's limitations and potentially moving to direct QEMU integration for full functionality.

**Key Insight**: We've been trying to make Unicorn do something it fundamentally cannot do (PC modification in hooks). Instead of fighting this, we should work within its constraints and use UAE for full Mac emulation while Unicorn serves as a fast validation tool.