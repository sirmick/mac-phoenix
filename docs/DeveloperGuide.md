# Developer Guide

Backend internals, common dev tasks, and contribution rules. The
high-level architecture lives in [Architecture.md](Architecture.md);
this doc covers the parts a contributor needs to actually change code.

## Backends and cores

`--backend` picks the machine (and architecture), `--core` the CPU core
that runs it:

| Backend | Arch | Cores (`--core`) | Machine side |
|---------|------|------------------|--------------|
| `uae` (default) | m68k | `uae` (default), `mame-68k` | `src/cpu/cpu_m68k.cpp`, `src/core/` |
| `kpx` | ppc | `kpx` (default), `mame-ppc` | `src/cpu/kpx/cpu_ppc_kpx.cpp` / `cpu_ppc_mame.cpp` + shared SheepShaver code in `src/cpu/kpx/` |
| `executor` | m68k | `mame-68k` | `src/executor/` (Toolbox in C++, no ROM) |

Cores other than Kheperix implement `cpu::Core`
(`src/cpu/core/cpu_core.h`): `uae` in `src/cpu/core/uae_core.cpp` over
`src/cpu/uae_cpu/`, `mame-68k` and `mame-ppc` in `src/cpu/mame/`. The
installers fill the same `g_platform` table
(`src/common/include/platform.h`); core code never references a backend
or core directly. A new core goes behind `cpu::Core` and into
`cpu::create()` (`src/cpu/core/registry.cpp`); a 68k one then runs the ROM
machine as is, a PPC one also needs the PPC machine to pick it
(`CPUContext::init_ppc()` chooses between `cpu_ppc_kpx_install()` and
`cpu_ppc_mame_install()` by name). See
[`deepdive/cpu/CpuBackendApi.md`](deepdive/cpu/CpuBackendApi.md).

## Machine Profiles

The 68k machine profile is picked from the ROM's version and checksum by
`set_machine_profile()` in `src/config/machine_profile.cpp`. Each sets the
CPU, FPU, RAM cap, ROM base, addressing mode, Gestalt machine type and
default display.

| Profile | ROM | CPU | RAM cap | ROM base | Display | Addr |
|---------|-----|-----|---------|----------|---------|------|
| `se` | ver `0x0276` | 68000 | 4 MB | `0x400000` | 512×342 mono | 24-bit |
| `macii` | ver `0x0178` | 68020 | 8 MB | `0x800000` | 640×480 colour | 24-bit |
| `iici` | checksum `0x368CADFE` | 68030 + FPU | 8 MB | `0x800000` | 640×480 colour | 24-bit |
| `quadra` | anything else (`0x067c`) | 68040 + FPU | none | after RAM | 640×480 colour | 32-bit |

`--model-id` overrides the Gestalt machine type the Mac is told. The PPC
machine is not a profile: a G3 (PVR of a 750 on `mame-ppc`), RAM from
`--ram` (at least 16 MB), screen modes up to 3840×2160 (68k: 1920×1080).

Add a profile by appending a `MachineProfile` in `machine_profile.cpp` and
a branch in `set_machine_profile()`.

## Backend internals (the parts you'd touch)

### 68k ROM machine

`cpu_m68k.cpp` is Basilisk II's machine on any 68k `cpu::Core`: it makes the
core from the profile's CPU type, sends `$71xx` EmulOps to
`m68k::EmulOp()` and drives the interrupt line from `InterruptFlags`. On
`uae`, `--jit` turns on the UAE JIT (x86 only); `mame-68k` brings MAME's
Musashi with a softfloat FPU. Guest memory is big-endian; see
[`deepdive/cpu/UaeQuirks.md`](deepdive/cpu/UaeQuirks.md) for UAE's fetch
path.

### PPC machine

SheepShaver's machine on Kheperix (`kpx`, `--jit` for its dyngen JIT) or on
MAME's PowerPC recompiler (`mame-ppc`, `--jit` for the native back-end).
Mixed-mode execution: the PPC nanokernel runs Mac OS's 68k emulator inside
the ROM, with the mode at `XLM_RUN_MODE`. Boot sequence, KernelData,
interrupt delivery and ROM patching: [`ppc/README.md`](ppc/README.md); the
MAME cores: [`cpu/PLAN.md`](cpu/PLAN.md).

### Executor

No ROM: the Executor 2000 Toolbox (C++) runs in the IPC child (or in-process
with `--no-webserver`) on a 68k core through a syn68k-compatible facade
(`src/executor/cpu/`) on `mame-68k`, in four address windows (no
`vm.mmap_min_addr`); PowerPC code goes through `PowerCore` on `mame-ppc`. Plan, milestones and
gotchas: [`executor/PLAN.md`](executor/PLAN.md).

## Common Development Tasks

### Adding a New EmulOp

1. Add it to the enum in `src/common/include/emul_op.h`, before
   `M68K_EMUL_OP_MAX` (the next free number is `$7140`):
   ```c
   M68K_EMUL_OP_NEW_FEATURE,       // 0x7140
   ```

2. Implement the handler in `m68k::EmulOp()`, `src/core/emul_op.cpp`:
   ```c
   case M68K_EMUL_OP_NEW_FEATURE:
       // r->d[], r->a[] are the guest registers
       break;
   ```

3. Write it where it's needed (`src/core/rom_patches.cpp`, or a trap
   patch like `text_feed.cpp`), always through `platform_make_emulop()`:
   ```c
   *wp++ = htons(platform_make_emulop(M68K_EMUL_OP_NEW_FEATURE));
   ```

4. If the PPC machine uses it too: add it to `src/cpu/kpx/compat/emul_op.h`
   and to `kpx_make_emulop_shared()` in `src/cpu/kpx/native_ops_ppc.cpp`.

### Debugging CPU Execution

```bash
# Every A-trap with its caller's PC (snapshots write atraps.tsv)
./build/mac-phoenix --trace-atraps ...

# Guest memory snapshot from a running emulator (src/core/snapshot.h) into
# <storage>/snapshots/NAME; tools/macdecode reads them
curl -X POST -d '{"name":"hang1"}' localhost:11000/api/snapshot

# Programmer's Key: NMI on 68k, Cmd+Power on PPC
curl -X POST localhost:11000/api/invoke-debug

# GDB: the CPU runs on the IPC child's main thread
break m68k::EmulOp             # every 68k EmulOp
break m68k_do_execute          # UAE interpreter loop (uae_cpu/newcpu.cpp)
```

Executor has its own switches (`--executor-logtraps`, `--executor-debug`,
`--watch`); `--debug-mame-ppc` traces the mame-ppc installer.

## Testing

```bash
# Unit + API
ctest --test-dir build -L "unit|api"

# Boot tests
ctest --test-dir build -L boot

# PPC only (Kheperix and mame-ppc)
ctest --test-dir build -R boot_ppc

# Executor
ctest --test-dir build -L executor

# Verbose
ctest --test-dir build -V
```

Full test inventory in [Testing.md](Testing.md).

## Profiling

```bash
sudo sysctl kernel.perf_event_paranoid=-1
perf record -g -F 997 ./build/mac-phoenix --backend uae \
    --no-webserver ~/storage/roms/quadra.rom
perf report
```

## Contributing

### Code Style
- Match the file you're in: imported code (Basilisk II, SheepShaver, UAE,
  MAME, Executor) keeps its own style, tabs included; new code uses 4-space
  indent
- Avoid allocation and STL containers in CPU hot paths
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
- [ppc/](ppc/) — the PPC machine
- [executor/](executor/) — the Executor backend
- [cpu/PLAN.md](cpu/PLAN.md) — MAME cores: Musashi with PMMU (C1), PowerPC recompiler (C2), host 68k on the PPC machine (C3)

### External
- Inside Macintosh — https://developer.apple.com/library/archive/documentation/mac/pdf/

### Glossary
- **EmulOp** — host-side dispatch from a synthetic illegal opcode
  (m68k `0x71xx`; PPC `0x18000000`+ family).
- **IPL** — m68k interrupt priority level (0–7).
- **VBR** — m68k vector base register.
- **KPX** — Kheperix; SheepShaver's PPC interpreter and dyngen JIT, the
  `kpx` core.
- **cpu::Core** — the interface every other core implements
  (`src/cpu/core/cpu_core.h`).
- **NativeOp** — PPC native operation thunk (`NATIVE_*` selectors in
  `src/cpu/kpx/compat/thunks.h`).
- **DR Emulator** — the 68k emulator inside Mac OS's PPC ROM.
