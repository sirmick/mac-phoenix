# mac-phoenix Troubleshooting Guide

## Quick Diagnostics

### 1. Check for IRQ Storm
```bash
# Should show ~20, NOT 780,000+
./build/mac-phoenix --backend unicorn --timeout 10 --no-webserver 2>&1 | grep -c poll_timer
```

**Expected**: ~20 polls
**If seeing 100,000+**: IRQ storm is back, check Phase 1 fix in rom_patches.cpp

### 2. Verify Timer Rate
```bash
# Should show 300 interrupts in 5 seconds (60Hz)
./build/mac-phoenix --backend unicorn --timeout 5 --no-webserver 2>&1 | grep "Timer:"
```

**Expected**: "Timer: Stopped after 300 interrupts (5 seconds)"
**If different**: Timer delivery issue

### 3. Compare Backends
```bash
# UAE (reference implementation)
./build/mac-phoenix --backend uae --timeout 5 --no-webserver 2>&1 | tail -20

# Unicorn (optimized implementation)
./build/mac-phoenix --backend unicorn --timeout 5 --no-webserver 2>&1 | tail -20
```

Both should show similar progress.

## Common Issues and Solutions

### Issue: IRQ Storm Returns
**Symptom**: Millions of EmulOp 0x7129 calls, system stuck
**Cause**: ROM patcher regression
**Solution**:
```c
// Check src/core/rom_patches.cpp lines 1043 and 1696
// MUST be: *wp++ = htons(0x7129);
// NOT: *wp++ = htons(make_emulop(M68K_EMUL_OP_IRQ));
```

### Issue: No Timer Interrupts
**Symptom**: No timer fires, boot hangs
**Cause**: Interrupt delivery broken
**Solution**:
1. Check timer setup in cpu_context.cpp
2. Verify poll_timer_interrupt() is being called
3. Check IPL masking in m68k_interrupt.c

### Issue: EmulOps Not Working
**Symptom**: EmulOps cause crashes or don't execute
**Cause**: Register update issues
**Solution**:
1. Verify immediate updates in unicorn_exec_loop.c
2. Check platform API linkage
3. Ensure deferred updates are disabled

### Issue: Boot Hangs Early
**Symptom**: Stuck before CLKNOMEM
**Cause**: Basic execution failure
**Debug Steps**:
```bash
# Enable CPU trace
CPU_TRACE=0-100 ./build/mac-phoenix --backend unicorn --no-webserver 2>&1 | head -100

# Check for illegal instructions
CPU_VERBOSE=1 ./build/mac-phoenix --backend unicorn --no-webserver 2>&1 | grep "illegal\|invalid"
```

### Issue: Performance Degradation
**Symptom**: Slow execution, <1000 instructions/sec
**Cause**: Execution batches too small
**Solution**:
Check batch sizes in unicorn_exec_loop.c:
- IRQ regions: 3 instructions (correct)
- ROM code: 20 instructions (correct)
- RAM code: 50 instructions (correct)

## Debug Environment Variables

### Execution Tracing
```bash
# Trace first N instructions
CPU_TRACE=0-1000

# Trace specific PC range
CPU_TRACE=0x02000000-0x02001000

# Verbose execution
CPU_VERBOSE=1
```

### Component-Specific Debug
```bash
# EmulOp debugging
EMULOP_VERBOSE=1

# Interrupt debugging
INTERRUPT_VERBOSE=1

# Timer debugging
TIMER_VERBOSE=1
```

### Performance Analysis
```bash
# Show execution statistics
CPU_STATS=1

# Show JIT block statistics
BLOCK_STATS=1
```

## Build Issues

### Clean Rebuild
```bash
cd /home/mick/macemu-dual-cpu/mac-phoenix
rm -rf build
meson setup build
ninja -C build
```

### Verify Installation
```bash
# Check binary exists
ls -la build/mac-phoenix

# Check libraries
ldd build/mac-phoenix | grep -E "unicorn|uae"

# Check ROM file
ls -la ~/quadra.rom
```

## Testing Commands

### Basic Functionality Test
```bash
# 1. Minimal test - should not crash
./build/mac-phoenix --backend unicorn --timeout 1 --no-webserver

# 2. Check boot progress
./build/mac-phoenix --backend unicorn --timeout 5 --no-webserver 2>&1 | grep "EmulOp"

# 3. Full comparison test
./scripts/compare_boot.sh
```

### Regression Tests
```bash
# Test IRQ storm fix
./build/mac-phoenix --backend unicorn --timeout 10 --no-webserver 2>&1 | grep -c "EmulOp 0x7129"
# MUST be < 100

# Test timer delivery
./build/mac-phoenix --backend unicorn --timeout 5 --no-webserver 2>&1 | grep "Timer:" | grep "300"
# MUST show 300 interrupts

# Test EmulOp execution
EMULOP_VERBOSE=1 ./build/mac-phoenix --backend unicorn --timeout 2 --no-webserver 2>&1 | grep "CLKNOMEM"
# MUST show CLKNOMEM calls
```

## Advanced Debugging

### GDB Debugging
```bash
# Build with debug symbols
meson setup build -Dbuildtype=debug
ninja -C build

# Run under GDB
gdb ./build/mac-phoenix
(gdb) break unicorn_execute_with_interrupts
(gdb) run --backend unicorn --timeout 10 --no-webserver
```

### Key Breakpoints
```gdb
# IRQ EmulOp handling
break rom_patches.cpp:1044

# Execution loop
break unicorn_exec_loop.c:unicorn_execute_with_interrupts

# Interrupt delivery
break m68k_interrupt.c:deliver_m68k_interrupt

# EmulOp handler
break handle_emulop_immediate
```

### Memory Debugging
```bash
# Check for memory leaks
valgrind --leak-check=full ./build/mac-phoenix --no-webserver

# Check for memory errors
valgrind --tool=memcheck ./build/mac-phoenix --no-webserver
```

## Log Analysis

### Finding Patterns
```bash
# Count specific EmulOps
grep "EmulOp 0x" logfile | sort | uniq -c | sort -rn

# Find timer events
grep -E "timer|Timer|interrupt" logfile

# Find errors
grep -iE "error|fail|crash|abort" logfile
```

### Performance Analysis
```bash
# Count instructions per second
timeout 10 ./build/mac-phoenix --no-webserver 2>&1 | grep "total_instructions" | tail -1

# Measure JIT efficiency
grep "TB executed" logfile | wc -l
```

## Contact and Resources

### Documentation
- Architecture: docs/Architecture.md
- Developer Guide: docs/DeveloperGuide.md
- IRQ Storm Analysis: docs/deepdive/UnicornIRQStormDebugSession.md

### Key Files for Debugging
1. `src/core/rom_patches.cpp` - ROM patching (IRQ fix)
2. `src/cpu/unicorn_exec_loop.c` - Execution loop
3. `src/cpu/m68k_interrupt.c` - Interrupt delivery
4. `src/cpu/cpu_unicorn.cpp` - Unicorn backend
5. `src/cpu/unicorn_wrapper.c` - Unicorn wrapper

### Common Error Messages

| Error | Meaning | Solution |
|-------|---------|----------|
| "Unsupported ROM type" | ROM file invalid | Use proper Mac ROM |
| "EmulOp 0xAE29" | Wrong encoding | Fix rom_patches.cpp |
| "UC_ERR_INSN_INVALID" | Illegal instruction | Check EmulOp handler |
| "Timer not initialized" | Timer setup failed | Check cpu_context.cpp |
| "IPL blocked" | Interrupts masked | Normal during boot |

## Quick Reference Card

```bash
# Run with Unicorn (fast)
./build/mac-phoenix --no-webserver

# Run with UAE (reference)
./build/mac-phoenix --backend uae --no-webserver

# Debug mode
CPU_VERBOSE=1 EMULOP_VERBOSE=1 ./build/mac-phoenix --no-webserver

# Trace execution
CPU_TRACE=0-1000 ./build/mac-phoenix --no-webserver

# Check IRQ storm
timeout 10 ./build/mac-phoenix --no-webserver 2>&1 | grep -c poll_timer
```

---

*Last Updated: March 2026 - Both backends boot to Mac OS 7.5.5 Finder desktop*