# PPC ROM Patching

Porting SheepShaver's ROM patching to mac-phoenix, targeting the Gossamer (Beige G3) ROM.

## Overview

PPC Mac ROMs contain a "nanokernel" — supervisor-mode code that manages exceptions, memory translation, and mode switching between native PPC and the built-in 68k interpreter. SheepShaver patches this nanokernel extensively to run under emulation instead of on real hardware.

The patches fall into four categories:
1. **Nanokernel boot patches** — Skip hardware init, set up emulator data structures
2. **Nanokernel runtime patches** — Redirect exceptions, disable supervisor ops
3. **68k emulator patches** — Install EmulOp dispatch, entry/exit points
4. **68k code patches** — NVRAM, drivers, memory setup, hardware suppression

## ROM Detection

ROM type detected at offset `0x30d064` via nanokernel ID string:

| String | Type | Machine |
|--------|------|---------|
| "Boot TNT" | ROMTYPE_TNT | Power Mac 7200/8200 |
| "Boot Alchemy" | ROMTYPE_ALCHEMY | Performa 6400 |
| "Boot Zanzibar" | ROMTYPE_ZANZIBAR | Power Mac 9500/8500 |
| "Boot Gazelle" | ROMTYPE_GAZELLE | iMac G3 (early) |
| "Boot Gossamer" | ROMTYPE_GOSSAMER | **Beige G3** (our target) |
| "NewWorld" | ROMTYPE_NEWWORLD | iMac G3 rev B+, G4, G5 |

## ROM Decoding

Some ROMs are compressed. `DecodeROM()` handles three formats:
1. **Plain 4MB image** — Used as-is (ROM_SIZE = 0x400000)
2. **CHRP-BOOT wrapper** — Contains LZSS-compressed data or parcels
3. **LZSS compressed** — Standard sliding-window decompression

Gossamer ROMs are typically plain 4MB images.

## Phase 1: patch_nanokernel_boot()

Reference: `legacy/SheepShaver/src/rom_patches.cpp` lines 738-1050

Patches the boot sequence at ROM offset 0x310000+.

### Step 1: Boot Structure Pointers (0x30d000 + offsets)

```
Offset  Value                    Purpose
0x9c    KernelDataAddr           → LA_InfoRecord
0xa0    KernelDataAddr           → LA_KernelData
0xa4    KernelDataAddr + 0x1000  → LA_EmulatorData
0xa8    ROMBase + 0x480000       → LA_DispatchTable
0xac    ROMBase + 0x460000       → LA_EmulatorCode
0x360   0                        → Physical RAM base
0xfd8   ROMBase + 0x2a           → 68k reset vector
```

### Step 2: SR/BAT/SDR Init Bypass

For Gossamer ROM:
- NOP at 0x310000 (skip SR init)
- Find sr_init pattern at 0x3101b0
- Replace PVR (Processor Version Register) read with load from XLM_PVR (0x281c)
- Load kernel data pointer into R1
- Set cache sizes in R13, R14, R15

### Step 3: CPU-Specific Data Setup

Based on PVR value, write cache/TLB parameters:
- **750 (G3)**: 32KB I-cache, 32KB D-cache, 32-byte lines, 128 TLB entries

### Step 4: Hardware Register Suppression

Patch out dangerous supervisor-mode operations:
- SPRG3 writes → NOP
- MSR reads → NOP
- DEC (decrementer) writes → NOP
- PVR reads → load from XLM_PVR
- SDR1 reads → NOP
- Page table clearing → NOP
- TLB invalidation → NOP

### Step 5: 68k Emulator Entry Point

Find jump-to-68k pattern at 0x314000-0x318000, replace with:
```asm
lwz  r3, 0x0634(r1)    ; Emulator data pointer
lwz  r4, 0x119c(r1)    ; Opcode table pointer
lwz  r0, 0x1184(r1)    ; Emulator init routine
mtctr r0
bctr                    ; Jump to 68k emulator
```

## Phase 2: patch_nanokernel()

Reference: lines 1305-1474

### Mixed Mode Trap
- Disable virt→phys translation: replace page lookup with `mr r31, r27` (identity mapping)

### Exception Table Activation
- PPC exception table → set `XLM_RUN_MODE = MODE_NATIVE`
- 68k exception table → set `XLM_RUN_MODE = MODE_68K`

### FPU State Management
- NOP out MSR FPU enable bit modifications (3+5 NOPs in save/restore)

### DEC Timer
- Skip decrementer setup: `li r31, 0`

### Suspend Suppression
- NOP out FE0F opcode dispatch (power management)

### Trap Return
- Replace `rfi` with `bctr` (branch to CTR)
- Return through 0x318000 trampoline for nested exception handling

## Phase 3: patch_68k_emul()

Reference: lines 1057-1298

### TWI Instruction Replacement

Find TWI (trap word immediate) patterns at 0x36e600-0x36ea00, replace with branch table:
- 0x36f900: Emulator start
- 0x36fa00: Mixed mode entry
- 0x36fb00: Reset/FC1E opcode
- 0x36fc00: FE0A opcode
- 0x36fd00: FE0F opcode

### EMUL_OP Dispatch Table

At ROM address 0x380000 + (opcode << 3), for each EmulOp selector:
- PowerPC instruction or dispatch to handler at 0x366084

### Entry Point Routines

Each saves CPU registers (R7-R13), flags (CR), condition codes, then dispatches.

## Phase 4: patch_68k()

Reference: lines 1481-2350

### Boot Code Patches
- Remove RESET instruction (0x4e70 → 0x4e71 NOP)
- Fake PowerMac ID (return 0x3020 in D0)
- Patch VIA initialization
- Skip RunDiags, get BootGlobs directly

### NVRAM/XPRAM
- Replace ROM NVRAM routines with EMUL_OP dispatch

### Memory Setup
- SysZone at RAM start
- Boot stack at RAM + 4MB
- TimeK fixed value (100)
- Gestalt: page size, CPU type, RAM size

### Gossamer-Specific
- UniversalInfo at 0x12d20 (vs 0x12b70 for Zanzibar)
- Extra NOPs for GC interrupt mask suppression
- SCSI variable initialization
- Force floppy driver installation
- AddrMap table at 0x2fd140

## EmulOp Mechanism for PPC

### SHEEP Opcode Format

```
31                              0
+------+------------------+--+------+------+
| 0x06 |    (unused)      |FN|  OP  |  xx  |
+------+------------------+--+------+------+
 Base: 0x18000000

Encoding:
  xx = 0: EMUL_RETURN (quit emulator)
  xx = 1: EXEC_RETURN (return from Execute68k)
  xx = 2: EXEC_NATIVE (dispatch native op, FN=return via LR)
  xx ≥ 3: EMUL_OP (selector = xx - 3)
```

### EmulOp Register Convention

On EMUL_OP entry, PPC GPRs shadow 68k registers:
```
GPR 8-15  ← d0-d7    (68k data registers)
GPR 16-22 ← a0-a6    (68k address registers)
GPR 1     ← a7       (68k stack pointer)
GPR 24    ← 68k PC
GPR 25    ← 68k SR (interrupt level in MSB)
GPR 31    ← emulator data pointer
```

### EmulOp Selectors (from emul_op.h)

| Selector | Name | Description |
|----------|------|-------------|
| 0 | OP_BREAK | Debugger breakpoint |
| 1-2 | OP_XPRAM1/2 | XPRAM read/write |
| 3-5 | OP_NVRAM1/2/3 | NVRAM operations |
| 6-8 | OP_FIX_MEMTOP/SIZE/BOOTSTACK | Memory layout fixes |
| 9+ | OP_SONY_OPEN/PRIME/CONTROL/STATUS | Floppy driver |
| 13+ | OP_DISK_* | Hard disk driver |
| 17+ | OP_CDROM_* | CD-ROM driver |
| 21+ | OP_SERIAL_* | Serial driver |
| 29 | OP_ADBOP | ADB (mouse/keyboard) |
| 30 | OP_INSTIME | Timer install |
| 33 | OP_MICROSECONDS | Microseconds timer |
| 37 | OP_INSTALL_DRIVERS | Driver installation |
| 38 | OP_NAME_REGISTRY | Name registry patch |
| 39 | OP_RESET | System reset |
| 40 | OP_IRQ | Interrupt dispatch |
| 42-44 | OP_SCSI_DISPATCH/1/2 | SCSI operations |
| 48 | OP_CHECK_SYSV | System version check |
| 49 | OP_NTRB_17_PATCH/etc | Trap table patches |
| 53 | OP_CHECK_LOAD_INVOC | Extension loading |
| 54 | OP_EXTFS_COMM | ExtFS communication |
| 55 | OP_EXTFS_HFS | ExtFS HFS dispatch |
| 56 | OP_IDLE_TIME | Idle detection |

### NATIVE_OP Selectors (from thunks.h)

| Selector | Name | FN | Description |
|----------|------|----|-------------|
| 0 | NATIVE_PATCH_NAME_REGISTRY | 0 | Patch name registry |
| 1 | NATIVE_VIDEO_INSTALL_ACCEL | 1 | Video acceleration |
| 2 | NATIVE_VIDEO_VBL | 1 | 60Hz vertical blank |
| 3 | NATIVE_VIDEO_DO_DRIVER_IO | 1 | Video I/O dispatch |
| 4-10 | NATIVE_ETHER_* | varies | Ethernet operations |
| 11-16 | NATIVE_SERIAL_* | varies | Serial port operations |
| 18-26 | NATIVE_NQD_* | varies | QuickDraw acceleration |
| 27 | NATIVE_CHECK_LOAD_INVOC | 0 | Extension load check |
| 28-31 | NATIVE_GET_RESOURCE etc | 1 | Resource manager |

## Porting Strategy

### What to port directly from SheepShaver

1. ROM decoding (LZSS, parcels) — copy decode_lzss(), decode_parcels()
2. ROM type detection — copy ID string matching
3. All four patch phases — adapt to mac-phoenix's memory access API
4. EmulOp selector dispatch — map to existing mac-phoenix drivers

### What changes from SheepShaver

1. **CPU access**: SheepShaver uses Kheperix CPU class methods. We use Unicorn register read/write.
2. **Memory access**: SheepShaver uses `ReadMacInt32(addr)` / `WriteMacInt32(addr, val)`. We use `g_platform.mem_read_long()` / host pointer arithmetic.
3. **Interrupt delivery**: SheepShaver uses SIGUSR2. We use Unicorn block hook + deferred interrupt.
4. **68k execution**: SheepShaver calls `ppc_cpu->execute_68k()`. We call Unicorn with appropriate register setup.
5. **Kernel data allocation**: SheepShaver uses shmget. We allocate in our existing memory buffer.

### ROM Patch Addresses

All offsets are from ROM base (ROMBase = typically 0x00400000 for SheepShaver, needs verification for our layout):

```
0x2a         68k reset vector
0x30d000+    Boot structures
0x310000+    Nanokernel boot code
0x312a3c     OldWorld interrupt routine
0x312b1c     NewWorld interrupt routine
0x313000+    Mixed mode, exception tables
0x314000+    68k emulator jump point
0x318000     Nested exception trampoline
0x36e600+    TWI entry points
0x36f800+    Extra routines
0x380000+    EMUL_OP dispatch table
0x460000     68k emulator code
0x480000     Dispatch table
```
