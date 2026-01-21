# Unicorn JIT Block Size Analysis

**Date**: January 3, 2026
**Author**: Claude & Mick
**Purpose**: Understand JIT block sizes and their impact on interrupt timing non-determinism

---

## Executive Summary

We measured Unicorn's JIT block compilation behavior during a 2-second execution run of the Quadra 650 ROM. The findings explain why interrupt timing differs between Unicorn and UAE:

**Key Findings**:
- **Average block size**: 1.95 instructions
- **Median block size**: 2 instructions
- **91% of blocks**: 1-2 instructions
- **Maximum block size**: 38 instructions
- **Interrupt latency**: Average 1.95 instructions, max 38 instructions

**Impact**: Interrupts are checked at block boundaries, not every instruction. This causes timing variation of up to ~38 instruction cycles, which is acceptable for Mac OS emulation but explains divergence from instruction-level UAE execution.

---

## Measurement Methodology

### Implementation

Added block statistics tracking to `unicorn_wrapper.c`:

1. **BlockStats structure**: Tracks total blocks, instructions, histogram, min/max
2. **hook_block() instrumentation**: Counts instructions in each basic block
3. **Instruction counting**: Rough M68K instruction length estimation (2-4 bytes)
4. **Histogram**: Distribution of block sizes from 1-100+ instructions

### Test Configuration

```bash
EMULATOR_TIMEOUT=2 CPU_BACKEND=unicorn ./build/macemu-next ~/quadra.rom
```

- **Duration**: 2 seconds
- **ROM**: Quadra 650 (1MB)
- **Total blocks**: 12,299,578
- **Total instructions**: 24,038,184

---

## Results

### Overall Statistics

| Metric | Value |
|--------|-------|
| Total blocks executed | 12,299,578 |
| Total instructions | 24,038,184 |
| Average block size | **1.95 instructions** |
| Median block size | **2 instructions** |
| Min block size | 1 instruction |
| Max block size | 38 instructions |
| Instructions per second | ~12 million |
| Blocks per second | ~6.1 million |

### Block Size Distribution

| Block Size | Count | Percentage | Cumulative |
|------------|-------|------------|------------|
| 1 instruction | 3,828,372 | 31.13% | 31.13% |
| **2 instructions** | **7,410,335** | **60.25%** | **91.37%** |
| 3 instructions | 331,198 | 2.69% | 94.07% |
| 4 instructions | 154,428 | 1.26% | 95.32% |
| 5 instructions | 191,502 | 1.56% | 96.88% |
| 6 instructions | 99,500 | 0.81% | 97.69% |
| 7 instructions | 239,958 | 1.95% | 99.64% |
| 8-10 instructions | 18,859 | 0.15% | 99.79% |
| 11-20 instructions | 23,467 | 0.19% | 99.98% |
| 21+ instructions | 2,075 | 0.02% | 100.00% |

**Key Observations**:
- **91.37%** of blocks are just 1-2 instructions
- **94.07%** of blocks are 3 or fewer instructions
- Only **0.21%** of blocks exceed 10 instructions
- Very few outliers (38 instructions is rare)

---

## Interrupt Timing Impact

### Why This Matters

Unicorn checks for interrupts at **basic block boundaries** using `UC_HOOK_BLOCK`:

```c
static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    // Check for pending interrupts
    if (PendingInterrupt) {
        // Process interrupt...
    }
}
```

This means:
- **UAE**: Checks interrupts every single instruction (via `SPCFLAG_INT`)
- **Unicorn**: Checks interrupts every ~2 instructions (average block size)

### Latency Analysis

| Scenario | UAE | Unicorn |
|----------|-----|---------|
| **Interrupt raised** | Next instruction | Next block boundary |
| **Typical latency** | 0-1 instructions | 0-2 instructions (avg 1.95) |
| **Maximum latency** | 0-1 instructions | 0-38 instructions |
| **99.8% of cases** | 0-1 instructions | 0-10 instructions |

### Timing Divergence Example

Consider a timer interrupt that should fire at instruction 1000:

**UAE execution**:
```
Instruction 999:  Execute
Instruction 1000: Check interrupt → FIRE! → Jump to handler
```

**Unicorn execution** (if block spans 999-1002):
```
Block start (PC=999):
  Instruction 999:  Execute
  Instruction 1000: Execute (interrupt pending but not checked yet!)
  Instruction 1001: Execute
  Instruction 1002: Execute
Block end → Check interrupt → FIRE! → Jump to handler
```

**Result**: Unicorn delays interrupt by 0-3 instructions (in this example), leading to different register states and memory accesses.

---

## Performance Implications

### Why JIT Uses Blocks

JIT compilers create basic blocks because:
1. **Efficiency**: Compile once, execute many times
2. **Optimization**: Can optimize within a block
3. **Hook overhead**: Checking conditions at block boundaries is much faster than per-instruction

### Trade-offs

| Approach | Advantages | Disadvantages |
|----------|-----------|---------------|
| **Per-instruction hooks** | Precise timing, easy validation | 10x slower (measured) |
| **Per-block hooks** | Fast (JIT-friendly) | Timing variation up to block size |

**Our choice**: Per-block hooks via `UC_HOOK_BLOCK` (commit ebd3d1b2) for 5-10x performance improvement.

---

## Comparison with Original Analysis

From [InterruptTimingAnalysis.md](InterruptTimingAnalysis.md), we identified:
- **Root cause**: Wall-clock timer (UAE) vs instruction-count checks (Unicorn)
- **Secondary factor**: Block-based interrupt checking

**This analysis quantifies the secondary factor**:
- Average latency: ~2 instruction cycles
- Max latency: ~38 instruction cycles
- 99.8% of cases: ≤10 instruction cycles

For Mac OS 7.0 timer interrupts (60 Hz = 16.7ms intervals):
- At ~12M instructions/sec: 16.7ms = ~200,000 instructions between interrupts
- Block delay of 2-38 instructions = **0.001%-0.02% timing variation**
- **Conclusion**: Negligible for Mac OS, but explains UAE divergence

---

## Why Block Sizes Are Small

### M68K Instruction Characteristics

The small average block size (1.95 instructions) is due to:

1. **Branch-heavy code**: ROM initialization has many conditionals
2. **Function calls**: JSR/BSR end blocks
3. **Returns**: RTS ends blocks
4. **EmulOps**: 0x71xx traps break blocks
5. **Trap instructions**: A-line/F-line end blocks

### Example: Small Block Pattern

Typical ROM code pattern:
```m68k
40802A:  MOVE.L  (A0)+, D0    ; Block 1: 1 instruction
40802C:  BEQ.S   408034       ; Block ends (branch)

40802E:  JSR     (A1)         ; Block 2: 1 instruction (call ends block)

408032:  RTS                  ; Block 3: 1 instruction (return ends block)

408034:  MOVEQ   #0, D0       ; Block 4: Start of next sequence
408036:  MOVE.L  D0, (A0)
```

Each branch, call, or return creates a new basic block boundary.

---

## Conclusions

### 1. Non-Determinism is Expected

Unicorn's block-based execution **inherently** introduces 0-38 instruction timing variation for interrupts. This is:
- **By design**: JIT efficiency requires block-level hooks
- **Acceptable**: 0.001%-0.02% variation is insignificant for Mac OS
- **Unavoidable**: Moving to per-instruction hooks would lose 90% performance

### 2. Accept Non-Determinism

**Recommendation**: Accept interrupt timing non-determinism as a characteristic of the Unicorn backend.

**Why**:
- Performance: 5-10x faster with block hooks
- Functionality: Mac OS doesn't rely on cycle-accurate interrupt timing
- Pragmatism: 99.8% of blocks ≤10 instructions is very tight

### 3. Focus on Functional Testing

Instead of instruction-level trace comparison:
- Test **outcomes**, not exact instruction sequences
- Validate **memory state** at key checkpoints
- Ensure **API calls** produce correct results
- Verify **boot progress** milestones

---

## Future Work

### Deterministic Mode (Optional)

If needed for debugging, we could add:
```c
#define UNICORN_DETERMINISTIC_MODE 1

#if UNICORN_DETERMINISTIC_MODE
    // Use UC_HOOK_CODE (per-instruction) for validation
#else
    // Use UC_HOOK_BLOCK (per-block) for performance
#endif
```

**Trade-off**: 10x slower but instruction-accurate timing

### Measurement in Different Phases

Block sizes may vary during:
- ROM initialization (current measurement: avg 1.95)
- Boot sequence (TODO)
- Application execution (TODO)

Worth measuring block distribution during different workloads.

---

## References

- [InterruptTimingAnalysis.md](InterruptTimingAnalysis.md) - Root cause analysis
- Commit ebd3d1b2 - Removed UC_HOOK_CODE, switched to UC_HOOK_BLOCK
- Commit 1305d3b2 - Added interrupt support via UC_HOOK_BLOCK
- [Unicorn Engine Documentation](https://www.unicorn-engine.org/docs/)
- [M68K Programmer's Reference Manual](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf)

---

## Implementation Details

### Code Changes

Files modified:
- `src/cpu/unicorn_wrapper.c`: Added BlockStats tracking
- `src/cpu/unicorn_wrapper.h`: Exported statistics functions
- `src/cpu/cpu_unicorn.cpp`: Added atexit() handler

### Statistics Functions

```c
void unicorn_print_block_stats(UnicornCPU *cpu);  // Print full report
void unicorn_reset_block_stats(UnicornCPU *cpu);   // Reset counters
```

Automatically called on exit via `atexit()` handler.
