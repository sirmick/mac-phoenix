# Unicorn vs UAE: CPU Backend Performance Analysis

**Test environment:** Linux x86-64, Quadra 650 ROM, Mac OS 7.5.5 boot to Finder

## Current status (April 2026, post-late-9b)

Unicorn (QEMU TCG JIT) boots to Finder in **~11.75s** vs UAE's **~2.67s** — a
**4.4x gap**, down from the 10.4x gap measured in March 2026. Gains came from
a series of 68k hot-path tightenings (see "April 2026 optimizations" below);
the fundamental bottleneck — first-time TB compilation — remains.

| Milestone | UAE | Unicorn (Mar) | Unicorn (post-late-9b) | Ratio (now) |
|-----------|-----|---------------|------------------------|-------------|
| Finder launched | 2.67s | 46.01s | 11.75s | **4.4x** slower |

A fresh flame-graph profile against the post-late-9b binary was recorded
on 2026-04-20 (see "Fresh profile (2026-04-20)" below). The March 2026
breakdowns remain the most detailed historical record; the April numbers
are lower-sample (gathered over ~10-20s boot runs) but capture where the
remaining headroom lives.

## Fresh profile (2026-04-20, post-late-9b)

`perf record -F 499 --call-graph dwarf` on headless boots against
`macos-7.6.1.img`. Raw data in `/tmp/perf-profiles/perf-{uae,unicorn-68k,unicorn-ppc}.data`;
flame graphs at `/tmp/perf-profiles/*.svg`.

### Unicorn 68k (15s run, 2417 samples @ 499 Hz)

| Function | Self % | Category |
|---------|--------|----------|
| `tcg_gen_code_m68k` | 14.47% | TCG codegen |
| `liveness_pass_1` | 13.55% | TCG liveness |
| `tcg_optimize_m68k` | 12.36% | TCG optimize |
| `la_cross_call` | 3.08% | TCG liveness |
| `tcg_out_opc` | 2.83% | TCG codegen |
| `store_helper` | 2.14% | Memory access |
| `tcg_out_branch` | 1.96% | TCG codegen |
| `tcg_temp_new_internal` | 1.86% | TCG codegen |
| `hook_block` | 1.15% | Hook overhead |
| `notdirty_write.isra.0` | 0.79% | Memory access |

**TCG compilation still dominates.** Optimize+liveness+codegen+regalloc
aggregates to roughly 45% of self time — same shape as March's breakdown,
just less extreme in absolute terms because the boot is shorter.

Residual opportunities (no single huge lever left):
- `store_helper` 2.14% + `notdirty_write` 0.79% = ~3% in the notdirty
  slow path. Restoring `cpu_physical_memory_set_dirty_flag()` (optimization
  #14) converts this to fast-path writes for non-code pages.
- `hook_block` 1.15% — post perf_now_ns gate, this is all bookkeeping
  (block-count, deferred register scan). The "block counter for timer
  poll at every 4096 blocks" could move to an atomic fetch-add into
  `env->icount_decr` or similar so the TCG prologue updates it for free.

### Unicorn PPC (20s run, 9908 samples @ 499 Hz; Desktop not reached)

| Function | Self % | Category |
|---------|--------|----------|
| `store_helper` | 8.24% | Memory access (notdirty) |
| `helper_lookup_tb_ptr_ppc` | 6.83% | TB cache lookup |
| `last_pc_cb` (anonymous lambda) | 6.58% | **Block-trace hook overhead** |
| `helper_check_exit_request_ppc` | 3.86% | Per-TB dispatch |
| `notdirty_write.isra.0` | 3.06% | Memory access |
| `page_find_alloc` | 1.55% | Memory access (notdirty) |
| `tb_htable_lookup_ppc` | 1.06% | TB cache lookup |
| `tb_invalidate_phys_page_fast_ppc` | 1.07% | Memory access (notdirty) |
| `tlb_set_page_with_attrs_ppc` | 0.67% | TLB fill |

**Notdirty path is the #1 hotspot by far.** `store_helper + notdirty_write +
page_find_alloc + tb_invalidate_phys_page_fast` = ~13.9% self time. PPC
hammers RAM harder than 68k (bigger framebuffer, more memcpy-y code paths),
and every write takes the slow path forever because the dirty bitmap is
stubbed. Optimization #14 (restore `cpu_physical_memory_set_dirty_flag`)
moves from "modest improvement" to the single largest identified win.

**Surprise: block tracer was 6.58%.** The `last_pc_cb` hook (installed in
`uppc_cpu_init`, `cpu_unicorn_ppc.cpp:649`) updated a 32-slot ring of
block PCs and a sequence counter on every TB. The header comment
predicted "~2% perf cost" but measured 6.58% — the hook-dispatch machinery,
not the bookkeeping, is the cost. **Fixed in late-9c (2026-04-20)**: env
var renamed from `MACEMU_PPC_NO_BLOCK_TRACE` (opt-out) to
`MACEMU_PPC_BLOCK_TRACE` (opt-in), so the tracer is off by default. Crash
handler loses its last-PC ring unless the user sets the env var for
debug sessions.

**TB lookup is expensive on PPC.** `helper_lookup_tb_ptr` + `tb_htable_lookup`
+ `tb_jmp_cache_hash_func` = ~8% of time in TB lookup alone. March's
analysis showed 68k's `TB_JMP_CACHE_BITS 12→16` didn't help 68k; the
same experiment on PPC may be more productive given how much time
lookup consumes here.

### Updated optimization priorities (post-2026-04-20 profile)

1. **Disable `last_pc_cb` by default on PPC** (trivial; save ~6.58% on PPC).
2. **Restore `cpu_physical_memory_set_dirty_flag`** (task #14; save
   ~14% on PPC, ~3% on 68k).
3. **Enlarge TB_JMP_CACHE on PPC** (retry the March experiment that
   didn't help 68k; save ~3-5% if successful).
4. **TB compilation caching** (unchanged; still the highest-impact
   large investment).

## Historical baseline (March 2026)

### Summary (March 2026)

Unicorn (QEMU TCG JIT) was **~10x slower** than UAE (hand-tuned interpreter) for M68K emulation. Linux `perf` profiling revealed the **dominant bottleneck is first-time TB (Translation Block) compilation** — 77% of CPU time was spent in QEMU's TCG compiler generating native code for ~1.4M unique guest code addresses encountered during Mac OS boot. Hook overhead was minimal at ~5%.

### Boot Milestone Timing (March 2026)

| Milestone | UAE | Unicorn | Ratio |
|-----------|-----|---------|-------|
| WLSC (warm start) | 4.05s | 0.87s | **0.2x** (Unicorn faster) |
| Boot blocks (#40) | 4.05s | 1.35s | **0.3x** (Unicorn faster) |
| Extensions (#375) | 4.14s | 12.87s | **3.1x** slower |
| 1000 resources | 4.31s | 33.03s | **7.7x** slower |
| Finder launched | 4.41s | 46.01s | **10.4x** slower |
| 2000 resources | 4.48s | 52.90s | **11.8x** slower |

Note: Unicorn was **faster** during early ROM init (tight loops where JIT wins). The gap widened as Mac OS loaded extensions (diverse code paths, small blocks, frequent traps).

### Memory Usage

| Backend | RSS at t=1s | RSS at t=58s | Growth |
|---------|-------------|--------------|--------|
| UAE | 51,108 kB | 51,108 kB | 0 kB (constant) |
| Unicorn | 54,360 kB | 57,184 kB | +2,824 kB (~47 kB/sec) |

Unicorn's memory growth is from QEMU's TB cache — new code paths are JIT-compiled but never freed. Growth stabilizes after ~60s as all code paths are covered.

## Linux perf Profiling (70-second boot, March 2026)

### Top Functions by Self Time (perf record -g -F 997 --call-graph dwarf)

69,434 samples collected over a full 70-second run (55.85s to Finder).

| Function | Self % | Category |
|----------|--------|----------|
| tcg_optimize_m68k | 5.7% | TCG optimize |
| liveness_pass_1 | 5.3% | TCG liveness |
| la_reset_pref | 3.3% | TCG liveness |
| tcg_out_opc | 3.0% | TCG codegen |
| tcg_reg_alloc_op | 2.4% | TCG regalloc |
| la_cross_call | 2.4% | TCG liveness |
| tcg_gen_code_m68k | 2.2% | TCG codegen |
| store_helper | 1.6% | Memory access |
| tcg_reg_alloc_call | 1.6% | TCG regalloc |
| la_func_end | 1.6% | TCG liveness |
| test_bit | 1.5% | TB lookup |
| vDSO clock_gettime | 1.5% | Hook overhead |
| reset_ts | 1.3% | TCG liveness |
| la_global_kill | 1.2% | TCG liveness |
| tcg_emit_op_m68k | 1.1% | TCG codegen |
| g_hash_table_lookup_node | 1.1% | TB lookup |
| tcg_reg_free | 1.1% | TCG regalloc |
| tcg_out8 | 1.0% | TCG codegen |
| reachable_code_pass | 1.0% | TCG optimize |
| la_global_sync | 1.0% | TCG liveness |
| address_space_translate_internal | 0.9% | Memory access |
| flatview_do_translate | 0.9% | Memory access |
| m68k_tr_translate_insn | 0.7% | M68K decoder |

### Aggregated by Category

| Category | % of CPU time | Key functions |
|----------|---------------|---------------|
| **TCG optimize + liveness** | **25.3%** | `tcg_optimize_m68k`, `liveness_pass_1`, `la_*`, `reset_ts` |
| **TCG codegen + regalloc** | **25.2%** | `tcg_out_opc`, `tcg_reg_alloc_*`, `tcg_gen_code_m68k` |
| **Memory access (softmmu)** | **8.5%** | `store_helper`, `address_space_translate`, `flatview_*` |
| **TB lookup + misc** | **5.3%** | `g_hash_table`, `test_bit`, `find_first_bit` |
| **M68K decoder** | **4.6%** | `m68k_tr_translate_insn`, `translator_loop_m68k` |
| **Hook overhead** | **3.9%** | `hook_block`, `hook_interrupt`, `perf_now_ns` (vDSO) |
| **notdirty_write path** | **1.3%** | `notdirty_write`, `tb_invalidate_phys_page_fast`, `page_flush_tb_1` |
| **Dispatch** | **1.0%** | `cpu_exec`, `helper_check_exit_request` |

**Key finding:** ~55% of self time (inclusive ~77%) is TB compilation. The two halves are roughly equal: optimization/liveness analysis (25.3%) and code generation/register allocation (25.2%).

### Notable: hook_block perf_now_ns overhead

`hook_block()` calls `perf_now_ns()` (clock_gettime via vDSO) **twice per block** — once at entry and once at exit — purely for performance counter timing. With 32M blocks executed, that's **64M clock_gettime calls** accounting for ~1.9% of total time. These serve no functional purpose; they only populate the `hook_block_ns` perf counter printed at exit. Removing them is the lowest-hanging fruit.

### Notable: notdirty_write permanent slow path

Because Unicorn stubs `cpu_physical_memory_set_dirty_flag()` as a no-op (see [JitSmcDetectionAnalysis.md](deepdive/JitSmcDetectionAnalysis.md)), pages **never transition out of TLB_NOTDIRTY**. Every RAM write goes through the `store_helper` → `notdirty_write` slow path forever. Restoring `set_dirty_flag()` would let non-code pages use the fast write path after first write, reducing the 1.6% `store_helper` overhead.

### TB Compilation Statistics (60-second boot)

```
tb_find() calls:            3,334,233
  tb_gen_code (compile):    2,799,002 (83.9%)
  cache hit:                  535,231 (16.1%)
Code buffer full flushes:   0
```

Mac OS 7.5.5 boot touches **~2.8M unique code addresses** that each need first-time JIT compilation. This is not cache thrashing — the code gen buffer never fills, and no flushes occur. The OS genuinely executes code from millions of distinct addresses (ROM routines, system heap, INITs, extensions, Finder).

### Why Compilation Dominates

Each TB compilation involves:
1. **M68K decode** → TCG IR (intermediate representation)
2. **TCG optimization** → constant folding, dead code elimination
3. **Liveness analysis** → register allocation prep
4. **Register allocation** → assign host registers to TCG temps
5. **Code generation** → emit x86 machine code

At ~17us per compilation and 2.8M blocks, that's ~48 seconds of compilation in a 60-second run. UAE's interpreter skips all of this — it just dispatches through a 64K function pointer table.

## Instrumented Performance Counters (70-second boot, March 2026)

```
=== Unicorn Performance Counters ===
Wall time in uc_emu_start():    69.981 s  (7751 calls, 9028.7 us/call)
  hook_block() total:           1.279 s  ( 1.8%)  (31.98M calls, 0.04 us/call)
  hook_interrupt() total:       2.392 s  ( 3.4%)  (161K EmulOps, 14.8 us/op)
  JIT execution (estimated):   66.310 s  (94.8%)
  Interrupts delivered:         3877
  TB cache flushes:             0
  tb_find() calls:              3.34M
    tb_gen_code (compile):      2.81M (83.9%)
  Code buffer full flushes:     0
  uc_emu_start() restarts:      7751 (110.8/sec)
Total blocks executed:          31.98M
```

Note: "JIT execution (estimated)" = total time minus hook time. This 94.8% includes both TB compilation (~77%) and actual JIT code execution (~18%).

## Why the JIT Loses to an Interpreter

### 1. Compilation cost dominates

The biggest factor is not JIT code quality — it's the cost of JIT compilation itself. Mac OS boot exercises ~2.8M unique code paths. Each compilation takes ~17us through QEMU's multi-pass TCG pipeline. UAE's interpreter has zero compilation cost.

### 2. Condition code overhead

M68K updates XNZVC flags on almost every instruction. QEMU stores operands and operation type (`cc_op`) and lazily computes flags when needed. But at every branch, it must materialize flags to evaluate the condition. A `beq` becomes 5-10 x86 instructions for what should be 1.

UAE's interpreter handles this with GCC's optimized flag macros that the C compiler can often fold into native test/branch sequences.

### 3. Memory-indirect register file

QEMU stores all M68K registers in a `CPUState` struct in memory. Every register read/write is a load/store. Each TB starts by loading registers and ends by storing them back. With blocks averaging ~8.7 instructions, that's significant overhead per useful instruction.

### 4. Small basic blocks

M68K code has small basic blocks (57% are 6 instructions or fewer). Each TB has fixed entry/exit overhead for register save/restore, flag sync, and TB chaining. Small blocks means this overhead is a large fraction of total work.

### 5. M68K is a second-class citizen in QEMU

ARM and x86 guests receive the most optimization attention. M68K's `translate.c` is relatively straightforward — it doesn't exploit TCG's optimization passes as aggressively.

## Why UAE's Interpreter Wins

UAE's inner loop (`m68k_do_execute()` in `newcpu.cpp`):

```c
for (;;) {
    opcode = *(uint16_t*)regs.pc_p;  // direct memory read via host pointer
    cpufunctbl[opcode](opcode);       // function pointer dispatch (64K table)
    if (SPCFLAGS) handle_specials();
}
```

- **Zero compilation cost** — every instruction executes immediately
- The C compiler (gcc -O2) optimizes each opcode handler aggressively
- `regs` struct is hot in L1 cache (accessed every instruction)
- Opcode dispatch is one indexed function pointer call
- No per-block translation overhead, no TB entry/exit, no hash lookups
- Condition codes computed inline with gcc-optimized flag macros

## Optimizations Attempted

### Applied (in production, chronological)

1. **Auto-ack interrupts** — Modified QEMU's `m68k_cpu_exec_interrupt()` to auto-acknowledge, eliminating stop/start cycle on every interrupt.

2. **`goto_tb` for backward branches** — Enabled QEMU's `goto_tb` for backward branches, allowing hot loops to chain without exiting for hook_block checks.

3. **Lean `hook_block()`** — Stripped per-block timing, statistics, and stale TB detector. Reduced per-block overhead to timer polling (every 4096 blocks) and deferred register updates.

### April 2026 optimizations (late-9 / late-9b)

Wall time: 15.78s → 11.75s (UAE ratio 5.9x → 4.4x), commit `1d0eb4f6` plus
the softmmu MR cache in `c9caeb1f`.

4. **Gate per-block `perf_now_ns()` behind `MACEMU_DEBUG_PERF` env var** — Previously feared as ~1.9% of wall time, the actual cost under load was much higher (single largest hotspot in `hook_block`). The vDSO is cheap per call, but 100M+ calls per boot add up. Two `clock_gettime` calls per block are now lazy-init + gated on env var; production runs skip them entirely. Patch 0008 in `subprojects/unicorn-patches/`.

5. **Extract SCSI probe accelerator from `hook_block`** — The SCSI accelerator was branched for 3 PCs on every block. Extracted into a dedicated `UC_HOOK_CODE` at the `0x020014be..0x020014ca` range so non-matching blocks never pay the branch cost.

6. **`any_deferred` short-circuit in `apply_deferred_updates_and_flush`** — The common case for `hook_block` fires has nothing queued. Added a flag so the per-register scan short-circuits when nothing is deferred.

7. **Remove dead `poll_timer_interrupt()` call** — No-op stub; 60 Hz tick thread already drives IRQs through `g_pending_interrupt_level`.

8. **4-way page-keyed LRU in `find_memory_mapping`** — Softmmu hammered `address_space_translate` on every TLB miss / notdirty write / unmapped probe (documented at ~8% of PPC wall time; also non-trivial on 68k). 4-entry cache keyed on page-aligned paddr catches nearly every call. Round-robin replacement, NULL results not cached, invalidated wholesale in `memory_map` / `memory_unmap` / `memory_map_io` / `memory_cow` / `memory_moveout` / `memory_movein`. Patch 0008.

9. **Per-TLB-entry MR pointer cache (late-9b)** — Extended the win in #8: add `MemoryRegion *mr` to `CPUTLBEntry`, populated lazily on first miss per-entry and reused on every subsequent access to the same page. Removes the `find_memory_mapping` call (and thus the 4-entry LRU lookup) from the hot path entirely for repeat accesses. Invalidated wholesale via `tlb_flush()`. Patch 0009.

10. **Unmapped read/write handlers no longer leak** — Silently leaked 1MB of `calloc`'d memory per fault (and returned the bogus mapping as success). Now log once and return false so the outer loop can react. Verified zero fires during normal boot — full 32-bit space is covered by RAM/ROM/ScratchMem/FrameBuffer + dummy/gap/MMIO/high_mem maps.

### Tested and Reverted (no measurable impact)

11. **TB_JMP_CACHE_BITS 12 to 16** — Increased direct-mapped TB lookup cache from 4096 to 65536 entries. Finder at 46.82s vs 46.01s baseline — no improvement. TB cache hit rate was already adequate; the bottleneck is compilation, not lookup.

12. **`lookup_and_goto_ptr` for cross-page jumps** — Replaced `exit_tb(NULL, 0)` with `lookup_and_goto_ptr` in `gen_jmp_tb`'s cross-page else branch. Finder at 46.40s vs 46.01s baseline — no improvement. Cross-page dispatch overhead is negligible compared to compilation cost.

### Tested and Reverted (no impact, carries risk)

13. **Disable self-modifying code detection** — Disabled `tb_invalidate_phys_page_fast` in QEMU's `notdirty_write()`. This prevents TB invalidation when guest writes to pages containing translated code. **Result:** No measurable improvement — TB miss rate identical (85.1%) with or without. The miss rate is from first-time compilation of new code, not from invalidation of existing TBs. **Risk:** Would break any program that modifies code at runtime (unlikely for classic Mac apps, but not impossible for copy-protection schemes, self-patching code, or JIT compilers running inside the emulated Mac).

### Not Attempted (still open)

14. **Restore `cpu_physical_memory_set_dirty_flag()`** — Currently stubbed as a no-op in Unicorn's `ram_addr.h`. This means every RAM write goes through the `notdirty_write` slow path forever. Restoring just this one function (set a bit in a bitmap) would let non-code pages transition to the fast write path after their first write. Late-9b's per-TLB-entry MR cache (#9) removed the `memory_mapping` call from the notdirty path, so the residual cost is lower than March's 1.3% baseline — but the path is still taken unnecessarily. **Estimated: modest improvement in memory-heavy phases.**

### Not Attempted (deep QEMU modifications)

15. **Selective CC flag materialization** — Instead of `gen_flush_flags()` computing all 5 flags (XNZVC) before every branch, only compute the flags the branch condition tests. Would require rewriting condition code handling in `target/m68k/translate.c`. Estimated weeks of work. Would improve JIT code quality but not compilation speed.

16. **Register pinning** — Keep D0-D2, A0-A1 in host x86 registers across TB boundaries. Requires changes to TCG register allocator. Would reduce memory-indirect overhead.

17. **Faster TCG compiler** — The TCG optimization/liveness/register-allocation pipeline is the core bottleneck (was 25.3% optimize+liveness, 25.2% codegen+regalloc in March). Making it faster would directly help. But this is deep QEMU infrastructure used by all architectures.

18. **TB compilation caching** — Serialize compiled TBs to disk and reload on subsequent boots. Would eliminate recompilation cost for repeated boots. Novel approach, not implemented in upstream QEMU. This is the highest-impact optimization possible — it would eliminate the dominant compilation overhead on subsequent boots.

## Conclusion

After late-9b, Unicorn 68k boots in ~11.75s vs UAE's ~2.67s (4.4x gap, down
from 10.4x in March). Remaining gap is expected to be dominated by
TB compilation — confirming this requires a fresh flame-graph profile
(task #17). The two components of the gap:

1. **TB compilation cost**: 2.8M unique code blocks need first-time JIT compilation. Each takes ~17µs through QEMU's multi-pass pipeline. Most of this cost is unavoidable without persistent TB caching (#18).

2. **JIT code quality + residual softmmu overhead**: The generated x86 code is less efficient than UAE's gcc-optimized interpreter handlers, due to condition code overhead, memory-indirect registers, and small basic blocks. Softmmu overhead shrank after late-9 (MR cache) and late-9b (per-TLB-entry MR), but `notdirty_write` still takes the slow path for all RAM writes (#14).

Both backends boot to Mac OS 7.5.5 Finder desktop. UAE is faster for end users; Unicorn's value is as an independent M68K implementation for validation and as a path toward future improvements.

**Next steps (in order of effort vs impact):**
1. Fresh flame-graph profile of post-late-9b binary to re-prioritize (task #17)
2. Restore `cpu_physical_memory_set_dirty_flag()` — small, reduces residual `notdirty_write` cost
3. TB compilation caching (persist compiled blocks across runs) — high effort but would eliminate the dominant compilation overhead on subsequent boots
