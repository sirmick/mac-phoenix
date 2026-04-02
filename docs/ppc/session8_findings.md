# Session 8 — PPC Boot Investigation (March 27-29, 2026)

> **Historical.** Superseded by sessions 9–10. NQD gap fixed (session 9),
> Microseconds/atomics/cursor fixed (session 10). Boot now loads all resources
> and runs NQD but stalls before SystemTask. See [README.md](README.md)
> at r25=0, not an NQD issue. See [README.md](README.md) for current status.

## Summary

Deep comparison between mac-phoenix PPC and SheepShaver-clean revealed:
- Interrupt path is **byte-identical** — not the cause of boot stall
- **PPC_CHECK_INTERRUPTS=0 never took effect** (session 6 fix was a red herring)
- **PatchAfterStartup_PPC was never called** due to C++ namespace mismatch in weak symbol linkage
- Eliminated all 14 `__attribute__((weak))` symbols from KPX codebase
- Boot progresses further but stalls at "Starting Up..." due to low NQD engagement

## Session 6 Corrections

### PPC_CHECK_INTERRUPTS=0 was ineffective

Session 6 set `-DPPC_CHECK_INTERRUPTS=0` in meson.build. But `compat/sysdeps.h`
unconditionally defines `PPC_CHECK_INTERRUPTS 1`, overriding the meson flag.
The `-w` compiler flag suppressed the macro redefinition warning.

**Verified via preprocessor:** `int result = PPC_CHECK_INTERRUPTS;` expands to `int result = 1;`
in all translation units. HandleInterrupt has been firing all along.

### 32MB RAM was a separate stall cause

The JSON config field `"ram": 64` wasn't being applied — the CLI `--ram 64` flag
was needed. With 32MB RAM, mode=1 peaked at 36/s. With 64MB, mode=1 peaks at 54/s
(matching legacy's 58/s). 64MB is required for PPC Mac OS 9 boot.

## Weak Symbol Audit

### Root cause: PatchAfterStartup namespace mismatch

`m68k::PatchAfterStartup()` called `ppc_PatchAfterStartup()` via weak symbol.
But the weak declaration was inside the `m68k` namespace, resolving to
`m68k::ppc_PatchAfterStartup` — different from the strong symbol
`::ppc_PatchAfterStartup` in init_ppc.cpp. The weak stub was never overridden.

This meant **VideoInstallAccel()** (NQD acceleration) and **InstallExtFS()** were
never called during PPC boot.

### Fix: Platform API

Replaced the fragile extern "C" weak bridge with `g_platform.patch_after_startup`
function pointer. KPX backend sets it during `cpu_ppc_kpx_install()`.

### All weak symbols eliminated

| # | Symbol | Resolution |
|---|--------|-----------|
| 1 | RAMBase/RAMSize/RAMBaseHost/ROMBase/ROMBaseHost | `ppc::` namespace (PPC-only naming) |
| 2 | KernelDataAddr | `ppc::` namespace |
| 3 | InterruptFlags + SetInterruptFlag/ClearInterruptFlag | SHARED — deleted KPX dups, core defs win |
| 4 | QuitEmulator | SHARED — deleted KPX stub, renamed bool collision |
| 5 | FindLibSymbol | PPC_UNIQUE — deleted stub, real impl in macos_util_ppc.cpp |
| 6 | InitCallUniversalProc | PPC_UNIQUE — deleted stub |
| 7 | Dump68kRegs | DELETED — unused, no real impl |
| 8 | EtherResetCachedAllocation | SHARED — deleted stub |
| 9 | ether_reset | SHARED — deleted stub |
| 10 | SerialInterrupt | SHARED — deleted stub |
| 11 | EtherInterrupt | SHARED — deleted stub |
| 12 | PatchNativeResourceManager | PPC_UNIQUE — deleted stub |
| 13 | AddSifter/FindSifter | PPC_UNIQUE — real impls in rsrc_patches_ppc.cpp |
| 14 | ppc_PatchAfterStartup | Platform API (`g_platform.patch_after_startup`) |

`-fno-weak` added to KPX meson build to prevent future weak symbol bugs.
`ppc_stubs.cpp` renamed to `ppc_memory.cpp` (SheepMem + Mac allocators).

## Boot Stall Analysis

### Timeline

Both mac-phoenix AND SheepShaver-clean show the same 125-second stall during boot.
This is a Disk First Aid or extension dialog timeout — inherent to the Mac OS 9 disk.

```
0-5s:     442 CHECKLOADs load (boot resources, drivers)
5-130s:   DEAD — only VBL fires at 57/s. Dialog blocking boot.
130-135s: Burst — OS times out the dialog, resumes loading
135s+:    Legacy: 3217 CHECKLOADs + NQD=3784 → reaches Finder
          Mac-phoenix: 836 CHECKLOADs + NQD=6 → stalls again
```

### Why legacy recovers but mac-phoenix doesn't

The difference is **NQD (Native QuickDraw) acceleration engagement**:

| Metric | Legacy | Mac-phoenix |
|--------|--------|-------------|
| NQD in first 5s | 114 | 13 |
| NQD at burst | 3784 | 6 |
| CHECKLOADs at burst | 3217 | 836 |
| Reaches Finder | YES | NO |

NQD hooks ARE installed (VideoInstallAccel runs, NQDMisc(6,...) completes 8 times).
The hooks ARE called (NQD=13 in first 5s, NQD=6 at burst). But they fire **9x less**
than legacy, and during the critical burst phase, **630x less**.

### What NQD does

NQD hooks intercept QuickDraw drawing operations (bitblt, fillrect) and execute
them as native PPC code instead of going through the 68k QuickDraw emulation.
Without NQD, the desktop rendering is extremely slow, and the boot sequence can't
complete before the next dialog/timeout stalls it again.

### NQD investigation status

- `VideoInstallAccel()` runs, `nqdmisc_tvect=0x00027450` (valid)
- `NQDMisc(6, hook_info)` called 8 times with valid hook/sync TVECTs
- `NQD_bitblt_hook` and `NQD_sync_hook` are NEVER called (0 trace hits in 30s)
- The hooks are registered but Mac OS QuickDraw rarely dispatches to them
- This is NOT a complete failure — NQD=13 means SOME acceleration happens
- The gap may be caused by GDevice state or PixMap depth mismatches

### What's been ruled out

- Interrupt path (byte-identical to legacy)
- HandleInterrupt (fires at 57/s, mode=1 dominant)
- OP_IRQ (fires at 57/s, all interrupt flags processed)
- ROM patches (identical to legacy)
- EmulOp handlers (identical to legacy)
- Thunks (identical to legacy)
- VideoDoDriverIO (identical to legacy)
- SheepMem executable flag (irrelevant — PPC interpreter reads opcodes as data)
- Framebuffer address (tested 0x50590000, vm_acquire at 0x10080000 — no difference)
- Video modes (tested single 32-bit, multi-depth 1/2/4/8/16/32 — no difference)
- boot_progress_report interference (disabled, no difference)
- Dialog dismiss mechanism (disabled, no difference)
- PatchAfterStartup timing (fires correctly via Platform API)

## Code Changes

### New files
- `src/cpu/kpx/gfxaccel_ppc.cpp` — NQD acceleration hooks (from legacy gfxaccel.cpp)
- `src/cpu/kpx/ppc_memory.cpp` — renamed from ppc_stubs.cpp

### Modified files
- `src/common/include/platform.h` — added `patch_after_startup` function pointer
- `src/core/rom_patches.cpp` — Platform API dispatch for PatchAfterStartup
- `src/core/cpu_context.cpp` — `ppc::` namespace for KernelDataAddr/RAM/ROM globals
- `src/core/boot_progress.cpp` — config guard for dialog dismiss
- `src/cpu/kpx/cpu_ppc_kpx.cpp` — weak symbols removed, namespace globals, instrumentation
- `src/cpu/kpx/emul_op_ppc.cpp` — OP_IRQ rate counter, boot_progress disabled for bisect
- `src/cpu/kpx/init_ppc.cpp` — Platform API wrapper for PatchAfterStartup
- `src/cpu/kpx/video_ppc.cpp` — legacy-matching VideoInit (vm_acquire, APPLE_CUSTOM, single 32-bit), real palette impl
- `src/cpu/kpx/meson.build` — `-fno-weak`, gfxaccel_ppc.cpp added
- `src/cpu/uae_wrapper.cpp` — renamed QuitEmulator bool to quit_emulator_flag
- `src/cpu/uae_cpu/main.h` — matching bool rename

## Next Steps

1. **Why NQD hooks fire 9x less in mac-phoenix** — the GDevice or PixMap state
   may differ, causing QuickDraw to skip acceleration for most drawing ops.
   Compare the GDevice structure between legacy and mac-phoenix at the point
   where NQD hooks should fire.

2. **Re-enable boot_progress_report** in emul_op_ppc.cpp (disabled during bisect).

3. **Test with a clean disk image** — the 125s dialog timeout adds 2+ minutes
   to every boot test. A clean disk would eliminate this overhead.

4. **Consider copying legacy's SDL video driver** instead of IPC — SheepShaver-clean
   boots with SDL (not IPC). The SDL dummy driver creates different GDevice state
   that may be what makes NQD work.
