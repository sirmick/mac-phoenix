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

## Linux perf Profiling (30-second boot)

### Top Functions by Self Time (perf record -g -F 997)

| Function | Self % | Category |
|----------|--------|----------|
| tcg_optimize_m68k | 5.5% | TCG compiler |
| liveness_pass_1 | 5.0% | TCG compiler |
| la_reset_pref | 3.4% | TCG compiler |
| tcg_out_opc | 3.1% | TCG compiler |
| la_cross_call | 2.7% | TCG compiler |
| tcg_reg_alloc_op | 2.4% | TCG compiler |
| tcg_gen_code_m68k | 2.2% | TCG compiler |
| store_helper | 2.1% | Memory access |
| tcg_reg_alloc_call | 1.6% | TCG compiler |
| la_func_end | 1.5% | TCG compiler |
| address_space_translate | 1.3% | Memory access |
| m68k_tr_translate_insn | 0.6% | M68K decoder |

### Aggregated by Category

| Category | % of CPU time |
|----------|---------------|
| **TB Compilation (TCG compiler)** | **~45%** |
| **TB Translation (M68K decoder)** | **~18%** |
| **Memory access (TLB + softmmu)** | **~8%** |
| **JIT-generated code execution** | **~2%** |
| Hook overhead (hook_block + hook_interrupt) | ~5% |
| Other (TB lookup, dispatch) | ~5% |

**Key finding:** 77% of time (inclusive) is under `tb_gen_code_m68k` — QEMU is spending most of its time **compiling** translation blocks, not executing them.

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

## Instrumented Performance Counters (60-second boot)

```
=== Unicorn Performance Counters ===
Wall time in uc_emu_start():    60.0 s  (6958 calls, 8621.6 us/call)
  hook_block() total:           1.1 s  ( 1.9%)  (32.5M calls, 0.03 us/call)
  hook_interrupt() total:       1.9 s  ( 3.2%)  (160K EmulOps, 11.9 us/op)
  JIT execution (estimated):   57.0 s  (95.0%)
  TB cache flushes:             0
  tb_find() calls:              3.3M
    tb_gen_code (compile):      2.8M (83.9%)
  Code buffer full flushes:     0
  uc_emu_start() restarts:      6958 (116.0/sec)
Total blocks executed:          32.5M
```

Note: "JIT execution (estimated)" = total time minus hook time. This 95% includes both TB compilation (~77%) and actual JIT code execution (~18%).

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

### Not Attempted (deep QEMU modifications)

7. **Selective CC flag materialization** — Instead of `gen_flush_flags()` computing all 5 flags (XNZVC) before every branch, only compute the flags the branch condition tests. Would require rewriting condition code handling in `target/m68k/translate.c`. Estimated weeks of work. Would improve JIT code quality but not compilation speed.

8. **Register pinning** — Keep D0-D2, A0-A1 in host x86 registers across TB boundaries. Requires changes to TCG register allocator. Would reduce memory-indirect overhead.

9. **Faster TCG compiler** — The TCG optimization/liveness/register-allocation pipeline is the core bottleneck. Making it faster would directly help. But this is deep QEMU infrastructure used by all architectures.

10. **TB compilation caching** — Serialize compiled TBs to disk and reload on subsequent boots. Would eliminate recompilation cost for repeated boots. Novel approach, not implemented in upstream QEMU.

## Conclusion

After optimization, hook overhead is minimal (5%). The ~10x gap has two components:

1. **TB compilation cost (~77% of time)**: 2.8M unique code blocks need first-time JIT compilation. Each takes ~17us through QEMU's multi-pass pipeline. This is a one-time cost per code path, but Mac OS boot exercises enormous code diversity.

2. **JIT code quality (~18% of time)**: The generated x86 code is less efficient than UAE's gcc-optimized interpreter handlers, due to condition code overhead, memory-indirect registers, and small basic blocks.

Both backends boot to Mac OS 7.5.5 Finder desktop. UAE is faster for end users; Unicorn's value is as an independent M68K implementation for validation and as a path toward future improvements.

The most impactful future optimization would be TB compilation caching (persist compiled blocks across runs), which would eliminate the dominant 77% compilation overhead on subsequent boots.
