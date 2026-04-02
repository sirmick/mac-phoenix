# PPC Session 11 Findings (2026-03-31)

## Summary

Session 11 resolved the interrupt delivery mechanism, fixed multiple config/driver issues, and narrowed the remaining boot blocker to a JIT compiler-generation issue. With interpreter mode, mac-phoenix is at parity with legacy SheepShaver (1462 CHECKLOADs, 490 MIPS). Full boot requires JIT, which is blocked by a GCC version mismatch in the code surrounding dyngen ops.

## Bugs Found and Fixed

### 1. Config mismatch (root cause of CL=458 vs 1462)
- `config.json` had 68k disk (`7.6.img`) as boot disk, PPC disk as CDROM
- `cpu_backend` was `uae` instead of `kpx`; RAM 128MB instead of 64MB
- Network `lwip` and audio enabled (legacy has neither)
- **Fix:** Corrected config.json to match legacy's `~/.sheepshaver_prefs` exactly

### 2. MODE_68K interrupt nesting (68k stack overflow)
- Without SIGUSR2, `HandleInterrupt` fires at every PPC block boundary
- Legacy's SIGUSR2 is kernel-atomic (can't re-enter); spcflags are cooperative
- Each HandleInterrupt set CR2; DR emulator dispatched before nanokernel RTE → 0x2e bytes pushed per cycle
- **Root cause:** HandleInterrupt MODE_68K fires hundreds of times during one nanokernel interrupt cycle
- **Fix:** Moved CR2+memory flag writes to tick thread (60Hz), HandleInterrupt MODE_68K is no-op
- **REVERTED:** Discovered legacy EMULATED_PPC uses identical spcflags mechanism (no SIGUSR2!). Legacy's HandleInterrupt MODE_68K sets CR2 normally. Nesting doesn't occur because HandleInterrupt only fires once per tick (60Hz TRIGGER → HANDLE conversion). Restored to legacy-identical code — nesting resolved.

### 3. PatchAfterStartup not firing for Mac OS 9.0.4
- 9.0.4 boot resource matches 7.6-8.1 pattern (rsrc_patches patch 3), not 9.0 (patch 5)
- Only `OP_NTRB_17_PATCH4` called PatchAfterStartup; `OP_NTRB_17_PATCH` did not
- **Fix:** Added PatchAfterStartup_PPC() to OP_NTRB_17_PATCH handler

### 4. idle_wait() blocking PPC thread
- `pthread_cond_wait()` blocks indefinitely; without SIGUSR2 to wake it, PPC freezes
- **Fix:** No-op for `SHEEPSHAVER` in `timer_unix.cpp`

### 5. DiskInterrupt workaround removed
- Mac-phoenix had a custom "wait for Finder" check instead of legacy's `acc_run_called` gate
- **Fix:** Reverted to match legacy exactly

### 6. NTRB patch debug logging added
- `rsrc_patches_ppc.cpp` now logs which NTRB patch pattern matched (patch 2-5)

## JIT Status — The Remaining Blocker

### The Problem
With JIT enabled, `codegen.execute()` enters compiled code that chains blocks forever without returning for spcflags checking. HandleInterrupt never fires. 0 MIPS after initial burst.

### What's Identical to Legacy
- Dyngen precompiled ops: `basic-dyngen-ops-x86_64.hpp` — same MD5 hash
- `ppc-translate.cpp` (JIT compiler): functionally identical
- `ppc-cpu.cpp` (JIT execute loop): identical except harmless `ppc_insn_counter` line
- `PPC_FLIGHT_RECORDER=1`: both legacy and mac-phoenix (legacy's sysdeps.h overrides ppc-config.hpp default of 0)
- `DYNGEN_DIRECT_BLOCK_CHAINING=1`: both
- `PPC_CHECK_INTERRUPTS=1`: both (sysdeps.h overrides meson -D0)

### What's Different
The surrounding C++ code (`ppc-translate.cpp`, `basic-dyngen.cpp`, `ppc-cpu.cpp`) is compiled with **GCC 13** in mac-phoenix vs **GCC used to build legacy** (likely older). The dyngen ops are precompiled binary blobs (identical), but the JIT infrastructure code that calls `codegen.execute()`, manages block lookup/chaining, and handles the execute loop is compiled differently by modern GCC. The optimizer may produce different machine code for the block dispatch loop, preventing the natural yield points that legacy's build has.

### Key Evidence
- Legacy EMULATED_PPC does NOT use SIGUSR2 — same spcflags mechanism as mac-phoenix
- Legacy JIT progresses normally (insn count grows from 2.5M to 1.8B)
- Mac-phoenix JIT stalls at ~730M insn (0 MIPS)
- Debug callbacks at GoMixedModeTrap addresses (0x50469728, 0x5046975c) accidentally acted as yield points, giving mode=1=61/61 — but they're not in legacy
- Removing callbacks → JIT loops forever
- The exact point where JIT stops yielding needs binary-level analysis

### Next Steps
1. **Compare compiled object code**: `objdump` the JIT execute loop in both legacy and mac-phoenix binaries to find where yield behavior diverges
2. **Try matching GCC version**: Build with the same GCC that built legacy
3. **Add proper spcflags check in JIT**: Generate spcflags test on backward branches (QEMU/TCG approach) — but this deviates from legacy

## Architecture: Interrupt Delivery (Final State)

Mac-phoenix now matches legacy EMULATED_PPC exactly:

```
Tick thread (60Hz)
  → SetInterruptFlag(INTFLAG_VIA)     [if XLM_IRQ_NEST == 0]
  → TriggerInterrupt()
    → ppc_cpu->trigger_interrupt()
      → spcflags.set(SPCFLAG_CPU_TRIGGER_INTERRUPT)

Interpreter loop (block boundary)
  → check_spcflags()
    → EXEC_RETURN: return false (exit nested execute)
    → TRIGGER → HANDLE conversion
    → HANDLE → HandleInterrupt(r)
      → MODE_68K: WriteMacInt16(KD+0x67c, 1) + CR2 |= KD+0x674
      → MODE_NATIVE: DisableInterrupt() + ppc_cpu->interrupt(ROM entry)
      → MODE_EMUL_OP: Execute68k(interrupt routine)
```

No SIGUSR2. No tick-thread CR2 writes. No HandleInterrupt CR2 clearing. Pure legacy code.

## Files Changed (uncommitted)

| File | Change |
|------|--------|
| `src/cpu/kpx/cpu_ppc_kpx.cpp` | HandleInterrupt matches legacy; JIT gated; tick thread IRQ_NEST guard |
| `src/cpu/kpx/emul_op_ppc.cpp` | PatchAfterStartup in NTRB_17_PATCH; stall tracing; OP_IRQ logging |
| `src/cpu/kpx/rsrc_patches_ppc.cpp` | NTRB patch match logging |
| `src/cpu/kpx/src/cpu/ppc/ppc-cpu.cpp` | Comment update only |
| `src/cpu/kpx/src/cpu/ppc/ppc-translate.cpp` | Debug block dump (cosmetic) |
| `src/cpu/kpx/compat/sysdeps.h` | PPC_CHECK_INTERRUPTS comment |
| `src/core/timer_unix.cpp` | idle_wait no-op for SHEEPSHAVER |
| `src/core/disk.cpp` | Reverted DiskInterrupt to match legacy |
| `~/.config/mac-phoenix/config.json` | Fixed PPC config to match legacy prefs |
