# PPC Memory Layout

Memory map for PowerPC Mac emulation.

## PPC Layout

A single 512MB virtual memory region is allocated with `mmap`, providing a
contiguous host buffer. `VMBaseDiff` = host_base - 0 gives the offset for
direct addressing (Mac address + VMBaseDiff = host pointer).

```
Mac Address     Size        Region
0x00000000      varies      RAM (default 32 MB, configurable up to 256 MB)
0x00400000      4 MB        ROM (ROM_SIZE, OldWorld Gossamer)
                            ROM_AREA_SIZE = 5 MB (extra for patched code)
0x00002800      2 KB        XLM (eXtra Low Memory) — kernel globals (inside RAM)
0x68FFE000      8 KB        Kernel Data (KERNEL_DATA_BASE)
0x5FFFE000      8 KB        Kernel Data 2 (KERNEL_DATA2_BASE, alternate)
0x68070000      64 KB       DR Emulator Base
0x69000000      512 KB      DR Cache Base
Top of RAM      ~64 KB      SheepMem (thunks area, allocated by SheepMem::Init)
```

### Key Points

- **ROM at 0x400000**: Inside the first 32MB, overlapping RAM address space. This
  is correct for PPC Macs — the ROM appears in physical address space overlapping RAM.
- **Kernel Data at 0x68FFE000**: Fixed high address, separate from RAM/ROM. 8KB total:
  4KB kernel variables (offsets 0x0000-0x0FFF) + EmulatorData (offset 0x1000+).
- **XLM at 0x2800**: Inside low RAM, always accessible. Host-side control words
  read/written by both the emulator and patched ROM code.
- **SheepMem**: Allocated at top of RAM by `SheepMem::Init()`. Contains thunk
  trampolines and the zero page.
- **No ScratchMem**: PPC doesn't use the M68K ScratchMem region. Hardware registers
  are handled through kernel data and ROM patching.
- **Framebuffer**: Address comes from the video driver (VideoDoDriverIO), not a fixed
  mapping. The existing triple-buffer video output is reused.

### Comparison with M68K Layout

| Aspect | M68K | PPC |
|--------|------|-----|
| ROM address | 0x02000000 (after RAM) | 0x00400000 (inside RAM space) |
| ROM size | 1 MB (Quadra) | 4 MB (Gossamer) |
| Kernel Data | N/A | 0x68FFE000 (8 KB) |
| XLM globals | N/A | 0x2800 (2 KB) |
| ScratchMem | 0x02100000 (64 KB) | N/A |
| Framebuffer | 0x02110000 (fixed) | Via video driver |
| Direct addressing | MEMBaseDiff | VMBaseDiff |

## XLM (eXtra Low Memory) Globals

Located at 0x2800 in RAM. Initialized by `InitXLM()` during boot.

```c
#define XLM_SIGNATURE           0x2800  // "Baah" magic
#define XLM_KERNEL_DATA         0x2804  // Pointer to KernelData struct
#define XLM_TOC                 0x2808  // PPC Table of Contents
#define XLM_RUN_MODE            0x2810  // 0=MODE_68K, 1=MODE_NATIVE, 2=MODE_EMUL_OP
#define XLM_68K_R25             0x2814  // Saved 68k interrupt level
#define XLM_IRQ_NEST            0x2818  // Interrupt nesting counter
#define XLM_PVR                 0x281C  // Processor Version Register value
#define XLM_BUS_CLOCK           0x2820  // Bus clock speed
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
#define XLM_ETHER_AO_GET_HWADDR    0x28B0  // Ethernet callbacks
#define XLM_ETHER_AO_ADD_MULTI     0x28B4
#define XLM_ETHER_AO_DEL_MULTI     0x28B8
#define XLM_ETHER_AO_SEND_PACKET   0x28BC
#define XLM_ETHER_INIT             0x28C0
#define XLM_ETHER_IRQ              0x28C4
#define XLM_VIDEO_DOIO             0x28C8  // Video I/O callback
```

## Kernel Data Structure

Located at KERNEL_DATA_BASE (0x68FFE000), 8KB total.

```c
// Key offsets within KernelData:
// +0x0020  Physical RAM base
// +0x0060  Nanokernel flags
// +0x0634  Emulator data pointer
// +0x0658  Emulator context block 2 (must differ from +0x065c!)
// +0x065c  Current context block pointer
// +0x0674  Condition register for interrupt (CR bits)
// +0x067c  Interrupt level word (poll word)
// +0x1000  EmulatorData start
// +0x1074  68k opcode table pointer
// +0x1078  68k emulator address
// +0x1100  First EmulatorData context block
// +0x1184  Emulator init routine
// +0x119c  Opcode table pointer
```

**Critical**: The nanokernel creates TWO EmulatorData context blocks during boot
initialization. KD+0x065c (current context) and KD+0x0658 (emulator context) must
point to DIFFERENT blocks. If they're equal, context switch saves clobber restore
data → garbage registers → crash. If this happens, the bug is in `InitKernelData()`
or `patch_nanokernel_boot()`.

## Run Modes

```c
#define MODE_68K      0    // 68k emulator running (ROM's DR interpreter)
#define MODE_NATIVE   1    // Native PPC code running
#define MODE_EMUL_OP  2    // Inside an EMUL_OP host handler
```

Stored at XLM_RUN_MODE (0x2810). Checked by `HandleInterrupt()` to determine
dispatch strategy.

## Address Translation

SheepShaver patches the nanokernel to use identity mapping (V=P: virtual = physical).
No page tables or BAT setup needed. The patch replaces the virt→phys lookup with
`mr r31, r27`. This is the same approach used in the M68K emulator's direct addressing.
