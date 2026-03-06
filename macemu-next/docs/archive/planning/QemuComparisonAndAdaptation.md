# QEMU vs Unicorn vs macemu-next: Architecture Comparison and Adaptation Strategy

## Executive Summary

After analyzing QEMU's architecture, it's clear why using QEMU wholesale is "too developed to hack" - it's a complete system emulator with complex subsystems. However, we can borrow specific architectural patterns and minimal code snippets to fix macemu-next's issues without the full QEMU overhead.

## How QEMU Handles Our Problem Areas

### 1. The Main Execution Loop

**QEMU's Approach** (`cpu-exec.c`):
```c
// Simplified from cpu_exec_loop() at line 935
static int cpu_exec_loop(CPUState *cpu) {
    while (!cpu_handle_exception(cpu, &ret)) {
        while (!cpu_handle_interrupt(cpu, &last_tb)) {  // ← CHECK INTERRUPTS
            // Find or generate TB
            tb = tb_lookup(cpu, state);
            if (!tb) {
                tb = tb_gen_code(cpu, state);
            }

            // Execute TB
            cpu_loop_exec_tb(cpu, tb, pc, &last_tb, &tb_exit);
        }
    }
}
```

**Key Insight**: QEMU checks interrupts **between every TB execution**, not during or after.

**Unicorn's Problem**: As a library, Unicorn only returns control after `uc_emu_start()`, missing the inter-TB interrupt check opportunity.

### 2. Translation Block Size Control

**QEMU's TB Generation** (`translator.c` line 157-202):
```c
while (true) {
    ops->translate_insn(db, cpu);

    if (db->is_jmp != DISAS_NEXT) {
        break;  // Control flow change - end TB
    }

    if (tcg_op_buf_full() || db->num_insns >= db->max_insns) {
        db->is_jmp = DISAS_TOO_MANY;
        break;  // TB size limit reached
    }
}
```

**TB Termination Triggers**:
1. Control flow instructions (branches, calls, returns)
2. Reaching `max_insns` limit
3. TCG buffer full
4. Page boundary crossed
5. Special cases (I/O, exceptions)

**For Mac IRQ Polling Loop**:
```asm
0x0200a29a: AE29    ; Should terminate TB here (exception)
0x0200a29c: 4A80    ; TST.L D0
0x0200a29e: 67FA    ; BEQ.S *-4 (backward branch should terminate)
```
QEMU would naturally break this into separate TBs, but Unicorn doesn't.

### 3. Exception Handling

**QEMU's M68K Exception** (`op_helper.c`):
```c
static void m68k_interrupt_all(CPUM68KState *env, int is_hw) {
    // Build exception frame
    do_stack_frame(env, &sp, format, oldsr, addr, env->pc);

    // Jump to vector
    env->pc = cpu_ldl_be_mmuidx_ra(env, env->vbr + vector,
                                    MMU_KERNEL_IDX, 0);
}
```

**QEMU's A-line/F-line Detection** (`translate.c`):
```c
DISAS_INSN(undef_mac) {
    gen_exception(s, s->base.pc_next, EXCP_LINEA);  // 0xAxxx
}

DISAS_INSN(undef_fpu) {
    gen_exception(s, s->base.pc_next, EXCP_LINEF);  // 0xFxxx
}
```

**Key**: QEMU generates exception-raising code during translation, not in hooks.

### 4. Interrupt Injection

**QEMU's Clean Design** (`q800-glue.c`):
```c
// Hardware triggers interrupt
if ((s->ipr >> i) & 1) {
    m68k_set_irq_level(s->cpu, i + 1, i + 25);
}

// In CPU execution:
bool m68k_cpu_exec_interrupt(CPUState *cs, int interrupt_request) {
    if (interrupt_request & CPU_INTERRUPT_HARD
        && ((env->sr & SR_I) >> SR_I_SHIFT) < env->pending_level) {
        // Take interrupt
        do_interrupt_m68k_hardirq(env);
        return true;
    }
}
```

## What We Can Adapt Without Full QEMU

### Solution 1: Mini Execution Loop Around Unicorn

**Concept**: Wrap Unicorn's `uc_emu_start()` in our own QEMU-style loop.

```c
// Our own "cpu_exec_loop" around Unicorn
int macemu_cpu_exec(UnicornCPU *cpu) {
    while (running) {
        // Check interrupts BEFORE executing TB (like QEMU)
        if (check_pending_interrupts(cpu)) {
            deliver_interrupt(cpu);
            continue;
        }

        // Execute limited instructions (small "TB")
        uc_err err = uc_emu_start(cpu->uc, pc, 0, 0, 10);  // Max 10 insns

        // Handle errors (EmulOps, exceptions)
        if (err == UC_ERR_INSN_INVALID) {
            handle_invalid_insn(cpu);
        }
    }
}
```

**Benefits**:
- Interrupt checking between "TBs"
- Control over execution granularity
- No Unicorn source modification

### Solution 2: Forced TB Breaks Using QEMU Logic

**Borrow QEMU's TB termination conditions**:

```c
// Detect patterns that should end TBs
bool should_break_tb(uint16_t opcode, uint32_t pc, uint32_t target) {
    // 1. Backward branches (like QEMU)
    if (is_branch(opcode) && target < pc) {
        return true;  // Loops should break TB
    }

    // 2. Exception-generating instructions
    if ((opcode & 0xF000) == 0xA000 ||  // A-line
        (opcode & 0xF000) == 0xF000) {  // F-line
        return true;
    }

    // 3. System instructions
    if (opcode == 0x4E73 ||  // RTE
        opcode == 0x4E75) {   // RTS
        return true;
    }

    return false;
}

// In execution:
if (should_break_tb(opcode, pc, target)) {
    uc_emu_stop(cpu->uc);
}
```

### Solution 3: Custom A-Line Handler (QEMU-Inspired)

**Instead of hooks, pre-process the ROM**:

```c
// Pre-scan ROM for A-line traps and patch them
void patch_aline_traps(uint8_t *rom, size_t size) {
    for (size_t i = 0; i < size - 2; i += 2) {
        uint16_t opcode = (rom[i] << 8) | rom[i+1];

        if ((opcode & 0xFFC0) == 0xAE00) {  // A-line EmulOp
            // Convert to illegal instruction
            rom[i] = 0x71;
            rom[i+1] = opcode & 0x3F;
        } else if ((opcode & 0xF000) == 0xA000) {  // Mac trap
            // Replace with JSR to our handler
            // This is complex but doable
        }
    }
}
```

### Solution 4: Minimal QEMU Component Integration

**Extract just the interrupt delivery mechanism**:

```c
// Borrow minimal code from QEMU's m68k_set_irq_level
void trigger_interrupt_qemu_style(UnicornCPU *cpu, int level) {
    // This is QEMU's approach, adapted for Unicorn
    uint32_t sr;
    uc_reg_read(cpu->uc, UC_M68K_REG_SR, &sr);

    int current_ipl = (sr >> 8) & 7;
    if (level > current_ipl) {
        // Build exception frame (from QEMU's do_stack_frame)
        uint32_t sp;
        uc_reg_read(cpu->uc, UC_M68K_REG_A7, &sp);

        sp -= 2;
        uc_mem_write(cpu->uc, sp, &sr, 2);  // Push SR
        sp -= 4;
        uc_mem_write(cpu->uc, sp, &pc, 4);  // Push PC

        // Update SR with new IPL
        sr = (sr & 0xF8FF) | (level << 8);
        uc_reg_write(cpu->uc, UC_M68K_REG_SR, &sr);

        // Jump to vector
        uint32_t vector_addr = VBR + (24 + level) * 4;
        uint32_t handler;
        uc_mem_read(cpu->uc, vector_addr, &handler, 4);
        uc_reg_write(cpu->uc, UC_M68K_REG_PC, &handler);
    }
}
```

## Specific Components We Should Adapt

### From QEMU's Architecture

1. **Execution Loop Pattern** - Check interrupts between TB executions
2. **TB Break Conditions** - Backward branches, exceptions, system calls
3. **Exception Frame Building** - Proper M68K stack frame format
4. **Interrupt Priority Logic** - IPL masking and comparison

### Code We Can Literally Copy (Minimal)

1. **Exception frame formats** from `op_helper.c:do_stack_frame()`
2. **IPL comparison logic** from `m68k_cpu_exec_interrupt()`
3. **Vector table reading** from `m68k_interrupt_all()`
4. **SR manipulation** patterns

### What NOT to Take from QEMU

1. **TCG (Tiny Code Generator)** - Too complex, Unicorn has its own
2. **Memory subsystem** - Unicorn's is simpler and sufficient
3. **Device emulation** - We have our own
4. **QEMU's object model** - Overly complex for our needs
5. **Multi-threading support** - Not needed

## Recommended Hybrid Approach

### Phase 1: Immediate Fixes

1. **Fix EmulOp encoding** (0xAE29 → 0x7129)
2. **Add execution loop** around `uc_emu_start()`
3. **Limit instruction count** per call (10-50 insns)

### Phase 2: QEMU-Inspired Improvements

4. **Copy interrupt delivery** code from QEMU
5. **Implement TB break detection** for backward branches
6. **Add proper exception frame** building

### Phase 3: Optimization

7. **Dynamic TB sizing** - Small for loops, large for linear code
8. **Cache EmulOp locations** - Avoid repeated scanning
9. **Profile and tune** interrupt check frequency

## Example Implementation

```c
// Minimal QEMU-inspired execution loop for macemu-next
int unicorn_execute_qemu_style(UnicornCPU *cpu) {
    const int MAX_INSNS_PER_ITER = 20;  // Like a small TB

    while (!should_exit) {
        // QEMU pattern: Check interrupts first
        if (g_pending_interrupt_level > 0) {
            int level = g_pending_interrupt_level;
            g_pending_interrupt_level = 0;

            // Use QEMU's interrupt delivery approach
            if (can_take_interrupt(cpu, level)) {
                deliver_m68k_interrupt(cpu, level);
                continue;  // Re-enter execution
            }
        }

        // Execute small batch (like one TB)
        uint32_t pc = unicorn_get_pc(cpu);
        uc_err err = uc_emu_start(cpu->uc, pc, 0, 0, MAX_INSNS_PER_ITER);

        // Handle special cases
        switch (err) {
        case UC_ERR_OK:
            // Normal completion, continue
            break;

        case UC_ERR_INSN_INVALID:
            // Check for EmulOp or real invalid
            if (is_emulop(pc)) {
                handle_emulop(cpu, pc);
                advance_pc(cpu, 2);
            } else {
                // Real illegal instruction
                generate_exception(cpu, EXCP_ILLEGAL);
            }
            break;

        default:
            // Handle other errors
            break;
        }

        // QEMU pattern: Check for TB-breaking conditions
        uint16_t last_insn = read_last_instruction(cpu);
        if (is_backward_branch(last_insn) ||
            is_exception_generating(last_insn)) {
            // Force interrupt check on next iteration
            continue;
        }
    }
}
```

## Conclusion

We don't need all of QEMU - just its **execution control patterns** and **interrupt delivery mechanism**. By wrapping Unicorn in a QEMU-style execution loop and borrowing minimal interrupt handling code, we can fix the IRQ storm and exception issues without the complexity of full QEMU integration.

The key insight: **QEMU is an emulator, Unicorn is a library**. We need to add the emulator control flow around the library to get QEMU-like behavior.