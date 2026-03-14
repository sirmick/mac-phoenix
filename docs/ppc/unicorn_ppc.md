# Unicorn Engine PPC Support

Detailed reference for the Unicorn PPC API as it exists in our subprojects/unicorn fork.

## Status

PPC source code is **complete** in the Unicorn subproject but **not currently built**. The build is configured with `UNICORN_ARCH=m68k` only. Rebuilding with `UNICORN_ARCH="m68k;ppc"` will produce `libppc-softmmu.a`. The QEMU PPC backend includes 300+ CPU models, full MMU, FPU, and exception handling.

```bash
cd subprojects/unicorn/build
cmake .. -DUNICORN_ARCH="m68k;ppc"
cmake --build . -j$(nproc)
```

## Initialization

```c
#include <unicorn/unicorn.h>
#include <unicorn/ppc.h>

uc_engine *uc;
uc_err err = uc_open(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN, &uc);

// Select G3 (750) CPU model
uc_ctl_set_cpu_model(uc, UC_CPU_PPC32_750_V2_0);
```

**Important**: PPC Macs are big-endian. Always use `UC_MODE_BIG_ENDIAN`.

## Registers

### General Purpose (32 x 32-bit)

| Register | Unicorn ID | SheepShaver Usage |
|----------|-----------|-------------------|
| R0 | `UC_PPC_REG_0` | Volatile |
| R1 | `UC_PPC_REG_1` | Stack pointer |
| R2 | `UC_PPC_REG_2` | TOC pointer (System V ABI) |
| R3-R10 | `UC_PPC_REG_3`–`UC_PPC_REG_10` | Arguments / return values |
| R8-R15 | `UC_PPC_REG_8`–`UC_PPC_REG_15` | Shadow 68k d0-d7 in SheepShaver |
| R16-R22 | `UC_PPC_REG_16`–`UC_PPC_REG_22` | Shadow 68k a0-a6 in SheepShaver |
| R13 | `UC_PPC_REG_13` | Small data area pointer |
| R24 | `UC_PPC_REG_24` | 68k PC in SheepShaver |
| R25 | `UC_PPC_REG_25` | 68k SR (interrupt level) |
| R31 | `UC_PPC_REG_31` | Emulator data pointer |

### Special Purpose

| Register | Unicorn ID | Description |
|----------|-----------|-------------|
| PC (NIP) | `UC_PPC_REG_PC` | Next Instruction Pointer |
| LR | `UC_PPC_REG_LR` | Link Register (return address) |
| CTR | `UC_PPC_REG_CTR` | Count Register (loop/branch target) |
| CR | `UC_PPC_REG_CR` | Condition Register (32-bit, 8 x 4-bit fields) |
| CR0-CR7 | `UC_PPC_REG_CR0`–`UC_PPC_REG_CR7` | Individual CR fields |
| MSR | `UC_PPC_REG_MSR` | Machine State Register |
| XER | `UC_PPC_REG_XER` | Integer Exception Register |
| FPSCR | `UC_PPC_REG_FPSCR` | FP Status/Control |

### Floating Point (32 x 64-bit)

| Register | Unicorn ID |
|----------|-----------|
| FPR0-FPR31 | `UC_PPC_REG_FPR0`–`UC_PPC_REG_FPR31` |

### Register Access

```c
// Read
uint32_t val;
uc_reg_read(uc, UC_PPC_REG_3, &val);

// Write
uint32_t val = 0x12345678;
uc_reg_write(uc, UC_PPC_REG_3, &val);

// PC
uint64_t pc;
uc_reg_read(uc, UC_PPC_REG_PC, &pc);  // Note: 64-bit even in PPC32 mode
```

## CPU Models for Mac Emulation

### Recommended: G3 (PowerPC 750)

```c
UC_CPU_PPC32_750_V2_0   // Beige G3 era — best match for Gossamer ROM
UC_CPU_PPC32_750_V2_1   // Slightly later revision
UC_CPU_PPC32_750_V3_0   // Latest 750 revision
UC_CPU_PPC32_750_V3_1
```

### Other Mac-relevant models

```c
// 603 series (early PPC Macs: 6100, 7100, 8100)
UC_CPU_PPC32_603
UC_CPU_PPC32_603E_V1_1 through UC_CPU_PPC32_603E_V4_1

// 604 series (Power Mac 9500, 8500)
UC_CPU_PPC32_604
UC_CPU_PPC32_604E_V2_4

// G4 (7400 series)
UC_CPU_PPC32_7400_V2_9
UC_CPU_PPC32_7410_V1_0

// G5 (970 series) — 64-bit, future target
UC_CPU_PPC64_970_V2_2
UC_CPU_PPC64_970FX_V1_0
```

## Memory Mapping

Same API as m68k — `uc_mem_map_ptr` for host-backed memory, `uc_mmio_map` for hardware registers:

```c
// Map RAM (host pointer)
uc_mem_map_ptr(uc, 0x00000000, ram_size, UC_PROT_ALL, ram_host_ptr);

// Map ROM (host pointer, read+exec after patching)
uc_mem_map_ptr(uc, rom_base, rom_size, UC_PROT_ALL, rom_host_ptr);

// Map MMIO for hardware registers
uc_mmio_map(uc, 0x50F00000, 0x40000, hw_read_cb, NULL, hw_write_cb, NULL);

// Map dummy regions (NuBus probes, phantom hardware)
uc_mmio_map(uc, gap_start, gap_size, dummy_read, NULL, dummy_write, NULL);
```

## Hook Types

### Interrupt Hook (primary mechanism for EmulOp dispatch)

```c
void hook_intr(uc_engine *uc, uint32_t intno, void *user_data) {
    // intno = exception number
    // POWERPC_EXCP_PROGRAM (6) for illegal instruction (our SHEEP opcodes)
    // POWERPC_EXCP_SYSCALL (8) for sc instruction
    // POWERPC_EXCP_DECR (10) for decrementer
}
uc_hook_add(uc, &hook, UC_HOOK_INTR, hook_intr, NULL, 1, 0);
```

### Invalid Instruction Hook (alternative for SHEEP opcode detection)

```c
bool hook_insn_invalid(uc_engine *uc, void *user_data) {
    uint64_t pc;
    uc_reg_read(uc, UC_PPC_REG_PC, &pc);
    uint32_t opcode;
    uc_mem_read(uc, pc, &opcode, 4);
    // Byte-swap for big-endian
    opcode = __builtin_bswap32(opcode);

    if ((opcode & 0xFC000000) == 0x18000000) {
        // SHEEP opcode — dispatch EmulOp or NativeOp
        handle_sheep_opcode(uc, opcode);
        return true;  // handled
    }
    return false;  // not handled — real illegal instruction
}
uc_hook_add(uc, &hook, UC_HOOK_INSN_INVALID, hook_insn_invalid, NULL, 1, 0);
```

### Block Hook (for interrupt delivery, timer polling)

```c
void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    // Poll timer, deliver pending interrupts, apply deferred register updates
}
uc_hook_add(uc, &hook, UC_HOOK_BLOCK, hook_block, NULL, 1, 0);
```

### Code Hook (for tracing/debugging)

```c
void hook_code(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    // Per-instruction trace
}
uc_hook_add(uc, &hook, UC_HOOK_CODE, hook_code, NULL, 1, 0);
```

## Interrupt Delivery

**Critical difference from m68k**: There is no `uc_ppc_trigger_interrupt()` public API like `uc_m68k_trigger_interrupt()`.

### Options for PPC interrupt injection:

**Option 1: MSR + exception injection via `UC_HOOK_INTR`**
- Set External Exception pending bit in CPU state
- Unicorn's QEMU backend will dispatch to vector 0x500 (External Input)
- Requires accessing internal `CPUPPCState` which is fragile

**Option 2: Direct exception simulation (recommended)**
- In block hook, when interrupt pending:
  1. Save PC and MSR to SRR0/SRR1
  2. Set PC to exception vector (0x500 for external interrupt)
  3. Update MSR (clear EE, set supervisor mode)
  4. Stop execution so next `uc_emu_start()` begins at vector
- This mirrors what the m68k backend does (push exception frame, jump to vector)

**Option 3: Use `ppc_set_irq()` internal function**
- Located in `qemu/include/hw/ppc/ppc.h`
- Would require exposing it through the Unicorn API (fork modification)
- Most correct but requires unicorn subproject changes

**Recommendation**: Start with Option 2 (direct simulation). It's self-contained, doesn't require Unicorn changes, and matches our m68k pattern. If we need more accurate exception behavior later, add Option 3.

## Deferred Register Updates

Same pattern as m68k: register writes inside Unicorn hooks don't reliably persist. Queue updates and apply them in the block hook or after `uc_emu_start()` returns.

```c
typedef struct {
    bool pending_gpr[32];
    uint32_t gpr_values[32];
    bool pending_pc;
    uint64_t pc_value;
    bool pending_lr;
    uint32_t lr_value;
    // ... etc
} PPCDeferredUpdates;
```

## Code Cache

```c
uc_ctl_flush_tb(uc);  // Flush translation blocks after ROM patching
```

## Endianness

PPC is big-endian natively. Memory reads via `uc_mem_read()` return bytes in memory order (big-endian). When reading instruction opcodes from host memory, byte-swap with `__builtin_bswap32()` on little-endian hosts.

For Mac memory access functions (`mem_read_word`, `mem_read_long`), the existing big-endian helpers used by m68k work unchanged — PPC Macs use the same byte order.

## Current Wrapper Issues

`unicorn_wrapper.c` has these bugs blocking PPC:

1. **`unicorn_create_with_model()`** (line 399): Hardcoded `uc_open(UC_ARCH_M68K, ...)` — ignores `arch` parameter
2. **Register accessors**: Only `unicorn_get_dreg/areg/sr/cacr/vbr` exist (m68k-only)
3. **Hook handlers**: `hook_interrupt` checks for m68k A-line exception (intno==10)
4. **Deferred updates**: Only queues for m68k D0-D7, A0-A7, SR

All fixable — the wrapper was designed with `UnicornArch` enum already, just never implemented.
