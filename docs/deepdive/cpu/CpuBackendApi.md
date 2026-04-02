# Unified CPU Backend API ✅ IMPLEMENTED

## Overview

All CPU backends conform to a common interface via the `Platform` struct in `src/common/include/platform.h`. The execution loop lives in `main.cpp`, not inside backends.

## Interface

Each backend implements these `Platform` function pointers:

### Lifecycle
- `cpu_init()` — Initialize backend
- `cpu_reset()` — Reset CPU state
- `cpu_set_type()` — Configure CPU model (68020-68040, FPU)

### Execution
- `cpu_execute_one()` — Single-step (returns 0=OK, 1=STOPPED, 2=breakpoint, 3=exception, 4=emulop, 5=divergence)
- `cpu_execute_fast()` — Optional run-to-completion (NULL if not supported)

### State
- `cpu_get_pc()`, `cpu_get_sr()`, `cpu_get_dreg(n)`, `cpu_get_areg(n)`

### Interrupts
- `cpu_trigger_interrupt(level)` — Signal interrupt delivery

### Trap Execution
- `cpu_execute_68k_trap()` — Execute 68k trap with register state
- `cpu_execute_68k()` — Execute 68k subroutine at address

## Backends

| Backend | File | `execute_fast` | Selection |
|---------|------|----------------|-----------|
| UAE | `cpu_uae.c` | Yes (continuous loop) | `--backend uae` (default for m68k) |
| Unicorn | `cpu_unicorn.cpp` | NULL (single-step only) | `--backend unicorn` |
| DualCPU | `cpu_dualcpu.c` | NULL (validation requires per-instruction) | `--backend dualcpu` |
| KPX | `cpu_ppc_kpx.cpp` | Yes (PPC interpreter/JIT loop) | `--arch ppc` |

## Execution Loop (main.cpp)

```c
if (platform->cpu_execute_fast) {
    platform->cpu_execute_fast();  // UAE/KPX: optimized loop
} else {
    while (true) {
        platform->cpu_execute_one();  // Unicorn/DualCPU: caller-controlled
    }
}
```

## Backend Selection (main.cpp)

```c
switch (emu_config.cpu_backend) {
    case CPUBackend::Unicorn:  cpu_unicorn_install(platform); break;
    case CPUBackend::DualCPU:  cpu_dualcpu_install(platform); break;
    case CPUBackend::KPX:      cpu_ppc_kpx_install(platform); break;
    default:                   cpu_uae_install(platform);      break;
}
```

## Design Benefits

1. **Clean separation** — backends execute instructions, main.cpp controls flow
2. **Optional fast path** — JIT backends optimize via `cpu_execute_fast()` without breaking interpreter
3. **Easy testing** — inject custom loops, run N instructions
4. **Backend-agnostic core** — all core code uses Platform API, never calls backends directly
