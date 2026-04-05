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

Patching strategy: version `$0178` is the gap in the current
`src/core/rom_patches.cpp` coverage. Neither `patch_rom_classic` (for
`$0276`) nor `patch_rom_32` (for `$067C`) handles it. See
[`PATCHING_APPROACH.md`](../PATCHING_APPROACH.md) §"What's missing
today" for the recommended path: port Basilisk II's signature-driven
`patch_rom_ii` rather than hand-crafting per-address patches.

---

## 1. Reset + dispatch header

| Offset | Content | Purpose |
|---|---|---|
| `$0000` | `97 85 1D B6` | Apple checksum |
| `$0004` | `40 80 00 2A` | Initial PC |
| `$0008` | `01 78`       | ROM version word |
| `$002A` | trampoline → `$8A` | Reset entry |

The reset vector chain lands at **`STARTINIT1`** (`$4080009A`), which
is much shorter than IIci's because Mac II has no MMU init, no Slot
Manager init in the early chain (deferred to BOOTRETRY), and no IOP
manager layer.

---

## 2. STARTINIT1 (`$4080009A`)

```
STARTINIT1:
  bsr     INITVIA           ; VIA1: $50F00000 base
  bsr     INITSCC           ; Z8530 at $50F04000 (R) / $50F06000 (W)
  bsr     INITIWM           ; IWM at $50F16000 (Mac II has IWM; IIx has SWIM)
  bsr     INITSCSI          ; NCR 5380 at $50F10000
  bsr     WHICHCPU          ; probe 68020 (or 68030 on IIx/IIcx/SE30)
  movea.l A6,A1             ; A1 = MemTop estimate
  movea.l A6,A0
  suba.w  #$300,A0
  jsr     RAMTEST           ; walking-ones RAM sizing
  movea.l #$50F18000,A3     ; IWM rev-check probe base
  bsr     REV8CHK           ; Rev-8 IWM / ASC hardware detection
  bne     LAB_408000CC
  movea.l #$50F14000,A3     ; ASC base (Apple Sound Chip)
LAB_408000CC:
  move.l  D7,-(SP)
  moveq   #$28,D0
  bsr     BOOTBEEP
  ...
  cmpi.l  #'WLSC',(DAT_CFC)  ; warm-start magic
  beq     LAB_408000F2
  jsr     RAMTEST
LAB_408000F2:
  movea.l A1,SP             ; install boot stack
  lea     ($100),A0
  lea     ($1E00),A1
  bsr     FILLWITHONES      ; clear low-mem globals
  move.b  D7,(CPUFlag)
  move.l  A6,(MemTop)
  lea     (6,PC),A6
  jmp     SYSERRINIT        ; install error stub table
  bsr     SETUPTIMEK        ; VIA T2 calibration — see §3
  bsr     VIATIMERENABLES
  jsr     MMU_INIT          ; much simpler than IIci's INITMMU
  movem.l ($F80080),D0/A0   ; check warm-restart via ROM-relative pattern
  ...
  bsr     INITHIMEMGLOBALS
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
BOOTRETRY:
  move    #$2700,SR          ; mask interrupts
  moveq   #1,D0
  movea.l (DAT_DBC),A0       ; indirect init pointer
  jsr     (A0)               ; dispatches to a ROM-resident init vector
  movea.l #$50F02000,A0      ; VIA2 base
  move.b  #$02,($1C00,A0)    ; VIA2 IER — enable slot interrupts
  bsr     INITGLOBALVARS
  bsr     INITXVECTTABLES    ; exception vectors
  bsr     INITDISPATCHER     ; trap dispatcher install — critical
  moveq   #0,D0              ; post-decompression state
  bsr     GETPRAM            ; read PRAM via VIA
  bsr     INITMEMMGR         ; create SysZone
  bsr     SETUPSYSAPPZONE
  bsr     INITSWITCHERTABLE
  bsr     INITRSRCMGR        ; Resource Manager — scans ROM DRVR/CODE
  bsr     INITTIMERMGR
  bsr     INITADBVARS        ; ADB Manager globals
  bsr     INITSLOTS          ; NuBus slot $9..$E scan
  move    #$2000,SR          ; drop to level 2
  jsr     INITADB            ; scan ADB bus
  bsr     INITVIDGLOBALS
  ...
  cmpi.l  #'WLSC',(DAT_CFC)
  beq     LAB_408001AE
  jsr     RAMTEST
LAB_408001AE:
  bsr     COMPBOOTSTACK
  ...
  jsr     INITQUEUE          ; I/O queue
  jsr     INITSCSIMGR        ; SCSI Manager
  bsr     INITIOMGR          ; .Sony/.Sound/.AIn/.AOut unit table install
  bsr     INITCRSRMGR
  ...
  movea.l #$50F02000,A0
  move.b  #$82,($1C00,A0)    ; VIA2 IER final state
  bsr     DRAWBEEPSCREEN     ; blank screen via slot manager / video card
  move.l  #'WLSC',(DAT_CFC)
  bra     BOOTME
```

Key milestones inside BOOTRETRY:

- **`INITDISPATCHER`** — after this returns, A-line traps route to
  the real ROM dispatcher. This is the earliest point any Toolbox
  call can be issued.
- **`INITSLOTS`** — walks NuBus slots `$9..$E` reading declaration
  ROMs. Requires real Nubus cards (or a patched `nuBusInfoPtr` that
  marks all slots empty, as in the `patch_rom_32` approach).
- **`INITIOMGR`** — walks ROM `DRVR` resources and calls `_Open` on
  each. This is the hook point where a host disk driver can be
  installed via an EmulOp at `.Sony _Open` or similar.
- **`DRAWBEEPSCREEN`** — issues slot manager traps (`_SVersion`,
  `_OpenSlot`, `_GetSlotBlock`) to locate video memory on a NuBus
  video card and clear it. This is where the boot chime + gray screen
  appear on real hardware.

---

## 5. Hardware base addresses (Mac II)

Unlike IIci (which has a `SETUPHWBASES` routine that reads a
decoder-info table), Mac II bakes hardware base addresses directly
into the early init code:

- VIA1  = `$50F00000` (baked into `INITVIA`)
- VIA2  = `$50F02000` (seen at `$014E`, `$01F4`)
- SCC R = `$50F04000`
- SCC W = `$50F06000`
- IWM   = `$50F16000`
- IWM rev-check probe = `$50F18000` at `$BA`
- SCSI  = `$50F10000`
- ASC   = `$50F14000` (set after rev-8 check)

The direct-baked nature means a future `patch_rom_ii` either needs
to find and NOP the bases where they're loaded (there are only about
half a dozen sites), **or** scan for the LMG table that `INITVIA`
populates from these bases and redirect it — whichever mirrors
Basilisk II upstream more closely.

---

## 6. NuBus slot manager — `INITSLOTS` (`$40800186`)

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

## 7. Driver install — `INITIOMGR` (`$408001C8`)

Same shape as SE's `INITIOMGR`. Walks the ROM's `DRVR` resource list,
for each:
1. Allocate a Unit Table entry
2. Call the driver's `DRVROpen` routine
3. Populate the DCE (Device Control Entry) at `$0134`

This is where `.Sony` (floppy), `.Sound`, `.AIn`, `.AOut` (ADB),
and `.ATalk` (AppleTalk) get their unit table entries. For emulator
purposes this is the hook point for providing a host-backed `.Sony`
/ `.Disk` driver via an `INSTALL_DRIVERS` EmulOp.

---

## 8. Mac IIx / IIcx / SE-30 divergence

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

## 9. See also

- `docs/roms/dissam/macii/rom.lst` — build artifact, the annotated listing
- `docs/roms/dissam/maciix/rom.lst` — the shared Mac IIx/IIcx/SE30 ROM
- `docs/roms/dissam/PATCHING_APPROACH.md` — signature-driven strategy
- `docs/roms/dissam/common/hardware_map.md` — MMIO reference
- `docs/roms/dissam/se/boot_flow.md` — simpler, more primitive cousin
- `docs/roms/dissam/iici/boot_flow.md` — more complex, 32-bit clean cousin
- Basilisk II upstream — reference implementation of `patch_rom_ii`
