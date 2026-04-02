# PPC Boot Investigation Status — March 20, 2026 (End of Day)

> **Historical.** KD+0x1720 was ruled out as root cause. Multiple bugs found and
> fixed in later sessions. See [README.md](README.md) for current status.

## Problem
KPX boots Mac OS 9, loads all 1313 extensions, runs video/ethernet drivers,
but never launches Finder. Legacy SheepShaver boots to Finder at 158 MIPS.

## Fixes Applied This Session

### Fix 1: PPC_FLIGHT_RECORDER must be 1
Dyngen ops hardcode struct offset 0xC0828 for block_cache. Flight recorder
adds 262KB before it. Setting to 0 shifts the offset → SIGSEGV.

### Fix 2: DR Emulator + DR Cache memory
Legacy has these `#if 0`'d but KPX needs them for NativeOp dispatch at
depth=2 (via ExecuteNative). Without them: 0 NativeOps. With them: 103.

### Fix 3: DR mmap MAP_FIXED (was MAP_FIXED_NOREPLACE)
The NOREPLACE variant failed silently. MAP_FIXED succeeds.

### Fix 4: ROM write-protection 5MB (matching legacy)
Legacy protects ROM_AREA_SIZE after init. KPX was only protecting 4MB.

### Fix 5: Memory map dump for differential debug
Added /proc/self/maps dump at PPC start to both KPX and legacy.

## Verified Identical Between KPX and Legacy

| Component | How verified |
|-----------|-------------|
| ROM file (G3 Gossamer 79D68D63) | Same file, md5 match |
| Disk image (Mac OS 9.0.4) | Same file |
| Dyngen precompiled ops (x86_64) | `diff` = 0 lines |
| ppc-execute.cpp | `diff` = 0 functional lines |
| ppc-decode.cpp | `diff` = 0 lines |
| ppc-cpu.hpp | `diff` = 0 lines |
| ppc-translate.cpp | `diff` = 0 functional lines |
| spcflags.hpp | 1 diff: `volatile` (KPX) vs plain `uint32` |
| ppc-cpu.cpp | 1 diff: `use_jit` vs `PPC_ENABLE_JIT` (both evaluate to true) |
| Struct sizeof | 1,050,808 bytes both |
| CR offset | 0x390 both |
| spcflags offset | 0x3b0 both |
| GPR0 offset | 0x10 both |
| Initial registers | All zero, GPR3/4 set identically |
| EmulOp dispatch (emul_op_ppc.cpp) | Functionally identical |
| NativeOp dispatch (cpu_ppc_kpx.cpp) | Functionally identical |
| execute_emul_op | Identical |
| execute_native_op | Identical |
| execute_68k | Identical |
| execute_sheep | Identical |
| HandleInterrupt | Identical (MODE_68K/NATIVE/EMUL_OP paths) |
| compile1 (JIT) | Identical |
| get_resource | Identical |
| ROM patches (rom_patches_ppc.cpp) | Identical |
| Resource patches (rsrc_patches_ppc.cpp) | Identical |
| Thunks (thunks_ppc.cpp) | Identical |
| VideoDriverStub.i | Identical |
| name_registry_ppc.cpp | Identical (except error handling) |
| GoMixedModeTrap handler code at runtime | Byte-identical at 0x50469720 |

## Runtime Metrics Comparison

| Metric | KPX | Legacy | Match? |
|--------|-----|--------|--------|
| CHECKLOADs | 1313 | 1310 | ~yes |
| DISK_PRIME | 6785 | 6782 | ~yes |
| PRIMETIME | 6 | 6 | yes |
| INSTIME | 5 | 5 | yes |
| SCSI_DISPATCH | 42 | 42 | yes |
| NTRB_17_PATCH4 | 36 | 36 | yes |
| NativeOps (total) | 103 | 652 | **NO** |
| - VIDEO_DO_DRIVER_IO | 87 | 80 | ~yes |
| - ETHER_IRQ | 14 | 14 | yes |
| - CHECK_LOAD_INVOC | 0 | 434 | **NO** |
| - NQD hooks | 0 | 111 | **NO** |
| MODE_NATIVE count | 9 | 459 | **NO** |
| KD+0x65c transitions | stuck 0x68fff100 | oscillates to 0x68fff400 | **NO** |
| KD+0x1720 | 0x0015842c | 0x00000000 | **NO** |
| MIPS (steady state) | 0.2 | 392 | **NO** |
| DT trace first 108 entries | identical | identical | yes |
| d0=1 from OP_IRQ | 60Hz | 60Hz | yes |
| Ticks counter | advances correctly | same | yes |
| Disk I/O errors | 0 | 0 | yes |
| EXTFS (Finder) | never | fires at DT#8681 | **NO** |

## Theories Tested and Disproven

### 1. Memory layout causes nanokernel divergence — DISPROVEN
Tested every combination:
- Framebuffer at 0x41659000, 0x61000000, 0x10080000, inside RAM, removed entirely
- DR Emulator+Cache present, absent, both removed
- ROM protection 4MB, 5MB
- All KPX-only regions removed simultaneously
- Memory map matched to legacy (only SheepMem size and JIT location differ)

**Result:** KD+0x1720 = 0x0015842c in every case. Memory layout has zero effect.

### 2. Resource patches missing — DISPROVEN
boot 3 loaded, NTRB patches fire, PatchNativeResourceManager succeeds,
all 8 XLM resource TVECTs set to valid RAM addresses. rsrc_patches code
is byte-identical to legacy.

### 3. Initial register state differs — DISPROVEN
Both: all GPRs=0, CR=0, LR=0, CTR=0, XER=0, PC=0. GPR3 and GPR4 set
to identical values (ROMBase+0x30d000 and KernelDataAddr+0x1000).

### 4. Interrupt timing corrupts CR bits — PARTIALLY DISPROVEN
Suppressing HandleInterrupt during HI #10-200 delays KD+0x1720 change
(from HI#28 to HI#221) and keeps MIPS at 157-199. But KD+0x1720 still
eventually becomes 0x0015842c. Interrupt suppression changes timing but
doesn't prevent the underlying computation.

Disabling CR modification in MODE_68K breaks boot (68k emulator needs it).
Protecting CR only during nanokernel code range either breaks boot (too wide)
or doesn't help (handler runs in 68k emulator area, not nanokernel area).

### 5. JIT direct block chaining timing — DISPROVEN
Set DYNGEN_DIRECT_BLOCK_CHAINING=0. KD+0x1720 still set. No effect.

### 6. Flight recorder callbacks change JIT behavior — DISPROVEN
Disabled all `start_log()` calls. Logging stays off, no `call_do_record_step`
in JIT blocks. KD+0x1720 still set. No effect.

### 7. PPC_FLIGHT_RECORDER=0 is a deeper JIT bug — DISPROVEN
It's a struct layout mismatch (dyngen ops hardcode offset 0xC0828).
Fixed by keeping PPC_FLIGHT_RECORDER=1. Not related to boot failure.

### 8. Framebuffer in Mac address space — DISPROVEN
Moved from 0x41659000 to 0x61000000 to 0x10080000 to inside RAM to
completely removed. No effect on KD+0x1720 in any case.

### 9. `use_jit && PPC_REENTRANT_JIT` vs `PPC_ENABLE_JIT && PPC_REENTRANT_JIT` — DISPROVEN
Both evaluate to `true && 1` = true. `use_jit` is always true for the
single CPU instance (set in default constructor).

### 10. Compiler optimization difference — DISPROVEN
Both KPX and legacy compile PPC CPU code at -O2 with GCC 13.3.0.

## Root Cause: KD+0x1720

### The mechanism
ROM code at 0x5036dad8 (`stw r0, 0x720(r31)`) stores r0 to KD+0x1720.
- Legacy: r0 = 0 → KD+0x1720 = 0 (always, even after full Finder boot)
- KPX: r0 = 0x0015842c → KD+0x1720 = heap address

This breaks GoMixedModeTrap handler at ROM+0x469728 which compares
KD+0x1720 with [r28+0x28]. When not equal → bne → fallback path →
no PPC mode switch → Finder never launches.

### What we know
- KD+0x1720 = 0 at OP_RESET (nanokernel init completes with it zero)
- Changes to 0x0015842c between HI #17-37 (during early boot, ~0.5s)
- The store goes through JIT-compiled x86 code (not interpreter)
- Interpreter-level watchpoint in vm_write_memory_4 doesn't catch it
- GDB hardware watchpoint hits on every read/write (too noisy)
- Same ROM code, same struct layout, same memory layout → different result

### What must be true
The PPC JIT engine compiles the ROM nanokernel code differently in KPX
vs legacy. Even though the source is identical, the JIT output differs
because:
1. **Code buffer address**: KPX at 0x70080000, legacy at 0x10402000
2. This affects absolute addresses embedded in x86 code
3. The x86 instructions are functionally equivalent BUT execute at
   different addresses, which may affect CPU branch prediction or
   cache behavior — though this shouldn't change computation results

OR: there is a subtle bug in the JIT codegen framework that manifests
differently at the two code buffer addresses (e.g., a 32-bit truncation
of a 64-bit address, or a signed/unsigned displacement overflow).

## Next Steps

### Option A: Force interpreter for nanokernel code
Add a check in the JIT execute loop: if PC is in ROM range 0x5036d000-
0x5036e000, fall through to the interpreter instead of JIT. The interpreter
has the vm_write_memory_4 watchpoint that would catch the store.

### Option B: Compare JIT x86 output
Dump the compiled x86 blocks for the nanokernel code in both KPX and legacy.
If the x86 code differs, the bug is in codegen. If identical, the bug is
in how the x86 code interacts with the different code buffer address.

### Option C: ptrace hardware watchpoint
Use a helper thread that attaches via ptrace and sets DR0 on 0x68FFF720.
This catches the write at the hardware level regardless of JIT/interpreter.

## Files Modified This Session
- src/cpu/kpx/compat/sysdeps.h — PPC_FLIGHT_RECORDER=1, DYNGEN_DIRECT_BLOCK_CHAINING
- src/cpu/kpx/compat/config.h — MAP_BASE testing
- src/cpu/kpx/meson.build — PPC_FLIGHT_RECORDER=1
- src/cpu/kpx/cpu_ppc_kpx.cpp — HI trace, KD+0x1720 tracking, memory map dump
- src/cpu/kpx/emul_op_ppc.cpp — DT trace, VDIO trace, OP_RESET trace, OP_IRQ comment
- src/cpu/kpx/rsrc_patches_ppc.cpp — PNRM trace, boot 3 trace
- src/cpu/kpx/init_ppc.cpp — handler dump, XLM dump
- src/cpu/kpx/video_ppc.cpp — framebuffer location, VDIO trace
- src/cpu/kpx/src/cpu/ppc/ppc-cpu.cpp — NESTED/EXEC/SMEM traces, GMMT trace
- src/cpu/kpx/src/cpu/ppc/ppc-translate.cpp — COMPILE trace
- src/cpu/kpx/src/cpu/vm.hpp — write watchpoint (interpreter only)
- src/core/cpu_context.cpp — DR allocation, ROM protection, memory map dump
- legacy/SheepShaver/src/kpx_cpu/sheepshaver_glue.cpp — memory map dump
- docs/ppc/investigation_status.md — this file
