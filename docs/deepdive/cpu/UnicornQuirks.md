# Unicorn quirks

Things about Unicorn (and the QEMU TCG core under it) that aren't
obvious from reading the API docs and that we have to work around. This
is the m68k backend; PPC quirks live in
[`../../ppc/UnicornPpcStatus.md`](../../ppc/UnicornPpcStatus.md).

## Register writes inside hooks don't persist — defer them

`UC_HOOK_INTR` fires for A-line / F-line / illegal-instruction
exceptions. Calling `uc_reg_write(UC_M68K_REG_PC, ...)` directly inside
that hook does not stick — QEMU restores `exception_next_eip` after
the hook returns. The same applies to other registers in some cases.

Solution: queue every register write that an EmulOp / trap handler
wants, return from the hook without `uc_emu_stop()`, and apply the
queue at the next `UC_HOOK_BLOCK` boundary in
`apply_deferred_updates_and_flush()`. By that point QEMU has finished
its post-hook PC restoration and our writes win.

```c
// inside hook_interrupt():
deferred_pc       = new_pc;
deferred_pc_valid = 1;
deferred_dregs[0] = new_d0;
deferred_dregs_valid |= 1;
// don't call uc_reg_write or uc_emu_stop here

// inside hook_block(), at every basic-block boundary:
if (deferred_pc_valid)    uc_reg_write(uc, UC_M68K_REG_PC, &deferred_pc);
if (deferred_sr_valid)    uc_reg_write(uc, UC_M68K_REG_SR, &sr32 /* uint32_t! */);
// … per-D/A register, then clear the valid bits
```

Keep the deferred-update structures local to `unicorn_wrapper.c`; nothing
else needs to know.

## SR is read as `uint32_t`, not `uint16_t`

`uc_reg_write(uc, UC_M68K_REG_SR, ptr)` reads **4 bytes** from `ptr`.
QEMU represents SR internally as 32-bit. Passing a `uint16_t *` reads
two bytes of garbage off the stack and corrupts the upper SR bits.

```c
uint32_t sr32 = 0x2700;
uc_reg_write(uc, UC_M68K_REG_SR, &sr32);    // ✅
```

## MMIO: use `uc_mmio_map`, not memory hooks

`UC_HOOK_MEM_READ` / `UC_HOOK_MEM_WRITE` do **not** fire for regions
mapped with `uc_mem_map_ptr()` because the TCG JIT compiles direct
loads/stores that bypass hooks. Hardware-register stubs (VIA, SCC, SCSI,
ASC, DAFB, NuBus dummy, the DR-probe regions on PPC) must use
`uc_mmio_map()` so the access goes through QEMU's MMIO callback path.

Memory hooks are still useful for trace / debug on `uc_mem_map` (no `_ptr`)
regions — just not for production hardware emulation.

## Self-modifying code — partial fast-path, STALE-TB safety net

Unicorn's QEMU fork stubbed every dirty-bitmap function in
`subprojects/unicorn/qemu/include/exec/ram_addr.h`
(`cpu_physical_memory_is_clean` always returns `true`,
`set_dirty_flag` is a no-op, `test_and_clear_dirty` returns `false`,
`invalidate_and_set_dirty` in `exec.c` is empty). Because `is_clean`
always returns true, every RAM page gets `TLB_NOTDIRTY` set on first
TLB fill, so guest stores go through `store_helper` →
`notdirty_write` → `tb_invalidate_phys_page_fast` — that path **does**
work for guest m68k stores into executable pages, which covers the
common case (Mac OS heap manager overwriting our EmulOp patches).

Three things this **doesn't** cover:

- Host writes via `uc_mem_write()`. `flatview_write_continue()` does a
  raw `memcpy` and the post-write invalidation is empty. Only matters
  for ROM patching at startup, before the JIT runs.
- Host writes via `uc_mem_map_ptr` host pointers (BasiliskII's
  `put_long()` etc.). Completely invisible to QEMU.
- Edge cases in TLB repopulation where `TLB_NOTDIRTY` isn't set before
  a write occurs.

The `STALE-TB` detector in `hook_block` validates block contents against
expected EmulOp opcodes at execution time and forces a targeted
`uc_ctl_flush_tb()` when a stale block is about to run. This is **not**
debug cruft — it's a production safety net. Keep it. In a 30 s boot it
fires ~18 times (vs. the 1804 blanket flushes that came from an earlier
60 Hz workaround).

Full background in
[`../JitSmcDetectionAnalysis.md`](../JitSmcDetectionAnalysis.md).

## Don't manually flush the cache after register writes

`uc_reg_write(UC_M68K_REG_PC, …)` already flushes the relevant TBs:
QEMU sets `quit_request` and calls `break_translation_loop` internally
when PC changes. Earlier code added `uc_ctl_remove_cache(pc, pc + 16)`
plus a redundant PC re-write after every register update; that
triple-flushed unnecessarily and pinned the engine at ~200 instructions
per second.

Other register writes (D0–D7, A0–A7, SR) do **not** require any cache
flush — TCG reads register values from `CPUState` at run time, they
aren't baked into compiled blocks. Manual flushing is only needed when
you actually modify code in memory (`uc_mem_write` to an executable
page).

## Don't `uc_emu_stop()` after every EmulOp

Returning from `UC_HOOK_INTR` continues execution naturally. Calling
`uc_emu_stop()` from inside the hook restarts the JIT, drops
chained-block state, and was responsible for the same 200 ips floor
that the cache-flush bug caused. The exception is hardware interrupt
delivery — those genuinely need a stop / restart cycle so the new PC
takes effect.

## EmulOp encoding: `0x71xx` (UAE) vs `0xAExx` (Unicorn)

Unicorn raises an A-line exception on `0xAExx` and only an
illegal-instruction exception on `0x71xx`. UAE goes the other way. ROM
patching has to know which encoding to insert based on the active
backend; `src/core/rom_patches.cpp` handles this. Older code that wrote
`make_emulop(M68K_EMUL_OP_IRQ)` instead of the literal `0x7129` caused
an IRQ storm because the patcher emitted `0xAE29`, which routed to the
wrong dispatch path. Use direct opcode literals when you know which one
you want.

## SCSI / VIA probe regions

The Quadra ROM probes NuBus slots and reads MMIO ranges that don't have
real hardware behind them. The dummy bank fills `0xFF00FF00` (matches
real Quadra behaviour for unpopulated slots). The MMIO stubs in
`cpu_unicorn.cpp` cover VIA1 (`0x50F00000`), VIA2 (`0x50F02000`), SCC
(`0x50F04000`), SCSI (`0x50F10000`), ASC (`0x50F14000`), and DAFB
(`0x50F20000`) — all return minimal values to keep the ROM walking
forward.

## The trap gap at `0xFF000000`

A 4 KB unmapped region is left at `0xFF000000` as the EmulOp return
detector during native-trap execution: when an inner `Execute68kTrap`
finishes, it branches to a marker address in this gap, the
mem-unmapped hook fires, and the outer loop knows the trap returned.

## Related

- [`UaeQuirks.md`](UaeQuirks.md) — UAE-side counterpart.
- [`ALineAndFLineStatus.md`](ALineAndFLineStatus.md) — deferred-update
  details for the trap path.
- [`CpuBackendApi.md`](CpuBackendApi.md) — the Platform interface both
  backends fill in.
