# PPC Architecture Integration

How PowerPC emulation fits into mac-phoenix's existing codebase.

## Current Abstraction Layers

mac-phoenix already has architecture awareness baked in:

```
EmulatorConfig
  ├── Architecture enum: M68K, PPC
  ├── M68KConfig { cpu_type, fpu, modelid, jit, ... }
  ├── PPCConfig  { cpu_type, fpu, modelid, jit, jit68k, ... }
  └── --arch m68k|ppc CLI flag (parsed but ignored)

Platform API (g_platform)
  ├── cpu_init/reset/execute_one/execute_fast
  ├── cpu_get_pc/sr/dreg/areg
  ├── cpu_trigger_interrupt
  ├── cpu_execute_68k_trap / cpu_execute_68k
  ├── mem_read_byte/word/long, mem_write_byte/word/long
  └── emulop_handler / trap_handler

CPUContext
  ├── init_m68k()  — fully implemented
  └── init_ppc()   — stub, returns false

Unicorn Wrapper
  ├── UnicornArch enum: UCPU_ARCH_M68K, UCPU_ARCH_PPC, UCPU_ARCH_PPC64
  └── unicorn_create_with_model() — hardcoded to UC_ARCH_M68K
```

## What Needs to Change

### Layer 1: Config Wiring (trivial)

`emulator_config.h` has `cpu_type_int()` and `fpu()` hardcoded to read from `m68k` sub-struct. Need architecture-aware dispatch:

```cpp
int cpu_type_int() const {
    return (architecture == Architecture::PPC) ? ppc.cpu_type : m68k.cpu_type;
}
```

`main.cpp` line 356 hardcodes `init_m68k()`. Need:
```cpp
if (emu_config.architecture == Architecture::PPC)
    success = g_cpu_ctx.init_ppc(emu_config);
else
    success = g_cpu_ctx.init_m68k(emu_config);
```

### Layer 2: Platform API Extension

The current Platform struct is m68k-flavored (D-regs, A-regs, SR). PPC needs:

**Option A: Parallel API** — Add PPC-specific accessors alongside m68k ones:
```c
uint32_t (*cpu_get_gpr)(int n);      // R0-R31
void     (*cpu_set_gpr)(int n, uint32_t val);
uint32_t (*cpu_get_lr)(void);
uint32_t (*cpu_get_ctr)(void);
uint32_t (*cpu_get_cr)(void);
uint32_t (*cpu_get_msr)(void);
```

**Option B: Generic API** — Replace D-reg/A-reg with numbered register access:
```c
uint32_t (*cpu_get_reg)(int regclass, int n);
void     (*cpu_set_reg)(int regclass, int n, uint32_t val);
```

**Recommendation: Option A.** The m68k and PPC register sets are too different for a clean generic API. The Platform struct is already backend-specific (it has `use_aline_emulops`). Adding PPC fields that are NULL for m68k backends is cleaner than a leaky abstraction.

### Layer 3: EmulOp Mechanism

m68k uses illegal opcodes (0x71xx for UAE, 0xAExx A-line for Unicorn) to trap into host handlers.

PPC SheepShaver uses a different scheme:
- Base opcode: `0x18000000` (an undefined PowerPC instruction)
- Lower 6 bits encode the operation type
- Bits 20-25 encode NATIVE_OP selector
- Bit 19 = return-via-LR flag

For Unicorn PPC, these will trigger `UC_HOOK_INSN_INVALID` or we can use `sc` (syscall) instructions which trigger `POWERPC_EXCP_SYSCALL`. The SheepShaver approach of using an undefined instruction is cleanest — Unicorn's invalid instruction hook catches it.

### Layer 4: 68k Execution Within PPC

PPC Mac OS still runs 68k code via the Mixed Mode Manager. SheepShaver handles this by:
1. Shadowing 68k registers in PPC GPRs 8-22 (d0-d7 → GPR8-15, a0-a6 → GPR16-22)
2. The ROM contains a 68k interpreter at address 0x460000
3. MODE_68K / MODE_NATIVE / MODE_EMUL_OP tracking in low memory

We need this same mechanism. The 68k interpreter lives in the PPC ROM itself — we don't need UAE or a separate 68k engine. The ROM's built-in interpreter handles it.

### Layer 5: Shared Subsystems (No Change Needed)

These subsystems are already architecture-agnostic:
- XPRAM / RTC
- Disk I/O (Sony, SCSI)
- Video output (triple buffer, WebRTC)
- Audio
- ADB (mouse/keyboard)
- Serial, Ethernet
- ExtFS
- Web server / API handlers
- Command bridge
- Boot progress tracking

The EmulOp handlers in `emul_op.cpp` call these subsystems. We need PPC-specific EmulOp entry points, but the underlying drivers are reusable.

## File Impact Summary

| File | Change | Size |
|------|--------|------|
| `src/config/emulator_config.h` | Fix `cpu_type_int()`, `fpu()` | ~10 lines |
| `src/config/emulator_config.cpp` | Honor `--arch ppc` | ~20 lines |
| `src/main.cpp` | Architecture dispatch | ~20 lines |
| `src/common/include/platform.h` | Add PPC register accessors | ~30 lines |
| `src/cpu/unicorn_wrapper.h` | PPC register functions | ~50 lines |
| `src/cpu/unicorn_wrapper.c` | PPC register impl, fix arch dispatch | ~250 lines |
| `src/cpu/cpu_unicorn_ppc.cpp` | **New**: PPC Unicorn backend | ~800 lines |
| `src/core/cpu_context.cpp` | Implement `init_ppc()` | ~300 lines |
| `src/core/rom_patches_ppc.cpp` | **New**: PPC ROM patching | ~1500 lines |
| `src/core/emul_op_ppc.cpp` | **New**: PPC EmulOp dispatch | ~500 lines |
| `meson.build` | Add new source files, link unicorn PPC | ~30 lines |
