# Unified CPU Backend API

## Problem Statement

The current architecture has three execution models:
1. **UAE CPU**: Infinite loop in `m68k_execute()`, only exits via `quit_program`
2. **Unicorn**: Clean single-step API with `unicorn_execute_one()`
3. **DualCPU**: Wrapper that manually calls both, implements its own loop

This creates several issues:
- ❌ Execution loop is embedded in CPU backend (UAE)
- ❌ Can't easily switch between single-step and run-to-completion
- ❌ Testing requires special `Start680x0_until_stopped()` hack
- ❌ DualCPU has to reimplement execution logic

## Design Goals

1. **Execution loop at higher level** - main.cpp controls when to stop
2. **Uniform API across backends** - UAE, Unicorn, DualCPU all look the same
3. **JIT compatibility** - Don't break existing JIT code
4. **Easy testing** - Can run N instructions or until condition

## Proposed API

### Core Backend Interface

```c
typedef enum {
    CPU_BACKEND_UAE,
    CPU_BACKEND_UNICORN,
    CPU_BACKEND_DUALCPU
} CPUBackendType;

typedef enum {
    CPU_EXEC_OK,           // Instruction executed successfully
    CPU_EXEC_STOPPED,      // Hit STOP instruction
    CPU_EXEC_BREAKPOINT,   // Hit breakpoint
    CPU_EXEC_EXCEPTION,    // Unhandled exception
    CPU_EXEC_EMULOP        // Executed EmulOp (for BasiliskII)
} CPUExecResult;

typedef struct CPUBackend {
    CPUBackendType type;

    // Lifecycle
    bool (*init)(void);
    void (*reset)(void);
    void (*destroy)(void);

    // Execution - SINGLE STEP ONLY
    // Higher-level code implements the loop!
    CPUExecResult (*execute_one)(void);

    // State query
    bool (*is_stopped)(void);
    uint32_t (*get_pc)(void);
    uint16_t (*get_sr)(void);
    uint32_t (*get_dreg)(int n);
    uint32_t (*get_areg)(int n);

    // State modification
    void (*set_pc)(uint32_t pc);
    void (*set_sr)(uint16_t sr);
    void (*set_dreg)(int n, uint32_t val);
    void (*set_areg)(int n, uint32_t val);

    // Memory access (for validation)
    void (*mem_read)(uint32_t addr, void *data, uint32_t size);
    void (*mem_write)(uint32_t addr, const void *data, uint32_t size);
} CPUBackend;
```

### Execution Loop Examples

#### Simple: Run until STOP

```c
CPUBackend *cpu = cpu_backend_create(CPU_BACKEND_UAE);
cpu->reset();

CPUExecResult result;
do {
    result = cpu->execute_one();
} while (result == CPU_EXEC_OK);

if (result == CPU_EXEC_STOPPED) {
    printf("CPU stopped at PC=0x%08x\n", cpu->get_pc());
}
```

#### Testing: Run N instructions

```c
for (int i = 0; i < 1000; i++) {
    CPUExecResult result = cpu->execute_one();
    if (result != CPU_EXEC_OK) {
        printf("Stopped after %d instructions: %d\n", i, result);
        break;
    }
}
```

#### Dual-CPU validation

```c
CPUBackend *uae = cpu_backend_create(CPU_BACKEND_UAE);
CPUBackend *unicorn = cpu_backend_create(CPU_BACKEND_UNICORN);

for (int i = 0; i < 1000; i++) {
    uae->execute_one();
    unicorn->execute_one();

    if (uae->get_pc() != unicorn->get_pc()) {
        printf("Divergence at instruction %d!\n", i);
        break;
    }
}
```

#### BasiliskII: Run until EmulOp

```c
CPUExecResult result;
do {
    result = cpu->execute_one();

    if (result == CPU_EXEC_EMULOP) {
        handle_emulop();
    }
} while (result == CPU_EXEC_OK || result == CPU_EXEC_EMULOP);
```

## Implementation Strategy

### Phase 1: UAE Wrapper

Modify `uae_wrapper.c` to expose clean single-step API:

```c
// Current: uae_cpu_execute_one() already exists!
// Just need to return CPUExecResult instead of void

CPUExecResult uae_execute_one(void) {
    // Execute one instruction (existing code)
    uae_u32 opcode = GET_OPCODE;
    (*cpufunctbl[opcode])(opcode);

    // Check result
    extern struct regstruct regs;
    if (regs.stopped) return CPU_EXEC_STOPPED;

    // Check for EmulOp return (quit_program set)
    extern bool quit_program;
    if (quit_program) {
        quit_program = false;
        return CPU_EXEC_EMULOP;
    }

    return CPU_EXEC_OK;
}
```

### Phase 2: Unified Backend

Create `src/cpu/cpu_backend.c`:

```c
static CPUBackend uae_backend = {
    .type = CPU_BACKEND_UAE,
    .init = uae_init,
    .reset = uae_reset,
    .execute_one = uae_execute_one,
    .get_pc = uae_get_pc,
    // ... etc
};

CPUBackend* cpu_backend_create(CPUBackendType type) {
    switch (type) {
        case CPU_BACKEND_UAE:
            return &uae_backend;
        case CPU_BACKEND_UNICORN:
            return &unicorn_backend;
        case CPU_BACKEND_DUALCPU:
            return &dualcpu_backend;
    }
}
```

### Phase 3: Update main.cpp

Remove `Start680x0()` infinite loop, implement loop at top level:

```c
// After InitAll():
CPUBackend *cpu = get_cpu_backend();  // Returns UAE or DualCPU

CPUExecResult result;
for (;;) {
    result = cpu->execute_one();

    switch (result) {
        case CPU_EXEC_OK:
            continue;  // Keep running

        case CPU_EXEC_STOPPED:
            printf("CPU stopped\n");
            goto exit_loop;

        case CPU_EXEC_EMULOP:
            // BasiliskII emulator operation returned
            // This is normal - EmulOp handles trap and returns
            continue;

        case CPU_EXEC_EXCEPTION:
            fprintf(stderr, "Unhandled exception!\n");
            goto exit_loop;
    }
}
exit_loop:
```

### Phase 4: JIT Support

JIT needs special handling. Two approaches:

**Option A: JIT as separate backend**
```c
if (UseJIT) {
    cpu = cpu_backend_create(CPU_BACKEND_UAE_JIT);
} else {
    cpu = cpu_backend_create(CPU_BACKEND_UAE);
}
```

**Option B: JIT mode flag**
```c
typedef struct CPUBackend {
    // ... existing fields ...
    void (*execute_until_stopped)(void);  // Optional: JIT fast path
} CPUBackend;

// Usage:
if (cpu->execute_until_stopped) {
    // JIT fast path - runs until EmulOp/exception
    cpu->execute_until_stopped();
} else {
    // Interpreter - manual loop
    while (cpu->execute_one() == CPU_EXEC_OK) {}
}
```

**Recommendation: Option B** - Allows JIT to keep its optimized loop while interpreter gets fine control.

## Benefits

### 1. **Clean Separation**
- CPU backend: Execute instructions, report results
- Main loop: Control flow, decide when to stop
- Testing: Inject custom loops easily

### 2. **Easy Testing**
```c
// Test ROM that ends with STOP
while (cpu->execute_one() == CPU_EXEC_OK) {}
assert(cpu->is_stopped());
```

### 3. **Flexible Dual-CPU**
```c
// DualCPU can be a real backend, not a wrapper!
CPUExecResult dualcpu_execute_one(void) {
    CPUExecResult uae_result = uae_execute_one();
    CPUExecResult uc_result = unicorn_execute_one();

    if (uae_result != uc_result) {
        report_divergence();
        return CPU_EXEC_EXCEPTION;
    }

    if (uae_get_pc() != unicorn_get_pc()) {
        report_divergence();
        return CPU_EXEC_EXCEPTION;
    }

    return uae_result;
}
```

### 4. **Better ROM Detection**

Instead of checking for STOP instruction, use magic header:

```c
#define TEST_ROM_MAGIC 0x54524F4D  // "TROM"

bool is_test_rom(uint8_t *rom) {
    // Check magic at offset 0x10 (unused in real Mac ROMs)
    uint32_t magic = read_be32(rom + 0x10);
    return magic == TEST_ROM_MAGIC;
}
```

Test ROM builder:

```python
# Build test ROM with magic header
struct.pack_into('>I', rom, 0x10, 0x54524F4D)  # "TROM" magic
```

## Migration Path

1. ✅ **Already have**: `uae_cpu_execute_one()` exists
2. ✅ **Already have**: `unicorn_execute_one()` exists
3. **Add**: Return `CPUExecResult` instead of `void`/`bool`
4. **Add**: `cpu_backend.c` wrapper
5. **Modify**: `main.cpp` to use execution loop
6. **Remove**: `Start680x0()`, `Start680x0_until_stopped()`
7. **Add**: Test ROM magic header detection

## Questions

### Does this break JIT?

**No!** JIT can still have its optimized loop:

```c
// For JIT backend:
void uae_jit_execute_until_stopped(void) {
    m68k_compile_execute();  // Existing JIT code
}

CPUBackend uae_jit_backend = {
    .execute_one = uae_jit_execute_one,  // Single-step (for debugging)
    .execute_until_stopped = uae_jit_execute_until_stopped,  // Fast path
};
```

Main loop:

```c
if (cpu->execute_until_stopped) {
    // JIT: Run in native loop for performance
    cpu->execute_until_stopped();
} else {
    // Interpreter: Fine-grained control
    while (cpu->execute_one() == CPU_EXEC_OK) {}
}
```

### What about interrupt handling?

Interrupts are special flags that can be checked between instructions:

```c
while (cpu->execute_one() == CPU_EXEC_OK) {
    if (check_host_timer()) {
        cpu->trigger_interrupt(1);  // New API method
    }
}
```

## Next Steps

1. Create `src/cpu/cpu_backend.h` with API definitions
2. Modify `uae_wrapper.c` to return `CPUExecResult`
3. Create `src/cpu/cpu_backend.c` with backend factory
4. Update `main.cpp` to inline `InitAll()` and use execution loop
5. Add test ROM magic header support
