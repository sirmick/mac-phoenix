# Better Mechanisms than EmulOps for Unicorn

## The Problem with EmulOps

EmulOps have several fundamental issues in a JIT environment:

1. **Ambiguous opcodes** - 0x7100-0x713F are valid MOVEQ instructions
2. **JIT optimization** - May be optimized away or combined
3. **Detection overhead** - Requires hooks or checks at TB boundaries
4. **Synchronization issues** - Register updates in hooks don't always persist

## Alternative Mechanisms

### 1. Memory-Mapped I/O (MMIO) - **RECOMMENDED**

Map a special memory region that triggers hooks when accessed:

```c
#define EMULOP_BASE 0xFF000000
#define EMULOP_SIZE 0x1000

// Setup
uc_mmio_map(uc, EMULOP_BASE, EMULOP_SIZE, mmio_read, mmio_write, user_data);

// Usage in 68k code
move.l #RESET_CMD, 0xFF000000     ; Trigger reset
move.l #SHUTDOWN_CMD, 0xFF000004  ; Trigger shutdown
```

**Advantages:**
- ✅ Always trapped - memory access hooks are reliable
- ✅ No opcode ambiguity
- ✅ Works mid-TB
- ✅ Can pass parameters via memory writes
- ✅ Can return values via memory reads

**Disadvantages:**
- ❌ Requires memory address space
- ❌ Slightly more complex to use

**Implementation:**
```c
static uint64_t mmio_read(uc_engine *uc, uint64_t addr, unsigned size, void *user_data) {
    uint32_t offset = addr - EMULOP_BASE;
    switch (offset) {
        case 0x00: return get_ticks();      // Read timer
        case 0x04: return get_xpram_byte(); // Read XPRAM
        // ...
    }
    return 0;
}

static void mmio_write(uc_engine *uc, uint64_t addr, unsigned size,
                       uint64_t value, void *user_data) {
    uint32_t offset = addr - EMULOP_BASE;
    switch (offset) {
        case 0x00: handle_reset(); break;
        case 0x04: handle_shutdown(); break;
        case 0x08: set_xpram_byte(value); break;
        // ...
    }
}
```

### 2. Hypercalls via Invalid Instructions

Use truly invalid opcodes that always trigger UC_HOOK_INSN_INVALID:

```asm
; Use undefined opcodes in gaps
dc.w 0x4AFC    ; Illegal instruction (not used by 68k)
dc.l command   ; Command ID
dc.l param1    ; Parameter 1
dc.l param2    ; Parameter 2
```

**Advantages:**
- ✅ Always trapped
- ✅ No ambiguity
- ✅ Can pass inline parameters

**Disadvantages:**
- ❌ Requires finding unused opcode space
- ❌ May conflict with future CPU extensions

### 3. Software Interrupts / Exceptions

Use the 68k TRAP instruction:

```asm
trap #15       ; Reserved for emulator
move.l #cmd,d0 ; Command in D0
; Parameters in other registers
```

**Advantages:**
- ✅ Standard 68k mechanism
- ✅ Always causes exception
- ✅ Clean register-based interface

**Disadvantages:**
- ❌ May conflict with OS traps
- ❌ Exception handling overhead

### 4. Breakpoint Instructions

Use the 68k BKPT instruction (68020+):

```asm
bkpt #7        ; Breakpoint with vector
; Registers contain command/parameters
```

**Advantages:**
- ✅ Designed for debugger/emulator use
- ✅ Always trapped
- ✅ Multiple vectors available

**Disadvantages:**
- ❌ Not available on 68000/68010
- ❌ May conflict with debuggers

### 5. Coprocessor Interface

Use the coprocessor instructions to communicate:

```asm
; Use coprocessor ID 7 (usually unused)
cp7 0x01       ; Coprocessor instruction
; Data in coprocessor registers
```

**Advantages:**
- ✅ Designed for extensions
- ✅ Rich instruction set
- ✅ Can transfer blocks of data

**Disadvantages:**
- ❌ Complex to implement
- ❌ May conflict with FPU/MMU

### 6. Magic Memory Sequences

Write specific patterns to normal memory that trigger actions:

```c
// Monitor writes to specific addresses
if (addr == 0x1000 && value == 0xDEADBEEF) {
    // Check next write
    if (next_addr == 0x1004 && next_value == 0xCAFEBABE) {
        // Trigger emulator function
    }
}
```

**Advantages:**
- ✅ No special opcodes needed
- ✅ Works with any CPU

**Disadvantages:**
- ❌ Risk of false positives
- ❌ Performance overhead
- ❌ Complex pattern matching

## Recommendation: MMIO Approach

For Unicorn, **Memory-Mapped I/O (MMIO)** is the best alternative:

### Example Implementation

```c
// Setup MMIO region
#define MMIO_BASE     0xFF000000
#define MMIO_SIZE     0x1000

// Command offsets
#define CMD_SHUTDOWN  0x00
#define CMD_RESET     0x04
#define CMD_GET_TICKS 0x08
#define CMD_XPRAM_RW  0x0C
#define CMD_TRAP_EXEC 0x10

// In initialization
uc_mmio_map(uc, MMIO_BASE, MMIO_SIZE, emul_mmio_read, emul_mmio_write, NULL);

// MMIO handlers
static uint64_t emul_mmio_read(uc_engine *uc, uint64_t addr, unsigned size, void *data) {
    switch (addr - MMIO_BASE) {
        case CMD_GET_TICKS:
            return get_current_ticks();
        case CMD_XPRAM_RW:
            return read_xpram(cached_index);
        default:
            return 0;
    }
}

static void emul_mmio_write(uc_engine *uc, uint64_t addr, unsigned size,
                            uint64_t value, void *data) {
    switch (addr - MMIO_BASE) {
        case CMD_SHUTDOWN:
            QuitEmulator();
            break;
        case CMD_RESET:
            CPUReset();
            break;
        case CMD_XPRAM_RW:
            write_xpram(cached_index, value);
            break;
        case CMD_TRAP_EXEC:
            execute_mac_trap(value);
            break;
    }
}
```

### Usage in 68k Code

```asm
; Shutdown emulator
move.l #0, $FF000000

; Reset CPU
move.l #0, $FF000004

; Get timer
move.l $FF000008, d0

; Execute Mac trap
move.w trap_num, $FF000010
```

## Comparison Table

| Mechanism | Reliability | Performance | Complexity | Compatibility |
|-----------|------------|-------------|------------|---------------|
| EmulOps | Medium | Good | Low | Poor (ambiguous) |
| **MMIO** | **High** | **Good** | **Low** | **Excellent** |
| Hypercalls | High | Good | Medium | Good |
| Traps | High | Medium | Low | Good |
| Breakpoints | High | Medium | Low | Limited (68020+) |
| Coprocessor | High | Good | High | Good |
| Magic Memory | Low | Poor | High | Excellent |

## Migration Path

To migrate from EmulOps to MMIO:

1. **Phase 1**: Add MMIO support alongside EmulOps
2. **Phase 2**: Convert ROM patches to use MMIO
3. **Phase 3**: Update test ROMs
4. **Phase 4**: Deprecate EmulOps
5. **Phase 5**: Remove EmulOp code

This provides a smooth transition without breaking existing functionality.

## Conclusion

MMIO is superior to EmulOps for Unicorn because:
- Always trapped regardless of TB boundaries
- No ambiguity with valid instructions
- Clean parameter passing
- Better performance characteristics
- Already partially implemented in the codebase

The main drawback is requiring a reserved memory region, but using high memory (0xFF000000) avoids conflicts with normal Mac software.