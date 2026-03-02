# Memory Layout and Addressing

## Overview

macemu-next uses BasiliskII's "direct addressing" mode where Mac virtual addresses map directly to host pointers via a simple offset. This provides excellent performance but requires careful setup.

## Memory Map

### Mac Address Space (32-bit)

```
0x00000000 - 0x01ffffff    RAM (32 MB in our test)
0x02000000 - 0x020fffff    ROM (1 MB Quadra 650 ROM)
0x40000000 - 0x4fffffff    ROM (alternative mapping, 24-bit compatible)
```

### Host Memory Layout

We allocate a single contiguous region using `mmap()`:

```
RAMBaseHost = mmap(32 MB + 1 MB)    // Contiguous allocation
ROMBaseHost = RAMBaseHost + 32 MB   // ROM follows RAM
```

Example actual addresses (these will vary per run):
```
RAMBaseHost = 0x7ffff5700000
ROMBaseHost = 0x7ffff7700000 (RAMBaseHost + 0x02000000)
```

## Direct Addressing

### The Translation

Mac addresses translate to host pointers via `MEMBaseDiff`:

```c
MEMBaseDiff = (uintptr)RAMBaseHost;  // e.g., 0x7ffff5700000

// Mac address 0x00000000 -> Host pointer:
uint8_t *host_ptr = (uint8_t *)MEMBaseDiff + mac_addr;
// = 0x7ffff5700000 + 0x00000000 = 0x7ffff5700000 (start of RAM)

// Mac address 0x02000000 -> Host pointer:
uint8_t *host_ptr = (uint8_t *)MEMBaseDiff + 0x02000000;
// = 0x7ffff5700000 + 0x02000000 = 0x7ffff7700000 (start of ROM)
```

### Key Variables

```c
// Host addresses (actual memory pointers)
uint8_t *RAMBaseHost;   // Points to allocated RAM
uint8_t *ROMBaseHost;   // Points to allocated ROM

// Mac addresses (as seen by M68K code)
uint32_t RAMBaseMac;    // Always 0x00000000
uint32_t ROMBaseMac;    // Calculated: Host2MacAddr(ROMBaseHost)

// Translation offset
uintptr MEMBaseDiff;    // RAMBaseHost value
```

### Helper Macros

```c
// Convert Mac address to host pointer
#define Mac2HostAddr(addr) ((uint8_t *)MEMBaseDiff + (addr))

// Convert host pointer to Mac address
#define Host2MacAddr(ptr) ((uint32_t)((uintptr)(ptr) - MEMBaseDiff))
```

## ROM Offset Calculation

The ROM is placed at a specific Mac address. Here's how we calculate it:

```c
// After allocating memory:
ROMBaseHost = RAMBaseHost + RAMSize;

// Calculate what Mac address this corresponds to:
ROMBaseMac = Host2MacAddr(ROMBaseHost);
// = (uintptr)ROMBaseHost - MEMBaseDiff
// = (uintptr)ROMBaseHost - (uintptr)RAMBaseHost
// = RAMSize
// For 32 MB RAM: ROMBaseMac = 0x02000000
```

So with 32 MB of RAM, the ROM naturally ends up at Mac address `0x02000000`.

## CPU Register Setup

### PC (Program Counter)

The M68K CPU's PC register contains a Mac address. UAE's `regs.pc_p` is a host pointer:

```c
// After m68k_reset(), the UAE CPU sets:
regs.pc = ROMBaseMac + 0x2a;              // Mac address: 0x0200002a
regs.pc_p = Mac2HostAddr(regs.pc);        // Host ptr: ROMBaseHost + 0x2a
```

### Stack Pointer

```c
// After m68k_reset():
regs.a[7] = 0x2000;  // A7 (stack) at Mac address 0x2000 (in RAM)
```

## Instruction Fetch

When the UAE CPU fetches an instruction:

1. **PC** = `0x0200002a` (Mac address)
2. **regs.pc_p** = `ROMBaseHost + 0x2a` (host pointer, e.g., `0x7ffff770002a`)
3. **GET_OPCODE** reads from `regs.pc_p`:
   ```c
   #define GET_OPCODE (do_get_mem_word_unswapped(regs.pc_p))
   ```
4. Reads 2 bytes from host memory at that pointer
5. Returns little-endian uint16 (e.g., `0xfa4e` for big-endian bytes `4e fa`)

## Memory Access Functions

BasiliskII provides wrappers for Mac memory access:

```c
// Read from Mac address
uint32_t get_long(uint32_t mac_addr) {
    uint32_t *host_ptr = (uint32_t *)Mac2HostAddr(mac_addr);
    return do_get_mem_long(host_ptr);  // Handles byte-swapping
}

uint32_t get_word(uint32_t mac_addr) {
    uint16_t *host_ptr = (uint16_t *)Mac2HostAddr(mac_addr);
    return do_get_mem_word(host_ptr);  // Handles byte-swapping
}

uint32_t get_byte(uint32_t mac_addr) {
    uint8_t *host_ptr = (uint8_t *)Mac2HostAddr(mac_addr);
    return do_get_mem_byte(host_ptr);  // No swapping needed
}

// Write to Mac address
void put_long(uint32_t mac_addr, uint32_t value);
void put_word(uint32_t mac_addr, uint32_t value);
void put_byte(uint32_t mac_addr, uint32_t value);
```

These are defined in `src/cpu/uae_cpu/memory.h`.

## Why Direct Addressing?

**Advantages:**
- ✅ Very fast - just pointer arithmetic, no table lookups
- ✅ Simple - easy to understand and debug
- ✅ Works well with modern OSes that provide large virtual address spaces

**Disadvantages:**
- ❌ Requires contiguous memory allocation
- ❌ Assumes 32-bit or 64-bit host (not portable to 16-bit systems)
- ❌ Memory is always allocated, even if Mac doesn't use it all

**Alternative: Real Addressing**
BasiliskII also supports "real addressing" where Mac memory is at a fixed host address. This is used on some platforms but requires special OS support.

**Alternative: Banking**
Some emulators use memory banking where different Mac address ranges map to different host buffers. This is more flexible but slower.

## 24-bit vs 32-bit Addressing

Early Macs used 24-bit addressing (only 16 MB address space). BasiliskII supports this via the `TwentyFourBitAddressing` flag:

```c
// When enabled, Mac addresses are masked to 24 bits:
#define Mac2HostAddr(addr) ((uint8_t *)MEMBaseDiff + ((addr) & 0xffffff))
```

For Quadra ROMs (68040), we use 32-bit addressing (flag = false).

## Practical Example

Let's trace a memory access at Mac address `0x00001000`:

```c
// 1. Mac code executes: MOVE.L $1000,D0
// 2. UAE CPU calls: get_long(0x00001000)
// 3. get_long() translates:
uint32_t *host_ptr = (uint32_t *)(MEMBaseDiff + 0x00001000);
//                 = (uint32_t *)(0x7ffff5700000 + 0x1000)
//                 = (uint32_t *)0x7ffff5701000

// 4. Read and byte-swap:
uint32_t value = do_get_mem_long(host_ptr);  // Reads 4 bytes, bswaps

// 5. Returns value to D0
```

The Mac thinks it's accessing address `0x1000`, but we're actually reading from host address `0x7ffff5701000`. The Mac is none the wiser!

## Memory Initialization

From `test_boot.cpp`:

```c
// 1. Allocate RAM + ROM
RAMSize = 32 * 1024 * 1024;  // 32 MB
RAMBaseHost = (uint8_t *)mmap(NULL, RAMSize + 0x100000,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
ROMBaseHost = RAMBaseHost + RAMSize;

// 2. Clear RAM
memset(RAMBaseHost, 0, RAMSize);

// 3. Set up direct addressing
MEMBaseDiff = (uintptr)RAMBaseHost;
RAMBaseMac = 0;
ROMBaseMac = Host2MacAddr(ROMBaseHost);  // = 0x02000000

// 4. Load ROM (kept in big-endian format)
int rom_fd = open(rom_path, O_RDONLY);
ROMSize = lseek(rom_fd, 0, SEEK_END);
lseek(rom_fd, 0, SEEK_SET);
read(rom_fd, ROMBaseHost, ROMSize);
close(rom_fd);
```

## Unicorn Backend Memory Map (March 2026)

The Unicorn backend maps a much larger address space than the basic RAM+ROM layout, covering the full 32-bit Quadra 650 memory map:

```
Region              Address Range           Size      Content
─────────────────────────────────────────────────────────────
RAM                 0x00000000-0x01FFFFFF    32MB      Host buffer (uc_mem_map_ptr)
ROM                 0x02000000-0x020FFFFF    1MB       Writable for patching
Dummy               0x02100000-0x030FFFFF    16MB      0xFF00FF00 fill pattern
NuBus Gap 1         0x03100000-0x50EFFFFF    ~1.2GB    dummy_bank (returns 0)
MMIO                0x50F00000-0x50F3FFFF    256KB     VIA/SCC/SCSI stubs (uc_mmio_map)
NuBus Gap 2         0x50F40000-0xEFFFFFFF    ~2.5GB    dummy_bank (returns 0)
High Mem            0xF0000000-0xFEFFFFFF    240MB     Zeroed
Trap Gap            0xFF000000-0xFF000FFF    4KB       Unmapped (EmulOp detection)
High Mem 2          0xFF001000-0xFFFFFFFF    ~16MB     Zeroed
```

### Key Design Decisions

**Dummy region (0xFF00FF00)**: Filled with this pattern to mimic what real Quadra hardware returns for unpopulated NuBus slots. The ROM reads this during slot detection.

**MMIO via `uc_mmio_map()`**: Hardware registers cannot use `uc_mem_map_ptr()` with `UC_HOOK_MEM_READ` because QEMU's JIT compiles direct memory loads that bypass hooks. `uc_mmio_map()` uses QEMU's proper MMIO callback mechanism.

**Trap Gap**: A small unmapped region at 0xFF000000 used for EmulOp return detection during native 68K trap execution.

**NuBus gaps**: Large unmapped address ranges filled with zeroed memory. The dummy_bank read callback returns 0 for all reads, simulating empty NuBus slots.

### MMIO Stubs

The MMIO region at 0x50F00000 contains stub callbacks for:
- **VIA1** (0x50F00000): Versatile Interface Adapter -- timer, keyboard, sound
- **VIA2** (0x50F02000): Second VIA -- slot interrupts, SCSI
- **SCC** (0x50F04000): Serial Communications Controller
- **SCSI** (0x50F10000): SCSI controller
- **ASC** (0x50F14000): Apple Sound Chip
- **DAFB** (0x50F20000): Display controller

All stubs return minimal values to prevent ROM crashes during hardware probing.

## See Also

- [cpu/UaeQuirks.md](cpu/UaeQuirks.md) - Byte-swapping details
- [cpu/UnicornQuirks.md](cpu/UnicornQuirks.md) - Unicorn-specific memory quirks
