# CPU Model Configuration

## Why the CPU model matters

The Quadra ROM (`~/quadra.rom`) expects a **68040** CPU and uses 68040-specific features:

1. **CACR (Cache Control Register)**: read via `MOVEC CACR,D0` within the first few ROM instructions
2. **Integrated FPU**: 68040 has a built-in FPU, 68020 uses a separate 68881/68882
3. **Instruction set**: 68040 has additional instructions (MOVE16, etc.)

UAE originally defaulted to 68020+FPU (`cpu_level=3`), set via the globals
`CPUType = CPU_68020; FPUType = FPU_68881;` and resolved in `build_cpufunctbl()`.
The ROM's early `MOVEC CACR,D0` executed on a 68020 configuration, but later
68040-only code did not, so the CPU level has to match the ROM.

## UAE CPU Configuration

`uae_set_cpu_type()` sets the CPU model before initialization:

**uae_wrapper.h**:
```c
/* CPU configuration - must be called before uae_cpu_init() */
void uae_set_cpu_type(int cpu_type, int fpu_type);
/* cpu_type: 2=68020, 4=68040; fpu_type: 0=none, 1=68881 */
```

**uae_wrapper.cpp**:
```cpp
void uae_set_cpu_type(int cpu_type, int fpu_type) {
    CPUType = cpu_type;
    FPUType = fpu_type;
}
```

With a 68040 configuration the startup log shows:

```
DEBUG: cpu_level=4 (68040)
Filled 1868 opcodes from op_smalltbl (68040 instruction table)
```

## CPU Model Reference

### UAE CPUType Values
- `0` = 68000
- `1` = 68010
- `2` = 68020
- `3` = 68030
- `4` = 68040
- `5` = 68060

## Related Files

- `src/cpu/uae_wrapper.h` - UAE wrapper API
- `src/cpu/uae_wrapper.cpp` - UAE wrapper implementation
- `src/cpu/uae_cpu/newcpu.cpp` - UAE CPU level selection in `build_cpufunctbl()`

## Current State

CPU model is now determined by **machine profiles** (`src/config/machine_profile.cpp`), auto-detected from the ROM version at startup:
- Mac SE ROM (0x0276) → 68000
- Mac II ROM (0x0178) → 68020
- Quadra 650 ROM (0x067c) → 68040

The `cpu_type` and `model_id` config fields have been removed — the machine profile is the single source of truth.
