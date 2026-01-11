# MMIO Implementation Plan for Unicorn
## Goal: 100% MMIO, Zero Legacy EmulOps

## 1. MMIO Memory Layout

```
Base Address: 0xFF000000
Size: 64KB (0x10000)

Memory Map:
0xFF000000-0xFF000FFF : Command/Control Region (4KB)
0xFF001000-0xFF001FFF : Data Transfer Region (4KB)
0xFF002000-0xFF002FFF : Status/Result Region (4KB)
0xFF003000-0xFF00FFFF : Reserved for future use (52KB)
```

### Command Region Layout (0xFF000000-0xFF000FFF)

```c
// Core System Commands (0xFF000000-0xFF0000FF)
#define MMIO_CMD_SHUTDOWN       0xFF000000  // Write any value to shutdown
#define MMIO_CMD_RESET          0xFF000004  // Write any value to reset
#define MMIO_CMD_BREAK          0xFF000008  // Breakpoint for debugging
#define MMIO_CMD_GET_TICKS      0xFF00000C  // Read to get timer ticks
#define MMIO_CMD_GET_TIME       0xFF000010  // Read to get current time

// XPRAM/PRAM Commands (0xFF000100-0xFF0001FF)
#define MMIO_XPRAM_INDEX        0xFF000100  // Write index, then read/write data
#define MMIO_XPRAM_DATA         0xFF000104  // Read/write XPRAM byte at index
#define MMIO_PRAM_INDEX         0xFF000108  // Write index for PRAM
#define MMIO_PRAM_DATA          0xFF00010C  // Read/write PRAM data

// ROM Patching Commands (0xFF000200-0xFF0002FF)
#define MMIO_PATCH_BOOT_GLOBS   0xFF000200  // Trigger PATCH_BOOT_GLOBS
#define MMIO_FIX_BOOTSTACK      0xFF000204  // Fix boot stack
#define MMIO_FIX_MEMSIZE        0xFF000208  // Fix memory size
#define MMIO_INSTALL_DRIVERS    0xFF00020C  // Install drivers

// Device Driver Commands (0xFF000300-0xFF0003FF)
#define MMIO_DISK_CMD           0xFF000300  // Disk driver command
#define MMIO_DISK_PARAM         0xFF000304  // Disk parameters
#define MMIO_DISK_STATUS        0xFF000308  // Disk status
#define MMIO_SERIAL_CMD         0xFF000310  // Serial command
#define MMIO_SERIAL_DATA        0xFF000314  // Serial data
#define MMIO_ETHER_CMD          0xFF000320  // Ethernet command
#define MMIO_ETHER_PACKET       0xFF000324  // Ethernet packet pointer

// Mac OS Trap Execution (0xFF000400-0xFF0004FF)
#define MMIO_TRAP_NUMBER        0xFF000400  // Write trap number
#define MMIO_TRAP_EXECUTE       0xFF000404  // Write to execute trap
#define MMIO_TRAP_RESULT        0xFF000408  // Read trap result

// Debug/Development (0xFF000500-0xFF0005FF)
#define MMIO_DEBUG_PRINT        0xFF000500  // Write value to print
#define MMIO_DEBUG_STRING       0xFF000504  // Write pointer to string
#define MMIO_REG_DUMP           0xFF000508  // Write to dump registers
#define MMIO_MEM_DUMP           0xFF00050C  // Write addr to dump memory
```

## 2. Files That Need Changes

### A. Create New MMIO Header
**File: `src/common/include/mmio.h`** (NEW)
```c
#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

// MMIO Base and Size
#define MMIO_BASE       0xFF000000UL
#define MMIO_SIZE       0x00010000UL  // 64KB

// Command definitions (as above)
// ...

// MMIO API functions
void mmio_init(void);
void mmio_cleanup(void);
uint32_t mmio_read(uint32_t addr);
void mmio_write(uint32_t addr, uint32_t value);

#endif
```

### B. Modify `unicorn_wrapper.c`
**Changes needed:**

1. **Remove EmulOp detection from hook_block()**
```c
// DELETE THIS ENTIRE SECTION:
if (opcode >= 0x7100 && opcode < 0x7140) {
    uc_emu_stop(uc);
    cpu->trap_ctx.in_emulop = true;
    return;
}
```

2. **Implement proper MMIO handlers**
```c
static uint64_t mmio_read_handler(uc_engine *uc, uint64_t addr,
                                  unsigned size, void *user_data) {
    uint32_t offset = addr - MMIO_BASE;

    switch (offset) {
        case 0x00C: // GET_TICKS
            return cpu_get_ticks();
        case 0x104: // XPRAM_DATA
            return read_xpram(mmio_xpram_index);
        case 0x408: // TRAP_RESULT
            return trap_result;
        default:
            return 0;
    }
}

static void mmio_write_handler(uc_engine *uc, uint64_t addr, unsigned size,
                               uint64_t value, void *user_data) {
    uint32_t offset = addr - MMIO_BASE;

    switch (offset) {
        case 0x000: // SHUTDOWN
            QuitEmulator();
            break;
        case 0x004: // RESET
            cpu_reset();
            break;
        case 0x100: // XPRAM_INDEX
            mmio_xpram_index = value & 0xFF;
            break;
        case 0x104: // XPRAM_DATA
            write_xpram(mmio_xpram_index, value);
            break;
        case 0x200: // PATCH_BOOT_GLOBS
            handle_patch_boot_globs();
            break;
        // ... etc
    }
}
```

3. **Register MMIO region with uc_mmio_map()**
```c
// In unicorn_create()
err = uc_mmio_map(cpu->uc, MMIO_BASE, MMIO_SIZE,
                  mmio_read_handler, mmio_write_handler, cpu);
if (err != UC_ERR_OK) {
    fprintf(stderr, "Failed to map MMIO region: %s\n", uc_strerror(err));
    return NULL;
}
```

### C. Remove EmulOp Handling from `unicorn_execute_n()`
**File: `src/cpu/unicorn_wrapper.c`**

Delete all EmulOp checking code:
```c
// DELETE:
if ((opcode & 0xFF00) == 0x7100) {
    // EmulOp detection code
}
```

### D. Convert ROM Patches
**File: `src/core/rom_patches.cpp`**

Change all EmulOp insertions to MMIO calls:

**Before:**
```c
*wp++ = htons(M68K_EMUL_OP_PATCH_BOOT_GLOBS);
```

**After:**
```c
// move.l #1, MMIO_PATCH_BOOT_GLOBS
*wp++ = htons(0x23FC);  // MOVE.L #imm, abs.L
*wp++ = htons(0x0000);  // immediate high
*wp++ = htons(0x0001);  // immediate low
*wp++ = htons(0xFF00);  // address high
*wp++ = htons(0x0200);  // address low (MMIO_PATCH_BOOT_GLOBS)
```

### E. Update Platform API
**File: `src/cpu/cpu_unicorn.cpp`**

Remove EmulOp handler, rely entirely on MMIO:
```c
// DELETE:
static bool unicorn_platform_emulop_handler(uint16_t opcode, bool is_primary) {
    // This entire function goes away
}

// In unicorn_backend_init():
// REMOVE: p->emulop_handler = unicorn_platform_emulop_handler;
```

## 3. Implementation Steps

### Step 1: Create MMIO Infrastructure
```c
// mmio.c - New file
#include "mmio.h"
#include "main.h"
#include "prefs.h"
#include "xpram.h"

static uint8_t xpram_index = 0;
static uint32_t trap_result = 0;

uint32_t mmio_read(uint32_t addr) {
    uint32_t offset = addr - MMIO_BASE;

    switch (offset) {
        case 0x00C: return GetTicks_usec();
        case 0x104: return XPRAM[xpram_index];
        // ... etc
    }
    return 0;
}

void mmio_write(uint32_t addr, uint32_t value) {
    uint32_t offset = addr - MMIO_BASE;

    switch (offset) {
        case 0x000: QuitEmulator(); break;
        case 0x100: xpram_index = value & 0xFF; break;
        case 0x104: XPRAM[xpram_index] = value; break;
        // ... etc
    }
}
```

### Step 2: Create Helper Macros for ROM Patches
```c
// Helper to emit MMIO write instruction
#define EMIT_MMIO_WRITE(wp, value, mmio_offset) \
    *wp++ = htons(0x23FC);  /* MOVE.L #imm, abs.L */ \
    *wp++ = htons((value) >> 16); \
    *wp++ = htons((value) & 0xFFFF); \
    *wp++ = htons((MMIO_BASE + (mmio_offset)) >> 16); \
    *wp++ = htons((MMIO_BASE + (mmio_offset)) & 0xFFFF)

// Usage:
EMIT_MMIO_WRITE(wp, 1, 0x200);  // Trigger PATCH_BOOT_GLOBS
```

### Step 3: Test ROM Conversion
Convert test ROMs to use MMIO:
```asm
; Old EmulOp way
dc.w $7101  ; SHUTDOWN

; New MMIO way
move.l #1, $FF000000  ; Write to MMIO_CMD_SHUTDOWN
```

## 4. Benefits of This Approach

1. **100% Reliable** - MMIO always traps, no TB issues
2. **Clean Interface** - Clear memory-mapped API
3. **Better Performance** - No instruction checking overhead
4. **Extensible** - Easy to add new commands
5. **Debuggable** - Can log all MMIO accesses
6. **No Ambiguity** - Clear separation from instructions

## 5. Testing Plan

1. Create MMIO test ROM that exercises all commands
2. Verify each ROM patch works with MMIO
3. Performance benchmark vs EmulOps
4. Stress test with rapid MMIO accesses
5. Verify no conflicts with Mac software

## 6. Migration Checklist

- [ ] Create mmio.h header
- [ ] Implement MMIO handlers in unicorn_wrapper.c
- [ ] Remove all EmulOp detection code
- [ ] Convert ROM patches to MMIO
- [ ] Update platform API
- [ ] Create MMIO test suite
- [ ] Update documentation
- [ ] Remove legacy EmulOp code
- [ ] Performance testing
- [ ] Final validation

This plan provides a clean, modern interface between 68k code and the emulator, eliminating all the complexity and unreliability of EmulOps.