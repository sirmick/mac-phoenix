# PPC Implementation Plan

Comprehensive plan for adding PowerPC emulation to mac-phoenix, incorporating both **Unicorn PPC** and **KPX CPU** backends, plus **DualCPU-PPC** validation mode.

---

## Architecture Overview

```
                    ┌─────────────┐
                    │ EmulatorConfig │
                    │ --arch ppc    │
                    │ --backend X   │
                    └──────┬──────┘
                           │
            ┌──────────────┼──────────────┐
            │              │              │
    ┌───────▼──────┐ ┌────▼─────┐ ┌──────▼──────┐
    │ Unicorn PPC  │ │ KPX CPU  │ │ DualCPU-PPC │
    │ (QEMU JIT)   │ │ (Kheperix│ │ (Unicorn +  │
    │              │ │  JIT)    │ │  KPX lockstep│
    └───────┬──────┘ └────┬─────┘ └──────┬──────┘
            │              │              │
            └──────────────┼──────────────┘
                           │
                    ┌──────▼──────┐
                    │ Platform API │  ← PPC register extensions
                    │ (g_platform) │
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
        ┌─────▼────┐ ┌────▼────┐ ┌────▼─────┐
        │ PPC ROM   │ │ PPC     │ │ Shared   │
        │ Patches   │ │ EmulOp  │ │ Drivers  │
        │ (Gossamer)│ │ Dispatch│ │ (reuse)  │
        └──────────┘ └─────────┘ └──────────┘
```

## CPU Backend Comparison

| Backend | Source | JIT | Interface | Drop-in? | Notes |
|---------|--------|-----|-----------|----------|-------|
| **Unicorn PPC** | QEMU TCG via Unicorn API | Yes (TCG) | `uc_*` C API | Moderate | Same pattern as m68k Unicorn backend |
| **KPX CPU** | `legacy/SheepShaver/src/kpx_cpu/` | Yes (dyngen) | C++ `powerpc_cpu` class | Near drop-in | Complete PPC interpreter+JIT, already has SheepShaver glue |
| **DualCPU-PPC** | New (validation) | N/A | Wraps both | New | Lockstep Unicorn+KPX for debugging |

### KPX CPU Assessment

The KPX (Kheperix) CPU is a **complete, proven PPC emulator** already integrated with SheepShaver. Key files:

- `legacy/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.hpp` — `powerpc_cpu` class (500 lines)
- `legacy/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp` — Core implementation (840 lines)
- `legacy/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-execute.cpp` — Instruction execution
- `legacy/SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-jit.cpp` — JIT compilation
- `legacy/SheepShaver/src/kpx_cpu/sheepshaver_glue.cpp` — `sheepshaver_cpu` subclass (1253 lines)

**Interface**: `sheepshaver_cpu` extends `powerpc_cpu` with:
- `execute_sheep(opcode)` — SHEEP opcode dispatch (EMUL_RETURN, EXEC_RETURN, EXEC_NATIVE, EMUL_OP)
- `execute_emul_op(selector)` — Maps GPR8-22 to M68kRegisters, calls EmulOp()
- `execute_native_op(selector)` — NATIVE_OP dispatch (video, ether, serial, etc.)
- `execute_68k(entry, regs)` — Run 68k code within PPC context
- `interrupt(entry)` — Handle 60Hz interrupt via nanokernel entry
- `trigger_interrupt()` — Set SPCFLAG for deferred interrupt handling

**JIT**: dyngen-based, translates PPC→host native code. Supports x86, x86-64, PPC, MIPS targets. Enabled via `PPC_ENABLE_JIT` define.

**Drop-in assessment**: Nearly drop-in. The `sheepshaver_glue.cpp` already does everything we need — SHEEP opcode handling, EmulOp dispatch, interrupt handling, Execute68k. We need to:
1. Wrap it behind our Platform API
2. Replace `ReadMacInt32`/`WriteMacInt32` with our memory accessors
3. Replace `PrefsFindBool` with our config system
4. Build it with our meson build system (currently autoconf/make)

---

## Name Collision Analysis

### Globals That Need Namespacing

These globals are used by both m68k and PPC code paths. Currently m68k-only but will conflict:

| Global | Current Use | Collision Risk | Resolution |
|--------|-------------|----------------|------------|
| `RAMBaseHost` | Host pointer to RAM | Both architectures | Keep shared (same RAM buffer) |
| `ROMBaseHost` | Host pointer to ROM | Different ROM locations | `m68k_ROMBaseHost` / `ppc_ROMBaseHost` |
| `RAMSize` | RAM size | Shared | Keep shared |
| `ROMSize` | ROM size | Different sizes (1MB vs 4MB) | `m68k_ROMSize` / `ppc_ROMSize` |
| `ROMBaseMac` | Mac address of ROM | 0x2000000 (m68k) vs 0x400000 (ppc) | Architecture-dispatched |
| `CPUType` | 68k CPU type (0-4) | PPC doesn't use | Guard with `#if EMULATED_68K` |
| `FPUType` | FPU type | PPC doesn't use | Guard with `#if EMULATED_68K` |
| `TwentyFourBitAddressing` | 24-bit mode | PPC always 32-bit | Guard |
| `ROMVersion` | ROM version enum | Different enum for PPC | `m68k_ROMVersion` / `ppc_ROMType` |
| `ScratchMem` | Fake HW base pointer | PPC uses kernel data instead | Architecture-conditional |
| `MEMBaseDiff` | Direct addressing offset | Different for PPC memory map | Architecture-dispatched |
| `MacFrameBaseMac` | Framebuffer Mac address | Different layout | Architecture-dispatched |

### Function Name Collisions

| Function | m68k | PPC Equivalent | Resolution |
|----------|------|----------------|------------|
| `PatchROM()` | `rom_patches.cpp` | `PatchROM_PPC()` | New function |
| `CheckROM()` | `rom_patches.cpp` | `CheckROM_PPC()` | New function |
| `EmulOp()` | `emul_op.cpp` | `EmulOp_PPC()` | New function |
| `cpu_uae_install()` | m68k only | N/A | No conflict |
| `cpu_unicorn_install()` | m68k | `cpu_ppc_unicorn_install()` | Rename PPC variant |
| `cpu_dualcpu_install()` | m68k | `cpu_ppc_dualcpu_install()` | Rename PPC variant |
| `Init680x0()` | m68k only | N/A (PPC doesn't need UAE banking) | No conflict |

### CPUBackend Enum Extension

```cpp
enum class CPUBackend {
    UAE,           // M68K only: Original interpreter
    Unicorn,       // M68K: Unicorn m68k; PPC: Unicorn PPC
    KPX,           // PPC only: Kheperix interpreter/JIT
    DualCPU,       // M68K: UAE+Unicorn; PPC: KPX+Unicorn
};
```

Backend validity matrix:

| Backend | M68K | PPC |
|---------|------|-----|
| UAE | Yes | No |
| Unicorn | Yes | Yes |
| KPX | No | Yes |
| DualCPU | Yes (UAE+Unicorn) | Yes (KPX+Unicorn) |

---

## Phase 0: Unicorn PPC Build & Smoke Test

**Goal**: Prove Unicorn PPC works in our build environment.

**Prereq**: Rebuild Unicorn with PPC support:
```bash
cd subprojects/unicorn/build
cmake .. -DUNICORN_ARCH="m68k;ppc"
cmake --build . -j$(nproc)
```

**Files**:
- `tests/test_unicorn_ppc.cpp` (new) — Standalone PPC instruction test
- `subprojects/unicorn/build/CMakeCache.txt` (modify)

**Work**:
1. Rebuild Unicorn with `m68k;ppc`
2. Write test: `uc_open(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)`
3. Test basic instructions (li, add, blr)
4. Test `UC_CPU_PPC32_750_V2_0` model selection
5. Test `UC_HOOK_INSN_INVALID` fires for SHEEP opcodes (0x18000003)
6. Test `UC_HOOK_BLOCK` fires
7. Add to meson test suite

**Est**: ~200 lines, 1 session.

---

## Phase 1: KPX CPU Build Integration

**Goal**: Get the KPX CPU compiling within mac-phoenix's meson build.

**Files**:
- `src/cpu/kpx/` (new directory) — Extracted KPX source
- `src/cpu/kpx/meson.build` (new)
- `meson.build` (modify)

**Work**:
1. Copy essential KPX files from `legacy/SheepShaver/src/kpx_cpu/` into `src/cpu/kpx/`:
   - `src/cpu/ppc/ppc-cpu.{hpp,cpp}` — Core CPU class
   - `src/cpu/ppc/ppc-execute.{hpp,cpp}` — Instruction execution
   - `src/cpu/ppc/ppc-decode.cpp` — Instruction decoder
   - `src/cpu/ppc/ppc-translate.cpp` — Instruction translation
   - `src/cpu/ppc/ppc-registers.hpp` — Register definitions
   - `src/cpu/ppc/ppc-instructions.hpp` — Instruction definitions
   - `src/cpu/ppc/ppc-operations.hpp` — ALU operations
   - `src/cpu/ppc/ppc-config.hpp` — Config defines
   - `src/cpu/ppc/ppc-bitfields.hpp` — Bit field helpers
   - `src/cpu/ppc/ppc-blockinfo.hpp` — Block cache info
   - `src/cpu/block-cache.hpp` — Block cache
   - `src/cpu/spcflags.hpp` — Special flags
   - `src/cpu/vm.hpp` — Virtual memory helpers
   - `include/basic-cpu.hpp`, `basic-blockinfo.hpp`, `block-alloc.hpp`, `nvmemfun.hpp`
   - `src/mathlib/` — Math library (IEEE FP)
   - `sheepshaver_glue.cpp` (will be adapted in Phase 5)
2. Create `sysdeps.h` compatibility shim for mac-phoenix
3. Create meson.build that compiles KPX as `kpx_lib` static library
4. Initially compile with JIT disabled (`PPC_ENABLE_JIT=0`) for simplicity
5. Verify it compiles and links (no functional test yet)

**Key decisions**:
- KPX uses `vm_read_memory_4()` macro that reads from direct host memory. This maps well to our `RAMBaseHost + offset` pattern.
- KPX's `SheepMem` class allocates memory for thunks in Mac address space — need to port or replace.
- The dyngen JIT can be enabled later after interpreter works.

**Est**: ~500 lines of build config + shims, 2 sessions.

---

## Phase 2: Config & Backend Wiring

**Goal**: `--arch ppc --backend unicorn|kpx` selects PPC code paths.

**Files**:
- `src/config/emulator_config.h` (modify)
- `src/config/emulator_config.cpp` (modify)
- `src/main.cpp` (modify)
- `src/common/include/platform.h` (modify)

**Work**:

### 2a: Config Changes
```cpp
// Add KPX to CPUBackend enum
enum class CPUBackend {
    UAE,      // M68K only
    Unicorn,  // M68K or PPC
    KPX,      // PPC only (Kheperix)
    DualCPU   // Validation mode
};

// Fix architecture-aware accessors
int cpu_type_int() const {
    return (architecture == Architecture::PPC) ? ppc.cpu_type : m68k.cpu_type;
}
bool fpu() const {
    return (architecture == Architecture::PPC) ? ppc.fpu : m68k.fpu;
}
bool is_ppc() const { return architecture == Architecture::PPC; }
```

### 2b: Platform API PPC Extensions
```c
// Add to Platform struct:
// PPC register accessors (NULL for m68k backends)
uint32_t (*cpu_get_gpr)(int n);        // R0-R31
void     (*cpu_set_gpr)(int n, uint32_t val);
uint32_t (*cpu_get_lr)(void);
uint32_t (*cpu_get_ctr)(void);
uint32_t (*cpu_get_cr)(void);
uint32_t (*cpu_get_msr)(void);
uint32_t (*cpu_get_xer)(void);

// PPC-specific execution
void (*cpu_execute_ppc)(uint32_t entry);  // Execute PPC code at address
```

### 2c: Main Wiring
```cpp
// In main.cpp, after backend install:
if (emu_config.is_ppc()) {
    // Validate backend choice
    if (emu_config.cpu_backend == config::CPUBackend::UAE) {
        fprintf(stderr, "ERROR: UAE backend not available for PPC\n");
        return 1;
    }
    switch (emu_config.cpu_backend) {
        case config::CPUBackend::Unicorn:
            cpu_ppc_unicorn_install(platform);
            break;
        case config::CPUBackend::KPX:
            cpu_ppc_kpx_install(platform);
            break;
        case config::CPUBackend::DualCPU:
            cpu_ppc_dualcpu_install(platform);
            break;
    }
    if (!g_cpu_ctx.init_ppc(emu_config)) { ... }
} else {
    // existing m68k path
}
```

**Est**: ~150 lines, 1 session.

---

## Phase 3: Unicorn Wrapper PPC Support

**Goal**: `unicorn_wrapper.c` supports PPC alongside m68k.

**Files**:
- `src/cpu/unicorn_wrapper.h` (modify)
- `src/cpu/unicorn_wrapper.c` (modify)

**Work**:
1. Fix `unicorn_create_with_model()` to dispatch on `UnicornArch`:
   - `UCPU_ARCH_M68K` → `UC_ARCH_M68K, UC_MODE_BIG_ENDIAN`
   - `UCPU_ARCH_PPC` → `UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN`
2. Add PPC register accessors (GPR, LR, CTR, CR, MSR, XER, PC)
3. Add PPC deferred update queues
4. Store `UnicornArch` in `UnicornCPU` struct for runtime dispatch
5. Modify `apply_deferred_updates_and_flush()` for PPC registers

**Est**: ~300 lines, 1 session.

---

## Phase 4: PPC Unicorn Backend

**Goal**: `cpu_ppc_unicorn_install()` provides a working Platform API for PPC.

**Files**:
- `src/cpu/cpu_ppc_unicorn.cpp` (new)

**Work**:
1. `cpu_ppc_unicorn_install(Platform *p)` fills in:
   - `cpu_name = "Unicorn PPC"`
   - `use_aline_emulops = false`
   - All lifecycle functions
   - PPC register query functions
   - Memory access (same big-endian helpers as m68k)
2. `unicorn_ppc_init()`:
   - Create engine: `UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN`
   - CPU model: `UC_CPU_PPC32_750_V2_0`
   - Map memory: RAM, ROM, KernelData, ScratchMem, FrameBuffer, MMIO
   - Register hooks:
     - `UC_HOOK_INSN_INVALID` → SHEEP opcode handler
     - `UC_HOOK_BLOCK` → timer polling, interrupt delivery
     - `UC_HOOK_MEM_READ_UNMAPPED` / `UC_HOOK_MEM_WRITE_UNMAPPED` → dummy bank
3. `unicorn_ppc_reset()`:
   - PC = ROM nanokernel entry
   - R1 = stack (RAM + 4MB)
   - R3 = ROMBase + 0x30d000
   - R4 = KernelDataAddr + 0x1000
4. SHEEP opcode handler (from invalid instruction hook):
   ```c
   if ((opcode & 0xFC000000) == 0x18000000) {
       switch (opcode & 0x3F) {
           case 0: quit_emulator(); break;
           case 1: exec_return(); break;
           case 2: execute_native_op(NATIVE_OP_FIELD(opcode)); break;
           default: execute_emul_op((opcode & 0x3F) - 3); break;
       }
   }
   ```
5. EmulOp handler: read GPR8-22 → M68kRegisters, call EmulOp_PPC(), write back
6. Interrupt delivery: write to KernelData flags (same approach as SheepShaver's `HandleInterrupt`)

**Est**: ~800 lines, 2-3 sessions.

---

## Phase 5: KPX CPU Backend

**Goal**: `cpu_ppc_kpx_install()` wraps KPX behind Platform API.

**Files**:
- `src/cpu/cpu_ppc_kpx.cpp` (new)

**Work**:
1. Create `sheepshaver_cpu` instance (adapted from `sheepshaver_glue.cpp`)
2. Map Platform API to KPX:
   - `cpu_init` → `new sheepshaver_cpu()`, set registers
   - `cpu_reset` → set PC/GPRs to boot entry
   - `cpu_execute_one` → `ppc_cpu->execute(entry)`
   - `cpu_get_pc` → `ppc_cpu->get_register(powerpc_registers::PC).i`
   - `cpu_get_gpr(n)` → `ppc_cpu->get_register(powerpc_registers::GPR(n)).i`
   - `cpu_trigger_interrupt` → `ppc_cpu->trigger_interrupt()`
3. Replace SheepShaver's `ReadMacInt32`/`WriteMacInt32` with our Platform memory API
4. Replace `PrefsFindBool("jit")` with `emu_config.ppc.jit`
5. Wire up `HandleInterrupt()` to use our timer system
6. Wire up `Execute68k()` and `Execute68kTrap()`

**Key**: The KPX CPU already handles everything internally (SHEEP opcodes, EmulOp dispatch, interrupt handling, 68k execution). The Platform API wrapper is thinner than Unicorn's because KPX does its own dispatch.

**Est**: ~600 lines, 2 sessions.

---

## Phase 6: PPC CPU Context Init

**Goal**: `init_ppc()` loads ROM, allocates memory, sets up kernel data.

**Files**:
- `src/core/cpu_context.cpp` (modify — implement `init_ppc()`)

**Work**:
1. Allocate memory:
   - RAM: configurable (32-256 MB)
   - ROM area: 5 MB (ROM_AREA_SIZE, at Mac address 0x400000)
   - KernelData: 8 KB at 0x68FFE000
   - ScratchMem: 64 KB
   - FrameBuffer: 8 MB
2. Load ROM file (4 MB for Gossamer)
3. Detect ROM type via ID string at offset 0x30d064
4. Decode if compressed (LZSS/parcels)
5. Set global pointers:
   - `RAMBaseHost`, `RAMSize` (shared with m68k path)
   - `ppc_ROMBaseHost` (different from m68k)
   - `ppc_ROMBaseMac = 0x00400000`
6. Initialize XLM globals at 0x2800:
   - XLM_SIGNATURE = "Baah"
   - XLM_KERNEL_DATA = 0x68FFE000
   - XLM_PVR = 0x00080200 (750 v2.0)
   - XLM_RUN_MODE = MODE_68K
7. Initialize KernelData structure at 0x68FFE000
8. Initialize shared subsystems (same as m68k: XPRAM, disk, video, ADB)
9. Install PPC backend (Unicorn or KPX)
10. Apply PPC ROM patches (Phase 7)
11. Call `platform_.cpu_init()` and `platform_.cpu_reset()`
12. Set up 60Hz timer

**Est**: ~350 lines, 1-2 sessions.

---

## Phase 7: PPC ROM Patching (Gossamer)

**Goal**: Port SheepShaver's ROM patches for Gossamer ROM.

**Files**:
- `src/core/rom_patches_ppc.cpp` (new)
- `src/core/rom_patches_ppc.h` (new)

This is the **largest and most critical phase**. Detailed breakdown in `docs/ppc/rom_patching.md`.

### Sub-phases:

**7a: ROM Detection & Decoding** (~200 lines)
- Port `decode_lzss()`, `decode_parcels()`
- ROM type detection via 0x30d064 ID string
- Reject non-Gossamer initially

**7b: Nanokernel Boot Patches** (~400 lines)
- Port `patch_nanokernel_boot()` — boot structure setup, SR/BAT bypass, PVR patches
- Critical: without this, ROM crashes immediately
- Key addresses: 0x30d000 (boot struct), 0x310000 (nanokernel entry)

**7c: Nanokernel Runtime Patches** (~200 lines)
- Port `patch_nanokernel()` — exception table, FPU, trap return
- Mixed mode trap: `mr r31, r27` (identity mapping V=P)
- DEC timer skip: `li r31, 0`

**7d: 68k Emulator Patches** (~300 lines)
- Port `patch_68k_emul()` — TWI replacement, EMUL_OP table
- Install EmulOp dispatch at ROM 0x380000+
- Entry points at 0x36f900-0x36fd00

**7e: 68k Code Patches** (~300 lines)
- Port `patch_68k()` — NVRAM, drivers, memory setup
- Gossamer-specific: UniversalInfo, GC mask, SCSI init
- Boot code: remove RESET, fake PowerMac ID

**7f: Driver Installation** (~100 lines)
- Port `InstallDrivers()` — Sony, Disk, CDROM, Serial stubs
- These insert EmulOp traps into driver resource forks

**Est**: ~1500 lines total, 3-5 sessions.

---

## Phase 8: PPC EmulOp Dispatch

**Goal**: Handle EMUL_OP selectors from PPC context.

**Files**:
- `src/core/emul_op_ppc.cpp` (new)
- `src/core/emul_op_ppc.h` (new)

**Work**:
1. `EmulOp_PPC(uint32_t selector, M68kRegisters *regs)` dispatcher
2. Map selectors to existing mac-phoenix subsystems:
   - OP_XPRAM → existing XPRAM code
   - OP_SONY/DISK/CDROM → existing disk drivers
   - OP_ADBOP → existing ADB code
   - OP_INSTIME/RMVTIME/PRIMETIME → existing timer code
   - OP_INSTALL_DRIVERS → PPC driver installation
   - OP_RESET → reset handler
   - OP_IRQ → interrupt dispatch
   - OP_EXTFS → existing ExtFS code
3. PPC-specific selectors not in m68k:
   - OP_NAME_REGISTRY → PPC Name Registry patching
   - OP_CHECK_SYSV → System version check
   - OP_CHECK_LOAD_INVOC → boot progress tracking
4. NATIVE_OP dispatch (video VBL, ethernet, serial thunks)

**Est**: ~500 lines, 1-2 sessions.

---

## Phase 9: DualCPU-PPC Validation Backend

**Goal**: Lockstep KPX + Unicorn PPC for early debugging.

**Files**:
- `src/cpu/cpu_ppc_dualcpu.cpp` (new)

**Work**:
1. Pattern after existing `cpu_dualcpu.c` (m68k UAE+Unicorn lockstep)
2. Primary CPU: KPX (proven, more likely correct)
3. Secondary CPU: Unicorn PPC (new, needs validation)
4. After each SHEEP opcode / block boundary:
   - Compare PC, GPR0-31, LR, CTR, CR, XER
   - Log divergences with full register dumps
5. Configurable: `--arch ppc --backend dualcpu`

**Est**: ~400 lines, 1-2 sessions. Defer until both backends work individually.

---

## Phase 10: Boot Testing Strategy

### Milestone 1: Nanokernel Entry (Week 1)

**Test**: ROM loads, nanokernel boot code executes, reaches 68k emulator entry.

**Verification**:
- Log PPC PC at each SHEEP opcode hit → proves patches work
- Log EmulOp selectors fired → proves dispatch works
- CPU state dumps at key addresses (0x310000, 0x314000, 0x460000)

**CLI**: `./build/mac-phoenix --arch ppc --rom gossamer.rom --timeout 5 --no-webserver --log-level 3`

**Expected output**:
```
[PPC] Nanokernel boot at PC=0x00710000
[PPC] PVR patch: loading 0x00080200 from XLM_PVR
[PPC] 68k emulator entry at PC=0x00860000
[PPC] EMUL_OP: selector=37 (INSTALL_DRIVERS)
```

### Milestone 2: EmulOp Logging (Week 2)

**Test**: All EmulOp selectors fire correctly during boot.

**Verification**:
- Log every EmulOp with selector name, GPR8-15 (d-regs), GPR16-22 (a-regs)
- Compare sequence with SheepShaver reference boot log
- Track boot progress via OP_CHECK_LOAD_INVOC calls

**Expected sequence**: INSTALL_DRIVERS → XPRAM → FIX_MEMTOP → DISK_OPEN → VIDEO_DOIO → ...

### Milestone 3: Screenshot of Early Boot (Week 3-4)

**Test**: Framebuffer shows something recognizable.

**Verification**:
- `/api/screenshot` returns non-black PNG
- Visible: grey desktop pattern, "Welcome to Macintosh", or extension parade
- Compare with SheepShaver screenshot at same boot stage

**CLI**: `./build/mac-phoenix --arch ppc --rom gossamer.rom --disk macos81.img --timeout 30 --screenshots`

### Milestone 4: Boot to Finder (Week 4-6)

**Test**: Full boot to desktop.

**Verification**:
- Boot phase reaches "Finder" or "desktop"
- Mouse/keyboard work via ADB EmulOps
- `/api/status` shows correct PPC boot phases
- Screenshot shows Finder desktop

**Test script**: `tests/test_boot_ppc.sh` (modeled on existing `test_boot_to_finder.sh`)

---

## Build System Changes

### meson_options.txt additions:
```meson
option('ppc_rom',
  type: 'string',
  value: '',
  description: 'Path to PPC ROM file for boot tests'
)
```

### meson.build changes:
```meson
# PPC CPU sources
ppc_cpu_sources = [
    'src/cpu/cpu_ppc_unicorn.cpp',
    'src/cpu/cpu_ppc_kpx.cpp',
    'src/cpu/cpu_ppc_dualcpu.cpp',
]

# KPX library (separate due to different compile flags)
subdir('src/cpu/kpx')

# Add to main executable link
macemu_next_link_with += [kpx_lib]
```

### Unicorn rebuild:
```bash
# One-time rebuild to add PPC support
cd subprojects/unicorn/build
cmake .. -DUNICORN_ARCH="m68k;ppc"
cmake --build . -j$(nproc)
```

Verify: `nm subprojects/unicorn/build/libunicorn.a | grep ppc_cpu`

---

## Risk Registry

| # | Risk | Impact | Likelihood | Mitigation |
|---|------|--------|------------|------------|
| 1 | ROM patches don't match our memory layout | Boot fails | Medium | Trace comparison with SheepShaver, address verification |
| 2 | Unicorn PPC interrupt delivery doesn't work | No 60Hz timer | Medium | Write to KernelData flags (avoid exception injection) |
| 3 | SHEEP opcode detection unreliable in Unicorn | EmulOps don't fire | Low | UC_HOOK_INSN_INVALID well-tested; fall back to UC_HOOK_INTR |
| 4 | KPX dyngen JIT doesn't build on modern x86-64 | No KPX JIT | Medium | Start with interpreter only, JIT is optimization |
| 5 | KPX dependencies (vm_alloc, sigsegv) don't port | Build failure | Medium | Shim with our existing implementations |
| 6 | Gossamer ROM unavailable | Can't test | Low | Multiple sources (archive.org, macintoshgarden.org) |
| 7 | Name collisions cause subtle bugs | Wrong behavior | High | Systematic rename pass early in Phase 2 |
| 8 | 68k register shadowing (GPR8-22) breaks | Wrong I/O | Medium | DualCPU-PPC validates both backends agree |
| 9 | Double interpretation too slow | Unusable | Low | Acceptable for initial work; optimize later |

---

## Phase Dependencies

```
Phase 0 (Unicorn smoke)─────────────────┐
Phase 1 (KPX build)──────────────────┐  │
Phase 2 (config wiring)───────────┐  │  │
                                   │  │  │
Phase 3 (Unicorn wrapper)─────────┼──┼──┤
                                   │  │  │
Phase 4 (Unicorn PPC backend)─────┤  │  │
Phase 5 (KPX backend)─────────────┤──┘  │
                                   │     │
Phase 6 (init_ppc)─────────────────┤     │
Phase 7 (ROM patches)──────────────┤     │
Phase 8 (EmulOp dispatch)──────────┤     │
                                   │     │
Phase 9 (DualCPU-PPC)─────────────┘     │
                                         │
Phase 10 (Boot testing)──────────────────┘
```

**Parallel tracks**:
- Phases 0, 1, 2 can proceed in parallel
- Phases 3+4 (Unicorn) and 1+5 (KPX) can proceed in parallel
- Phases 6, 7, 8 are sequential
- Phase 9 requires both 4 and 5 complete
- Phase 10 can start incrementally as each milestone is reached

---

## File Impact Summary

| File | Change | Est. Lines |
|------|--------|-----------|
| `src/config/emulator_config.h` | Add KPX backend, fix accessors, add `is_ppc()` | ~30 |
| `src/config/emulator_config.cpp` | Parse `--backend kpx`, validate arch/backend combos | ~40 |
| `src/main.cpp` | Architecture dispatch for PPC backends | ~60 |
| `src/common/include/platform.h` | Add PPC register accessors, PPC backend installs | ~40 |
| `src/cpu/unicorn_wrapper.h` | PPC register function declarations | ~30 |
| `src/cpu/unicorn_wrapper.c` | PPC arch dispatch, register impl | ~300 |
| `src/cpu/cpu_ppc_unicorn.cpp` | **New**: PPC Unicorn backend | ~800 |
| `src/cpu/cpu_ppc_kpx.cpp` | **New**: KPX Platform API wrapper | ~600 |
| `src/cpu/cpu_ppc_dualcpu.cpp` | **New**: PPC dual-CPU validation | ~400 |
| `src/cpu/kpx/` | **New**: Extracted KPX source + build | ~500 (shims) |
| `src/core/cpu_context.cpp` | Implement `init_ppc()` | ~350 |
| `src/core/rom_patches_ppc.cpp` | **New**: PPC ROM patching | ~1500 |
| `src/core/emul_op_ppc.cpp` | **New**: PPC EmulOp dispatch | ~500 |
| `meson.build` + sub-meson.builds | Add PPC sources, KPX lib, options | ~100 |
| `tests/test_unicorn_ppc.cpp` | **New**: Unicorn PPC smoke test | ~200 |
| `tests/test_boot_ppc.sh` | **New**: PPC boot test | ~100 |
| **Total** | | **~5,550** |
