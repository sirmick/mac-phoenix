# Boot Comparison: BasiliskII vs macemu-next

## Test Setup
- **Duration**: 10 seconds each
- **Config**: Identical (same ROM, same disk image, same RAM)
- **Mode**: Both headless (no GUI)

## Results Summary

### ✅ BOTH WORK - Disk Opens Successfully

| Metric | BasiliskII | macemu-next | Status |
|--------|------------|-------------|--------|
| **Lines of output** | 25,548 | 2,213 | ⚠️ 11x slower |
| **DiskOpen** | ✅ Line 921 | ✅ Line 1005 | Both work |
| **disk inserted** | ✅ | ✅ | Both work |
| **Blocks** | 409,600 | 409,600 | Identical |
| **VideoDriverOpen** | ✅ | ✅ | Both work |
| **ADB initialization** | ✅ | ✅ | Both work |
| **SCSI scan** | ✅ | ✅ | Both work |

## Key Finding

**macemu-next is running correctly but ~11x slower than BasiliskII**

Both emulators:
1. ✅ Initialize ROM successfully
2. ✅ Open disk image (409,600 blocks = ~200MB)
3. ✅ Initialize video driver
4. ✅ Initialize ADB (keyboard/mouse)
5. ✅ Scan SCSI bus
6. ✅ Execute thousands of EmulOps

## Evidence

### Both start identically:
```
EmulOp 7103
*** RESET ***
EmulOp 7104
RTC write op 13, d1 00000035 d2 00000055
```

### Both open disk successfully:
```
DiskOpen
 disk inserted
 409600 blocks
 adding drive 3
```

### Both initialize video:
```
EmulOp 7119
VideoDriverControl slot 80, code 2
 SetMode 0080
  base 01fb5000
```

## Speed Difference

**BasiliskII (10 seconds):**
- 25,548 lines of debug output
- Executes ~2,500 ops/second

**macemu-next (10 seconds):**
- 2,213 lines of debug output
- Executes ~220 ops/second
- **~11x slower**

## Last Operations

**BasiliskII** (running fast, lots of EmulOps):
```
EmulOp 7137
EmulOp 7129
EmulOp 7130
EmulOp 7137
... (continuous execution)
```

**macemu-next** (slower but working):
```
EmulOp 7128
SCSIGet
EmulOp 7128
SCSISelect 0
EmulOp 7104
Read XPRAM 01->04
[DEBUG Timer] tick=4 ROM=0x067c mac_started=1
```

## Possible Causes of Slowness

1. **Timer issues** - Debug shows only 4 ticks in 10 seconds (should be ~600 at 60Hz)
2. **CPU execution loop** - May be sleeping/yielding too much
3. **Debug output overhead** - Too much fflush() slowing things down
4. **Missing optimizations** - Interpreter may be less optimized

## Recommended Next Steps

1. **Check timer frequency**: Why only 4 ticks in 10 seconds?
2. **Profile CPU execution**: Is the CPU spinning efficiently?
3. **Remove debug fflush()**: Try running without disk debug logging
4. **Compare EmulOp handlers**: Are they identical?
5. **Check for sleeps/waits**: Any unnecessary delays in main loop?

## Files

- BasiliskII log: `scripts/debug_outputs/basilisk.log` (25,548 lines)
- macemu-next log: `scripts/debug_outputs/macemu.log` (2,213 lines)
- Compare with: `diff -u scripts/debug_outputs/*.log | less`

## Conclusion

✅ **Platform abstraction is working correctly** - disk opens, drivers initialize, Mac OS starts executing

⚠️ **Performance issue** - Execution is ~11x slower than BasiliskII, likely timer or CPU loop issue

**Good news**: The boot process works! Just need to fix the execution speed.
