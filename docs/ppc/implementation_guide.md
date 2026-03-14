# PPC Implementation Guide for mac-phoenix

## Goal

Add PPC (PowerPC) Mac emulation to mac-phoenix by porting SheepShaver's PPC support
with maximum code reuse from `legacy/SheepShaver/`. The KPX (Kheperix) interpreter
is the CPU backend. Boot target: Mac OS 8.x on an OldWorld 4MB ROM.

## Principles

1. **Copy/paste from legacy first, adapt second.** Don't rewrite what SheepShaver already does.
2. **Minimal KPX modifications.** The `src/cpu/kpx/` files should stay as close to
   `legacy/SheepShaver/src/kpx_cpu/` as possible. Changes only for build integration.
3. **Reuse existing mac-phoenix subsystems** (video, disk, serial, ether, audio, ADB, timer).
4. **Abstraction at Platform API boundary only.** Inside PPC code, use SheepShaver idioms
   (ReadMacInt32, Mac2HostAddr, etc.) — these already work via `mac_memory.h`.
5. **Don't break M68K.** All PPC code is behind `config.is_ppc()` checks or in separate files.

---

## Phase 1: KPX Interpreter (CPU backend)

### Files to create

**`src/cpu/cpu_ppc_kpx.cpp`** — Port of `legacy/SheepShaver/src/kpx_cpu/sheepshaver_glue.cpp`

This is the most important file. It bridges KPX to mac-phoenix's Platform API.

**Copy these verbatim from legacy sheepshaver_glue.cpp:**

- `class sheepshaver_cpu : public powerpc_cpu` (the whole class)
  - Constructor with SHEEP opcode decoder registration
  - `execute_sheep(uint32 opcode)` — SHEEP dispatch (EMUL_RETURN/EXEC_RETURN/EXEC_NATIVE/EMUL_OP)
  - `execute_emul_op(uint32 emul_op)` — marshals GPR8-15→d[0-7], GPR16-22→a[0-6], GPR1→a[7]
  - `execute_68k(uint32 entry, M68kRegisters *r)` — sets up DR emulator registers (r24=68kPC, r25=SR, r27=prefetch, r29/r30=dispatch tables, r31=KD+0x1000), pushes EXEC_RETURN, calls `execute()`
  - `execute_macos_code(uint32 tvect, int nargs, uint32 const *args)` — read proc/toc from TVECT, save/restore r2+args, call `execute(proc)`
  - `execute_ppc(uint32 entry)` — simple wrapper, sets LR to EXEC_RETURN trampoline
  - `interrupt(uint32 entry)` — save PC/LR/CTR/SP, set up nanokernel registers (r1=KernelDataAddr, r6=KD+0x65c context, etc.), rlwimi/cr setup, call `execute(entry)`
  - `HandleInterrupt(powerpc_registers *r)` — poll timer, check IRQ_NEST, dispatch by XLM_RUN_MODE (MODE_68K/MODE_NATIVE/MODE_EMUL_OP)

- `init_emul_ppc()` / `exit_emul_ppc()` / `emul_ppc(uint32 entry)` — create CPU, start execution

**What to change from legacy:**

| Legacy | mac-phoenix | Why |
|--------|-------------|-----|
| `#include "prefs.h"` | Remove; use EmulatorConfig | Config system |
| SDL event pumping in HandleInterrupt | Remove | No SDL |
| SIGSEGV handler setup | Remove | Handled by `sigsegv.cpp` |
| `EmulOp(opcode, r)` call | `ppc_emul_op(selector, r)` | Separate EmulOp file |
| `NativeOp(selector)` call | `ExecuteNative(selector)` | Dispatch in ppc_macos_util |
| Direct global exports | Platform API function pointers | Abstraction layer |
| `SheepMem::Init()` in init_emul_ppc | Already done in cpu_context.cpp | Init order |

**What NOT to change:**

- `execute_68k()` register setup — copy exactly
- `interrupt()` register setup and CR manipulation — copy exactly
- `HandleInterrupt()` mode dispatch logic — copy exactly
- `execute_macos_code()` — copy exactly (no context fixup hacks!)
- Memory access (ReadMacInt32 etc.) — already works via mac_memory.h
- gZeroPage/gKernelData/VMBaseDiff declarations — needed by KPX vm.hpp

**Platform API bridge (bottom of file):**

```cpp
static void kpx_cpu_execute_fast() { emul_ppc(ROMBase + 0x310000); }
static void kpx_cpu_stop() { s_ppc_cpu->stop(); }
static void kpx_cpu_trigger_interrupt(int level) { s_ppc_cpu->trigger_interrupt(); }
// ... register accessors ...

void cpu_ppc_kpx_install(Platform &p) {
    p.cpu_ppc_execute = kpx_cpu_execute_fast;
    p.cpu_ppc_stop = kpx_cpu_stop;
    p.cpu_ppc_interrupt = kpx_cpu_trigger_interrupt;
    // ... etc
}
```

### KPX interpreter files (`src/cpu/kpx/`)

These are already committed in the scaffolding. Keep them as-is from legacy. Required changes:

- `ppc-config.hpp`: Set `PPC_DECODE_CACHE 1`, `PPC_ENABLE_JIT 0` (interpreter only)
- `ppc-cpu.cpp`: No changes needed beyond what's committed
- `ppc-execute.cpp`: No changes needed. Do NOT add debug tracing to hot paths.
- `vm.hpp`: Ensure DIRECT_ADDRESSING mode uses VMBaseDiff
- `spcflags.hpp`: Must support `trigger_interrupt()` from timer thread (atomic flag)
- `meson.build`: Build as static library, link to mac-phoenix

**Do NOT add to KPX files:**
- Per-instruction PC tracing
- Per-instruction PC range checks
- Per-instruction sample counters
- fprintf calls in interpret/decode-cache loops

---

## Phase 2: ROM Patching

### `src/core/rom_patches_ppc.cpp`

Already committed and close to legacy. Key functions (all copy/paste from `legacy/SheepShaver/src/rom_patches.cpp`):

- `decode_lzss()`, `decode_parcels()` — verbatim
- `DecodeROM_PPC()` — ROM type detection (Gossamer, NewWorld, etc.)
- `PatchROM_PPC()` — calls patch_nanokernel_boot/patch_68k_emul/patch_nanokernel/patch_68k
- `patch_nanokernel_boot()` — boot structure at ROM+0x30d000
- `patch_68k_emul()` — DR emulator patches
- `patch_nanokernel()` — nanokernel code patches
- `patch_68k()` — 68k ROM code patches, EMUL_OP insertion
- `InstallDrivers_PPC()` — driver installation

**What to add (from legacy `main_unix.cpp` lines 225-254):**

`InitXLM()` — initialize the XLM (Extra Low Memory) area:
```cpp
void InitXLM() {
    // Zero low memory
    Mac_memset(0, 0, 0x3000);
    // Signature
    WriteMacInt32(XLM_SIGNATURE, FOURCC('B','a','a','h'));
    WriteMacInt32(XLM_KERNEL_DATA, KernelDataAddr);
    WriteMacInt32(XLM_PVR, PVR);
    WriteMacInt32(XLM_BUS_CLOCK, BusClockSpeed);
    WriteMacInt32(XLM_EXEC_RETURN_OPCODE, M68K_EXEC_RETURN);
    WriteMacInt32(XLM_ZERO_PAGE, SheepMem::ZeroPage());
    // SHEEP trampolines, native op addresses, etc.
    // Copy from legacy main_unix.cpp
}
```

`InitKernelData()` — initialize the KernelData structure at `KernelDataAddr`:
```cpp
void InitKernelData() {
    // Zero 0x2000 bytes at KernelDataAddr
    Mac_memset(KernelDataAddr, 0, 0x2000);
    // ROM type-specific CPU descriptors (cache, TLB, etc.)
    // Copy from legacy rom_patches.cpp patch_nanokernel_boot()
}
```

These are called from `cpu_context.cpp::init_ppc()` after PatchROM_PPC().

---

## Phase 3: EmulOp Dispatch

### `src/core/emul_op_ppc.cpp`

Port of `legacy/SheepShaver/src/emul_op.cpp`. The OP_* case table is identical.

**Copy verbatim from legacy:**
- All ~35 OP_* cases (OP_FIX_MEMTOP through OP_CHECKLOAD)
- All driver dispatch cases (OP_SONY_*, OP_DISK_*, OP_CDROM_*, OP_SCSI_*)
- Timer cases (OP_INSTIME, OP_RMVTIME, OP_PRIMETIME)
- OP_IRQ handler (60Hz interrupt processing)

**Wrapper function:**
```cpp
void ppc_emul_op(uint32 selector, M68kRegisters *r) {
    // Direct equivalent of legacy EmulOp()
    switch (selector) {
        case OP_FIX_MEMTOP: ...
        // all cases from legacy emul_op.cpp
    }
}
```

**What NOT to add:**
- WLSC warm start hacks
- Forced 68k interrupt injection in HandleInterrupt
- Extensive fprintf logging on every OP_IRQ

The OP_IRQ handler should match legacy exactly:
```cpp
case OP_IRQ:
    WriteMacInt16(ReadMacInt32(KernelDataAddr + 0x67c), 0);
    r->d[0] = 0;
    if (InterruptFlags & INTFLAG_60HZ) {
        ClearInterruptFlag(INTFLAG_60HZ);
        // TimerInterrupt, ADBInterrupt, etc. — same as legacy
    }
    // ... INTFLAG_SERIAL, INTFLAG_ETHER, etc.
    break;
```

---

## Phase 4: Mac OS Utilities

### `src/core/ppc_macos_util.cpp`

Port of PPC-specific functions from `legacy/SheepShaver/src/macos_util.cpp`.

**Copy verbatim:**
- `FindLibSymbol()` — CFM shared library lookup (68k CFMDispatch trap)
- `InitCallUniversalProc()` — resolve InterfaceLib TVECTs
- `CallUniversalProc()` — wrapper

**Add:**
- `ExecuteNative(int selector)` — dispatch table for NATIVE_* ops
  (Port from `legacy/SheepShaver/src/kpx_cpu/sheepshaver_glue.cpp` execute_native_op)

### `src/core/name_registry.cpp`

**Copy verbatim** from `legacy/SheepShaver/src/name_registry.cpp`. This file is already 98%
identical. The only change: include paths.

### `src/common/include/name_registry.h`

Header with `DoPatchNameRegistry()` and `PatchNameRegistry()` declarations.

---

## Phase 5: Native Operations (Thunks)

### How NATIVE_OP works in legacy

1. `rom_patches_ppc.cpp` writes PPC SHEEP opcodes into ROM at specific addresses
2. When the nanokernel executes one, KPX calls `execute_sheep()` → `execute_native_op(selector)`
3. `execute_native_op()` is a giant switch dispatching to C++ functions
4. Key selectors:
   - `NATIVE_PATCH_NAME_REGISTRY` → `DoPatchNameRegistry()`
   - `NATIVE_VIDEO_DO_DRIVER_IO` → `VideoDoDriverIO()`
   - `NATIVE_ETHER_*` → ethernet driver functions
   - `NATIVE_SERIAL_*` → serial driver functions
   - `NATIVE_GET_RESOURCE` → resource patching

### What to implement

The `execute_native_op()` switch in cpu_ppc_kpx.cpp, copied from legacy
sheepshaver_glue.cpp lines 1048-1155. Arguments come from GPR3-GPR7.

---

## Phase 6: Video Driver (PPC-specific)

### Why PPC video is different

M68K video: Simple framebuffer at a fixed address. EmulOp patches in ROM handle
video mode changes.

PPC video: Full Mac OS driver model. The system calls `VideoDoDriverIO()` through
a NATIVE_OP. The driver must:
1. Resolve TVECTs from VideoServicesLib and DriverServicesLib (via FindLibSymbol)
2. Handle kInitialize/kOpen/kClose/kFinalize/kControl/kStatus commands
3. Register VBL interrupt service (VSLNewInterruptService)
4. Report video parameters (depth, resolution, base address)

### Implementation

**Copy `VideoDoDriverIO()` from `legacy/SheepShaver/src/video.cpp`** (lines ~1030-1180).

This function already exists in the legacy code and handles all the command dispatch.
The key is that it calls `FindLibSymbol()` to resolve TVECTs during kInitialize, so
FindLibSymbol must be working first.

The existing mac-phoenix `video.cpp` handles framebuffer management and is
architecture-neutral. The PPC video driver adds the Mac OS driver protocol on top.

---

## Phase 7: CPU Context & Config Integration

### `src/core/cpu_context.cpp::init_ppc()`

Already partially implemented. The flow:

```
1. Allocate 512MB VM space (mmap)
2. Map ROM into VM at PPC_ROM_BASE (0x400000)
3. Load ROM file
4. Set globals: RAMBaseHost, ROMBaseHost, RAMBaseMac, ROMBaseMac, VMBaseDiff
5. init_mac_subsystems() — shared with M68K (disk, video, serial, ether, audio)
6. CheckROM_PPC() / DecodeROM_PPC()
7. PatchROM_PPC()
8. InitXLM() — set up XLM area
9. InitKernelData() — set up nanokernel data structures
10. SheepMem::Init() — allocate thunks area at top of RAM
11. Install CPU backend (cpu_ppc_kpx_install)
12. Set GPR3 = ROMBase + 0x30d000, GPR4 = KernelDataAddr + 0x1000
13. Start execution: cpu_ppc_execute()
```

### Config changes

Already in place: `--arch ppc`, `--backend kpx`, `PPCConfig` struct.
No additional config changes needed.

---

## Phase 8: What NOT to Do

These are mistakes from the previous attempt that should be avoided:

1. **Don't add g_ppc_context_ptr hacks.** If KD+0x65c == KD+0x658, the nanokernel
   initialization didn't work. Fix the root cause (InitKernelData, ROM patching)
   instead of patching around it.

2. **Don't add forced 68k interrupt injection in HandleInterrupt.** The legacy
   HandleInterrupt writes the poll word and CR bits for MODE_68K — that's it.
   The DR emulator handles the rest. If interrupts aren't being consumed, the
   issue is elsewhere (wrong CR bits, wrong poll word address, etc.).

3. **Don't add per-instruction debug tracing to KPX.** The interpreter runs at
   300M IPS. Adding fprintf or even a branch to every instruction kills performance.
   Use breakpoint-style tracing (check specific PC values) if needed.

4. **Don't add WLSC warm start hacks to OP_NVRAM1.** The warm start flag ($0CFC)
   is written by ROM code naturally during boot. If it's not being written,
   the boot sequence hasn't reached that point yet.

5. **Don't modify execute_macos_code.** The legacy version is 20 lines and works.
   No context fixups, no extra register saves, no KernelData manipulation.

6. **Don't put video driver code in cpu_ppc_kpx.cpp.** The video driver belongs
   in video.cpp (or a PPC-specific video file), dispatched through NATIVE_VIDEO_DO_DRIVER_IO
   in execute_native_op(). Keep cpu_ppc_kpx.cpp focused on CPU glue.

---

## File Summary

| File | Source | Notes |
|------|--------|-------|
| `src/cpu/cpu_ppc_kpx.cpp` | sheepshaver_glue.cpp | Main KPX-to-platform bridge |
| `src/cpu/kpx/**` | kpx_cpu/ | KPX interpreter, minimal changes |
| `src/core/rom_patches_ppc.cpp` | rom_patches.cpp | ROM patching + InitXLM/InitKernelData |
| `src/core/emul_op_ppc.cpp` | emul_op.cpp | EmulOp dispatch, verbatim cases |
| `src/core/ppc_macos_util.cpp` | macos_util.cpp | FindLibSymbol, CallUniversalProc |
| `src/core/name_registry.cpp` | name_registry.cpp | Nearly verbatim copy |
| `src/core/cpu_context.cpp` | main_unix.cpp (init) | PPC memory layout, init sequence |

## Boot Sequence Summary

```
cpu_context::init_ppc()
  → PatchROM_PPC() → InitXLM() → InitKernelData()
  → cpu_ppc_kpx_install()
  → GPR3 = ROM+0x30d000, GPR4 = KD+0x1000
  → execute(ROM+0x310000)
    → Nanokernel boots, creates context blocks, enters DR emulator
    → DR emulator runs 68k ROM code
    → OP_RESET → OP_FIX_MEMSIZE → OP_NAME_REGISTRY → OP_INSTALL_DRIVERS
    → Boot blocks → Extensions → Finder
```

The nanokernel creates TWO EmulatorData context blocks during its own
initialization (using the KernelData structure we set up). If the nanokernel
init fails or produces only one context, the bug is in InitKernelData() or
patch_nanokernel_boot() — not in execute_macos_code.
