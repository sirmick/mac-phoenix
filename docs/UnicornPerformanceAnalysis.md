# Unicorn vs UAE: CPU Backend Performance Analysis

**Date:** March 2026
**Test environment:** Linux x86-64, Quadra 650 ROM, Mac OS 7.5.5 boot to Finder

## Summary

Unicorn (QEMU TCG JIT) is **~10x slower** than UAE (hand-tuned interpreter) for M68K emulation. Linux `perf` profiling revealed the **dominant bottleneck is first-time TB (Translation Block) compilation** — 77% of CPU time is spent in QEMU's TCG compiler generating native code for ~1.4M unique guest code addresses encountered during Mac OS boot. Hook overhead is minimal at ~5%.

### Boot Milestone Timing (March 2026)

| Milestone | UAE | Unicorn | Ratio |
|-----------|-----|---------|-------|
| WLSC (warm start) | 4.05s | 0.87s | **0.2x** (Unicorn faster) |
| Boot blocks (#40) | 4.05s | 1.35s | **0.3x** (Unicorn faster) |
| Extensions (#375) | 4.14s | 12.87s | **3.1x** slower |
| 1000 resources | 4.31s | 33.03s | **7.7x** slower |
| Finder launched | 4.41s | 46.01s | **10.4x** slower |
| 2000 resources | 4.48s | 52.90s | **11.8x** slower |

Note: Unicorn is **faster** during early ROM init (tight loops where JIT wins). The gap widens as Mac OS loads extensions (diverse code paths, small blocks, frequent traps).

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

Because Unicorn stubs `cpu_physical_memory_set_dirty_flag()` as a no-op (see [JIT_SMC_Detection_Analysis.md](deepdive/JIT_SMC_Detection_Analysis.md)), pages **never transition out of TLB_NOTDIRTY**. Every RAM write goes through the `store_helper` → `notdirty_write` slow path forever. Restoring `set_dirty_flag()` would let non-code pages use the fast write path after first write, reducing the 1.6% `store_helper` overhead.

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

### Applied (in production)

1. **Auto-ack interrupts** — Modified QEMU's `m68k_cpu_exec_interrupt()` to auto-acknowledge, eliminating stop/start cycle on every interrupt.

2. **`goto_tb` for backward branches** — Enabled QEMU's `goto_tb` for backward branches, allowing hot loops to chain without exiting for hook_block checks.

3. **Lean `hook_block()`** — Stripped per-block timing, statistics, and stale TB detector. Reduced per-block overhead to timer polling (every 4096 blocks) and deferred register updates.

### Tested and Reverted (no measurable impact)

4. **TB_JMP_CACHE_BITS 12 to 16** — Increased direct-mapped TB lookup cache from 4096 to 65536 entries. Finder at 46.82s vs 46.01s baseline — no improvement. TB cache hit rate was already adequate; the bottleneck is compilation, not lookup.

5. **`lookup_and_goto_ptr` for cross-page jumps** — Replaced `exit_tb(NULL, 0)` with `lookup_and_goto_ptr` in `gen_jmp_tb`'s cross-page else branch. Finder at 46.40s vs 46.01s baseline — no improvement. Cross-page dispatch overhead is negligible compared to compilation cost.

### Tested and Reverted (no impact, carries risk)

6. **Disable self-modifying code detection** — Disabled `tb_invalidate_phys_page_fast` in QEMU's `notdirty_write()`. This prevents TB invalidation when guest writes to pages containing translated code. **Result:** No measurable improvement — TB miss rate identical (85.1%) with or without. The miss rate is from first-time compilation of new code, not from invalidation of existing TBs. **Risk:** Would break any program that modifies code at runtime (unlikely for classic Mac apps, but not impossible for copy-protection schemes, self-patching code, or JIT compilers running inside the emulated Mac).

### Not Attempted (quick wins identified by perf)

7. **Remove `perf_now_ns()` from `hook_block()`** — Two `clock_gettime` vDSO calls per block × 32M blocks = 64M calls, costing ~1.9% of total time. These only feed the `hook_block_ns` perf counter printed at exit. Removing them is trivial and saves measurable time. **Estimated: ~1-2% improvement.**

8. **Restore `cpu_physical_memory_set_dirty_flag()`** — Currently stubbed as a no-op in Unicorn's `ram_addr.h`. This means every RAM write goes through the `notdirty_write` slow path forever. Restoring just this one function (set a bit in a bitmap) would let non-code pages transition to the fast write path after their first write. `store_helper` is 1.6% self time; reducing slow-path writes could help. **Estimated: modest improvement in memory-heavy phases.**

### Not Attempted (deep QEMU modifications)

9. **Selective CC flag materialization** — Instead of `gen_flush_flags()` computing all 5 flags (XNZVC) before every branch, only compute the flags the branch condition tests. Would require rewriting condition code handling in `target/m68k/translate.c`. Estimated weeks of work. Would improve JIT code quality but not compilation speed.

10. **Register pinning** — Keep D0-D2, A0-A1 in host x86 registers across TB boundaries. Requires changes to TCG register allocator. Would reduce memory-indirect overhead.

11. **Faster TCG compiler** — The TCG optimization/liveness/register-allocation pipeline is the core bottleneck (25.3% optimize+liveness, 25.2% codegen+regalloc). Making it faster would directly help. But this is deep QEMU infrastructure used by all architectures.

12. **TB compilation caching** — Serialize compiled TBs to disk and reload on subsequent boots. Would eliminate recompilation cost for repeated boots. Novel approach, not implemented in upstream QEMU. This is the highest-impact optimization possible — it would eliminate the dominant ~55% compilation overhead on subsequent boots.

## Conclusion

After optimization, hook overhead is minimal (~3.9% including vDSO timer calls). The ~10x gap has two components:

1. **TB compilation cost (~55% self, ~77% inclusive)**: 2.8M unique code blocks need first-time JIT compilation. Each takes ~17µs through QEMU's multi-pass pipeline. The cost splits roughly evenly between optimization/liveness analysis (25.3%) and code generation/register allocation (25.2%). This is a one-time cost per code path, but Mac OS boot exercises enormous code diversity.

2. **JIT code quality + softmmu overhead (~18% of time)**: The generated x86 code is less efficient than UAE's gcc-optimized interpreter handlers, due to condition code overhead, memory-indirect registers, and small basic blocks. Additionally, the permanently-broken dirty bitmap causes all RAM writes to take the slow `notdirty_write` path.

Both backends boot to Mac OS 7.5.5 Finder desktop. UAE is faster for end users; Unicorn's value is as an independent M68K implementation for validation and as a path toward future improvements.

**Next steps (in order of effort vs impact):**
1. Remove `perf_now_ns()` from `hook_block()` — trivial, saves ~1.9%
2. Restore `cpu_physical_memory_set_dirty_flag()` — small, reduces softmmu overhead
3. TB compilation caching (persist compiled blocks across runs) — high effort but would eliminate the dominant ~55% compilation overhead on subsequent boots
