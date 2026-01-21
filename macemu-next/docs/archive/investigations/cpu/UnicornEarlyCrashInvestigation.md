# Unicorn Early Crash Investigation

## Problem Statement

When running Unicorn-only CPU mode, the emulator crashes at instruction #3,698 with `UC_ERR_WRITE_UNMAPPED`. This is much earlier than the dual-CPU divergence at instruction #514,126.

## Initial Hypothesis (INCORRECT)

Initially believed the crash was caused by a bug in Unicorn's ADDA.W instruction with predecrement addressing mode, where A7 was being set to 0x00000000 instead of the correct value.

## Root Cause Analysis

Through detailed trace comparison between UAE and Unicorn standalone modes, discovered the actual divergence occurs much earlier:

### Actual Divergence Point: Instruction #3690

**Instruction**: `CMPI.L #imm,D3` (opcode 0x0C83 at PC=0x02000A76)

**Condition Code Divergence**:
- **UAE** sets SR = 0x2709
  - CCR: X=0 N=1 Z=0 V=0 C=1
- **Unicorn** sets SR = 0x2704
  - CCR: X=0 N=0 Z=1 V=0 C=0

The two CPUs compute **completely different condition codes** for the same CMPI.L instruction!

### Cascade Effect: Instruction #3691

**Instruction**: `BGE.B +12` (opcode 0x6C0C at PC=0x02000A7C)

**Branch Decision**:
- BGE condition: N == V
- **UAE** with SR=0x2709: N=1, V=0 → N≠V → should NOT branch
  - But UAE **DOES** branch to 0x02000A8A (WRONG!)
- **Unicorn** with SR=0x2704: N=0, V=0 → N==V → should branch
  - But Unicorn **does NOT** branch, goes to 0x02000A7E (WRONG!)

### Result

After the divergence at #3691, the CPUs execute completely different code paths:

**UAE path**:
- #3692: PC=02000A8A OP=263C
- #3693: PC=02000A90 OP=C78F
- #3694: PC=02000A92 OP=DFD6 (ADDA.L -(A6),A7 → A7=0x00180000)

**Unicorn path**:
- #3692: PC=02000A7E OP=E48B
- #3693: PC=02000A80 OP=4C3C
- #3694: PC=02000A88 OP=6006
- #3695: PC=02000A90 OP=C78F (MULS.W A7,D3 → corrupts A7 to 0x00000000!)
- #3696: PC=02000A92 OP=DFD6 (tries to execute with A7=0)
- #3697: Crashes with UC_ERR_WRITE_UNMAPPED

## Bugs Identified

### Bug #1: CMPI.L Condition Code Bug
**Location**: Instruction #3690, opcode 0x0C83
**Symptom**: UAE and Unicorn set completely different condition codes for the same CMPI.L instruction
**Impact**: Critical - causes immediate divergence

### Bug #2: BGE Branch Logic Bug
**Location**: Instruction #3691, opcode 0x6C0C
**Symptom**: Both UAE and Unicorn make incorrect branch decisions based on their (already incorrect) condition codes
**Impact**: Critical - causes code path divergence

### Bug #3: MULS.W Register Corruption (Unicorn)
**Location**: Unicorn's divergent path at #3695, opcode 0xC78F
**Symptom**: `MULS.W A7,D3` somehow corrupts A7 to 0x00000000
**Impact**: Causes immediate crash in Unicorn-only mode

## Trace Comparison

### Instruction #3690: CMPI.L #imm,D3

```
       PC       Opcode  D0       D1       A0       A7       SR
UAE:   02000A76 0C83    0000773F 00000000 0200337C 0200010A 2709  ← WRONG SR
UC:    02000A76 0C83    0000773F 00000000 0200337C 0200010A 2704  ← WRONG SR
```

### Instruction #3691: BGE.B +12

```
       PC       Opcode  Next PC   SR   N V  Branch?
UAE:   02000A7C 6C0C    02000A8A  2709 1 0  YES (WRONG - should be NO)
UC:    02000A7C 6C0C    02000A7E  2709 1 0  NO (WRONG - should be YES with SR=2709, but SR should be 2704!)
```

Wait - both show SR=2709 at #3691, but Unicorn had SR=2704 at #3690. This means Unicorn's SR changed from 2704 to 2709 between instructions, which shouldn't happen!

### Divergent Paths

**UAE continues**:
```
#3692: PC=02000A8A OP=263C  (MOVE.L #imm,D3)
#3693: PC=02000A90 OP=C78F  (MULS.W A7,D3)
#3694: PC=02000A92 OP=DFD6  (ADDA.L -(A6),A7) → A7 becomes 0x00180000
```

**Unicorn continues**:
```
#3692: PC=02000A7E OP=E48B  (LSR.L #2,D3)
#3693: PC=02000A80 OP=4C3C  (MULU.L #imm,D3)
#3694: PC=02000A88 OP=6006  (BRA.B +6)
#3695: PC=02000A90 OP=C78F  (MULS.W A7,D3) → A7 becomes 0x00000000!
#3696: PC=02000A92 OP=DFD6  (ADDA.L -(A6),A7) → tries to write with A7=0
#3697: CRASH: UC_ERR_WRITE_UNMAPPED
```

## Next Steps

1. **Investigate CMPI implementation** in Unicorn's M68K translator
   - File: `macemu-next/external/unicorn/qemu/target/m68k/translate.c`
   - Search for CMPI opcode handler

2. **Investigate BGE implementation** in both UAE and Unicorn
   - Why does UAE branch when N≠V?
   - Why does Unicorn not branch when N==V (with SR=2709)?

3. **Investigate MULS.W** in Unicorn
   - Why does `MULS.W A7,D3` corrupt A7?
   - MULS should only modify D3, not A7!

4. **Improve logging** to make divergence obvious
   - Add side-by-side diff output
   - Highlight condition code mismatches
   - Show branch decisions clearly

## Test Files

- **Standalone test**: `macemu-next/tests/test_unicorn_adda_bug.c` (NOTE: Tests wrong bug - needs update)
- **Investigation**: This document

## Timeline

- Instruction #3690: CMPI.L sets wrong CCR (both CPUs wrong, differently)
- Instruction #3691: BGE branches incorrectly (both CPUs wrong)
- Instructions #3692-3694: CPUs execute different code paths
- Instruction #3695 (Unicorn only): MULS.W corrupts A7 to 0x00000000
- Instruction #3697 (Unicorn only): CRASH with UC_ERR_WRITE_UNMAPPED

## Improved Logging Results

With the new detailed logging showing condition codes, we can now see the **exact** divergence:

### Instruction #3689: MOVE.L (A6),D3 (OP=0x262E)
- **UAE**: D3=0x00000000 → D3=?? (next instruction shows D3=0x02000000), SR=2709 [N=1 Z=0 V=0 C=1]
- **Unicorn**: D3=0x00000000 → D3=0x00000000 (unchanged), SR=2700 [N=0 Z=0 V=0 C=0]

### Instruction #3690: CMPI.L #imm,D3 (OP=0x0C83)
- **UAE**: D3=0x02000000, SR becomes 2709 [N=1 Z=0 V=0 C=1]
- **Unicorn**: D3=0x00000000, SR becomes 2704 [N=0 Z=1 V=0 C=0]

The D3 values are different! This causes CMPI to compute different results.

### Instruction #3691: BGE.B +12 (OP=0x6C0C)
- **UAE**: N=1, V=0 → N≠V → branch should NOT be taken, but goes to 0x02000A8A (WRONG!)
- **Unicorn**: N=1, V=0 → N≠V → branch NOT taken, goes to 0x02000A7E (CORRECT!)

## Root Cause

The divergence starts at instruction #3689 with **MOVE.L (A6),D3**:
- UAE loads D3 from memory and gets a different value than Unicorn
- OR: UAE and Unicorn have different memory contents at (A6)
- OR: UAE and Unicorn have different A6 values

Need to trace back further to see where A6 diverges!

## Improved Logging

Added `cpu_trace_log_detailed()` function that:
- Shows CPU name (UAE vs Unicorn) for easy comparison
- Expands condition codes: [X=x N=n Z=z V=v C=c]
- Shows all D and A registers for full state visibility

Usage:
```bash
# Compare traces side-by-side
cd macemu-next
./scripts/compare_traces.sh 3688-3693 ~/quadra.rom
```

## Files Created

- `docs/unicorn_early_crash_investigation.md` - This document
- `scripts/compare_traces.sh` - Script to run and compare UAE vs Unicorn traces side-by-side
- `src/cpu/cpu_trace.c` - Enhanced with `cpu_trace_log_detailed()`
- `src/cpu/cpu_trace.h` - Added detailed logging function
- `src/cpu/uae_wrapper.cpp` - Updated to use detailed logging
- `src/cpu/cpu_unicorn.cpp` - Updated to use detailed logging
- `tests/test_unicorn_adda_bug.c` - Standalone test (NOTE: May be testing wrong bug)
