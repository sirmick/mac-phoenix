# Developer Guide

Backend internals, common dev tasks, and contribution rules. The
high-level architecture lives in [Architecture.md](Architecture.md);
this doc covers the parts a contributor needs to actually change code.

## Backends

| Backend | Arch | File | `--backend` token |
|---------|------|------|-------------------|
| UAE | m68k | `src/cpu/cpu_uae.c`, `src/cpu/uae_cpu/` | `uae` (default) |
| Unicorn-m68k | m68k | `src/cpu/cpu_unicorn.cpp`, `unicorn_wrapper.c` | `unicorn-m68k` |
| Unicorn-PPC | ppc | `src/cpu/cpu_unicorn_ppc.cpp` | `unicorn-ppc` |
| KPX | ppc | `src/cpu/kpx/cpu_ppc_kpx.cpp` + `src/cpu/kpx/src/` | `kpx` |
| DualCPU | m68k | `src/cpu/cpu_dualcpu.c` | `dualcpu` |

Backend installers all write into the same `g_platform` table
(`src/common/include/platform.h`). Core code never references a backend
directly.

## Machine Profiles

Profiles auto-detect from the ROM at startup via `set_machine_profile()` in
`src/config/machine_profile.cpp`. Each profile sets CPU type, RAM caps,
display size, and addressing mode.

| Profile | ROM ver | CPU | RAM | Display | Addr |
|---------|---------|-----|-----|---------|------|
| `se` | `0x0276` | 68000 | 4 MB | 512×342 mono | 24-bit |
| `quadra` | `0x067c` | 68040 | unlimited | 640×480 color | 32-bit |
| PPC | (4 MB ROM) | PPC 750 (G3) | configurable | up to 1600×1200 | 32-bit |

Add a new profile by appending a `MachineProfile` struct in
`machine_profile.cpp` and a ROM-version branch in `set_machine_profile()`.

## Backend internals (the parts you'd touch)

### UAE

Default m68k. Hand-tuned interpreter from BasiliskII; `--jit` enables the
WinUAE JIT compiler. ~5 s boot interpreter, ~3 s with JIT. Memory is
big-endian and accessed through `do_get_mem_*` byte-swap macros — see
[`deepdive/cpu/UaeQuirks.md`](deepdive/cpu/UaeQuirks.md).

### Unicorn-m68k

QEMU TCG JIT. ~12 s boot — the perf gap to UAE is structural (TCG
compilation dominates; see
[`UnicornPerformanceAnalysis.md`](UnicornPerformanceAnalysis.md)).

Execution flow:

1. `hook_block` (UC_HOOK_BLOCK) — apply deferred register updates, poll
   60 Hz timer at 4096-block intervals, deliver pending interrupts.
2. `hook_interrupt` (UC_HOOK_INTR) — A-line / F-line trap dispatch into
   `g_platform.emulop_handler` / `trap_handler`.
3. All register writes from inside `hook_interrupt` are **deferred** —
   QEMU overwrites PC after the hook returns, so changes are queued and
   applied at the next `hook_block` boundary.

Key files: `cpu_unicorn.cpp` (backend install, memory map, MMIO via
`uc_mmio_map`), `unicorn_wrapper.c` (hooks, deferred updates, perf
counters), `unicorn_exec_loop.c` (`unicorn_execute_with_interrupts`),
`unicorn_exception.c` (A-line dispatch into `op_illg`).

Quirks live in [`deepdive/cpu/UnicornQuirks.md`](deepdive/cpu/UnicornQuirks.md)
and [`deepdive/cpu/ALineAndFLineStatus.md`](deepdive/cpu/ALineAndFLineStatus.md).
SMC/dirty-bit story in [`deepdive/JitSmcDetectionAnalysis.md`](deepdive/JitSmcDetectionAnalysis.md).

### KPX

PPC interpreter from SheepShaver. ~45 s boot. `--jit` compiles dyngen but
is currently blocked by a GCC codegen difference in the block dispatch loop;
interpreter is the working default. `--jit68k` (default on) controls the
68k-on-PPC DR JIT.

Mixed-mode execution — PPC nanokernel runs Mac OS's built-in 68k emulator
inside the ROM, with mode tracked at `XLM_RUN_MODE`. The boot sequence,
KernelData layout, IRQ delivery, and ROM patching are documented in
[`ppc/README.md`](ppc/README.md).

### Unicorn-PPC

Experimental QEMU TCG PPC backend. Reaches Finder under 7.6.1 but unstable —
status, debug knobs, and known crashes in
[`ppc/UnicornPpcStatus.md`](ppc/UnicornPpcStatus.md).

### DualCPU

UAE + Unicorn-m68k in lockstep. Returns `CPU_EXEC_DIVERGENCE` on register
mismatch. ~2× slower than either alone — debugging tool only.

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
# Unit + API
ctest --test-dir build -L "unit|api"

# Boot tests (~3 min total)
ctest --test-dir build -L boot

# PPC only
ctest --test-dir build -R boot_ppc

# Verbose
ctest --test-dir build -V

# DualCPU lockstep run (catch m68k divergences)
./build/mac-phoenix --backend dualcpu --no-webserver ~/storage/roms/quadra.rom
```

Full test inventory in [Testing.md](Testing.md).

## Profiling

```bash
sudo sysctl kernel.perf_event_paranoid=-1
perf record -g -F 997 ./build/mac-phoenix --backend unicorn-m68k \
    --no-webserver ~/storage/roms/quadra.rom
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

### Internal docs
- [Architecture.md](Architecture.md), [Commands.md](Commands.md),
  [Testing.md](Testing.md), [TroubleshootingGuide.md](TroubleshootingGuide.md)
- [deepdive/](deepdive/) — quirks and detailed analyses
- [ppc/](ppc/) — PPC backends + Unicorn-PPC live status

### External
- Unicorn Engine — https://www.unicorn-engine.org/docs/
- QEMU m68k target — https://github.com/qemu/qemu/tree/master/target/m68k
- Inside Macintosh — https://developer.apple.com/library/archive/documentation/mac/pdf/

### Glossary
- **EmulOp** — host-side dispatch from a synthetic illegal opcode
  (m68k `0x71xx` for UAE, `0xAExx` for Unicorn-m68k; PPC `0x18000000`+
  family).
- **IPL** — m68k interrupt priority level (0–7).
- **VBR** — m68k vector base register.
- **TB** — QEMU translation block (JIT-compiled code).
- **KPX** — Kheperix; the SheepShaver-derived PPC interpreter.
- **NativeOp** — PPC native operation thunk (38 selectors).
- **DR Emulator** — the 68k emulator inside Mac OS's PPC ROM.
