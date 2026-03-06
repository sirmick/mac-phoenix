# Unicorn vs UAE: CPU Backend Performance Analysis

**Date:** March 2026
**Test environment:** Linux x86-64, Quadra 650 ROM, Mac OS 7.5.5 boot to Finder

## Summary

Unicorn (QEMU TCG JIT) is **~10x slower** than UAE (hand-tuned interpreter) for M68K emulation. After March 2026 optimizations (auto-ack interrupts, goto_tb backward branches, lean hook_block), hook overhead was reduced to 5.3% of total execution time. The remaining 94.7% is pure JIT execution — the bottleneck is QEMU's TCG code quality for M68K, not our infrastructure.

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

Unicorn's memory growth is from QEMU's TB cache — new code paths are JIT-compiled but never freed.

## Profiling Results (30-second Unicorn boot)

Instrumented with `clock_gettime(CLOCK_MONOTONIC)` around all major code paths:

```
=== Unicorn Performance Counters ===
Wall time in uc_emu_start():    30.000 s  (3233 calls, 9279.2 us/call)
  hook_block() total:           1.327 s  (4.4% of emu_start)
    timer polling:              0.002 s  (3299 polls, 0.8 us/poll)
    deferred updates:           0.484 s
    interrupt delivery:          0.001 s  (1622 interrupts, 0.6 us/int)
  hook_interrupt() total:       1.083 s  (60061 EmulOps, 18.0 us/op)
  JIT execution (estimated):   27.589 s  (92.0% of emu_start)
```

| Component | Time | % of total |
|-----------|------|------------|
| JIT execution (QEMU TCG) | 27.6s | 92.0% |
| hook_block() overhead | 1.3s | 4.4% |
| hook_interrupt() / EmulOps | 1.1s | 3.6% |
| Interrupt delivery (stop/start) | 0.001s | 0.003% |
| Timer polling | 0.002s | 0.006% |

### Block Execution Statistics

```
Total blocks executed:      13,513,447
Total instructions:         118,090,390
Average block size:         8.74 instructions
uc_emu_start() calls:       3,233 (one restart per ~4181 blocks)
```

Block size distribution — 57% of blocks are 6 instructions or fewer:

| Size (insns) | % | Cumulative |
|-------------|---|------------|
| 2 | 7.2% | 7.2% |
| 4 | 19.2% | 26.4% |
| 6 | 30.5% | 56.9% |
| 8 | 15.9% | 72.8% |
| 10 | 9.2% | 82.0% |

## Why the JIT Loses to an Interpreter

### 1. Condition code overhead

M68K updates XNZVC flags on almost every instruction. x86 also has flags but they don't map 1:1 — M68K's X (extend) flag has no x86 equivalent. QEMU can't use native x86 flags directly.

Instead, QEMU stores the operands and operation type (`cc_op`) and lazily computes flags when needed. But at every branch, it must materialize flags to evaluate the condition. A `beq` becomes: compute NZ from stored operands → test → branch — 5-10 x86 instructions for what should be 1.

UAE's interpreter handles this with GCC's optimized flag macros that the C compiler can often fold into native test/branch sequences.

### 2. Memory-indirect register file

QEMU stores all M68K registers in a `CPUState` struct in memory. Every register read/write is a load/store to that struct. Each translation block (TB) starts by loading registers from memory and ends by storing them back. With blocks averaging 8.7 instructions, that's significant load/store overhead per useful instruction.

UAE keeps `regs` in a C struct too, but the C compiler can optimize the inner loop to keep hot values in x86 registers. QEMU's TCG can't do that across translation block boundaries.

### 3. No cross-block optimization

Each TB is compiled independently. QEMU doesn't do trace compilation or block chaining optimization across branches. A hot inner loop like:

```asm
.loop:  move.l  (A0)+, D0
        add.l   D0, D1
        dbra    D2, .loop
```

Gets translated as separate TBs. Each iteration: exit TB → lookup next TB → enter TB. UAE's interpreter just runs a tight C dispatch loop — no per-block entry/exit overhead.

### 4. TB lookup overhead

After each TB finishes, QEMU looks up the next TB by guest PC in a hash table. With 450K blocks/sec, that's 450K hash lookups/sec. UAE's `cpufunctbl[opcode]` dispatch is a single array index — one instruction.

### 5. M68K is a second-class citizen in QEMU

ARM and x86 guests receive the most optimization attention. M68K's `translate.c` is relatively straightforward — it doesn't exploit TCG's optimization passes as aggressively. FPU emulation goes through softfloat (pure C library calls from JIT'd code) rather than mapping to x86 SSE/AVX.

## Why UAE's Interpreter Wins

UAE's inner loop (`m68k_do_execute()` in `newcpu.cpp`):

```c
for (;;) {
    opcode = *(uint16_t*)regs.pc_p;  // direct memory read via host pointer
    cpufunctbl[opcode](opcode);       // function pointer dispatch (64K table)
    if (SPCFLAGS) handle_specials();
}
```

- The C compiler (gcc -O2) optimizes each opcode handler aggressively
- `regs` struct is hot in L1 cache (accessed every instruction)
- Opcode dispatch is one indexed function pointer call
- No per-block translation overhead, no TB entry/exit, no hash lookups
- Condition codes computed inline with gcc-optimized flag macros

For a JIT to beat this, its output must be significantly better than what gcc produces for the interpreter. For M68K's small blocks and pervasive condition codes, it isn't. JIT wins on architectures with large basic blocks and simple flag semantics (e.g., ARM on x86). M68K is the worst case for a simple JIT.

## Unicorn Has No Interpreter Mode

Unicorn is purely JIT — built on QEMU's TCG, which always translates guest to host code. There is no interpreter fallback. QEMU has `--tcg-interpreter` (TCI) that interprets TCG IR, but it's even slower and Unicorn doesn't expose it.

## What Would Help (Theoretical)

### Within QEMU/Unicorn (deep modifications)
- **Lazy flag optimization**: Tighter `cc_op` implementation, avoid materializing flags when the next instruction overwrites them
- **Block chaining**: Link TBs directly so hot paths skip the hash lookup
- **Register pinning**: Keep D0-D2 and A0-A1 in x86 registers across TBs

These are deep modifications to `target/m68k/translate.c` and `tcg/` — weeks of work for uncertain gains.

### Reduce hook overhead (easy, ~4% gain)
- Only call `hook_block()` every N blocks for timer polling
- Batch deferred register updates

### Alternative second backend
- **Musashi**: Popular M68K interpreter (used by MAME). Clean C code, well-tested. Would slot behind our Platform API as a drop-in Unicorn replacement.

## March 2026 Optimizations

Three key optimizations reduced hook overhead from ~10% to ~5.3%:

1. **Auto-ack interrupts** — Modified QEMU's `m68k_cpu_exec_interrupt()` to auto-acknowledge interrupts, eliminating the stop/start cycle that broke JIT block chaining on every interrupt.

2. **`goto_tb` for backward branches** — Enabled QEMU's `goto_tb` optimization for backward branches, allowing hot loops to chain without exiting the JIT for hook_block checks.

3. **Lean `hook_block()`** — Stripped per-block performance timing, block statistics histogram, and stale TB detector from hook_block, reducing per-block overhead to just timer polling (every 4096 blocks) and deferred register updates.

## Conclusion

After optimization, hook overhead is minimal (5.3%). The ~10x gap is structural — inherent to QEMU's TCG JIT architecture when applied to M68K's small basic blocks and complex condition code semantics. The JIT execution quality is the sole limiting factor.

Both backends boot to Mac OS 7.5.5 Finder desktop. UAE is faster for end users; Unicorn's value is as an independent M68K implementation for validation and as a path toward future JIT improvements.
