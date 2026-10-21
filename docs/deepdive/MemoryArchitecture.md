# Memory Layout and Addressing (68k ROM machine)

## Overview

The 68k ROM machine keeps all guest memory in one contiguous host block
and maps it at guest address 0, so a Mac address turns into a host pointer
by adding one offset (Basilisk II's "direct addressing"). The block is
registered with the CPU core's `cpu::GuestMemory`
(`src/cpu/core/cpu_core.h`), which every core and the Platform memory
entries read through. The PPC machine and Executor lay out memory
differently; see `../Architecture.md`.

## Memory Map

`CPUContext::init_m68k()` (`src/core/cpu_context.cpp`):

```
Mac address                  Size        Contents
0                            RAMSize     RAM (--ram, default 64 MB)
ROMBaseMac                   ROMSize     ROM (file loaded as-is, big-endian; 1 MB reserved)
ROMBaseMac + ROMSize         64 KB       ScratchMem
ROMBaseMac + ROMSize + 64 KB 8 MB        Frame buffer
```

The block reserves 1 MB for the ROM, but ScratchMem and the frame buffer
follow the ROM file's actual size. `ROMBaseMac` is `RAMSize` on 32-bit
machines (the Quadra profile), so with 32 MB of RAM and the 1 MB Quadra ROM
the ROM is at `0x02000000`, ScratchMem at `0x02100000` and the frame buffer
at `0x02110000`. Machines with a fixed ROM base put it there:

| Profile | ROM base | RAM cap | Addressing |
|---------|----------|---------|------------|
| `se` | `0x400000` | 4 MB | 24-bit |
| `macii`, `iici` | `0x800000` | 8 MB | 24-bit |
| `quadra` | `RAMSize` | none | 32-bit |

On those, the gap between RAM and ROM is part of the block (unused), and
the Mac II and IIci blocks are at least 32 MB so addresses the CPU forms
near the top of the 24-bit space stay mapped. Profiles:
`src/config/machine_profile.cpp`.

## Host Allocation

```c
total = rom_offset + 0x100000 /* ROM */ + 0x10000 /* ScratchMem */ + 0x800000 /* FB */;
block = mmap(NULL, total, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
/* heap fallback (no JIT) if MAP_32BIT fails */

RAMBaseHost = block;
ROMBaseHost = block + rom_offset;
MEMBaseDiff = (uintptr)block;
RAMBaseMac  = 0;
ROMBaseMac  = rom_offset;

cpu_m68k_memory().map(0, total & ~0xFFFF, block);           /* one window */
cpu_m68k_memory().set_address_bits(twenty_four_bit ? 24 : 32);
```

`MAP_32BIT` keeps `MEMBaseDiff` within a 32-bit displacement, which the
UAE JIT needs.

## Translation

`GuestMemory` keeps a delta per 64 KB page; with one window every mapped
page has the same delta, `MEMBaseDiff`:

```c
host = MEMBaseDiff + mac_addr          /* mapped pages */
```

Addresses beyond the block are unmapped: reads return 0 and writes are
dropped (no I/O handler is installed on this machine), and
`g_platform.mem_mac_to_host()` returns a pointer into a dummy page rather
than NULL.

The helpers code uses:

```c
/* src/common/include/cpu_emulation.h */
ReadMacInt32(addr)   -> get_long(addr)   -> g_platform.mem_read_long(addr)
WriteMacInt16(addr,v)-> put_word(addr,v) -> g_platform.mem_write_word(addr,v)
Mac2HostAddr(addr)   -> get_real_address(addr)    -> g_platform.mem_mac_to_host
Host2MacAddr(ptr)    -> get_virtual_address(ptr)  -> g_platform.mem_host_to_mac
```

`g_platform.mem_*` are installed by `cpu::platform_install()`
(`src/cpu/core/platform_cpu.cpp`) and call `GuestMemory::read32()` and
friends. The UAE accessors in `src/cpu/uae_cpu/memory.h` abort with a
message if they run before the Platform table is set up.

## Endianness

Guest memory is stored **big-endian**, byte for byte as on a Mac: the ROM is
copied in unchanged and `GuestMemory` assembles and splits values a byte at
a time. Host code that reads guest memory through a host pointer must swap
(`ntohl`, `do_get_mem_long`) or use `ReadMacInt*`. The UAE core fetches
instructions straight from host memory through `regs.pc_p`; see
[`cpu/UaeQuirks.md`](cpu/UaeQuirks.md).

## 24-bit vs 32-bit Addressing

Early Macs used 24-bit addressing: the top byte of an address is ignored
(the Memory Manager keeps flags there). `GuestMemory::set_address_bits(24)`
masks every access and lookup with `0x00FFFFFF`, and the cores mask
themselves too (`cpu::Config::address_bits`; UAE's `address_reg_mask`).
The IIci ROM's 32-bit addresses (`$40800000`, `$Fsxxxxxx`) wrap onto the
24-bit layout through that mask.

## CPU Reset

`m68k_cpu_reset()` (`src/cpu/cpu_m68k.cpp`) does what Basilisk II's
`m68k_reset()` always did, on any core: SR `$2700`, A7 (SSP) `$2000`, PC
`ROMBaseMac + $2A`. The ROM's reset vectors are not used.

## ScratchMem and Frame Buffer

**ScratchMem** (64 KB right after the ROM image) holds data that must survive Mac OS
zone management, such as the SE's unit table. The `ScratchMem` pointer
points into the middle of the block (Basilisk II's convention).

**Frame buffer** (8 MB after ScratchMem) is outside RAM so drawing can't
corrupt the Mac heap. It is sized for the largest 68k mode, 1920×1080 at
32 bits. The video driver (`video_ipc_m68k_init()` in `src/main.cpp` for the
IPC child) places it at `ROMBaseHost + ROMSize + 0x10000`, and the 60 Hz
tick copies it into the IPC shared memory.

## See Also

- [`cpu/UaeQuirks.md`](cpu/UaeQuirks.md) — UAE's fetch path and byte-swapping
- [`cpu/CpuBackendApi.md`](cpu/CpuBackendApi.md) — `cpu::Core` and `GuestMemory`
- [`../executor/MEMORY_MAP.md`](../executor/MEMORY_MAP.md) — Executor's layout
