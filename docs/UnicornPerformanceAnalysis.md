# Unicorn-m68k vs UAE perf

Where the time goes on `--backend unicorn-m68k`, why it's ~4× slower than
UAE booting Mac OS 7.6.1 to Finder, and what's already been wrung out.

## End state

| Metric | UAE | Unicorn-m68k | Ratio |
|--------|-----|--------------|-------|
| Time to Finder | ~2.7 s | ~11.8 s | **4.4×** slower |
| RSS at idle | ~51 MB | ~57 MB | growing slowly until TB cache stabilises (~60 s) |
| TB cache flushes during boot | n/a | 0 | — |
| `tb_find` first-time compile rate | n/a | ~71 % | — |

## Where the time goes

`perf record -F 499 --call-graph dwarf` on a 15 s headless boot
(`macos-7.6.1.img`):

| Function | Self % | Category |
|---------|--------|----------|
| `tcg_gen_code_m68k` | 14.5 % | TCG codegen |
| `liveness_pass_1` | 13.5 % | TCG liveness |
| `tcg_optimize_m68k` | 12.4 % | TCG optimise |
| `la_cross_call` | 3.1 % | TCG liveness |
| `tcg_out_opc` | 2.8 % | TCG codegen |
| `store_helper` | 2.1 % | softmmu (notdirty path) |
| `tcg_out_branch` | 2.0 % | TCG codegen |
| `tcg_temp_new_internal` | 1.9 % | TCG codegen |
| `hook_block` | 1.2 % | UC_HOOK_BLOCK body (perf_now_ns gated off in production) |
| `notdirty_write.isra.0` | 0.8 % | softmmu (notdirty path) |

**TCG compilation dominates.** Optimise + liveness + codegen + regalloc
together is ~45 % of self-time. Mac OS boot touches roughly 2.8 M unique
guest code addresses; each first-time TB takes ~17 µs through QEMU's
multi-pass pipeline, so most of the wall-clock cost is the compiler, not
the compiled code.

## Why JIT loses to an interpreter here

1. **Compilation cost.** UAE's interpreter has zero compilation
   overhead — a 64 K function-pointer dispatch table. Unicorn pays per
   unique block.
2. **Condition codes.** m68k updates X/N/Z/V/C on almost every
   instruction. QEMU stores `cc_op` and lazily computes flags at every
   branch, so a `beq` materialises into 5–10 host instructions for what
   should be 1.
3. **Memory-indirect register file.** Every TB starts by loading the
   m68k register file from `CPUState` and ends by storing it back.
   Average TB is ~1.95 instructions (see
   `deepdive/cpu/JitBlockSizeAnalysis.md`), so this fixed cost is a
   meaningful fraction of total work.
4. **Small basic blocks.** 91 % of TBs are 1–2 instructions. Per-TB
   entry/exit overhead dominates.
5. **m68k is a second-class QEMU target.** `target/m68k/translate.c`
   doesn't exploit TCG passes as aggressively as ARM/x86.

## What's already in the production path

These are the tunings landed in `subprojects/unicorn-patches/` and the
`unicorn_*.c` wrapper that produced the current ~12 s figure (down from
a ~46 s baseline before any tuning):

- **Auto-ack interrupts** in QEMU's `m68k_cpu_exec_interrupt`, so a 60 Hz
  IRQ doesn't stop/restart the engine.
- **`goto_tb` for backward branches**, so hot loops chain natively.
- **Lean `hook_block`.** Per-block `clock_gettime` is gated behind
  `MACEMU_DEBUG_PERF`; SCSI-probe accelerator extracted into a dedicated
  `UC_HOOK_CODE` so non-matching blocks don't pay the branch cost; the
  deferred-update scan short-circuits when nothing's queued.
- **4-way page-keyed LRU in `find_memory_mapping`.** Caches softmmu
  translation results so non-TLB callers don't hammer
  `address_space_translate`.
- **Per-TLB-entry MR pointer.** Every TLB entry caches its
  `MemoryRegion *` so the five hot softmmu sites skip the indirect
  `uc->memory_mapping` call on warm pages. Invalidated wholesale via
  `tlb_flush()`.
- **Unmapped read/write handlers** no longer leak 1 MB of `calloc`'d
  memory per fault, no longer mark the bogus mapping as success.

## What's left on the table

Open levers, in rough order of effort vs. impact:

1. **Replace `UC_HOOK_BLOCK` with QEMU's native interrupt path** —
   move IRQ delivery to `cpu->interrupt_request` +
   `cc->tcg_ops->cpu_exec_interrupt`. Deferred register updates already
   run from `hook_interrupt`. Estimated 4–9 s reclaim — the per-TB
   `hook_block` dispatch trampoline is the biggest hidden cost.
2. **Direct `env_ptr` / `RAMBaseHost` access in `hook_interrupt`.** Drop
   the three `uc_*` API calls per EmulOp; removes the redundant
   `break_translation_loop` triggered by `uc_reg_write(PC)`. Estimated
   2–4 s reclaim.
3. **Restore `cpu_physical_memory_set_dirty_flag()`.** Currently stubbed
   in `subprojects/unicorn/qemu/include/exec/ram_addr.h:75`; every RAM
   write goes through `notdirty_write` forever instead of transitioning
   to the fast path after first write. Modest win on m68k, larger on
   PPC.
4. **Persistent TB cache across runs.** Highest-impact, biggest effort.
   Serialise compiled TBs to disk so subsequent boots skip first-time
   compilation. Not implemented in upstream QEMU.

Tested and reverted (no measurable impact, evidence kept for the
record):

- `TB_JMP_CACHE_BITS 12 → 16` — no change. Hit rate already adequate;
  bottleneck is compilation, not lookup.
- `lookup_and_goto_ptr` for cross-page jumps — no change. Cross-page
  dispatch is negligible vs. compilation.
- Disabling `notdirty_write` SMC detection entirely — no change in TB
  miss rate. Carries correctness risk for self-modifying guest code.

Heavy-rewrite ideas not attempted: selective CC flag materialisation
(only compute the flags a branch tests), TCG register pinning
(D0-D2/A0-A1 in host x86 across TB boundaries), faster TCG compiler.

## Why UAE wins

UAE's inner loop (`m68k_do_execute()` in `newcpu.cpp`):

```c
for (;;) {
    opcode = *(uint16_t *)regs.pc_p;     /* host-pointer fetch */
    cpufunctbl[opcode](opcode);           /* one indirect call  */
    if (SPCFLAGS) handle_specials();
}
```

Zero compilation, gcc -O2 over each opcode handler, `regs` hot in L1,
condition codes computed inline with optimised flag macros, no per-block
overhead, no hash lookups, no TB chaining.

## Reproducing

```bash
sudo sysctl kernel.perf_event_paranoid=-1
perf record -F 499 -g --call-graph dwarf \
    ./build/mac-phoenix --backend unicorn-m68k --no-webserver \
    --timeout 15 ~/storage/roms/quadra.rom
perf report
```

The instrumented counter dump prints on exit when
`MACEMU_DEBUG_PERF=1` is set.
