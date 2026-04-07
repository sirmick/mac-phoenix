# Developer Guide

## Architecture Overview

### Core Components

```
┌─────────────────────────────────────────────────┐
│               Mac Application                    │
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
│  • UAE      (M68K default, fast + JIT)          │
│  • Unicorn  (M68K QEMU JIT, validation)         │
│  • DualCPU  (M68K lockstep validation)          │
│  • KPX      (PPC interpreter + dyngen JIT)      │
└─────────────────────────────────────────────────┘
```

### Key Design Principles

1. **Backend Independence**: All CPU operations go through Platform API
2. **Clean Separation**: No direct dependencies between backends
3. **Validation First**: DualCPU mode catches M68K bugs early
4. **Performance Second**: Optimize after correctness

## Machine Profiles

Machine profiles configure hardware parameters based on the ROM. Each profile sets the CPU type, RAM limits, display dimensions, addressing mode, and more. Profiles are auto-detected at startup via `set_machine_profile()` in `src/config/machine_profile.cpp`.

| Profile | ROM Version | CPU | RAM | Display | Addressing |
|---------|-------------|-----|-----|---------|------------|
| `se` | 0x0276 | 68000 | 4 MB max | 512×342 mono | 24-bit |
| `quadra` | 0x067c | 68040 | Unlimited | 640×480 color | 32-bit |
| PPC | (4 MB ROM) | PPC 603e | Configurable | Up to 1600×1200 | 32-bit |

To add a new machine profile, define a `MachineProfile` struct in `machine_profile.cpp` and add a ROM version check in `set_machine_profile()`.

## CPU Backends

### UAE (M68K)

Default M68K backend. Hand-tuned interpreter with optional JIT compiler.

- **Flags**: `--backend uae` (default), `--jit`/`--no-jit`
- **Boot time**: ~5s to Finder
- **Files**: `src/cpu/cpu_uae.c`, `src/cpu/uae_cpu/`

### Unicorn (M68K)

QEMU-based JIT via Unicorn Engine. ~10x slower than UAE due to QEMU TCG M68K overhead.

**Execution Flow**:
1. `hook_block()` — Apply deferred register updates, poll timer, deliver interrupts
2. `hook_interrupt()` — Handle A-line/F-line traps via EmulOp dispatch
3. All register writes deferred (QEMU overwrites PC after hook return)

**Key Files**:

| File | Purpose |
|------|---------|
| `unicorn_wrapper.c` | Hooks, deferred updates, diagnostics |
| `unicorn_exec_loop.c` | Main execution loop |
| `cpu_unicorn.cpp` | Backend interface, MMIO, memory map |
| `timer_interrupt.cpp` | 60Hz timer via `clock_gettime` |

**Key Concepts**:
- **Deferred Register Updates**: EmulOp handlers queue register writes, applied at next `hook_block()`
- **MMIO**: Must use `uc_mmio_map()` — JIT compiles direct loads for `uc_mem_map_ptr` regions
- **JIT TB Invalidation**: QEMU's `notdirty_write()` + STALE-TB detector

### KPX (PPC)

Kheperix interpreter from SheepShaver, targeting Gossamer (Beige G3) ROMs.

- **Flags**: `--arch ppc`, `--ppc-jit`/`--no-ppc-jit`
- **Boot time**: ~45s to Finder (interpreter)
- **OS**: Mac OS 9.0.4 (tested), 8.1-9.2.2 (expected)
- **Files**: `src/cpu/kpx/`

**Execution Model**: Mixed-mode — PPC nanokernel runs Mac OS 68K emulator (DR Emulator) which handles 68K code. PPC native code runs directly. Mode switches via EmulOps and NativeOps.

**Key Files**:

| File | Purpose |
|------|---------|
| `cpu_ppc_kpx.cpp` | sheepshaver_cpu, HandleInterrupt, Platform API |
| `emul_op_ppc.cpp` | EmulOp dispatch (40+ operations) |
| `rom_patches_ppc.cpp` | ROM patching (4 phases) |
| `video_ppc.cpp` | Video driver (VideoDoDriverIO) |
| `gfxaccel_ppc.cpp` | NQD acceleration hooks |

**JIT Status**: Dyngen JIT compiled and available via `--ppc-jit`. Blocked by GCC 13 codegen difference in block dispatch loop — interpreter is the working default.

See `docs/ppc/` for comprehensive PPC documentation.

### DualCPU (M68K Validation)

Runs UAE + Unicorn in lockstep, compares registers after each instruction. Returns `CPU_EXEC_DIVERGENCE` on mismatch.

- **Flag**: `--backend dualcpu`
- Not for end users — ~2x slower

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

```bash
# Enable tracing
CPU_TRACE=0-1000 ./build/mac-phoenix

# GDB breakpoints
break unicorn_execute_with_interrupts
break handle_emulop_immediate

# EmulOp frequency
grep "EmulOp" logfile | sort | uniq -c
```

## Testing

```bash
# All tests
ctest --test-dir build

# Fast tests (~20s)
ctest --test-dir build -R "api_endpoints|boot_uae|mouse_position|command_bridge|extfs"

# PPC boot test
ctest --test-dir build -R boot_ppc_interp

# Verbose
ctest --test-dir build -V

# Dual-CPU validation
./build/mac-phoenix --backend dualcpu --no-webserver ~/quadra.rom
```

## Profiling

```bash
sudo sysctl kernel.perf_event_paranoid=-1
perf record -g -F 997 ./build/mac-phoenix --backend unicorn --no-webserver ~/quadra.rom
perf report
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
```

### Testing Requirements
1. Boot tests pass for affected backends
2. No regressions in existing tests
3. New features need test coverage

## Resources

### Documentation
- [Architecture.md](Architecture.md) — System design
- [TroubleshootingGuide.md](TroubleshootingGuide.md) — Debug help
- [deepdive/](deepdive/) — Technical deep dives
- [ppc/](ppc/) — PPC-specific documentation

### External References
- [Unicorn Engine](https://www.unicorn-engine.org/docs/)
- [QEMU M68K](https://github.com/qemu/qemu/tree/master/target/m68k)
- [Inside Macintosh](https://developer.apple.com/library/archive/documentation/mac/pdf/)

### Key Concepts
- **EmulOp**: Emulator operation (0xAExx for Unicorn, 0x71xx for UAE)
- **IPL**: Interrupt Priority Level (0-7)
- **VBR**: Vector Base Register (interrupt vectors)
- **TB**: Translation Block (JIT compiled code)
- **KPX**: Kheperix PPC interpreter/JIT engine
- **NativeOp**: PPC native operation thunk (38 operations)
- **DR Emulator**: Macintosh 68K emulator running under PPC nanokernel

---

*Last Updated: April 2026*
