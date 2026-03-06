# ROM Patching Required for Dual-CPU Boot Testing

## Problem Discovered

The dual-CPU boot test (`test_boot_dualcpu`) crashes after 9 instructions with a segmentation fault:

```
Program received signal SIGSEGV, Segmentation fault.
0x00005555558e82f9 in get_byte (addr=1358961664) at memory.h:148
```

Address 0x51001C00 (1358961664 decimal) is in Mac I/O space (0x50000000-0x60000000).

## Root Cause

The test executes **raw, unpatched ROM** which tries to access real Macintosh hardware:
- VIA1/VIA2 (versatile interface adapters) at 0x50F00000/0x50F20000
- SCSI controller
- Sound hardware
- Other I/O devices

Since we're running in software emulation without hardware access, these memory accesses crash.

## How BasiliskII Solves This

BasiliskII uses **ROM patching** to replace hardware access with emulation:

1. **CheckROM()**: Identifies ROM version and validates it
2. **PatchROM()**: Replaces Mac OS ROM routines with EMUL_OP instructions
3. **EMUL_OP Instructions**: Illegal opcodes (0x7100-0x71FF) that trap to C++ handlers
4. **Hardware Emulation**: C++ code emulates VIA, SCSI, sound, etc.

### Instruction Flow

**Without patching**:
```
ROM: TST.B (0x51001C00)  → Access VIA1 register → SEGFAULT (no hardware)
```

**With patching**:
```
ROM: 0x7100              → EMUL_OP trap
     → EmulOp() handler in C++
     → Emulated VIA read
     → Return to ROM
```

## Current Test Architecture

### test_boot.cpp (Works)
```
InitAll()
  ├─ CheckROM()
  ├─ Init680x0()  (UAE CPU)
  └─ PatchROM()   (Replace hardware access with EMUL_OP)

Execute ROM with patched UAE
```

### test_boot_dualcpu.cpp (Crashes)
```
dualcpu_create()
  ├─ UAE CPU init
  └─ Unicorn CPU init

Load raw ROM (NO PATCHING!)

Execute ROM → crashes at hardware access
```

## Why We Can't Just Call InitAll()

The problem is architectural:

1. **InitAll() calls Init680x0()** which initializes UAE's CPU
2. **dualcpu harness also initializes UAE** via `uae_cpu_init()`
3. **Double initialization conflict**

Additionally:
- InitAll() sets up UAE-specific memory layout
- dualcpu harness uses different memory management (contiguous buffer for DIRECT_ADDRESSING)
- Memory layout incompatibility

## Solutions Considered

### Option 1: Map Dummy I/O Space ❌
**Tried**: Allocate 1.5GB memory buffer to cover I/O space
**Problem**: ROM still needs proper hardware responses, not just zero bytes

### Option 2: Call Just PatchROM() ❌
**Problem**: PatchROM() depends on:
- CheckROM() identifying ROM version
- Memory system being initialized
- Various subsystems (XPRAM, timers, etc.)
- UAE CPU being initialized

### Option 3: Hybrid Architecture ✅ (Recommended)
Modify test_boot_dualcpu to:

```cpp
// 1. Use InitAll() for complete UAE setup (including ROM patching)
InitAll(NULL);

// 2. Create Unicorn CPU separately
UnicornCPU *unicorn = unicorn_create_with_model(UCPU_ARCH_M68K, UC_CPU_M68K_M68040);

// 3. Sync ROM from UAE to Unicorn
unicorn_map_rom(unicorn, ROMBaseMac, ROMBaseHost, ROMSize);

// 4. Execute with validation loop
while (...) {
    // Capture UAE state
    CPUStateSnapshot uae_before = capture_uae_state();

    // Execute on UAE
    uae_cpu_execute_one();

    // Sync UAE memory changes to Unicorn
    sync_memory_uae_to_unicorn();

    // Execute on Unicorn
    unicorn_execute_one(unicorn);

    // Compare states
    if (!states_match(uae_before, unicorn_before)) {
        report_divergence();
    }
}
```

## Implementation Plan

1. **Refactor test_boot_dualcpu.cpp**:
   - Include all BasiliskII headers (like test_boot.cpp)
   - Call `PrefsInit()`, set RAM size, CPU type
   - Call `InitAll()` for complete UAE setup
   - Create Unicorn separately
   - Map UAE's memory to Unicorn

2. **Sync Memory**:
   - After each UAE instruction, copy modified memory to Unicorn
   - OR: Use memory hooks to track writes
   - OR: Just sync entire RAM (slow but simple)

3. **Handle EMUL_OP**:
   - UAE will execute EMUL_OP handlers
   - Unicorn will see 0x71xx as illegal instruction
   - Need to skip/stub EMUL_OP on Unicorn side

## Why This Matters

Without ROM patching, the dual-CPU validation can only test:
- Simple instruction execution
- ALU operations
- Branches
- Basic memory access

With ROM patching, we can test:
- Complete ROM boot sequence
- OS initialization
- Trap handling
- Real-world Mac OS code paths

This is **essential** for validating that UAE and Unicorn are truly compatible for BasiliskII use.

## Next Steps

1. Create new `test_boot_dualcpu_v2.cpp` with hybrid architecture
2. Test that it boots further than instruction 9
3. Handle EMUL_OP divergences
4. Document findings

## References

- `src/core/main.cpp:InitAll()` - Main initialization
- `src/core/rom_patches.cpp:PatchROM()` - ROM patching implementation
- `src/core/emul_op.cpp` - EMUL_OP trap handlers
- `tests/boot/test_boot.cpp` - Working boot test with patching
