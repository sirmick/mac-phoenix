# Unicorn Performance Analysis

## Date: January 24, 2026

## Executive Summary
After successfully fixing the IRQ storm issue, analysis reveals that Unicorn is **15x slower** than UAE during Mac OS boot, primarily due to execution loop overhead in tight loops like CLKNOMEM.

## Key Findings

### Performance Metrics
- **EmulOp Throughput**: UAE: 1410/sec, Unicorn: 92/sec (15.23x difference)
- **Boot Progress in 5s**: UAE reaches 27 unique EmulOps, Unicorn only 7
- **CLKNOMEM Loop**: Both execute similar counts but Unicorn takes much longer

### Root Cause Analysis

#### 1. Execution Loop Overhead
The QEMU-style execution loop we implemented has significant overhead:
- Interrupt checking before every batch
- Small batch sizes (20 instructions for ROM code)
- Hook overhead (1230 hook calls in 5 seconds)

#### 2. Translation Block Management
- Each EmulOp forces a TB exit and restart
- Tight loops constantly invalidate and rebuild TBs
- No caching between interrupt checks

#### 3. CLKNOMEM Hot Loop
The CLKNOMEM loop at `0x0200b1e4-0x0200b1e6`:
- Executes EmulOp 0x7104
- Returns to check result
- Branches back if not ready
- This 2-instruction loop is pathological for our current approach

## Optimization Opportunities

### 1. Increase Batch Sizes
Current: 20 instructions for ROM code
Proposed: 100-500 for non-IRQ regions

### 2. Reduce Interrupt Check Frequency
Current: Before every batch
Proposed: Every N batches or on backward branches only

### 3. Cache Translation Blocks
Current: Invalidated frequently
Proposed: Keep TBs valid across interrupt checks

### 4. Special-case CLKNOMEM
Detect and optimize this specific pattern

## Next Steps

1. **Immediate**: Tune batch sizes in `unicorn_exec_loop.c`
2. **Short-term**: Implement smarter interrupt checking
3. **Medium-term**: Add TB caching mechanism
4. **Long-term**: Consider full QEMU integration for better performance

## Testing Commands

```bash
# Performance comparison
./scripts/analyze_performance_issue.sh

# Boot progress analysis
./scripts/analyze_boot_divergence.sh

# Detailed tracing
CPU_TRACE=0x0200b1e0-0x0200b1f0 CPU_BACKEND=unicorn ./build/macemu-next
```

## Conclusion
While the IRQ storm is fixed, the execution loop implementation needs optimization for production use. The 15x performance gap is primarily due to overhead in handling tight loops, not fundamental Unicorn limitations.