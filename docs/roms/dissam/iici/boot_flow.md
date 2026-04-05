# Mac IIci Boot Flow

Factual walk of the Mac IIci ROM (`$368CADFE`, 512 KB, version `$067C`
— the "Universal 32-bit clean" ROM) from reset through driver install,
using the labels imported from `cy384/68k-mac-rom-maps/MacIIciROM.lst.txt`.
All ROM addresses are physical post-overlay (`$40800000 + offset`).
Companion listing: `docs/roms/dissam/iici/rom.lst`.

Patching strategy: IIci is a version `$067C` ROM, handled by the
signature-driven `patch_rom_32` function in
`src/core/rom_patches.cpp`. See
[`PATCHING_APPROACH.md`](../PATCHING_APPROACH.md) for the full
strategy — the short version is "locate `UniversalInfo` by byte
signature, patch the ROM's own data structures rather than
NOP-ing its code".

---

## 1. Reset vector

| Offset | Content | Purpose |
|---|---|---|
| `$0000` | `36 8C AD FE` | Apple ROM checksum (matches filename) |
| `$0004` | `40 80 00 2A` | Initial PC |
| `$0008..$0029` | JMP-table header | Dispatch offsets to init phases |
| `$002A` | `jmp $4080008c` | Cold-init trampoline |
| `$008C` | Real cold-init entry | |

Bytes starting at `$8C`:

```
40800090  move    #$2700,SR      ; mask all interrupts
40800094  reset                   ; 68K hardware reset instruction
40800096  move.l  #$2000,D0       ; cache enable value
4080009C  movec   D0,CACR         ; enable 68030 instruction cache
408000A0  movec   CACR,D0         ; verify
408000A4  tst.l   D0
408000A6  beq     $408000AC
408000A8  lea     (-$1C,PC),A0
408000AC  pmove.l (A0),TC         ; load MMU translation control
```

The reset vector chain is identical across the `$067C` ROM family
(Mac II rev B, IIci, IIsi, IIvx, Quadra, …) because Apple's ROM
header format is fixed. `patch_rom_32` installs its reset shim at
offset `$8C` here, replacing the `move #$2700,SR` with an `EMUL_OP_RESET`
followed by `JMP $BA`.

---

## 2. STARTINIT1 (`$408000B8`) — cold hardware init

This is where real hardware would initialize. Structure:

```
STARTINIT1:
  moveq    #0,D2
  movem.l  D5-D7/A5-A6,-(SP)
  lea      (6,PC),A6
  jmp      GETHARDWAREINFO       ; $40802F18 — probe box/CPU/memory
  movea.l  (8,A0),A4              ; A4 = decoderInfoPtr
  moveq    #$40,D4
  and.b    (A4),D4
  lea      (6,PC),A6
  jmp      INITVIAS               ; $40802E8C — configure VIA1/VIA2
  ...
  bsr      WHICHCPU               ; probe 68020 / 68030 / 68040
  bsr      WHICHBOARD             ; read box ID
  bsr      CONFIGURERAM           ; walk RAM rows
  bsr      INITMMU                ; $408042FE — 68030 PMMU setup
  movea.l  (-$14,A4),A6           ; BootGlobs pointer
  movea.l  A4,A5
  adda.l   (-$C,A4),A5
  ...
  bsr      SETUPHWBASES           ; populate LMG hw base table
  bsr      INITSCC                ; $40800A2E
  bsr      INITIWM                ; $408009C0 (SWIM on IIci)
  bsr      INITSCSI               ; $408009A0
  ...
  bsr      SETUPHWBASES           ; second pass after MMU on
  move.b   D7,(CPUFlag)
  ...
  bsr      INITMMUGLOBALS
  jmp      SYSERRINIT
  move.l   #$3919,D0
  movec    D0,CACR                ; enable data cache
  bsr      ENABLEEXTCACHE         ; IIci L2 cache
  ...
  bsr      SETUPTIMEK
  bsr      INITHIMEMGLOBALS       ; ≥ 1 MB memory globals
```

### The `UniversalInfo` structure — central to this ROM family

When `GETHARDWAREINFO` runs, it reads a static `UniversalInfo`
structure embedded in the ROM. Every field the ROM needs to know
about the machine is in this structure:

| Offset from UniversalInfo | Field | Purpose |
|---|---|---|
| `+0`  | `decoderInfoPtr` | offset to the hardware-base address table |
| `+12` | `nuBusInfoPtr`   | NuBus slot presence / interrupt map |
| `+18` | `productKind`    | model ID (Mac II, IIci, IIsi, Quadra 650, …) |
| `+22` | `defaultRSRCs`   | FPU/MMU default presence flags |

The `decoderInfoPtr` points to a table of per-hardware-device address
slots (VIA1, VIA2, SCC R/W, SCSI, IWM/SWIM, ASC, RBV, …). A separate
table at ROM offset `$94A` lists, for each slot, which low-memory
global gets populated with the resolved address. The ROM reads these
at runtime to discover where its hardware lives.

**This is what `patch_rom_32` exploits.** Instead of NOP-ing the
hardware probes, it finds `UniversalInfo` by byte signature and
rewrites the decoder info slots so every device address points at
`ScratchMem` — a benign host-allocated buffer. The ROM's init then
runs normally, configures "hardware" that reads back zeros, and
continues into the OS.

---

## 3. INITMMU (`$408042FE`) — 68030 PMMU setup

IIci is 68030, so the PMMU is real. `INITMMU` builds an identity-ish
address translation map so the 32-bit ROM can also run 24-bit-clean
Macintosh code:

```
INITMMU:
  link.w   A5,#-$74
  bsr      FUN_40804392           ; read RAM config from CPUFlag
  bsr      FUN_40804538           ; build translation control word
  cmpi.b   #1,(-$1A,A6)
  bne      LAB_4080431E
  ; single-bank fast path
  ...
LAB_4080431E:
  ; multi-bank: build segment + page tables
  bsr      FUN_4080463E           ; fill segment table
  bsr      FUN_4080476C           ; fill page table
  bsr      FUN_40804480
  bsr      FUN_408043FE
  pmove.d  (-$8,A3),CRP           ; load CPU root pointer
  pflusha                          ; flush MMU TLB
  movec    CACR,D5
  ori.w    #$808,D5
  movec    D5,CACR                 ; enable data cache + burst
  jmp      A6
```

`INITMMU` is one of the routines `patch_rom_32` does *not* replace
inline — it's signature-scanned via `init_mmu_dat` / `init_mmu2_dat`
/ `init_mmu3_dat` and only individual instructions are NOPed to
bypass features the emulator can't support (MMU-present check on
unknown CPU, RBV probe, full MMU init on 68040/060).

---

## 4. BOOTRETRY (`$408001A6`) — restartable OS init

```
BOOTRETRY:
  move     #$2700,SR
  moveq    #1,D0
  movea.l  (DAT_DBC),A0
  jsr      (A0)                   ; indirect init pointer
  jsr      INITGLOBALVARS
  jsr      $4080A03E
  jsr      $40809F56
  jsr      $40809C9C
  bsr      INITMMUTRAP             ; install _HWPriv MMU traps
  bsr      GETPRAM                 ; read parameter RAM
  bsr      INITMEMMGR              ; create SysZone
  ...
  bsr      SETUPSYSAPPZONE
  bsr      INITSWITCHERTABLE
  bsr      INITRSRCMGR             ; scans ROM DRVR/CODE resources
  jsr      $4081CA60               ; resource scan
  jsr      $4080AEBC
  bsr      INITSHUTDOWNMGR         ; IIci-specific
  jsr      $40800FF8
  bsr      INITDTQUEUE             ; Deferred Task Queue
  ...
  jsr      $40809D6A               ; EnableParityPatch / Enable60HzInts
  move     #$2000,SR               ; drop to level 2
  bsr      INITVIDGLOBALS
  bsr      COMPBOOTSTACK
  ...
  bsr      INITIOMGR               ; driver install — see §6
  bsr      INITCRSRMGR
  ...
  bra      BOOTME
```

---

## 5. INITSLOTMGR (`$40805E20`) — NuBus slot scan

IIci has 3 NuBus slots (`$C..$E`) plus an on-board pseudo-slot for
the RBV video controller. `INITSLOTMGR` walks slots `$E..$1` looking
for valid declaration ROMs:

```
INITSLOTMGR:
  movem.l  D1/A1-A2,-(SP)
  bsr      FUN_40805E48            ; seed slot iteration state
  moveq    #$E,D1                  ; start at slot $E
LAB_40805E2A:
  move.b   D1,($31,A0)
  moveq    #$2F,D0
  movea.l  (A0),A1                 ; A1 = slot base
  tst.w    (4,A1)                  ; probe declaration ROM
  bmi      LAB_40805E3E            ; no card
  bsr      FUN_40805F2A            ; init card
LAB_40805E3E:
  dbf      D1,LAB_40805E2A
  movem.l  (SP)+,D1/A1-A2
  rts
```

On the golden path, `patch_rom_32` patches `nuBusInfoPtr` to mark all
slots empty before `INITSLOTMGR` runs, so the scan finds nothing and
returns cleanly. The RBV pseudo-slot is a separate concern that
Basilisk does not expose because it would require video emulation.

---

## 6. INITIOMGR + INITIOPMGR (`$408010F0`) — driver install

IIci layers two levels of I/O manager init:

1. **`INITIOMGR`** — classic driver table install. Walks the ROM's
   `DRVR` resource list and calls `_Open` on each, populating the
   Unit Table. Same shape as the Mac II / SE variants.

2. **`INITIOPMGR`** — IIci-specific. Initializes the Apple SWIM II
   and ADB Transceiver as IOP-accelerated devices.

`patch_rom_32`'s `INSTALL_DRIVERS` EmulOp hooks the driver install
step by finding the `.Sony` DRVR resource (`find_rom_resource('DRVR',
4)`), overwriting its entry with the host-backed driver stubs
(`sony_driver[]`, `disk_driver[]`, `cdrom_driver[]`), and letting
the ROM's own driver install wire them into the Unit Table
normally.

---

## 7. OPENSDRVR (`$40800CA0`) — per-driver open helper

Called by `INITIOMGR` for each DRVR resource. Allocates a Unit Table
entry and invokes the driver's `DRVROpen`. Control returns here from
every driver's open routine.

After `INITIOMGR` completes, `BOOTRETRY` falls through into boot
volume selection, File Manager init, and eventually Finder launch.

---

## 8. Hardware bases (Mac IIci)

| Device | Base | Notes |
|---|---|---|
| VIA1   | `$50F00000` | ADB, PRAM, PM-FPU interrupt |
| VIA2   | `$50F02000` | NuBus, RBV interrupts, Ethernet |
| SCC R  | `$50F04000` | Serial read |
| SCC W  | `$50F06000` | Serial write |
| SCSI   | `$50F10000` | NCR 5380 |
| SCSI DMA | `$50F12060` | pseudo-DMA channel |
| ASC    | `$50F14000` | Apple Sound Chip |
| SWIM   | `$50F16000` | IIci uses SWIM, not IWM |
| RBV    | `$50F24000` | on-board video controller |
| PRAM   | via VIA1    | 256 bytes battery-backed |

The `UniversalInfo.decoderInfoPtr` table lists these in a
machine-readable form; the ROM's own `SETUPHWBASES` at `$40800910`
walks that table and publishes each base into the corresponding
low-memory global (using the `(bit, lmg_offset)` table at ROM offset
`$94A`). `patch_rom_32` walks the same `$94A` table and overwrites
each decoder info slot with `ScratchMem` before `SETUPHWBASES` runs,
so the LMGs end up pointing at benign memory.

---

## 9. See also

- `docs/roms/dissam/iici/rom.lst` — build artifact, annotated listing
- `docs/roms/dissam/PATCHING_APPROACH.md` — the signature-driven strategy
- `docs/roms/dissam/common/hardware_map.md` — MMIO reference
- `src/core/rom_patches.cpp::patch_rom_32` — canonical patch function
- `cy384/68k-mac-rom-maps/MacIIciROM.lst.txt` — upstream source of labels
