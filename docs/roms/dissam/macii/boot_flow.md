# Mac II Boot Flow

Walk of the Mac II ROM (`$97851DB6`, 256 KB, version `$0178`) from
reset to driver install, annotated with labels imported from
`cy384/68k-mac-rom-maps/MacIIROM.lst.txt`. All addresses are physical
post-overlay (`$40800000 + offset`).

> **Also applies to Mac IIx, IIcx, and SE/30** — those machines share
> the same version-`$0178` ROM image (`$97221136`). The shared binary
> is covered separately at `docs/roms/dissam/maciix/`, but the early
> boot flow and patch set are identical down to the addresses because
> version `$0178` means "same code". Hardware differences are dispatched
> at runtime via `WHICHCPU` / `WHICHBOARD` tests.

> **WIP status (2026-04-04).** The Mac II path in
> `src/core/rom_patches.cpp` (`patch_rom_ii`) does not boot to Finder.
> Last known progress: two-phase INSTALL_DRIVERS with SR leak
> compensation (commit `1a085f16`). All 34 patches in `patch_rom_ii`
> are `PATCH-WIP`.

---

## 1. Reset + dispatch header

| Offset | Content | Purpose |
|--------|---------|---------|
| `$0000` | `97 85 1D B6` | Apple checksum |
| `$0004` | `40 80 00 2A` | Initial PC |
| `$0008` | `01 78` | ROM version word |
| `$002A` | trampoline → `$8A` | Reset entry |

The reset vector chain lands at **`STARTINIT1`** (`$4080009A`), which
is much shorter than IIci's because Mac II has no MMU init, no Slot
Manager init (done later), and no IOP manager.

---

## 2. STARTINIT1 (`$4080009A`)

The complete first-phase init:

```
STARTINIT1:
  bsr     INITVIA           ; VIA1: $50F00000 base
  bsr     INITSCC           ; Z8530 at $50F04000 (read) / $50F06000 (write)
  bsr     INITIWM           ; IWM at $50F16000 (Mac II has IWM, IIx has SWIM)
  bsr     INITSCSI          ; NCR 5380 at $50F10000
  bsr     WHICHCPU          ; probe 68020 (or 68030 on IIx)
  movea.l A6,A1             ; A1 = MemTop estimate
  movea.l A6,A0
  suba.w  #$300,A0
  jsr     RAMTEST           ; [PATCH-WIP 1975] walking-ones — NOPed
  movea.l #$50F18000,A3     ; IWM control base probe
  bsr     REV8CHK           ; [PATCH-WIP 1983] Rev-8 hardware check — NOPed
  bne     LAB_408000CC
  movea.l #$50F14000,A3     ; ASC base (Apple Sound Chip)
LAB_408000CC:
  move.l  D7,-(SP)
  moveq   #$28,D0
  bsr     BOOTBEEP          ; [PATCH-WIP 1989] BSR $5E4A — NOPed
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
  bsr     SETUPTIMEK        ; [PATCH-WIP 1998] — see §3
  bsr     VIATIMERENABLES
  jsr     MMU_INIT          ; much simpler than IIci's INITMMU
  movem.l ($F80080),D0/A0   ; check warm-restart via ROM-relative pattern
  ...
  bsr     INITHIMEMGLOBALS
  fall through to BOOTRETRY
```

### STARTINIT1 patches

| Addr | cpp line | Purpose |
|------|----------|---------|
| `$B6` | 1975 | NOP `RAMTEST` (JSR (d16,PC) $35BC, 4 bytes) |
| `$C0` | 1983 | NOP `REV8CHK` (BSR $073E, 4 bytes) — I/O goes to dummy bank |
| `$D0` | 1989 | NOP `BOOTBEEP` / delay (BSR $5E4A, 4 bytes) |
| `$118` | 1998 | `$0560` stores result in `$0D00`; with NOP it stays 0 meaning "no VIA timer" |

---

## 3. SETUPTIMEK (`$40800526`)

Same role as SE's timer calibration — counts DBF iterations against
VIA T2 timeout to populate `TimeDBRA`. Same risk pattern as SE: VIA
writes are no-ops in the emulator, so the 60 Hz tick can fire during
calibration.

The Mac II version is slightly different from SE because VIA1 lives at
`$50F00000` instead of `$EFE1FE`, but the algorithmic structure is the
same. If SE's `tick_inhibit` fix is right, the analogous fix is needed
here. **This has not been attempted yet** — Mac II debugging is behind
SE.

---

## 4. BOOTRETRY (`$40800142`) — high-level init chain

Mac II's BOOTRETRY is the **most aggressively NOPed part of the
entire Mac II path**. Almost every line carries a `PATCH-WIP` comment.

```
BOOTRETRY:
  move    #$2700,SR          ; mask interrupts
  moveq   #1,D0
  movea.l (DAT_DBC),A0       ; indirect init pointer
  jsr     (A0)               ; [PATCH-WIP 2110] NOP MOVEA+JSR (6 bytes)
  movea.l #$50F02000,A0      ; VIA2 base
  move.b  #2,($1C00,A0)      ; VIA2 IER — enable slot interrupts
  bsr     INITGLOBALVARS
  bsr     INITXVECTTABLES    ; exception vectors
  bsr     INITDISPATCHER     ; trap dispatcher install — critical
  moveq   #0,D0              ; [PATCH-WIP 2101] post-decompression shim
  nop                        ; [PATCH-WIP 2103] was SysBeep
  bsr     GETPRAM            ; [PATCH-WIP 2008] reorder with $023E
  bsr     INITMEMMGR         ; [PATCH-WIP 2228] verify order
  bsr     SETUPSYSAPPZONE    ; [PATCH-WIP 2023/2229]
  nop                        ; [PATCH-WIP 2026] was BSR $0464
  bsr     INITRSRCMGR        ; [PATCH-WIP 2029] (kept, was NOPed)
  nop                        ; [PATCH-WIP 2032] was BSR $0C80
  bsr     INITTIMERMGR       ; [PATCH-WIP 2035]
  nop                        ; [PATCH-WIP 2038] was BSR $0C32 INITADBVARS
  bsr     INITSLOTS          ; [PATCH-WIP 2043] keep masked
  move    #$2000,SR          ; drop to level 2
  jsr     INITADB            ; [PATCH-WIP 2050/2162/2231]
  bsr     INITVIDGLOBALS
  ...
  cmpi.l  #'WLSC',(DAT_CFC)
  beq     LAB_408001AE
  jsr     RAMTEST
LAB_408001AE:
  bsr     COMPBOOTSTACK
  ...
  jsr     INITQUEUE          ; [PATCH-WIP 2055] SCSI scan hangs
  jsr     INITSCSIMGR        ; [PATCH-WIP 2058] post-boot startup
  bsr     INITIOMGR          ; [PATCH-WIP 2089] driver install
  bsr     INITCRSRMGR        ; [PATCH-WIP 2091]
  ...
  movea.l #$50F02000,A0
  move.b  #$82,($1C00,A0)    ; VIA2 IER final state
  bsr     DRAWBEEPSCREEN     ; [PATCH-WIP 2063] calls slot manager traps
  move.l  #'WLSC',(DAT_CFC)
  bra     BOOTME
```

### Why Mac II BOOTRETRY has so many NOPs

The Mac II patch strategy fundamentally differs from IIci's. Where
IIci uses large EmulOp trampolines to hook at specific entry points,
Mac II tries to **neuter individual BSRs** because the surrounding
code (memory manager, slot manager) is more tightly coupled and harder
to bypass wholesale. Each NOP is a specific init step we can't safely
run:

- `$0186 INITSLOTS` → NuBus card scan that we don't emulate
- `$018E INITADB` → ADB transceiver protocol we don't fully model
- `$01BE INITQUEUE` → SCSI device scan that hangs without real 5380
- `$01C2 INITSCSIMGR` → post-boot startup hooks
- `$0200 DRAWBEEPSCREEN` → calls `_SVersion`, `_OpenSlot`, `_GetSlotBlock`

The two-phase `INSTALL_DRIVERS` from commit `1a085f16` wires an EmulOp
that replaces a substantial chunk of this sequence with host-side
driver table population.

### The "SR leak compensation" in commit `1a085f16`

The patch at `$018A` (`move #$2000,SR`) drops from interrupt level 7
to level 2. The issue: our EmulOp for INSTALL_DRIVERS runs at a
different time than the stock ROM expects, and the SR was leaking
between phases. The two-phase fix splits install into a phase-1
(before SR change) and phase-2 (after), with the EmulOp reconstructing
the SR state on entry.

---

## 5. SETUPHWBASES equivalent — missing on Mac II

Unlike IIci (which has a dedicated `SETUPHWBASES` routine), Mac II
bakes hardware base addresses directly into STARTINIT1:

- VIA1  = `$50F00000` (MOVEA.L in `INITVIA`)
- VIA2  = `$50F02000` (seen at `$014E`, `$01F4`)
- SCC R = `$50F04000`
- SCC W = `$50F06000`
- IWM   = `$50F16000` (also `$50F18000` probe for rev-8 check at `$BA`)
- SCSI  = `$50F10000`
- ASC   = `$50F14000` (set after rev-8 check)

The hard-coded nature means `rom_patches.cpp` doesn't need to rewrite
a decoder-info table here, which is why `patch_rom_ii` is smaller than
`patch_rom_32`.

---

## 6. Slot Manager init — `INITSLOTS` (`$40800186`)

Mac II has NuBus slots `$9..$E`. `INITSLOTS` walks them looking for
cards with declaration ROMs. We've patched this site (`2043`) to stay
with interrupts masked for now, and rely on the `INSTALL_DRIVERS`
EmulOp to skip Nubus scanning.

No slot manager support means:
- Video cards on NuBus are invisible (IIci uses on-board RBV, Mac II
  relies on a NuBus card).
- Ethernet, SCSI accelerators etc. all invisible.
- Boot must go through an EmulOp-handled disk driver.

---

## 7. Driver install — `INITIOMGR` (`$408001C8`)

Same shape as SE's `INITIOMGR` but without the IIci's `INITIOPMGR`
layer. Walks ROM `DRVR` resources and calls `_Open` on each. The
`INSTALL_DRIVERS` EmulOp from recent commits hooks here (on Mac II)
to substitute our own .Sony/.Sound unit table entries.

---

## 8. Patch site summary — 34 unique in `patch_rom_ii`

Rough categories:

| Category | Offsets | cpp lines |
|----------|---------|-----------|
| Reset shim | `$2A`, `$2E` | 1958–1963 |
| Skip hardware init | `$B6`, `$C0`, `$D0`, `$E8` | 1975–1989 |
| SETUPTIMEK neuter | `$118` | 1998 |
| Indirect init NOPs | `$148`, `$14C` | 2110 |
| BOOTRETRY surgery | `$16A..$186` | 2008–2038 |
| Slot manager skip | `$186`, `$200` | 2043, 2063 |
| ADB bypass | `$18E` | 2050, 2162, 2231 |
| Driver install | `$1C8`, `$1CC` | 2089, 2091 |
| INSTALL_DRIVERS hooks | various | 2089+, two-phase from `1a085f16` |
| SR level-change leak fix | `$18A` | from `1a085f16` |

---

## 9. Mac IIx / IIcx / SE-30 divergence

These machines share the same ROM image (`$97221136`) with a **different
checksum** but **version still `$0178`**. Differences from stock Mac II
ROM (`$97851DB6`):

1. **SWIM floppy controller** instead of IWM → `INITIWM` probes differently
2. **68030 CPU** → `WHICHCPU` returns 2 instead of 1; MMU available
3. **SE/30 has PDS slot** instead of NuBus (IIx has NuBus, IIcx has 3 NuBus)
4. **SE/30 has built-in video** (compact Mac display hardware)
5. **Box ID** via `WHICHBOARD` returns different values

All these are resolved at runtime inside the same ROM code — the
hardware probes dispatch to different code paths. **MacPhoenix
currently maps all of these to `patch_rom_ii`**, which is a
simplification that may hide SE/30-specific issues (especially around
the built-in video).

See `docs/roms/dissam/maciix/rom.lst` for the shared binary. Its
symbols come from **0 ROM-map entries** (no cy384 map exists for
`$97221136`). Consider applying `MacIIROM.lst.txt` symbols to it as a
first-order approximation — the address layout may match for the first
tens of kilobytes.

---

## 10. Current state + blockers

From commit `1a085f16` and project notes:
1. **INSTALL_DRIVERS fires** — the two-phase EmulOp reaches the driver
   install step.
2. **SR leak compensation** fixes the interrupt-level transition.
3. **Slot Manager bypass** — avoids hangs from NuBus scan.
4. **Next blocker**: boot gets past INSTALL_DRIVERS but does not reach
   `BOOTME` / `FINDSTARTUPDEVICE`. The crash point varies; typically a
   stray hardware access in a routine we NOPed only partially.

---

## 11. See also

- Listing: `docs/roms/dissam/macii/rom.lst`
- Patches: `src/core/rom_patches.cpp::patch_rom_ii`
- Shared ROM: `docs/roms/dissam/maciix/rom.lst` (IIx/IIcx/SE30)
- Memory: `memory/project_macii_boot.md`
- SE walkthrough (similar structure, simpler hardware):
  `docs/roms/dissam/se/boot_flow.md`
- IIci walkthrough (more complex, 32-bit-clean): 
  `docs/roms/dissam/iici/boot_flow.md`
