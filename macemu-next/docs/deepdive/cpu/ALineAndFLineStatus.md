# A-line/F-line Exception Handling - Status

## Current State (2026-01-21)

### ⚠️ BROKEN - Unicorn Fundamental Limitation

**CRITICAL**: A-line/F-line trap handling in Unicorn **DOES NOT WORK** due to a fundamental Unicorn engine limitation.

### The Problem

**Unicorn cannot change PC from interrupt hooks** - this is a known architectural issue (Unicorn GitHub issue #1027):

1. A-line (0xAxxx) or F-line (0xFxxx) instruction triggers `UC_ERR_EXCEPTION`
2. Unicorn calls `UC_HOOK_INTR` callback
3. Our code builds exception stack frame correctly
4. Our code sets PC to exception handler address
5. **Unicorn ignores the PC change** and overwrites it with `exception_next_eip`
6. Execution continues from wrong address → **HANG**

This affects:
- All non-EmulOp A-line traps (e.g., `0xA05D`, `0xA247`)
- ROM boot sequence after PATCH_BOOT_GLOBS
- Any Mac OS trap execution

### What We Tried

From commits `9464afa4` and `32a6926b`:
1. ✗ `uc_ctl_remove_cache()` + `uc_reg_write()` - Unicorn still overwrites PC
2. ✗ `uc_emu_stop()` to break execution - causes other issues
3. ✗ Skipping instruction - prevents infinite loop but doesn't execute handler

All approaches **fail** because Unicorn's interrupt handling intentionally overwrites PC after hooks return.

### Code Status

- ⚠️ **Exception simulation code exists but doesn't work** ([unicorn_wrapper.c](../src/cpu/unicorn_wrapper.c)):
  - Stack frame construction implemented correctly
  - Supervisor mode switching works
  - Vector table lookup works
  - **PC change is ignored by Unicorn** ❌

- ✅ **A-line EmulOps (0xAE00-0xAE3F) work** - These use UC_HOOK_INTR differently and don't require PC changes

- ⚠️ **DualCPU validation workaround** - Execute A-line/F-line on UAE only, sync state to Unicorn

**Evidence**:
```
[23250] UNICORN EXECUTION FAILED
PC: 0x02003E08, Opcode: 0xA247 (A-line trap - SetToolTrap)
Error: Unhandled CPU exception (UC_ERR_EXCEPTION)
```

**Root Cause**: Unicorn treats 0xAxxx and 0xFxxx as architecturally valid M68K instructions that generate exceptions, not as "invalid" opcodes. The hook mechanism is designed for truly invalid/undefined instructions.

**Current Workaround**: Execute A-line/F-line on UAE only, sync state to Unicorn (same as EmulOps).

## Current Workarounds

### A-line EmulOps (0xAE00-0xAE3F)
**Status**: ✅ **WORKING** in Unicorn standalone mode

These are BasiliskII-specific A-line instructions that:
- Use `UC_HOOK_INTR` to detect the instruction
- Don't require PC changes (just parameter extraction)
- Enable core emulation features

See [unicorn_wrapper.c](../src/cpu/unicorn_wrapper.c) `hook_interrupt()` function.

### DualCPU Validation
**Status**: ⚠️ **Workaround only** - not true dual-CPU execution

Execute A-line/F-line on UAE only, sync state to Unicorn:
- UAE executes the trap fully
- Full register state copied to Unicorn
- RAM synced after execution
- Unicorn continues from post-trap state

This works for validation but defeats the purpose of independent CPU comparison.

## Possible Solutions

### Solution 1: Patch Unicorn Source
Modify Unicorn to not overwrite PC after interrupt hooks:
- Fork Unicorn repository
- Remove `exception_next_eip` overwrite in interrupt handling
- Build custom Unicorn version

**Pros**: Would fix the issue completely
**Cons**: Maintenance burden, breaks Unicorn updates, architectural change

### Solution 2: Different Emulation Approach
Use a different hook mechanism:
- Catch exception before Unicorn processes it
- Handle A-line/F-line in outer execution loop
- Never let Unicorn see the exception

**Status**: All attempts failed (see "What We Tried" above)

### Solution 3: Switch CPU Emulation Engine
Options:
- Musashi (pure interpreter, slower but predictable)
- QEMU directly (heavyweight, complex integration)
- Write custom M68K emulator (huge effort)

### Solution 4: Accept Limitation
**Current approach**: Don't try to make Unicorn handle A-line/F-line exceptions natively.

**For standalone Unicorn**:
- Only A-line EmulOps (0xAE00-0xAE3F) work
- Other A-line traps cause hangs
- Limits what ROM code can execute

**For DualCPU validation**:
- Continue UAE-only execution workaround
- Accept that we can't validate exception handling
- Focus validation on normal instruction execution

## Impact on Project Goals

### What Still Works ✅
- Normal M68K instruction execution (UAE and Unicorn both work)
- A-line EmulOps (0xAE00-0xAE3F) in Unicorn
- Interrupt handling via UC_HOOK_BLOCK
- EmulOps (0x71xx illegal instructions)
- RTE instruction (return from exception)

### What Doesn't Work ❌
- Mac OS A-line traps (0xA000-0xAFFF except EmulOps)
- Mac OS F-line traps (0xF000-0xFFFF)
- Full ROM boot on Unicorn standalone
- True dual-CPU exception handling validation

### Recommended Path Forward

1. **Continue using UAE as primary backend** for full Mac emulation
2. **Use Unicorn for validation only** with UAE-execute workaround for traps
3. **Document the limitation** clearly (this document!)
4. **Consider Unicorn patch** if project needs full Unicorn standalone mode

The core project goal (dual-CPU validation of normal instructions) is **not significantly impacted**.

## Code Structure

```
src/cpu/
├── unicorn_exception.c     # Exception simulation (ready but unused)
├── unicorn_exception.h     # Header with extern "C" guards
├── unicorn_wrapper.c       # Hook infrastructure (detection works, invocation doesn't)
├── unicorn_wrapper.h       # ExceptionHandler API
└── unicorn_validation.cpp  # DualCPU workaround (UAE-only execution + sync)
```

## Testing

Run DualCPU validation:
```bash
cd macemu-next
EMULATOR_TIMEOUT=2 CPU_BACKEND=dualcpu ./build/macemu-next ~/quadra.rom
```

Expected output:
```
=== DualCPU Divergence ===
Instructions executed: 23275
```

Check validation log:
```bash
tail cpu_validation.log
```

Expected divergence:
```
[23275] D1 DIVERGENCE at 0x02009A7C (opcode 0x4E7A)
UAE D1: 0x000091C0 → 0x00000001
UC  D1: 0x000091C0 → 0x00000000
```
