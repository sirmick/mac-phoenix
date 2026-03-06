# Next Steps

What needs fixing, in priority order.

---

## ~~1. Gut `hook_block()` (unicorn_wrapper.c)~~ — DONE

Reduced from 1,318 lines to 148 lines (89% reduction). All debug diagnostics removed. Preserved essential logic: block stats, interrupt ack, STALE-TB detector, SCSI D5 cap, timer polling, deferred updates, PC tracing, interrupt delivery.

---

## ~~2. Fix TB Invalidation Properly~~ — DONE

Removed 60Hz `uc_ctl_flush_tb()`. QEMU's `notdirty_write()` path handles most SMC. STALE-TB detector catches the ~18 remaining edge cases. See [deepdive/JIT_SMC_Detection_Analysis.md](deepdive/JIT_SMC_Detection_Analysis.md).

---

## ~~3. Clean Up Debug Logging~~ — DONE

- EmulOp handler (`cpu_unicorn.cpp`): removed per-opcode fprintf for IRQ, RESET, PATCH_BOOT_GLOBS, SCSI_DISPATCH
- EmulOp dispatch (`emul_op.cpp`): removed unconditional fprintf (kept `D(bug(...))` for debug builds)
- Timer (`timer_interrupt.cpp`): removed OS/Toolbox trap table dumps, kept minimal 1Hz heartbeat
- Execution loop (`unicorn_exec_loop.c`): removed verbose mode, PC=0 register dumps
- Startup diagnostics: removed "first N" fprintf guards from `unicorn_wrapper.c` and `cpu_unicorn.cpp`

---

## ~~4. Fix Fake Instruction Counter~~ — DONE

Renamed `max_total_insns` to `max_iterations` since it counts loop restarts, not instructions. Removed the fake `total_executed += 1000`. Simplified the execution loop from 137 lines to 77 lines, removing legacy stubs and dead code.

---

## ~~5. Deduplicate M68kRegisters~~ — DONE

Created `m68k_registers.h` with a single C/C++ compatible definition using `uint32_t`. Updated all consumers:
- `common/include/main.h` — includes `m68k_registers.h`
- `cpu/uae_cpu/main.h` — includes `m68k_registers.h`
- `cpu_unicorn.cpp` — includes `m68k_registers.h`
- `unicorn_wrapper.c` — includes `m68k_registers.h`
- `emulop_c_wrapper.cpp` — simplified to direct call (no struct conversion needed)

---

## 6. Application Support (Next Feature Work)

**Status**: Both backends boot to Mac OS 7.5.5 Finder desktop. Next goal is running Mac applications.

**What's needed**:
- Test HyperCard stacks
- Test classic games
- Stability for 30+ minute sessions

---

## Summary

Items 1-5 completed in March 2026. The Unicorn backend is clean and boots to Finder, but is ~10x slower than UAE (94.7% JIT execution, hooks add only 5.3%). Next work is application support and stability.
