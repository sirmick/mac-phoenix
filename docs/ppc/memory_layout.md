# PPC Memory Layout

Memory map for PowerPC Mac emulation, compared with the existing m68k layout.

## M68K Layout (Current)

```
Address         Size    Region
0x00000000      32 MB   RAM
0x02000000      1 MB    ROM
0x02100000      64 KB   ScratchMem (fake hardware bases)
0x02110000      4 MB    FrameBuffer
0x02510000+     ---     Dummy banks (NuBus probes → return 0)
0x50F00000      256 KB  MMIO (VIA1, VIA2 hardware registers)
0x50F40000+     ---     Dummy banks (high address probes)
```

All allocated as a single contiguous host buffer, mapped into Unicorn with `uc_mem_map_ptr`.

## PPC Layout (SheepShaver Reference)

```
Address         Size    Region
0x00000000      var     RAM (typically 32-256 MB)
0x00400000      4 MB    ROM (ROM_SIZE)
                        ROM_AREA_SIZE = 5 MB (extra for patched code)
0x2800-0x2FFF   2 KB    XLM (eXtra Low Memory) — kernel globals
0x5FFFE000      8 KB    Kernel Data (alternate base, KERNEL_DATA2_BASE)
0x68070000      64 KB   DR Emulator Base
0x68FFE000      8 KB    Kernel Data (primary base, KERNEL_DATA_BASE)
0x69000000      512 KB  DR Cache Base
```

### Key Differences from M68K

1. **ROM at 0x400000** instead of after RAM at 0x2000000
2. **Kernel Data** at fixed high address (0x68FFE000) — separate from RAM/ROM
3. **XLM globals** at 0x2800 — inside low RAM (always accessible)
4. **No ScratchMem** — PPC uses real hardware register emulation or kernel data
5. **Framebuffer** — handled differently (through video driver MMIO, not a fixed address)

## Proposed mac-phoenix PPC Layout

Adapt SheepShaver's layout to fit our allocation model:

```
Address         Size    Region          Allocation
0x00000000      32 MB   RAM             uc_mem_map_ptr (host buffer)
0x00400000      4 MB    ROM             uc_mem_map_ptr (inside RAM? or separate)
0x02000000      64 KB   ScratchMem      uc_mem_map_ptr (reuse m68k pattern)
0x02010000      4 MB    FrameBuffer     uc_mem_map_ptr
0x50F00000      256 KB  Hardware MMIO   uc_mmio_map (VIA, CUDA, etc.)
0x68FFE000      8 KB    Kernel Data     uc_mem_map_ptr (separate allocation)
```

**Decision needed**: ROM placement. SheepShaver puts it at 0x400000 (inside the first 32MB). If RAM is also at 0x0, they overlap. SheepShaver handles this by making RAM start after ROM, or by using the ROM area as part of the address space with ROM content mapped there. We need to verify the exact Gossamer ROM expectations.

**Likely approach**: Allocate 128MB contiguous buffer. Map first 32MB as RAM. ROM is loaded into the 0x400000 region (which is inside RAM space — this is correct for PPC Macs, the ROM appears in the physical address space overlapping RAM).

## XLM (eXtra Low Memory) Globals

Located at 0x2800 in RAM. These are host-side control words read/written by both the emulator and patched ROM code.

```c
#define XLM_SIGNATURE           0x2800  // "SHEEP" or "Baah" magic
#define XLM_KERNEL_DATA         0x2804  // Pointer to KernelData struct
#define XLM_TOC                 0x2808  // PPC Table of Contents
#define XLM_RUN_MODE            0x2810  // 0=MODE_68K, 1=MODE_NATIVE, 2=MODE_EMUL_OP
#define XLM_68K_R25             0x2814  // Saved 68k interrupt level
#define XLM_IRQ_NEST            0x2818  // Interrupt nesting counter
#define XLM_PVR                 0x281C  // Processor Version Register value
#define XLM_EMUL_RETURN_PROC    0x2824  // Return-from-68k handler address
#define XLM_EXEC_RETURN_PROC    0x2828  // Execute68k return handler
#define XLM_EMUL_OP_PROC        0x282C  // EMUL_OP handler address
#define XLM_EMUL_RETURN_STACK   0x2830  // Return stack for nested calls
#define XLM_RES_LIB_TOC         0x2834  // Resource library TOC
#define XLM_GET_RESOURCE        0x2838  // GetResource callback
#define XLM_GET_1_RESOURCE      0x283C  // Get1Resource callback
#define XLM_GET_IND_RESOURCE    0x2840  // GetIndResource callback
#define XLM_GET_1_IND_RESOURCE  0x2844  // Get1IndResource callback
#define XLM_R_GET_RESOURCE      0x2848  // RGetResource callback
#define XLM_EXEC_RETURN_OPCODE  0x284C  // M68K_EXEC_RETURN opcode value
#define XLM_ZERO_PAGE           0x2850  // Zero-filled page address
#define XLM_ETHER_AO_GET_HWADDR    0x28B0  // Ethernet callbacks...
#define XLM_ETHER_AO_ADD_MULTI     0x28B4
#define XLM_ETHER_AO_DEL_MULTI     0x28B8
#define XLM_ETHER_AO_SEND_PACKET   0x28BC
#define XLM_ETHER_INIT             0x28C0
#define XLM_ETHER_IRQ              0x28C4
#define XLM_VIDEO_DOIO             0x28C8  // Video I/O callback
```

## Kernel Data Structure

Located at KERNEL_DATA_BASE (0x68FFE000), 8KB total:

```c
struct KernelData {
    uint32 v[0x400];        // 4KB kernel variables
    EmulatorData ed;        // 68k emulator state (at +0x1000)
};

// Key offsets within KernelData:
// +0x0634  Emulator data pointer
// +0x0674  Condition register for interrupt
// +0x067c  Interrupt level word
// +0x1074  68k opcode table pointer
// +0x1078  68k emulator address
// +0x1184  Emulator init routine
// +0x119c  Opcode table pointer
```

## Run Modes

```c
#define MODE_68K      0    // 68k emulator running (ROM's built-in interpreter)
#define MODE_NATIVE   1    // Native PPC code running
#define MODE_EMUL_OP  2    // Inside an EMUL_OP host handler
```

Stored at XLM_RUN_MODE (0x2810). Checked by interrupt handler to determine dispatch strategy.

## Hardware Register Addresses

PPC Macs use different I/O addresses than m68k Macs:

| Address | Device | Notes |
|---------|--------|-------|
| 0x50F00000 | VIA1 | Timer, RTC, ADB |
| 0x50F02000 | VIA2 | Slot interrupts |
| 0x50F04000 | SCC | Serial |
| 0x50F10000 | SCSI (53C96) | Disk controller |
| 0x50F14000 | ASC/AWACS | Audio |
| 0x50F16000 | SWIM3 | Floppy |
| 0x50F20000 | CUDA | ADB, power management |
| 0xF3000000 | Framebuffer | Display memory (varies) |

For Gossamer (Beige G3), the MMIO map is similar but uses Grand Central (GC) or Heathrow as the I/O controller.

### MMIO Strategy

Same as m68k: use `uc_mmio_map` with callbacks. Most hardware can return 0 or fixed values — the ROM patches suppress most real hardware access. Key exceptions:

- **VIA1/VIA2**: Need basic interrupt acknowledgment for 60Hz timer
- **CUDA**: May need minimal ADB response for keyboard/mouse
- **Framebuffer**: Map as regular RAM at display address

## Address Translation

SheepShaver patches the nanokernel to use identity mapping (virtual = physical). This means:
- No page tables needed
- No BAT (Block Address Translation) setup
- `mr r31, r27` replaces virt→phys lookup
- Simplifies Unicorn memory mapping enormously

This is the same V=P (virtual equals physical) approach already used in the m68k emulator.
