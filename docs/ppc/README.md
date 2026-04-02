# PPC Emulation

Adding PowerPC Mac emulation to mac-phoenix using the **KPX (Kheperix)** interpreter,
targeting OldWorld 4MB ROMs (Gossamer / Beige G3).

## Current Status (Session 12, April 2026)

**PPC boots Mac OS 9 to Finder in interpreter mode.** JIT mode is blocked by a
GCC code-generation difference (see below).

With interpreter, mac-phoenix matches legacy SheepShaver: 1462 CHECKLOADs, 490 MIPS,
NQD acceleration, ExtFS, and full Finder desktop. Session 12 stripped all debug
divergence — PPC subsystems are now character-for-character identical to legacy.

### What Works

- Full boot to Finder desktop (interpreter mode)
- ROM patches (PatchROM_PPC, all 4 phases) verified identical to legacy
- EmulOps (all 40+), NativeOps (all 38) — functionally identical
- HandleInterrupt (all 3 modes), KernelData init, XLM setup
- Driver init (.Sony, .Disk, .AppleCD, serial) — identical sequence
- CHECKLOAD sequence: 1462 OP_CHECKLOAD + 851 boot_progress = 2313 total resources
- Resource patches (rsrc_patches_ppc) — character-for-character match with legacy
- NQD acceleration hooks installed and working
- ExtFS installed (via forced PatchAfterStartup)
- Video driver init: IOCommandIsComplete, VSL*, NQDMisc TVECTs all resolved
- Virtual clock: timer_current_time uses ppc_insn_counter for PPC
- Atomic interrupt flags: SetInterruptFlag/ClearInterruptFlag use __sync builtins
- Microseconds() uses virtual PPC clock (not wall clock)

### Remaining Issue: JIT

With JIT enabled, `codegen.execute()` enters compiled code that chains blocks
without returning for spcflags checking. HandleInterrupt never fires, causing
0 MIPS after the initial burst. The dyngen precompiled ops are identical to legacy
(same MD5), but the surrounding C++ infrastructure is compiled with GCC 13 vs
legacy's older GCC, producing different machine code for the block dispatch loop.

See [Session 11 Findings](session11_findings.md) for full JIT analysis.

### Session Fixes (cumulative)

- **Session 12**: Stripped all debug logging divergence from legacy — EmulOp handlers, HandleInterrupt diagnostics, tick thread bloat, timer logging
- **Session 11**: Config mismatch fixes (CL=458→1462), idle_wait no-op for PPC, PatchAfterStartup for Mac OS 9.0.4 pattern, DiskInterrupt reverted to legacy
- **Session 10**: Microseconds() virtual clock, atomic interrupt flags, IPC video driver parity, forced PatchAfterStartup from NTRB_17_PATCH4
- **Session 9**: NQD real implementations, idle_resume real, virtual clock, JIT wiring, debug cleanup
- **Session 8**: PatchAfterStartup weak-symbol fix, all 14 weak symbols eliminated, `-fno-weak`
- **Session 5**: RAM 32→64MB, XPRAM file, Ethernet blob
- **Session 4**: Flight recorder counter fix, ADBInterrupt weak symbol

## Reference Code

The **IPC video driver** is the correct comparison target for mac-phoenix, not SDL:

- `legacy/SheepShaver-clean/src/IPC/video_ipc_sheep.cpp` — **correct reference**
- Both use: `DIS_SCREEN`, `APPLE_CUSTOM`, `video_can_change_cursor()=true`
- Both allocate framebuffer via `vm_acquire` with `Host2MacAddr`
- Both have single 32-bit mode, no multi-depth

## Documents

| Document | Contents | Status |
|----------|----------|--------|
| [Architecture](architecture.md) | Platform API, config, CPU backends | Current |
| [Implementation Guide](implementation_guide.md) | Phased porting plan | Reference |
| [ROM Patching](rom_patching.md) | Nanokernel patches, EmulOp mechanism | Current |
| [Memory Layout](memory_layout.md) | PPC Mac memory map, kernel data, XLM | Current |
| [Execution Model](execution_model.md) | Boot sequence, mode switching, interrupts | Current |
| [Legacy Comparison](legacy_comparison.md) | Full code diff with legacy IPC/SDL | Current (session 10) |
| [Session 12 Findings](session12_findings.md) | Full PPC audit, debug stripping | Current |
| [Session 11 Findings](session11_findings.md) | Config fixes, JIT blocker analysis | Current |
| [Session 8 Findings](session8_findings.md) | Weak symbols, NQD gap | Superseded |
| [Session 6 Findings](session6_findings.md) | PPC_CHECK_INTERRUPTS (disproven) | Superseded |
| [Investigation Status](investigation_status.md) | KD+0x1720 (ruled out) | Superseded |
| [Unicorn PPC](unicorn_ppc.md) | Unicorn engine PPC API (unimplemented) | Reference |

**Superseded docs** are kept for history but their findings have been corrected
or ruled out by later sessions.

## KPX File Map

```
src/cpu/kpx/
  cpu_ppc_kpx.cpp          — sheepshaver_cpu, HandleInterrupt, tick thread,
                             execute_native_op, Platform API install
  emul_op_ppc.cpp          — EmulOp dispatch (OP_IRQ, OP_RESET, OP_CHECKLOAD, etc.)
  init_ppc.cpp             — InitAll_PPC, ExitAll_PPC, PatchAfterStartup_PPC
  rom_patches_ppc.cpp      — ROM patching, nanokernel setup, InstallDrivers
  rsrc_patches_ppc.cpp     — Resource manager patches (CheckLoad)
  video_ppc.cpp            — Video driver (VideoInit, VideoDoDriverIO, VideoVBL)
  gfxaccel_ppc.cpp         — NQD acceleration hooks (bitblt, fillrect) — in namespace ppc
  name_registry_ppc.cpp    — Open Firmware device tree setup
  macos_util_ppc.cpp       — CFM (FindLibSymbol, InitCallUniversalProc)
  thunks_ppc.cpp           — Native op thunks, ExecuteNative
  ppc_memory.cpp           — SheepMem::Init/Exit, Microseconds (virtual clock), ExtFS bridge
  compat/                  — Header shims bridging KPX includes to mac-phoenix
  src/                     — KPX interpreter engine (upstream kanjitalk755)
  dyngen_precompiled/      — JIT bytecode (upstream, x86_64)
  meson.build              — Build config (-fno-weak, -DSHEEPSHAVER=1)
```

## Target

- **ROM**: OldWorld 4MB (Gossamer / Beige Power Macintosh G3)
- **CPU**: PowerPC 750 (G3), PVR 0x000c0000
- **OS**: Mac OS 9.0.4 (tested), 8.1-9.2.2 (expected)
- **Backend**: KPX interpreter (dyngen JIT available via --ppc-jit flag)
- **RAM**: 64MB required

## Boot Command

```bash
# Interpreter (default, matches legacy)
./build/mac-phoenix --arch ppc --rom /path/to/g3.rom --disk /path/to/mac9.hfv \
  --no-ppc-jit --ram 64 --screen 640x480

# With JIT (experimental)
./build/mac-phoenix --arch ppc --rom /path/to/g3.rom --disk /path/to/mac9.hfv \
  --ppc-jit --ram 64 --screen 640x480
```

## Design Decisions

- **No weak symbols**: All `__attribute__((weak))` eliminated from KPX.
  Cross-arch dispatch uses Platform API (`g_platform.patch_after_startup`).
  PPC-specific globals in `ppc::` namespace. Shared globals use core definitions.

- **No SDL dependency**: PPC video uses custom VideoInit (no SDL). IPC video
  for subprocess mode. Framebuffer allocated via `vm_acquire`. Matches legacy
  IPC driver architecture (DIS_SCREEN, APPLE_CUSTOM, hardware cursor).

- **JIT optional**: dyngen JIT compiled and available via `--ppc-jit` flag.
  Default is interpreter-only (matches tested legacy config).

- **Virtual clock**: `timer_current_time()` uses `ppc_insn_counter * 4ns` when
  PPC is active. `Microseconds()` also uses virtual clock with epoch offset.
  Both match legacy's instruction-counter-based timing.

- **Atomic interrupts**: `SetInterruptFlag`/`ClearInterruptFlag` use
  `__sync_fetch_and_or`/`__sync_fetch_and_and` matching legacy's `atomic_or`/`atomic_and`.

- **Forced PatchAfterStartup**: Called from NTRB_17_PATCH4 handler since
  Sony accRun never fires (SystemTask not reached). Installs NQD + ExtFS.

## Verified Identical to Legacy

All of these have been audited file-by-file against `legacy/SheepShaver-clean`:

- EmulOp handlers (all 40+) — same selectors, same logic
- NativeOp enum values (all 38) — same numbering
- ROM patches (PatchROM_PPC, 4 phases) — same offsets, same patches
- Resource patches (rsrc_patches_ppc) — same pattern matching, same byte sequences
- HandleInterrupt (all 3 modes) — same KernelData offsets, same interrupt copy
- KernelData initialization — same 3 ROM type branches
- XLM low memory globals — same addresses and values
- execute_sheep / execute_emul_op / execute_68k — same register handling
- ThunksInit / generate_powerpc_thunks — same PPC thunk templates
- Disk/Sony/CDROM/SCSI/ExtFS drivers — shared code (same .cpp files)
- VideoDoDriverIO / VideoControl / VideoStatus — character-for-character match
- VideoInstallAccel / NQD hooks (gfxaccel) — identical in namespace ppc
- ADBOp / ADBInterrupt — functionally identical (Platform API indirection)
- Tick thread — same 60Hz, same INTFLAG_VIA, same TriggerInterrupt
- Init sequence (InitAll_PPC) — same driver init order, same XLM setup
- check_spcflags / trigger_interrupt — identical PPC CPU core
- PRECISE_TIMING timer thread — same clock_nanosleep + wakeup mechanism
- DiskPrime / DiskControl / SonyControl — shared code, no differences
