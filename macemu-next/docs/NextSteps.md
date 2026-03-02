# Next Steps

What needs fixing, in priority order.

---

## 1. Gut `hook_block()` (unicorn_wrapper.c)

**Problem**: `hook_block()` is 1,318 lines. It runs on every basic block boundary -- the hottest path in the emulator. ~90% of it is debug diagnostics accumulated during boot debugging.

**The real logic is ~50 lines:**
- Apply deferred register updates
- Poll timer every ~4096 blocks
- Flush TB cache (workaround)
- Check/deliver pending interrupts

**Everything else is debug cruft:**
- IRQ-ACK tracing (first 5 interrupts, static counter)
- IRQ handler block tracing (first 3 interrupts)
- 60Hz handler address matching (hardcoded ROM offsets)
- POST-FIXMEM block tracing
- INSTALL_DRIVERS area tracing (hardcoded ROM ranges)
- DD3-CHECK byte watch
- Sony driver entry tracing
- CODE-WATCH for address 0x0001CC2E
- STALE-TB detector (inline code verification system)
- BAD-PC diagnostics (32-entry ring buffer, hex dumps)
- STALL detector (linked list chain walker)
- DIAG output every 2M blocks ($0b78 watch, trap table scan, memory watch)

**Fix**: Extract the ~50 lines of real logic into a clean `hook_block()`. Move all diagnostics behind `#ifdef UNICORN_DEBUG` or into a separate `unicorn_diagnostics.c` module that can be compiled out entirely.

---

## 2. Fix TB Invalidation Properly

**Problem**: `uc_ctl_flush_tb()` is called 60 times/second, flushing the entire JIT cache every time. This defeats much of QEMU's JIT -- every translation block gets recompiled from scratch after each flush.

**Why it exists**: Mac OS heap overwrites RAM containing EmulOp patch code. QEMU's JIT retains stale compiled translations for the old code. Executing stale TBs crashes at PC=0x00000002.

**Root Cause (RESEARCHED)**: Unicorn **gutted** QEMU's entire dirty memory bitmap. See [deepdive/JIT_SMC_Detection_Analysis.md](deepdive/JIT_SMC_Detection_Analysis.md) for full analysis. Key findings:
- `cpu_physical_memory_is_clean()` hardcoded to `return true` in `ram_addr.h`
- `cpu_physical_memory_set_dirty_flag()` is an empty no-op
- `invalidate_and_set_dirty()` in `exec.c` has an empty body
- `flatview_write_continue()` does raw `memcpy()` with no TB invalidation for API writes
- `page_collection_lock()` in `translate-all.c` is `#if 0`'d out

**What still works**: Guest M68K stores go through `store_helper()` → `notdirty_write()` → `tb_invalidate_phys_page_fast()`. Since `is_clean()` always returns true, ALL pages get `TLB_NOTDIRTY`, so guest writes to executable pages DO trigger per-page TB invalidation. This may already handle our Mac OS heap overwrite case.

**Recommended fix approach**:
1. **First**: Test if removing `uc_ctl_flush_tb()` still works (the `notdirty_write()` path may already handle it)
2. **If needed**: Use `uc_ctl_remove_cache(start, end)` for specific code ranges
3. **Nuclear option**: Restore the full dirty bitmap in the Unicorn fork

**Key files**:
- `subprojects/unicorn/qemu/include/exec/ram_addr.h` -- stubbed dirty bitmap functions
- `subprojects/unicorn/qemu/exec.c:1531` -- empty `invalidate_and_set_dirty()`
- `subprojects/unicorn/qemu/accel/tcg/cputlb.c:1189` -- `notdirty_write()` (partially works)
- `subprojects/unicorn/qemu/accel/tcg/translate-all.c:2019` -- `tb_invalidate_phys_page_fast()` (works)

**Impact**: Highest-impact performance optimization. Removing the 60Hz full flush could dramatically improve JIT effectiveness.

---

## 3. Clean Up Debug Logging in EmulOp Handler

**Problem**: `unicorn_platform_emulop_handler()` in cpu_unicorn.cpp has per-opcode `fprintf` debug logging for IRQ (0x7129), RESET (0x7103), PATCH_BOOT_GLOBS (0x7107), and SCSI_DISPATCH (0x7128). These fire thousands of times during boot.

**Fix**: Remove or put behind `#ifdef UNICORN_DEBUG`. The EmulOp handler should be: read registers, call EmulOp(), defer changed registers. No fprintf in the hot path.

---

## 4. Clean Up `one_second()` in timer_interrupt.cpp

**Problem**: Dumps the entire OS trap table (256 entries) and scans the toolbox trap table (1024 entries) every time `$0b78` changes. Also dumps PC, resource chain sentinel, TopMap, SysMap, SysZone every second.

**Fix**: Remove the trap table dumps and memory watches. Keep a minimal 1Hz heartbeat log (one line with seconds elapsed and backend name). Diagnostics can be re-added behind an env var like `TIMER_VERBOSE=1`.

---

## 5. Fix Fake Instruction Counter

**Problem**: `unicorn_exec_loop.c` line 81: `total_executed += 1000`. Running with `count=0` (unlimited instructions) but faking 1000 per iteration. The max_total_insns loop termination is based on this lie.

**Fix**: Either track real instruction count (from block stats) or remove the pretense of counting. If the loop is "run until stopped", make it `while (!stopped)` instead of pretending to count instructions.

---

## 6. Deduplicate M68kRegisters

**Problem**: `M68kRegisters` struct is defined independently in three places:
- `unicorn_wrapper.c` (as `M68kRegistersC` with `#define`)
- `cpu_unicorn.cpp` (as `struct M68kRegisters`)
- `main.h` or other headers (the "real" definition)

This exists to work around C vs C++ linkage conflicts.

**Fix**: Define it once in a shared C-compatible header (e.g., `m68k_registers.h`) with proper `extern "C"` guards. Include everywhere.

---

## 7. SCSI Disk Emulation (Next Feature Work)

**Problem**: Both backends stall at resource chain search (PC=0x0001c3d4). The ROM is looking for system resources from a SCSI boot disk that doesn't exist.

**What's needed**: Enough SCSI emulation to present a disk image containing a Mac OS System file. The ROM needs to find resources in the resource chain to continue booting.

**This is the next feature milestone** -- everything above is cleanup of existing code.

---

## Order of Operations

1. **Gut hook_block** -- biggest code quality win, may also improve performance
   - **NOTE**: The STALE-TB detector must be preserved -- it's a production SMC safety net, not debug cruft
2. **Clean debug logging** -- EmulOp handler and timer (quick wins)
3. **Fix fake counter** -- trivial
4. **Deduplicate M68kRegisters** -- trivial
5. ~~**Fix TB invalidation**~~ -- **DONE** (March 2026). Removed 60Hz `uc_ctl_flush_tb()`. QEMU's `notdirty_write()` path + STALE-TB detector handles SMC. See [deepdive/JIT_SMC_Detection_Analysis.md](deepdive/JIT_SMC_Detection_Analysis.md).
6. **SCSI disk emulation** -- next feature work

Items 1-4 are cleanup that could be done in one session. Item 5 is done. Item 6 is a new feature.
