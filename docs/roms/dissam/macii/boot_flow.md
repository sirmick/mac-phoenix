# Mac II Boot Flow

Factual walk of the Mac II ROM (`$97851DB6`, 256 KB, version `$0178`)
from reset through driver install, using the labels imported from
`cy384/68k-mac-rom-maps/MacIIROM.lst.txt`. All addresses are physical
post-overlay (`$40800000 + offset`). Companion listing:
`docs/roms/dissam/macii/rom.lst`.

> **Also applies to Mac IIx, IIcx, and SE/30** — those machines share
> the same version-`$0178` ROM image (`$97221136`, covered separately
> at `docs/roms/dissam/maciix/`). The early boot flow and init
> sequence are identical down to the addresses because version `$0178`
> means "same code"; hardware differences are dispatched at runtime
> via `WHICHCPU` / `WHICHBOARD` tests.

Patching strategy: version `$0178` will use fixed-offset patches
like `patch_rom_classic` ($0276), not the signature-driven approach
from `patch_rom_32` ($067C). Both $0276 and $0178 lack UniversalInfo,
so the approach is the same: fixed-offset patches in the reset-vector
zone plus trap/resource-based lookups for everything else.

---

## 1. Reset + dispatch header

| Offset | Content | Purpose |
|---|---|---|
| `$0000` | `97 85 1D B6` | Apple checksum (also initial SSP) |
| `$0004` | `40 80 00 2A` | Initial PC |
| `$0008` | `01 78`       | ROM version word |
| `$002A` | `jmp thunk` → `$90` | Reset entry (jumps to thunk at $90) |
| `$0090` | `jmp thunk_FUN_4083F856` | Thunk to reset code in high ROM |
| `$0096` | `jmp FUN_40802A14` | SYSERRINIT thunk (not used at reset) |
| `$009A` | `bsr INITVIA` | STARTINIT1 entry |

The reset flow is: `$002A` → `$0090` (thunk) → `$83F856`
(high-ROM init: sets SR=$2700, configures VBR/CACR) → falls through
to **`STARTINIT1`** at `$009A`.  This is shorter than IIci's because
Mac II has no UniversalInfo, no IOP manager, and defers NuBus/Slot
Manager init to BOOTRETRY.

---

## 2. STARTINIT1 (`$4080009A`)

```
STARTINIT1 ($4080009A):
  $009A: bsr  FUN_408006F2   ; INITVIA — VIA1 at $50F00000
  $009E: bsr  FUN_408007B4   ; INITSCC — Z8530 at $50F04000 (R) / $50F06000 (W)
  $00A2: bsr  FUN_408006AA   ; INITIWM — IWM at $50F16000 (Mac II); SWIM on IIx
  $00A6: bsr  FUN_4080066C   ; INITSCSI — NCR 5380 at $50F10000
  $00AA: bsr  FUN_40800532   ; WHICHCPU — probe 68020 (II) / 68030 (IIx/SE30)
  $00AE: movea.l A6,A1       ; A1 = MemTop estimate
  $00B0: movea.l A6,A0
  $00B2: suba.w  #$300,A0
  $00B6: jsr  RAMTEST        ; walking-ones RAM sizing
  $00BA: movea.l #$50F18000,A3  ; IWM rev-check probe base
  $00C0: bsr  REV8CHK        ; Rev-8 IWM / ASC hardware detection
  $00C4: bne  LAB_408000CC
  $00C8: movea.l #$50F14000,A3  ; ASC base (Apple Sound Chip)
  $00CC: move.l  D7,-(SP)
  $00CE: moveq   #$28,D0
  $00D0: bsr  BOOTBEEP       ; startup chime
  ...
  $00E0: cmpi.l  #'WLSC',($CFC).w  ; warm-start magic
  $00E8: beq  LAB_408000F2
  $00EA: jsr  RAMTEST        ; RAMTEST #2 — full walk if cold boot
  $00F2: movea.l A1,SP       ; install boot stack
  $00F4: lea  ($100),A0
  $00F8: lea  ($1E00),A1
  $00FC: bsr  FILLWITHONES   ; clear low-mem globals $100..$1DFF
  $0100: move.b  D7,(CPUFlag)   ; $12F = CPU type from WHICHCPU
  $0104: move.l  A6,(MemTop)    ; $108 = RAMTEST result
  $0108: lea  (6,PC),A6
  $010C: jmp  SYSERRINIT     ; install error stub table
  $0110: bsr  SETUPTIMEK     ; VIA T2 calibration — see §3
  $0114: bsr  VIATIMERENABLES
  $0118: jsr  MMU_INIT       ; simpler than IIci's INITMMU
  ...
  $013E: bsr  INITHIMEMGLOBALS
  ; fall through to BOOTRETRY
```

---

## 3. SETUPTIMEK (`$40800526`)

Same role as SE's timer calibration — counts DBF iterations against
VIA1 T2 timeout to populate `TimeDBRA` (`$0D00`). The Mac II version
lives at a different offset from SE because VIA1 base is different
(`$50F00000` vs `$EFE1FE`), but the algorithmic structure is
identical: disable T2 IRQ via `$1200` write, arm T2 via `$1C00`
write, drop interrupt mask, count DBF iterations.

---

## 4. BOOTRETRY (`$40800142`) — restartable OS init

```
BOOTRETRY ($40800142):
  $0142: move    #$2700,SR          ; mask interrupts
  $0146: moveq   #1,D0
  $0148: movea.l (DAT_DBC),A0       ; indirect init pointer
  $014C: jsr     (A0)               ; dispatches to ROM-resident init vector
  $014E: movea.l #$50F02000,A0      ; VIA2 base
  $0154: move.b  #$02,($1C00,A0)    ; VIA2 IER — enable slot interrupts
  $015A: bsr     FUN_40800300       ; INITGLOBALVARS
  $015E: bsr     FUN_408004B6       ; INITCPUVARS
  $0162: bsr     FUN_40800D0A       ; INITXVECTTABLES — exception vectors
  $0166: jsr     thunk_FUN_4083F7B4 ; INITDISPATCHER — trap dispatcher install
  $016C: nop
  $016E: bsr     FUN_40800E04       ; GETPRAM — read PRAM via VIA
  $0172: bsr     FUN_4080023E       ; INITMEMMGR — _InitZone at $40800242
  $0176: bsr     FUN_40800464       ; SETUPSYSAPPZONE
  $017A: bsr     FUN_40800E22       ; INITRSRCMGR — _InitResources at $40800E2C
  $017E: bsr     FUN_40800E80       ; INITTIMERMGR — _NewPtrSysClear at $40800E82
  $0182: bsr     FUN_408002CA       ; INITADBVARS — _NewPtrSysClear at $408002CE
  $0186: bsr     FUN_40800E32       ; INITSLOTS — NuBus $9..$E scan
  $018A: move    #$2000,SR          ; drop to level 2
  $018E: jsr     FUN_40806D80       ; INITIOMGR — driver install (see §7)
  $0192: bsr     FUN_408007E6       ; INITCRSRMGR
  ...
  $01B8: _SetApplLimit               ; application heap limit
  ...
  $01DA: _SetApplBase                ; application base address
  ...
  $01F4: movea.l #$50F02000,A0
  $01FA: move.b  #$82,($1C00,A0)    ; VIA2 IER final state
  $0200: bsr     DRAWBEEPSCREEN     ; blank screen via slot manager
  $0204: move.l  #'WLSC',($CFC).w  ; warm-start flag
  $020C: bra     BOOTME             ; → $40800E96
```

**A-line traps called during BOOTRETRY init sequence:**

| Address | Trap | Called from |
|---------|------|------------|
| `$40800242` | `_InitZone` | INITMEMMGR — creates SysZone |
| `$408001B8` | `_SetApplLimit` | after INITCRSRMGR |
| `$408001DA` | `_SetApplBase` | zone expansion ($4000 offset) |
| `$40800E2C` | `_InitResources` | INITRSRCMGR — ROM resources |
| `$40800E82` | `_NewPtrSysClear` | INITTIMERMGR ($12 bytes) |
| `$408002CE` | `_NewPtrSysClear` | INITADBVARS ($172 bytes) |

Key milestones inside BOOTRETRY:

- **`INITDISPATCHER`** (`$0166`) — after this returns, A-line traps
  route to the real ROM dispatcher. Earliest point any Toolbox call
  can be issued.
- **`INITMEMMGR`** (`$0172`) — calls `_InitZone` to create SysZone.
  Zone starts small; expanded later by `_SetApplBase` at `$01DA`.
- **`INITSLOTS`** (`$0186`) — walks NuBus slots `$9..$E` reading
  declaration ROMs. Must be NOP'd or short-circuited in the emulator
  (no real NuBus cards).
- **`INITIOMGR`** (`$018E`) — at `$40806D80`, installs the I/O
  dispatcher and opens ROM DRVR resources. This is the hook point
  for host disk driver installation via EmulOp.
- **`DRAWBEEPSCREEN`** (`$0200`) — issues slot manager traps to
  locate video memory on a NuBus card and clear it.

---

## 5. BOOTME (`$40800E96`) — boot device and startup

```
BOOTME ($40800E96):
  $0E96: bsr  FUN_4080151C       ; FINDSTARTUPDEVICE — find boot device
  $0E9A: movea.l ($2A6).w,A0     ; ApplZone
  $0E9E: move.l A0,($118).w      ; store in ApplBase
  $0EA2: move.l A0,($2AA).w      ; BufPtr
  $0EA6: move.l (A0),($114).w
  ...
  $0ED8: _MountVol               ; mount boot volume
  $0EDA: bne  LAB_408011AE       ; error → retry
  ...
  $0F00: _BlockMove              ; copy boot block to low mem
  $0F08: _InitResources          ; open System file resources
  $0F0A: tst.w (SP)+
  $0F0C: bpl  LAB_40800F1C       ; success → continue boot
  $0F0E: ...
  $0F16: _UnmountVol             ; error → unmount, retry
  $0F18: jmp  LAB_408011AE
  ; --- success path ---
  $0F1C: ...
  $0F30: _InitFonts              ; font manager
  $0F38: _OpenResFile            ; open boot resource fork
  ...
  $0F7E: _DrawPicture            ; boot screen (welcome / happy Mac)
  $0F84: _CloseResFile
  ; ... execute startup document, launch Finder
```

**A-line traps called during BOOTME:**

| Address | Trap | Purpose |
|---------|------|---------|
| `$40800ED8` | `_MountVol` | mount boot volume (reads MDB) |
| `$40800F00` | `_BlockMove` | copy boot block to low memory |
| `$40800F08` | `_InitResources` | open System file on boot volume |
| `$40800F16` | `_UnmountVol` | error path: unmount and retry |
| `$40800F30` | `_InitFonts` | font manager init |
| `$40800F38` | `_OpenResFile` | open boot resource fork |
| `$40800F4E` | `_HLock` | lock resource handle |
| `$40800F7E` | `_DrawPicture` | draw welcome screen |
| `$40800F84` | `_CloseResFile` | close resource file |

FINDSTARTUPDEVICE at `$4080151C` walks the drive queue, checks each
drive's status/driver, reads boot blocks, and validates the "LK"
signature. Same structure as the SE version — rejects drives whose
refnum is outside the accepted range.

---

## 7. Hardware base addresses (Mac II)

Unlike IIci (which has a `SETUPHWBASES` routine that reads a
decoder-info table), Mac II bakes hardware base addresses directly
into the early init code:

| Register | Address | Init function |
|----------|---------|---------------|
| VIA1 | `$50F00000` | `INITVIA` at `$408006F2` |
| VIA2 | `$50F02000` | BOOTRETRY at `$014E`, `$01F4` |
| SCC Read | `$50F04000` | `INITSCC` at `$408007B4` |
| SCC Write | `$50F06000` | `INITSCC` |
| IWM | `$50F16000` | `INITIWM` at `$408006AA` |
| IWM rev-check | `$50F18000` | STARTINIT1 at `$00BA` |
| SCSI | `$50F10000` | `INITSCSI` at `$4080066C` |
| ASC | `$50F14000` | STARTINIT1 at `$00C8` (after rev-8 check) |

The direct-baked nature means `patch_rom_ii` needs to redirect these
low-memory globals (`$01D4`-`$0C04`) to ScratchMem via an EmulOp,
same approach as the SE patcher's hardware base redirect.

---

## 8. NuBus slot manager — `INITSLOTS` (`$40800186`)

Mac II has slots `$9..$E` (slots `$0..$8` are reserved). `INITSLOTS`
walks them looking for cards with declaration ROMs at each slot's
base address (`$F9000000`, `$FA000000`, …, `$FE000000`).

On a real Mac II you need at least one NuBus video card for any
visual output (the machine has no on-board video). MAME's reference
environment typically uses `-nbe mdc48` (Apple Display Card 4/8) or
`mdc824` (Display Card 8/24). In an emulator the slot scan must be
short-circuited — either by emulating a video card, or by patching
the equivalent of `nuBusInfoPtr` (if the `$0178` ROM has one; this
is one of the things to verify when porting `patch_rom_ii` from
Basilisk II upstream).

---

## 9. Driver install — `INITIOMGR` (`$40806D80`)

Lives at `$40806D80` (offset `$6D80`). Installs the I/O dispatcher
and opens ROM DRVR resources (.Sony, .Sound, .AIn, .AOut, .ATalk).

```
INITIOMGR ($40806D80):
  $6D80: clr.b  ($21E).w          ; clear flag
  $6D84: bsr    FUN_4080795A      ; setup
  $6D8C: movea.l ($CF8).w,A3      ; driver queue head
  $6D90: lea    ($272,PC),A0      ; dispatcher address
  $6D94: move.l A0,($21A).w      ; install dispatcher
  ...
  $6DCC: bset.b #5,($15D,A3)     ; enable dispatcher
  $6DD2: bsr    FUN_40806DEA      ; init dispatcher tables
  $6DE0: jsr    FUN_408077F4      ; scan ROM DRVRs, _Open each
  $6DE4: jsr    FUN_40807834      ; finalize driver queue
```

This is the hook point for providing a host-backed `.Sony` / `.Disk`
driver via an `INSTALL_DRIVERS` EmulOp at `.Sony _Open` or similar.

---

## 10. Mac IIx / IIcx / SE-30 divergence

The shared `$0178` ROM image (`$97221136`) is loaded by the Mac IIx,
IIcx, II FDHD, and SE/30. Differences from the stock Mac II ROM
(`$97851DB6`):

1. **SWIM floppy controller** instead of IWM → `INITIWM` probes
   differently
2. **68030 CPU** → `WHICHCPU` returns 2 instead of 1; the 68030 PMMU
   is available
3. **SE/30 has a PDS slot** instead of NuBus slots (IIx has NuBus,
   IIcx has 3 NuBus slots, SE/30 has a single PDS slot)
4. **SE/30 has built-in video** (compact Mac display) via
   `se30vrom.uk6`
5. **Box ID** via `WHICHBOARD` returns different values; the ROM
   dispatches to different init paths based on this

All of these are resolved at runtime inside the same ROM code — the
hardware probes in `STARTINIT1` select different code paths. A
future `patch_rom_ii` can use the same signature scan regardless of
which machine is actually running, because the signature targets
shared code, not machine-specific branches.

---

## 11. See also

- `docs/roms/dissam/macii/rom.lst` — build artifact, the annotated listing
- `docs/roms/dissam/maciix/rom.lst` — the shared Mac IIx/IIcx/SE30 ROM
- `docs/roms/dissam/PATCHING_APPROACH.md` — signature-driven strategy
- `docs/roms/dissam/common/hardware_map.md` — MMIO reference
- `docs/roms/dissam/se/boot_flow.md` — simpler, more primitive cousin
- `docs/roms/dissam/iici/boot_flow.md` — more complex, 32-bit clean cousin
- Basilisk II upstream — reference implementation of `patch_rom_ii`
