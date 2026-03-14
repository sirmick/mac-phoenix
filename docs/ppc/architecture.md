# PPC Architecture Integration

How PowerPC emulation fits into mac-phoenix's existing codebase.

## Abstraction Layers

### Config

```
EmulatorConfig
  ├── Architecture enum: M68K, PPC
  ├── M68KConfig { cpu_type, fpu, modelid, jit, ... }
  ├── PPCConfig  { cpu_type, fpu, modelid, jit, jit68k, ... }
  ├── --arch m68k|ppc CLI flag
  └── --backend uae|unicorn|kpx CLI flag
```

`is_ppc()` dispatches architecture-specific accessors (`cpu_type_int()`, `fpu()`, etc.).
Backend validation: UAE is M68K-only, KPX is PPC-only, Unicorn supports both.

### Platform API

```
Platform API (g_platform)
  ├── Shared: cpu_init/reset/execute_fast, cpu_get_pc, cpu_trigger_interrupt
  ├── M68K:   cpu_get_sr/dreg/areg, cpu_execute_68k_trap, emulop_handler
  ├── PPC:    cpu_get_gpr/lr/ctr/cr/msr, cpu_set_gpr, cpu_execute_ppc
  └── Shared: mem_read_byte/word/long, mem_write_byte/word/long
```

PPC fields are NULL for M68K backends and vice versa. The Platform struct already
has backend-specific fields (`use_aline_emulops`), so adding PPC-specific ones is
consistent with the existing pattern.

### CPU Context

```
CPUContext
  ├── init_m68k()  — fully implemented (UAE, Unicorn, DualCPU)
  └── init_ppc()   — allocates 512MB VM, loads ROM, patches, starts KPX
```

### CPU Backend

```
CPUBackend enum
  ├── UAE       — M68K only (hand-tuned interpreter)
  ├── Unicorn   — M68K or PPC (QEMU TCG JIT via Unicorn API)
  ├── KPX       — PPC only (Kheperix interpreter, primary backend)
  └── DualCPU   — Validation (UAE+Unicorn for M68K, KPX+Unicorn for PPC)
```

KPX is the primary PPC backend. It's a proven, complete PPC interpreter already
integrated with SheepShaver. Unicorn PPC and DualCPU-PPC are future work.

## EmulOp Mechanism

M68K uses illegal opcodes (0x71xx for UAE, 0xAExx A-line for Unicorn) to trap
into host handlers.

PPC uses SHEEP opcodes: `0x18000000` (undefined PowerPC instruction).
- Lower 6 bits: operation type (0=EMUL_RETURN, 1=EXEC_RETURN, 2=EXEC_NATIVE, ≥3=EMUL_OP)
- Bits 20-25: NATIVE_OP selector
- Bit 19: return-via-LR flag

KPX catches these in its decode loop via a registered opcode handler. The
`sheepshaver_cpu::execute_sheep()` method dispatches them — this is already
implemented in legacy `sheepshaver_glue.cpp`.

## 68k Execution Within PPC

PPC Mac OS runs 68k code via the ROM's built-in DR (Dispatch Router) emulator.
We do NOT need UAE or a separate 68k engine for PPC mode. The ROM contains a
PPC-native 68k interpreter at ~0x460000.

SheepShaver handles 68k ↔ PPC transitions by:
1. Shadowing 68k registers in PPC GPRs 8-22 (d0-d7 → GPR8-15, a0-a6 → GPR16-22)
2. MODE_68K / MODE_NATIVE / MODE_EMUL_OP tracking at XLM_RUN_MODE (0x2810)
3. `execute_68k()` sets up DR emulator registers and enters the interpreter

## Shared Subsystems (No Change Needed)

These subsystems are architecture-agnostic:
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

PPC needs its own EmulOp entry points (`emul_op_ppc.cpp`), but the underlying
drivers are reused directly.

## PPC Video Driver

The one area where PPC differs significantly from M68K is the video driver:

- **M68K**: Simple framebuffer at a fixed address. EmulOp patches handle mode changes.
- **PPC**: Full Mac OS driver model. The system calls `VideoDoDriverIO()` through
  `NATIVE_VIDEO_DO_DRIVER_IO`. The driver resolves TVECTs from VideoServicesLib,
  handles kInitialize/kOpen/kClose/kControl/kStatus, and registers VBL interrupts.

The existing `video.cpp` handles framebuffer management and is architecture-neutral.
The PPC video driver adds the Mac OS driver protocol on top.

## File Impact Summary

| File | Change |
|------|--------|
| `src/config/emulator_config.{h,cpp}` | KPX backend, arch-aware accessors |
| `src/main.cpp` | Architecture dispatch |
| `src/common/include/platform.h` | PPC register accessors |
| `src/cpu/cpu_ppc_kpx.cpp` | KPX Platform API bridge |
| `src/cpu/kpx/` | KPX interpreter (near-verbatim from legacy) |
| `src/core/cpu_context.cpp` | `init_ppc()` implementation |
| `src/core/rom_patches_ppc.cpp` | PPC ROM patching |
| `src/core/emul_op_ppc.cpp` | PPC EmulOp dispatch |
| `src/core/ppc_macos_util.cpp` | FindLibSymbol, ExecuteNative |
| `src/core/name_registry.cpp` | Name Registry (near-verbatim from legacy) |
