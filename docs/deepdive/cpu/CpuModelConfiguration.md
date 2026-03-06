# CPU Model Configuration for Dual-CPU Validation

## Problem

When running dual-CPU validation (UAE vs Unicorn), both CPUs must be configured to emulate the **same M68K CPU model**. Otherwise, they will have different instruction sets and control registers, causing false divergences.

### Initial State

- **UAE**: Defaulted to 68020+FPU (`cpu_level=3`)
  - Set via globals: `CPUType = CPU_68020; FPUType = FPU_68881;`
  - Determined in `build_cpufunctbl()` based on CPUType/FPUType

- **Unicorn**: Defaulted to ColdFire V4e
  - `uc_open()` without CPU model parameter defaults to `UC_CPU_M68K_CFV4E`
  - ColdFire is embedded/microcontroller variant with different registers

### Why This Mattered

The Quadra ROM (`~/quadra.rom`) expects a **68040** CPU and uses 68040-specific features:

1. **CACR (Cache Control Register)**: 68040 has this, ColdFire handles it differently
2. **Integrated FPU**: 68040 has built-in FPU, 68020 uses separate 68881/68882
3. **Instruction set**: 68040 has additional instructions (MOVE16, etc.)

**Symptom**: At instruction 7, the ROM executes `MOVEC CACR,D0` (read cache control register):
- **UAE (68020)**: Executed successfully (CACR exists in 68020+)
- **Unicorn (ColdFire)**: Raised `UC_ERR_EXCEPTION` (different control register layout)

## Solution

### 1. UAE CPU Configuration

Added `uae_set_cpu_type()` function to allow setting CPU model before initialization:

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

### 2. Unicorn CPU Configuration

Added `unicorn_create_with_model()` function to specify CPU model during creation:

**unicorn_wrapper.h**:
```c
UnicornCPU* unicorn_create_with_model(UnicornArch arch, int cpu_model);
/* M68K: UC_CPU_M68K_M68040 = 3, etc. */
```

**unicorn_wrapper.c**:
```c
UnicornCPU* unicorn_create_with_model(UnicornArch arch, int cpu_model) {
    // ... create engine ...

    if (cpu_model >= 0) {
        uc_ctl_set_cpu_model(cpu->uc, cpu_model);
    }

    return cpu;
}
```

### 3. Dual-CPU Harness Configuration

Modified `dualcpu_create()` to configure both CPUs for 68040:

**dualcpu.c**:
```c
DualCPU* dualcpu_create(void) {
    DualCPU *dcpu = calloc(1, sizeof(DualCPU));

    /* Configure both CPUs for 68040 */
    uae_set_cpu_type(4, 0);  /* CPU_68040, no separate FPU */

    /* Create Unicorn with 68040 model */
    #define UC_CPU_M68K_M68040 3
    dcpu->unicorn = unicorn_create_with_model(UCPU_ARCH_M68K, UC_CPU_M68K_M68040);

    /* Initialize UAE */
    uae_cpu_init();

    // ...
}
```

## Verification

### Before Fix
```
[6] BEFORE: PC=0x02004058 opcode=0x4E7B  UAE_SR=0x2704  UC_SR=0x2704
[7] BEFORE: PC=0x0200405C opcode=0x4E7A  UAE_SR=0x2704  UC_SR=0x2704

❌ CPU DIVERGENCE DETECTED!
Error: Unicorn execution failed: Unhandled CPU exception (UC_ERR_EXCEPTION)
```

Execution stopped at instruction 7 (MOVEC CACR,D0) with Unicorn exception.

### After Fix
```
DEBUG: cpu_level=4 (68040)
Filled 1868 opcodes from op_smalltbl (68040 instruction table)

[6] BEFORE: PC=0x02004058 opcode=0x4E7B  UAE_SR=0x2704  UC_SR=0x2704  ✅
[7] BEFORE: PC=0x0200405C opcode=0x4E7A  UAE_SR=0x2704  UC_SR=0x2704  ✅
[8] BEFORE: PC=0x02004060 opcode=0x0800  UAE_SR=0x2704  UC_SR=0x2704  ✅
[9] BEFORE: PC=0x02004064 opcode=0x6722  UAE_SR=0x2700  UC_SR=0x2700  ✅
```

Both CPUs successfully execute MOVEC CACR instruction and continue in lockstep!

## CPU Model Reference

### UAE CPUType Values
- `0` = 68000
- `1` = 68010
- `2` = 68020
- `3` = 68030
- `4` = 68040
- `5` = 68060

### Unicorn UC_CPU_M68K Values
```c
UC_CPU_M68K_M5206      = 0,
UC_CPU_M68K_M68000     = 1,
UC_CPU_M68K_M68020     = 2,
UC_CPU_M68K_M68030     = 3,  // This is actually M68040!
UC_CPU_M68K_M68040     = 3,  // Same as M68030 in Unicorn
UC_CPU_M68K_M68060     = 4,
UC_CPU_M68K_M5208      = 5,
UC_CPU_M68K_CFV4E      = 6,  // ColdFire (default if not specified)
// ... other ColdFire variants ...
```

**Note**: Unicorn's numbering is confusing - `UC_CPU_M68K_M68030 = 3` actually creates a 68040!

## Related Files

- `src/cpu/uae_wrapper.h` - UAE wrapper API
- `src/cpu/uae_wrapper.cpp` - UAE wrapper implementation
- `src/cpu/unicorn_wrapper.h` - Unicorn wrapper API
- `src/cpu/unicorn_wrapper.c` - Unicorn wrapper implementation
- `src/cpu/dualcpu.c` - Dual-CPU harness
- `src/cpu/uae_cpu/newcpu.cpp` - UAE CPU level selection in `build_cpufunctbl()`

## Future Work

For production use with BasiliskII/SheepShaver, CPU model should be:
1. Read from prefs file (`cpu 4` = 68040)
2. Passed to both CPU wrappers during initialization
3. Validated to ensure both CPUs match

Currently hardcoded to 68040 for dual-CPU boot testing.
