# CPU Trace Debugging Tools

## Overview

This document describes the enhanced CPU tracing and comparison tools for debugging divergences between UAE and Unicorn CPU implementations.

## Features

### 1. Standardized Trace Format

Both UAE and Unicorn now output traces in an identical, easily-diffable format:

```
[NNNNN] PC OPCODE | D0-D7 | A0-A7 | SR FLAGS
```

Example:
```
[03689] 02000A72 262E | 0000773F 00000000 CC000505 00000000 ... | 0200337C ... | 2709 01001
```

Where:
- `NNNNN` = instruction count (5 digits, zero-padded)
- `PC` = program counter (8 hex digits)
- `OPCODE` = instruction opcode (4 hex digits)
- `D0-D7` = data registers (8×8 hex digits)
- `A0-A7` = address registers (8×8 hex digits)
- `SR` = status register (4 hex digits)
- `FLAGS` = condition codes XNZVC (5 binary digits: X=eXtend, N=negative, Z=zero, V=overflow, C=carry)

### 2. Memory Read Tracing

When `CPU_TRACE_MEMORY=1` is set, every memory read is logged:

```
[03689] 02000A72 262E | ...registers...
  MEM[01FFFFE8]=02000000 (L)
```

Where:
- Address is shown in hex
- Value is shown in hex (properly byte-swapped for M68K big-endian)
- Size is shown as B (byte), W (word), or L (long)

### 3. Automated Divergence Detection

The `diff_cpus.sh` script:
- Runs both UAE and Unicorn with the same trace range
- Compares outputs line-by-line
- Reports the FIRST divergence with context
- Analyzes whether it's a PC/opcode divergence or register state divergence

## Usage

### Basic Trace Comparison

```bash
cd macemu-next
./scripts/diff_cpus.sh 3680-3700 ~/quadra.rom
```

Output:
```
=== CPU Trace Comparison ===
Range: 3680-3700
ROM: /home/mick/quadra.rom

Running UAE...
Running Unicorn...
Done.

Instructions logged:
  UAE:     20
  Unicorn: 20

=== Finding First Divergence ===

First divergence at line 2:
UAE:     [03682] 02000108 4E75 | ...
Unicorn: [03682] 02000108 4E75 | ...

Instruction #03682
ℹ️  Same PC and opcode, but different register state
```

### With Memory Tracing

```bash
CPU_TRACE_MEMORY=1 ./scripts/diff_cpus.sh 3689-3690 ~/quadra.rom
```

This will show memory reads inline with each instruction, making it easy to see what values were loaded from RAM.

### Manual Trace Collection

For more control, run each CPU separately:

```bash
# UAE trace
CPU_TRACE=3680-3700 CPU_TRACE_QUIET=1 CPU_BACKEND=uae \
  ./build/macemu-next ~/quadra.rom 2>&1 | grep '^\[' > uae_trace.txt

# Unicorn trace
CPU_TRACE=3680-3700 CPU_TRACE_QUIET=1 CPU_BACKEND=unicorn \
  ./build/macemu-next ~/quadra.rom 2>&1 | grep '^\[' > unicorn_trace.txt

# Compare
diff -u uae_trace.txt unicorn_trace.txt
```

## Environment Variables

### CPU_TRACE
**Format**: `START-END` or `N`
**Example**: `CPU_TRACE=3680-3700` or `CPU_TRACE=100`
**Description**: Enable CPU tracing for specified instruction range

### CPU_TRACE_QUIET
**Format**: `1` or unset
**Example**: `CPU_TRACE_QUIET=1`
**Description**: Suppress banner messages (only output trace lines)

### CPU_TRACE_MEMORY
**Format**: `1` or unset
**Example**: `CPU_TRACE_MEMORY=1`
**Description**: Log all memory reads with addresses and values

### CPU_BACKEND
**Format**: `uae`, `unicorn`, or `dualcpu`
**Example**: `CPU_BACKEND=uae`
**Description**: Select which CPU implementation to use

### EMULATOR_TIMEOUT
**Format**: seconds
**Example**: `EMULATOR_TIMEOUT=5`
**Description**: Auto-exit after N seconds (prevents infinite loops)

## Implementation Details

### UAE Memory Tracing
Memory tracing is implemented in `src/cpu/uae_cpu/memory.h` by hooking the `get_long()`, `get_word()`, and `get_byte()` functions.

### Unicorn Memory Tracing
Memory tracing is implemented using Unicorn's `UC_HOOK_MEM_READ` hook, registered in `src/cpu/unicorn_wrapper.c`.

### Trace Output
All trace output goes to stderr, making it easy to separate from other emulator output.

## Files Modified

- `src/cpu/cpu_trace.c` - Core tracing infrastructure
  - Added `CPU_TRACE_QUIET` support
  - Added `CPU_TRACE_MEMORY` support
  - Standardized output format
  - Added `cpu_trace_log_mem_read()` function

- `src/cpu/cpu_trace.h` - Public API
  - Added memory tracing functions
  - Updated trace state structure

- `src/cpu/uae_cpu/memory.h` - UAE memory hooks
  - Added tracing to `get_long()`, `get_word()`, `get_byte()`

- `src/cpu/unicorn_wrapper.c` - Unicorn memory hooks
  - Added `hook_mem_trace()` callback
  - Registers `UC_HOOK_MEM_READ` when tracing enabled

- `src/cpu/uae_wrapper.cpp` - UAE trace integration
  - Updated to use `cpu_trace_log_detailed()`

- `src/cpu/cpu_unicorn.cpp` - Unicorn trace integration
  - Updated to use `cpu_trace_log_detailed()`

- `scripts/diff_cpus.sh` - Automated comparison tool
  - Runs both CPUs with same parameters
  - Finds and reports first divergence
  - Shows context around divergence

## Example Output

### Without Memory Tracing
```
[03689] 02000A72 262E | 0000773F 00000000 CC000505 00000000 00000000 00000000 00000000 00050003 | 0200337C 0200374C 00000000 00000000 020000F6 00000000 01FFFFE4 0200010A | 2709 01001
```

### With Memory Tracing
```
[03689] 02000A72 262E | 0000773F 00000000 CC000505 00000000 00000000 00000000 00000000 00050003 | 0200337C 0200374C 00000000 00000000 020000F6 00000000 01FFFFE4 0200010A | 2709 01001
  MEM[01FFFFE8]=02000000 (L)
```

This shows that instruction #3689 (opcode 0x262E = MOVE.L (A6),D3) read the value 0x02000000 from address 0x01FFFFE8.

## Troubleshooting

### "No divergence found" but emulators behave differently
- Extend the trace range - divergence may be earlier
- Check if one CPU crashed before reaching the trace range
- Verify both CPUs are starting from the same state

### Memory trace shows too many reads
- Narrow the instruction range
- Filter output with grep: `grep -A1 '^\[0369[0-9]'`

### Traces are different lengths
- One CPU likely crashed or hit an infinite loop
- Check the last instruction in the shorter trace
- Use `EMULATOR_TIMEOUT` to prevent hangs

## Next Steps

With these tools, you can:
1. Quickly find the exact instruction where CPUs diverge
2. See what values were read from memory
3. Analyze why the divergence occurred
4. Test fixes by running the same trace again

The standardized format makes it easy to:
- Use standard Unix tools (`diff`, `grep`, `awk`)
- Write custom analysis scripts
- Share traces with others for debugging
