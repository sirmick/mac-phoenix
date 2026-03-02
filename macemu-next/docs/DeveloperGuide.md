# macemu-next Developer Guide

## Architecture Overview

### Core Components

```
┌─────────────────────────────────────────────────┐
│                Mac Application                   │
└─────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────┐
│               Platform API                       │
│  • Backend-agnostic interface                   │
│  • Register access, memory, interrupts          │
│  • EmulOp handling                              │
└─────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────┐
│           CPU Backend (pluggable)               │
│  • UAE (reference)                              │
│  • Unicorn (optimized) ← PRIMARY                │
│  • DualCPU (validation)                         │
└─────────────────────────────────────────────────┘
```

### Key Design Principles

1. **Backend Independence**: All CPU operations go through Platform API
2. **Clean Separation**: No direct dependencies between backends
3. **Validation First**: DualCPU mode catches bugs early
4. **Performance Second**: Optimize after correctness

## Understanding the Unicorn Backend

### Execution Flow

1. **QEMU-Style Loop** (`src/cpu/unicorn_exec_loop.c`)
   ```c
   while (running) {
       check_interrupts();      // Before execution
       batch_size = adaptive();  // 3-50 instructions
       uc_emu_start(batch);      // Execute batch
       check_branches();         // Force interrupt check on loops
   }
   ```

2. **Hook Block** (`src/cpu/unicorn_wrapper.c:hook_block()`)
   - Apply deferred register updates from previous EmulOp
   - Poll timer every ~4096 blocks
   - Flush JIT translation blocks (TB invalidation workaround)
   - Check for pending interrupts

3. **Hook Interrupt** (`src/cpu/unicorn_wrapper.c:hook_interrupt()`)
   - Fires on A-line exception (0xAExx opcodes)
   - Identifies EmulOp opcode, calls handler
   - **Defers** all register updates (writes inside hooks don't persist in QEMU)
   - Updates applied at next `hook_block()` call

4. **Interrupt Delivery** (in `hook_block()`)
   - Build M68K exception frames manually
   - Handle priority masking (IPL)
   - Deliver timer at 60Hz (Level 1, Vector 25)

### Critical Files

| File | Purpose | Key Functions |
|------|---------|---------------|
| `unicorn_wrapper.c` | Hooks, deferred updates, diagnostics | `hook_block()`, `hook_interrupt()`, `apply_deferred_updates_and_flush()` |
| `unicorn_exec_loop.c` | Main execution loop | `unicorn_execute_with_interrupts()` |
| `cpu_unicorn.cpp` | Backend interface, MMIO, memory map | Platform API, `uc_mmio_map()` callbacks |
| `rom_patches.cpp` | ROM modifications | IRQ EmulOp encoding fix |
| `timer_interrupt.cpp` | 60Hz timer | `poll_timer_interrupt()` |

### Key Technical Concepts

**Deferred Register Updates**: EmulOp handlers run inside `UC_HOOK_INTR` callbacks. QEMU overwrites PC after hook returns. Solution: queue all register writes and apply them at the next `hook_block()` boundary via `apply_deferred_updates_and_flush()`.

**JIT TB Invalidation**: Mac OS heap can overwrite RAM containing EmulOp patch code. QEMU's JIT cache retains stale compiled translations. Workaround: `uc_ctl_flush_tb()` on every 60Hz timer tick. Proper fix: investigate QEMU's `TLB_NOTDIRTY` mechanism.

**MMIO**: Hardware registers must use `uc_mmio_map()`, not `UC_HOOK_MEM_READ`. QEMU's JIT compiles direct memory loads for `uc_mem_map_ptr` regions, bypassing hooks.

**SR uint32_t**: `uc_reg_write()` for SR reads 4 bytes. Passing `uint16_t*` causes garbage in upper bits.

## Common Development Tasks

### Adding a New EmulOp

1. Define in `src/common/include/emul_op.h`:
   ```c
   M68K_EMUL_OP_NEW_FEATURE = 0x7140,
   ```

2. Implement handler in `src/core/emul_op.cpp`:
   ```c
   case M68K_EMUL_OP_NEW_FEATURE:
       // Your implementation
       break;
   ```

3. Patch ROM if needed in `src/core/rom_patches.cpp`:
   ```c
   *wp++ = htons(0x7140);  // Direct encoding
   ```

### Debugging CPU Execution

1. **Enable tracing**:
   ```bash
   CPU_TRACE=0-1000 ./build/macemu-next
   ```

2. **Add breakpoints** in GDB:
   ```gdb
   break unicorn_execute_with_interrupts
   break handle_emulop_immediate
   ```

3. **Check specific issues**:
   ```bash
   # IRQ storm check
   grep -c "poll_timer" logfile

   # EmulOp frequency
   grep "EmulOp" logfile | sort | uniq -c
   ```

### Modifying Interrupt Handling

The interrupt system has several layers:

1. **Timer Source** → `poll_timer_interrupt()`
2. **Execution Loop** → `poll_and_check_interrupts()`
3. **Delivery** → `deliver_m68k_interrupt()`
4. **Exception Frame** → `build_exception_frame()`

To add a new interrupt source:
```c
// In m68k_interrupt.c
void deliver_custom_interrupt(UnicornCPU *cpu, int level) {
    deliver_m68k_interrupt(cpu, level, 24 + level);  // Autovector
}
```

## Performance Optimization

### Current Optimizations

1. **Adaptive Batch Sizing**
   - 3 instructions for hot loops
   - 20 for ROM code
   - 50 for application code

2. **Minimal Hooks**
   - Only UC_HOOK_BLOCK (not per-instruction)
   - UC_HOOK_INSN_INVALID for EmulOps

3. **Immediate Updates**
   - No deferred register queues
   - Direct memory access

### Profiling

```bash
# CPU profiling
perf record -g ./build/macemu-next
perf report

# Instruction counting
CPU_STATS=1 ./build/macemu-next

# JIT efficiency
BLOCK_STATS=1 ./build/macemu-next
```

### Optimization Opportunities

1. **Translation Block Caching**
   - Currently rebuilds on every interrupt
   - Could cache and reuse

2. **Batch Size Tuning**
   - Profile actual instruction patterns
   - Adjust thresholds

3. **Hook Reduction**
   - Combine multiple checks
   - Use conditional hooks

## Testing

### Unit Tests
```bash
# Run all tests
meson test -C build

# Run specific test
./build/tests/boot/test_boot
```

### Integration Tests
```bash
# Compare backends
./scripts/compare_boot.sh

# Validate against UAE
CPU_BACKEND=dualcpu ./build/macemu-next
```

### Regression Tests
```bash
# IRQ storm test (MUST pass)
timeout 10 ./build/macemu-next 2>&1 | grep -c poll_timer
# Expected: <100

# Timer test (MUST pass)
timeout 5 ./build/macemu-next 2>&1 | grep "300 interrupts"
# Expected: found
```

## Contributing

### Code Style
- C: K&R style, 4-space indent
- C++: Similar to C, avoid STL in hot paths
- Comments: Explain WHY, not WHAT

### Commit Messages
```
component: Brief description

Detailed explanation of what changed and why.
Reference issue numbers if applicable.

Fixes #123
```

### Testing Requirements
1. No IRQ storm (test included)
2. Timer at 60Hz (test included)
3. Boots to same point as UAE
4. No memory leaks (valgrind clean)

## Troubleshooting Development Issues

### Build Failures
```bash
# Clean rebuild
rm -rf build
meson setup build
ninja -C build

# Check dependencies
pkg-config --libs unicorn
```

### Runtime Crashes
```bash
# Run under GDB
gdb ./build/macemu-next
run --no-webserver

# Check backtrace
bt full

# Check registers
info registers
```

### Performance Issues
```bash
# Check batch sizes
CPU_VERBOSE=1 ./build/macemu-next 2>&1 | grep batch

# Check interrupt rate
grep -c interrupt logfile
```

## Architecture Decisions

### Why QEMU-Style Loop?
- Unicorn's JIT needs interrupt check points
- Small batches prevent infinite loops
- Adaptive sizing balances performance

### Why Deferred Updates?
- Register writes inside `UC_HOOK_INTR` don't persist (QEMU overwrites PC)
- Deferred updates applied at `hook_block()` boundary work correctly
- All A-line/F-line traps now functional with this approach

### Why M68K Exception Frames?
- Required for proper RTE handling
- Mac OS expects specific format
- Matches real hardware behavior

## Future Work

### Phase 5: TB Break Detection (Optional)
- Detect patterns requiring TB termination
- Optimize backward branch handling
- Reduce unnecessary breaks

### Phase 6: Optimization (Optional)
- Profile hot paths
- Optimize register access
- Cache translation blocks

### Current Priority (March 2026)
- SCSI disk emulation (required for further boot progress)
- More complete VIA emulation
- Video framebuffer initialization

### Long-term Goals
- Full Mac OS boot to desktop
- Network support
- Sound emulation
- Graphics acceleration

## Resources

### Documentation
- [Architecture.md](Architecture.md) - System design
- [TROUBLESHOOTING_GUIDE.md](TROUBLESHOOTING_GUIDE.md) - Debug help
- [deepdive/](deepdive/) - Technical deep dives

### External References
- [Unicorn Engine Docs](https://www.unicorn-engine.org/docs/)
- [QEMU M68K](https://github.com/qemu/qemu/tree/master/target/m68k)
- [Inside Macintosh](https://developer.apple.com/library/archive/documentation/mac/pdf/)

### Key Concepts
- **EmulOp**: Emulator operation (0x71xx opcodes)
- **IPL**: Interrupt Priority Level (0-7)
- **VBR**: Vector Base Register (interrupt vectors)
- **TB**: Translation Block (JIT compiled code)

---

*Last Updated: March 2026*