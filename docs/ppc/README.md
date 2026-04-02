# PPC Emulation

Adding PowerPC Mac emulation to mac-phoenix using the **KPX (Kheperix)** interpreter,
targeting OldWorld 4MB ROMs (Gossamer / Beige G3).

## Current Status (Session 10, March 30 2026)

**Boot loads all resources and patches, NQD acceleration works, but never reaches Finder.**

The system successfully loads 2313 resources (extensions, fonts, ntrb patches),
installs the native resource manager, and runs NQD acceleration hooks. However,
the 68k code never calls `SystemTask()`, so the Sony driver's accRun never fires
naturally, and the system never transitions from "loading" to "running."

### What Works

- ROM patches (PatchROM_PPC, all 4 phases) verified identical to legacy
- EmulOps (all 40+), NativeOps (all 38) — functionally identical
- HandleInterrupt (all 3 modes), KernelData init, XLM setup
- Driver init (.Sony, .Disk, .AppleCD, serial) — identical sequence
- CHECKLOAD sequence: 1462 OP_CHECKLOAD + 851 boot_progress = 2313 total resources
- Resource patches (rsrc_patches_ppc) — character-for-character match with legacy
- PatchNativeResourceManager fires correctly (ntrb 17 detected)
- NQD acceleration hooks installed and working (69 calls in first second)
- ExtFS installed (via forced PatchAfterStartup)
- Video driver init: IOCommandIsComplete, VSL*, NQDMisc TVECTs all resolved
- Virtual clock: timer_current_time uses ppc_insn_counter for PPC
- Atomic interrupt flags: SetInterruptFlag/ClearInterruptFlag use __sync builtins
- Microseconds() uses virtual PPC clock (not wall clock)
- Mouse device present (ADBMouseMoved injected at boot)
- Disk I/O: DiskPrime returns success, positions advance normally

### The Remaining Bug

After loading all resources, the 68k code enters a **DISK_PRIME + PRIMETIME loop**:

| Metric | Mac-phoenix | Legacy |
|--------|-------------|--------|
| Total resources loaded | 2313 | 5000+ |
| DISK_PRIME calls | 2896 | 5300 |
| DISK_CONTROL calls | 57 | 105 |
| SONY_CONTROL calls | 1 (open only) | 2 (open + accRun) |
| MODE_NATIVE peak | 18/s | 55/s |
| NQD calls | 69 | 53 |
| Reaches IDLE_TIME_2 | No | Yes (Finder) |

Legacy's sequence: DISK_PRIME (5300 reads) → DISK_CONTROL → IDLE_TIME_2 (Finder).
Mac-phoenix's: DISK_PRIME (2896 reads) → PRIMETIME repeating → stall.

The system does fewer disk reads and then gets stuck rescheduling timer tasks.
It never calls `SystemTask()`, so the Sony driver's periodic action (accRun)
never fires naturally (we force PatchAfterStartup as a workaround).

The EmulOp sequences match for the first ~600 non-IRQ operations, then diverge
due to interrupt timing differences. Legacy's `SDL_PumpEvents()` in HandleInterrupt
and the IPC ControlIPC input handler create subtle timing differences that affect
which 68k instruction the interrupt hits.

### Session 10 Fixes

| Fix | Impact |
|-----|--------|
| Microseconds() changed from wall clock to virtual PPC clock | Correct timing for Mac OS |
| SetInterruptFlag/ClearInterruptFlag now atomic (__sync builtins) | No lost interrupts |
| video_can_change_cursor() = true (matches legacy IPC driver) | Correct cursor support |
| display_type = DIS_SCREEN (matches legacy IPC driver) | Correct video mode |
| srand(1) in ADBInit for deterministic enumeration | Reproducible ADB |
| ADBMouseMoved(320,240) once at boot | Mouse device present |
| Force PatchAfterStartup from NTRB_17_PATCH4 handler | NQD=69, ExtFS installed |

### Previous Session Fixes (cumulative)

- **Session 9**: NQD real implementations, idle_resume real, virtual clock, JIT wiring, debug cleanup
- **Session 8**: PatchAfterStartup weak-symbol fix, all 14 weak symbols eliminated, `-fno-weak`
- **Session 5**: RAM 32→64MB, XPRAM file, Ethernet blob
- **Session 4**: Flight recorder counter fix, ADBInterrupt weak symbol

## RAM Divergence Analysis (Session 10)

Using RAM checksums at each CHECKLOAD, both systems are **byte-identical** through
CL#10 (first 10 resources). Divergence starts at CL#39 where legacy SDL generates
a mouse event (`mouse reg 3 630`) that writes ADB data to Mac RAM.

The resource loading order is nearly identical: only 2 differences in 1462 resources:
1. Mac-phoenix loads an extra `accl id=0` at CL#39 (display probe difference)
2. CL#846 loads `KMAP id=0` vs legacy's `KMAP id=5` (keyboard type)

A 2MB RAM snapshot at CL#1462 shows 62% of bytes differ — but this is all heap
pointer offsets from non-deterministic allocation order. The Lv1Int handler code
at those different addresses is byte-identical.

## Correct Reference Code

The **IPC video driver** is the correct comparison target for mac-phoenix, not SDL:

- `legacy/SheepShaver-clean/src/IPC/video_ipc_sheep.cpp` — **correct reference**
- Both use: `DIS_SCREEN`, `APPLE_CUSTOM`, `video_can_change_cursor()=true`
- Both allocate framebuffer via `vm_acquire` with `Host2MacAddr`
- Both have single 32-bit mode, no multi-depth

The SDL driver (`video_sdl2.cpp`) uses `DIS_WINDOW`, `PrefsFindBool("hardcursor")=false`,
multi-depth modes — these are **wrong** comparison targets for mac-phoenix.

## Interrupt/Timer Subsystem (Verified Identical)

Thorough code-level comparison (session 10) confirmed:

- HandleInterrupt: all 3 mode cases character-for-character identical
- OP_IRQ: all INTFLAG checks identical (VIA, SERIAL, ETHER, TIMER, AUDIO, ADB)
- Tick thread: same 60Hz period (16625us), same SetInterruptFlag + TriggerInterrupt
- PRECISE_TIMING timer thread: same clock_nanosleep + virtual clock check
- check_spcflags / trigger_interrupt: identical in PPC CPU core
- PrimeTime scheduling: same wakeup_time calculation and thread suspend/resume

Only non-functional differences: legacy uses `select()` for delay vs `nanosleep()`,
and `TimerDateTime()` vs `time()+offset` for the 1Hz clock write.

## What's NOT the Cause (Eliminated)

| Hypothesis | How eliminated |
|-----------|---------------|
| Video acceleration code differs | Identical to legacy (gfxaccel, VideoControl/Status) |
| Resource patches missing | rsrc_patches identical, all patches fire correctly |
| Disk I/O errors | DiskPrime returns noErr, positions advance |
| Missing NQD/ExtFS | Forced on, NQD=69 — still stalls |
| Missing mouse/ADB | Injected, events processing — still stalls |
| Non-atomic interrupt flags | Fixed to atomic — still stalls |
| Microseconds() wrong clock | Fixed to virtual — still stalls |
| XPRAM/NVRAM state | Zapped — still stalls |
| Extra disks in config | Removed — still stalls |
| ExtFS initialization | Disabled — still stalls |
| Keyboard type mismatch | Fixed to 0 — still stalls |
| KD+0x1720 (GoMixedModeTrap) | Ruled out: never taken in either system |
| Interrupt replay needed | Legacy boots without replay |

## Documents

| Document | Contents | Accuracy |
|----------|----------|----------|
| [Architecture](architecture.md) | Platform API, config, CPU backends | Current |
| [Implementation Guide](implementation_guide.md) | Phased porting plan | Reference |
| [ROM Patching](rom_patching.md) | Nanokernel patches, EmulOp mechanism | Current |
| [Memory Layout](memory_layout.md) | PPC Mac memory map, kernel data, XLM | Current |
| [Execution Model](execution_model.md) | Boot sequence, mode switching, interrupts | Current |
| [Legacy Comparison](legacy_comparison.md) | Full code diff with legacy IPC/SDL | **Updated session 10** |
| [Session 8 Findings](session8_findings.md) | Weak symbols, NQD gap | Historical |
| [Unicorn PPC](unicorn_ppc.md) | Unicorn engine PPC API (unimplemented) | Reference |

**Superseded docs** (session 6, investigation_status): kept for history but findings
have been corrected or ruled out by later sessions.

## KPX File Map

```
src/cpu/kpx/
  cpu_ppc_kpx.cpp          — sheepshaver_cpu, HandleInterrupt, tick thread,
                             execute_native_op, stall detector, Platform API install
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
