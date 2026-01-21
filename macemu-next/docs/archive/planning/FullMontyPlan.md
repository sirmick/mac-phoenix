# The Full Monty: Dual-CPU Validation in BasiliskII

## Goal

Integrate Unicorn CPU alongside UAE in the real BasiliskII emulator to validate instruction-by-instruction compatibility during actual ROM boot and OS execution.

## Architecture

```
BasiliskII Main Loop (main.cpp)
  │
  ├─ InitAll()
  │   ├─ Init680x0()  (UAE)
  │   ├─ PatchROM()
  │   └─ ... (all subsystems)
  │
  ├─ [NEW] InitUnicornValidation()
  │   ├─ Create Unicorn CPU (68040)
  │   ├─ Map ROM to Unicorn
  │   ├─ Sync initial RAM state
  │   └─ Set up divergence logging
  │
  └─ Start680x0()
      │
      └─ m68k_execute() [MODIFIED]
          │
          For each instruction:
            ├─ Capture UAE state BEFORE
            ├─ Execute on UAE
            ├─ Capture UAE state AFTER
            │
            ├─ [NEW] Sync memory changes to Unicorn
            ├─ [NEW] Execute on Unicorn
            ├─ [NEW] Compare states
            │
            └─ [NEW] If divergence:
                ├─ Log details
                ├─ Optionally break/continue
                └─ Save state for debugging
```

## Implementation Steps

### 1. Add Unicorn Validation Module

**File**: `src/cpu/unicorn_validation.cpp/h`

```cpp
class UnicornValidator {
private:
    UnicornCPU *unicorn;
    bool enabled;
    FILE *log_file;
    uint64_t instruction_count;
    uint64_t divergence_count;

public:
    void init();  // Create Unicorn, map ROM
    void sync_memory(uint32_t addr, uint32_t size);  // Sync RAM changes
    bool validate_instruction();  // Execute and compare
    void report_divergence();
    void shutdown();
};
```

### 2. Modify UAE Execution Loop

**File**: `src/cpu/uae_cpu/newcpu.cpp` or wrapper

**Current**:
```cpp
void m68k_execute(void) {
    while (!quit) {
        m68k_do_execute();  // Execute one instruction
    }
}
```

**Modified**:
```cpp
void m68k_execute(void) {
    while (!quit) {
        #ifdef DUAL_CPU_VALIDATION
        if (unicorn_validator.is_enabled()) {
            unicorn_validator.validate_instruction();
        } else
        #endif
        {
            m68k_do_execute();
        }
    }
}
```

### 3. Handle EMUL_OP Divergences

**Problem**:
- UAE executes EMUL_OP (0x71xx) → calls C++ handler → modifies state
- Unicorn sees 0x71xx → illegal instruction exception

**Solution**: Skip EMUL_OP on Unicorn
```cpp
bool validate_instruction() {
    uint16_t opcode = read_word(uae_get_pc());

    if ((opcode & 0xFF00) == 0x7100) {
        // EMUL_OP - execute on UAE only
        m68k_do_execute();

        // Sync Unicorn state to match UAE result
        sync_full_state_to_unicorn();

        // Skip this instruction on Unicorn
        unicorn_set_pc(unicorn, uae_get_pc());
        return true;  // No divergence
    }

    // Normal instruction - execute on both
    return execute_and_compare();
}
```

### 4. Memory Synchronization

**Challenge**: UAE modifies memory, Unicorn needs to see changes

**Options**:

A. **Full Sync** (Simple, slow):
```cpp
void sync_all_memory() {
    unicorn_mem_write(unicorn, 0, RAMBaseHost, RAMSize);
}
```

B. **Dirty Page Tracking** (Complex, fast):
```cpp
// Use mprotect() to catch writes
// Sync only modified pages
```

C. **Instruction-based** (Medium):
```cpp
// After memory-writing instructions (MOVE, etc.)
// Sync just the affected addresses
```

**Start with A, optimize later**

### 5. Configuration

**Build-time** (`meson_options.txt`):
```
option('dual_cpu_validation', type: 'boolean', value: false,
       description: 'Enable dual-CPU validation with Unicorn')
```

**Runtime** (prefs):
```
dual_cpu_validation true
dual_cpu_log_file "cpu_validation.log"
dual_cpu_break_on_divergence false
```

## Testing Plan

### Phase 1: Boot ROM
- Enable validation
- Run BasiliskII
- Boot to ROM
- Check log for divergences
- Target: Boot past "Happy Mac" icon

### Phase 2: Load System
- Boot to Mac OS desktop
- Check divergences during:
  - Trap dispatch
  - Memory manager
  - File system
  - QuickDraw

### Phase 3: Run Applications
- Launch SimpleText
- Open About box
- Check complex code paths

## Expected Divergences

### Known Issues to Handle:

1. **EMUL_OP Instructions**: Skip on Unicorn ✅
2. **Timing**: UAE vs Unicorn cycle counts may differ (ignore)
3. **Undefined Behavior**: Different handling of illegal/unimplemented instructions
4. **FPU Precision**: Floating-point rounding differences
5. **Lazy Flags**: Already fixed! ✅

### Real Bugs to Find:

1. **Instruction Implementation**: Opcodes computed differently
2. **Addressing Modes**: Effective address calculation bugs
3. **Exception Handling**: Different exception priority/timing
4. **Privilege**: Supervisor/user mode edge cases

## Benefits

1. **Confidence**: Proves UAE and Unicorn are compatible
2. **Bug Detection**: Finds emulation bugs in both CPUs
3. **Regression Testing**: Validates future changes
4. **Documentation**: Shows exactly where differences occur

## Risks / Limitations

1. **Performance**: ~2x slower (running two CPUs)
2. **Memory**: 2x memory usage (two CPU states)
3. **EMUL_OP**: Can't validate Mac OS trap handling
4. **Complexity**: More code to maintain

## Success Criteria

✅ **Minimum**: Boot ROM to "Happy Mac" with <10 divergences
✅ **Good**: Boot to desktop with <100 divergences
✅ **Excellent**: Run application with <1000 divergences
✅ **Perfect**: Zero divergences on all instruction types

## Files to Create/Modify

### New Files:
- `src/cpu/unicorn_validation.h`
- `src/cpu/unicorn_validation.cpp`
- `docs/Dual-CPU-Validation-Usage.md`

### Modified Files:
- `src/cpu/uae_cpu/newcpu.cpp` (add validation hooks)
- `src/core/main.cpp` (InitUnicornValidation)
- `meson.build` (add unicorn_validation module)
- `meson_options.txt` (add option)

## Next Steps

1. Create unicorn_validation module skeleton
2. Hook into UAE execution loop
3. Test with simple ROM boot
4. Iterate and fix divergences
5. Document findings

This will be the **ultimate test** of our dual-CPU work!
