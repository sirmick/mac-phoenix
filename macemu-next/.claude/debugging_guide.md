# macemu-next Debugging Guide

## Quick Reference Card

### 🎯 Most Common Debug Commands

```bash
# ALWAYS START HERE
cd /home/mick/macemu-dual-cpu/macemu-next

# Quick test of a backend (2 seconds)
EMULATOR_TIMEOUT=2 CPU_BACKEND=uae ./build/macemu-next --no-webserver
EMULATOR_TIMEOUT=2 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver

# Compare with BasiliskII (primary debugging tool)
./scripts/compare_boot.sh

# Full instruction traces
./scripts/run_traces.sh
```

## Debugging Workflows

### 1. "Why isn't it booting?" Workflow

```bash
# Step 1: Compare with BasiliskII
./scripts/compare_boot.sh

# Step 2: Check EmulOp counts
grep -c EmulOp scripts/debug_outputs/basilisk.log    # Should be ~24000
grep -c EmulOp scripts/debug_outputs/macemu.log      # If much less, there's a problem

# Step 3: Find where they diverge
diff scripts/debug_outputs/basilisk.log scripts/debug_outputs/macemu.log | head -100

# Step 4: Trace the problem area
# If stuck at instruction 2, trace first 10:
EMULATOR_TIMEOUT=1 CPU_BACKEND=uae CPU_TRACE=0-10 ./build/macemu-next --no-webserver 2>&1 | grep "^\[0"
```

### 2. "UAE vs Unicorn Differences" Workflow

```bash
# Step 1: Generate traces for both
./scripts/run_traces.sh

# Step 2: Look at trace files
ls -la /tmp/macemu_traces_*/
# Files created:
# - uae_trace.txt: Just the instruction traces
# - unicorn_trace.txt: Just the instruction traces
# - uae_full.log: Complete output with all messages
# - unicorn_full.log: Complete output

# Step 3: Find first divergence
head -20 /tmp/macemu_traces_*/uae_trace.txt > /tmp/uae_first.txt
head -20 /tmp/macemu_traces_*/unicorn_trace.txt > /tmp/unicorn_first.txt
diff /tmp/uae_first.txt /tmp/unicorn_first.txt

# Step 4: Check register states at divergence
# Look for SR register differences (interrupt mask issues are common)
```

### 3. "EmulOp Not Working" Workflow

```bash
# Step 1: See which EmulOps are being called
EMULATOR_TIMEOUT=2 CPU_BACKEND=uae EMULOP_VERBOSE=1 ./build/macemu-next --no-webserver 2>&1 | grep EmulOp | head -20

# Step 2: Trace specific EmulOp (e.g., 0x7103 = RESET)
EMULATOR_TIMEOUT=1 CPU_BACKEND=uae EMULOP_VERBOSE=1 ./build/macemu-next --no-webserver 2>&1 | grep "7103"

# Step 3: Check register state before/after EmulOp
EMULATOR_TIMEOUT=1 CPU_BACKEND=uae CPU_TRACE=0-5 ./build/macemu-next --no-webserver 2>&1 | grep -E "^\[|EmulOp"

# Step 4: Compare with BasiliskII behavior
grep "EmulOp 7103" scripts/debug_outputs/basilisk.log
grep "EmulOp 7103" scripts/debug_outputs/macemu.log
```

### 4. "Stuck in Loop" Workflow

```bash
# Step 1: Quick check - are we executing millions of the same instruction?
EMULATOR_TIMEOUT=2 CPU_BACKEND=uae CPU_TRACE=0-100 ./build/macemu-next --no-webserver 2>&1 | grep "^\[0" | uniq -c

# Step 2: If seeing repeated PC, check what's there
# Example: stuck at 0x0200008C
EMULATOR_TIMEOUT=1 CPU_BACKEND=uae CPU_TRACE=0-20 ./build/macemu-next --no-webserver 2>&1 | grep "0200008C"

# Step 3: Check interrupt mask (SR register)
# SR=0x2700 means interrupts blocked (bad)
# SR=0x2000 means interrupts enabled (good)
EMULATOR_TIMEOUT=1 CPU_BACKEND=uae CPU_TRACE=0-10 ./build/macemu-next --no-webserver 2>&1 | grep "^\[0" | head -5
# Look at the SR column (second to last)
```

## Understanding Output Formats

### Instruction Trace Format
```
[00001] PC______ OPCD | D0______ D1______ D2______ D3______ D4______ D5______ D6______ D7______ | A0______ A1______ A2______ A3______ A4______ A5______ A6______ A7______ | SR__ FLAGS
```

Example:
```
[00001] 0200008C 7103 | 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 | 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00002000 | 2700 00000
```
- `[00001]` = Instruction number
- `0200008C` = Program Counter (PC)
- `7103` = Opcode (instruction)
- Next 8 values = Data registers D0-D7
- Next 8 values = Address registers A0-A7
- `2700` = Status Register (SR)
- `00000` = CPU flags

### Status Register (SR) Decoding
- `0x2700` = Supervisor mode, interrupt mask 7 (all interrupts blocked) ❌
- `0x2000` = Supervisor mode, interrupt mask 0 (interrupts enabled) ✅
- `0x2704` = Like 0x2700 but with some flags set

### Common EmulOps
- `0x7103` = M68K_EMUL_OP_RESET (Initialize Mac OS)
- `0x7129` = M68K_EMUL_OP_IRQ (Timer interrupt)
- `0x7104` = M68K_EMUL_OP_CLKNOMEM (Clock/memory setup)

## Troubleshooting

### "Command not found"
```bash
# You're probably in the wrong directory
pwd
cd /home/mick/macemu-dual-cpu/macemu-next
```

### "No such file or directory"
```bash
# Binary might not be built
ls -la build/macemu-next
meson compile -C build
```

### "Launching web server variant"
```bash
# You forgot --no-webserver flag
# WRONG: ./build/macemu-next
# RIGHT: ./build/macemu-next --no-webserver
```

### "Timeout: command not found"
```bash
# Use environment variable, not timeout command
# WRONG: timeout 2 ./build/macemu-next
# RIGHT: EMULATOR_TIMEOUT=2 ./build/macemu-next --no-webserver
```

### Crash with no source info
```bash
# Need debug symbols
meson configure build -Dbuildtype=debug
meson compile -C build
```

## Tips & Tricks

1. **Always use full paths in grep patterns**
   ```bash
   # Bad: might miss the trace lines
   grep "7103" output.log

   # Good: gets instruction traces
   grep "^\[0" output.log | grep "7103"
   ```

2. **Save intermediate results**
   ```bash
   # Instead of long pipelines, save steps
   EMULATOR_TIMEOUT=2 CPU_BACKEND=uae ./build/macemu-next --no-webserver 2>&1 > /tmp/uae_run.log
   grep EmulOp /tmp/uae_run.log > /tmp/uae_emulops.log
   wc -l /tmp/uae_emulops.log
   ```

3. **Use diff effectively**
   ```bash
   # Side-by-side diff for traces
   diff -y --width=200 /tmp/uae.trace /tmp/unicorn.trace | less

   # Show only differences
   diff --suppress-common-lines /tmp/uae.trace /tmp/unicorn.trace
   ```

4. **Quick iteration during debugging**
   ```bash
   # Set up aliases for common commands
   alias test_uae='EMULATOR_TIMEOUT=1 CPU_BACKEND=uae ./build/macemu-next --no-webserver'
   alias test_uni='EMULATOR_TIMEOUT=1 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver'
   alias trace_uae='EMULATOR_TIMEOUT=1 CPU_BACKEND=uae CPU_TRACE=0-20 ./build/macemu-next --no-webserver 2>&1 | grep "^\[0"'
   ```

5. **Watch for patterns**
   - Stuck at same PC = infinite loop (check SR for interrupt mask)
   - EmulOp 7129 repeating = normal timer interrupts
   - SR never changes from 0x2700 = interrupts blocked
   - Execution in 0xFF000000 range = trap region (debugging artifact)