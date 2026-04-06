# Mac SE Boot Flow

Factual walk of the Mac SE ROM (`$B2E362A8`, 256 KB, version `$0276`)
from power-on reset through driver install, using the labels imported
from `cy384/68k-mac-rom-maps/MacSEROM.lst.txt`. Every ROM address
below refers to the physical post-overlay layout
(`$00400000 + offset`). Companion listing:
`docs/roms/dissam/se/rom.lst` — regenerate from the ROM via
`tools/export_listing.sh se`.

Patching strategy: SE is a version `$0276` ROM, which is handled by
`patch_rom_classic` in `src/core/rom_patches.cpp`. Unlike the
32-bit-clean ROMs, `$0276` does not carry a `UniversalInfo` structure
large enough for signature-driven hardware-base redirection, so
`patch_rom_classic` uses a small set of fixed-offset NOP/EmulOp
patches around the early init routines. See
[`PATCHING_APPROACH.md`](../PATCHING_APPROACH.md) §"patch_rom_classic".

---

## 1. Reset vector

The CPU boots into the ROM overlay (ROM temporarily mapped at
`$00000000`). Byte layout of the overlay:

| File offset | Content | Purpose |
|---|---|---|
| `$0000` | `B2 E3 62 A8` | Apple ROM checksum (matches filename) |
| `$0004` | `00 40 00 2A` | Initial PC (read by CPU reset vector) |
| `$0008–$0029` | JMP table | Early dispatch header |
| `$002A` | first executed byte | trampoline into `STARTINIT1` |

After the CPU latches the initial PC, execution enters `STARTINIT1`
at **`$00400048`** — the real entry point for the init sequence.
(Ghidra's brute-force disassembler may present the header bytes as
garbled instructions; the real control-flow target is `STARTINIT1`.)

---

## 2. STARTINIT1 — cold hardware init (`$00400048`)

The first thing that runs with a sensible environment. It initializes
peripherals in a fixed order before the OS heap exists.  ROM offsets
verified against the disassembly listing.

```
STARTINIT1 ($00400048):
  $48: bsr  FUN_0040052A  ; INITVIA — VIA1: ADB, timers, sys interrupt
  $4C: bsr  FUN_00400598  ; INITSCC — Z8530 serial controller
  $50: bsr  FUN_004004F0  ; INITIWM — IWM floppy controller (patched: NOP)
  $54: bsr  FUN_004004CE  ; INITSCSI — NCR 5380 SCSI
  $58: bsr  FUN_004003EE  ; WHICHCPU — probe 68000 vs 68020
  $5C: movea.l A6,A1      ; A1 = A6 (initial MemTop guess from CPU probe)
  $5E: movea.l A6,A0      ; A0 = A6
  $60: suba.w #$5900,A0   ; A0 = A6 - $5900 (probe range start)
  $64: jsr  FUN_00401D3E  ; RAMTEST #1 — walk A0..A6, size RAM
  $68: moveq #$28,D0
  $6A: bsr  FUN_004029EA  ; BOOTBEEP — startup chime (patched: NOP)
  $6E: movea.l SP,A0      ; save SP
  $70: movea.l #$40000,A1 ; A1 = 256 KB default
  $76: cmpa.l A1,A6       ; if A6 >= 256 KB
  $78: bcc.b $7C          ;   keep A1 = 256 KB
  $7A: movea.l A6,A1      ;   else A1 = actual RAM
  $7C: cmpi.l #'WLSC',($CFC).w  ; warm-start check
  $84: beq.b $8A          ; skip RAMTEST #2 if warm
  $86: jsr  FUN_00401D3E  ; RAMTEST #2 — full walk (confirmation)
  $8A: movea.l A1,SP      ; SP = min(A6, 256 KB) — initial boot stack
  $8C: move.l ($CFC).w,-(SP)  ; save warm-start flag
  $90: lea ($100).w,A0    ; A0 = $100
  $94: lea ($1600).w,A1   ; A1 = $1600
  $98: bsr  FUN_0040017E  ; FILLWITHONES — fill $100..$15FF with $FFFFFFFF
  $9C: move.l (SP)+,($CFC).w  ; restore warm-start flag
  $A0: move.b D7,($12F).w ; CPUFlag = D7 (CPU type from WHICHCPU)
  $A4: move.l A6,($108).w ; MemTop = A6 (RAMTEST result)
  $A8: lea (6,PC),A6      ; A6 = $B0 (return addr for SYSERRINIT)
  $AC: jmp  FUN_00401284  ; SYSERRINIT — install error handler table
  $B0: bsr  FUN_0040041C  ; SETUPTIMEK — VIA Timer 2 calibration (see §3)
  $B4: bsr  FUN_0040054E  ; VIATIMERENABLES
  ...                     ; test for diagnostic ROM, then fall into BOOTRETRY
```

**Note:** FILLWITHONES at `$98` fills `$100`-`$15FF` with `$FFFFFFFF`
but preserves the warm-start flag at `$CFC` via push/pop.  MemTop
(`$108`) is overwritten by the fill, then restored at `$A4` from A6.

---

## 3. SETUPTIMEK (`$0040041C`) — timer calibration

Measures how many 68000 DBF-loop iterations happen in one VIA Timer 2
tick, so `Microseconds()` and other time-sensitive code can calibrate
itself. Stores the count at `TimeDBRA` (`$0D00`).

```
SETUPTIMEK:
  move   SR,-(SP)          ; save interrupt mask
  move.l ($0064),-(SP)     ; save vector table entry 0x64
  movea.l #$EFE1FE,A1      ; A1 = VIA1 base (byte address)
  bclr.b #5,($1600,A1)     ; clear VIA1 IFR bit 5
  move.b #$FF,($1200,A1)   ; VIA1 T2 counter low — disables T2 IRQ
  move.b #$A0,($1C00,A1)   ; VIA1 IER — arm T2 enable
  andi   #$F8FF,SR         ; enable interrupts
  ...                      ; DBF loop — count iterations
  move.w D0,(TimeDBRA).w   ; store result at $0D00
```

On real hardware the writes to `$1200`/`$1C00` discipline VIA1's
timer-2 interrupt source before the CPU enables interrupts, so the
calibration loop runs uninterrupted. On an emulator without a
fully-modelled VIA, this region deserves careful attention: either
the VIA must honor the writes, or the downstream code must handle
`TimeDBRA == 0` (which is a legal state meaning "no timer").

---

## 4. BOOTRETRY (`$004000D2`) — restartable OS init

Designed to be re-entered on non-fatal failures (corrupted PRAM,
failed boot device, etc.), which is why it's split from `STARTINIT1`.
ROM offsets verified against the disassembly listing.

```
BOOTRETRY ($004000D2):
  $D2: move #$2700,SR             ; disable interrupts
  $D6: bsr  FUN_00400262          ; INITGLOBALVARS — sets hardware bases
                                  ;   (VIA $01D4, SCC $01D8/$01DC, IWM $01E0,
                                  ;    SCSI $0C00/$0C04), ROMBase ($02AE),
                                  ;    clears event/timer globals
  $DA: bsr  FUN_00400396          ; INITXVECTTABLES — exception vectors
  $DE: bsr  FUN_004006DA          ; INITDISPATCHER — trap table install (see §5)
  $E2: bsr  FUN_00400358          ; GETPRAM — read parameter RAM via ClkNoMem
  $E6: bsr  FUN_004007CA          ; Sets Lo3Bytes ($031A), MinStack ($031E),
                                  ;   DefltStack ($0322=$2000), MMDefFlags ($0326)
  $EA: bsr  FUN_0040019C          ; SETUPSYSAPPZONE:
                                  ;   lea ($2E,PC),A0    → A0 = $4001CC (params)
                                  ;   _InitZone ($A019)  → zone at $1600, limit $2E00
                                  ;   SysZone ($02A6) = TheZone ($0118)
                                  ;   ApplZone ($02AA) = SysZone
                                  ;   HeapEnd ($0114) = bkLim
                                  ;   COMPBOOTSTACK: A0 = MemTop/2 - $400
                                  ;   _SetApplLimit ($A02D) → A0 - $2000
  $EE: bsr  FUN_00400344          ; INITSWITCHERTABLE
  $F2: bsr  FUN_004007E8          ; INITRSRCMGR — reads KCHR, KMAP from ROM
  $F6: bsr  FUN_004007F8          ; INITTIMERMGR
  $FA: bsr  FUN_00400228          ; INITADBVARS — ADB globals + trap vector
  $FE: move #$2000,SR             ; enable interrupts (level-2 mask)
 $102: jsr  FUN_00403306          ; INITADB — scan ADB bus (patched: NOP at $3364)
 $106: bsr  FUN_004005CA          ; INITVIDGLOBALS — ScrnBase, ScreenRow, etc.
 $10A: movea.l SP,A0              ; save current SP
 $10C: movea.l ($10C).w,A1        ; A1 = BufPtr ($3FA700)
 $110: movea.l ($108).w,A6        ; A6 = MemTop ($400000)
 $114: cmpi.l #'WLSC',($CFC).w   ; warm-start magic check
 $11C: beq.b  $122                ; warm start → skip RAMTEST
 $11E: jsr  FUN_00401D3E          ; RAMTEST (cold start: walk RAM, set MemTop)
 $122: bsr  FUN_0040018E          ; COMPBOOTSTACK: A0 = MemTop/2 - $400
 $126: movea.l A0,SP              ; SP = A0 ($1FFC00 for 4MB)
 $128: suba.w #$2000,A0           ; A0 -= 8KB = $1FDA00
 $12C: (2 bytes)                  ; likely: move.l A0,($130).w → ApplLimit
 $12E: lea ($308).w,A1            ; A1 = DrvQHdr
 $132: jsr  FUN_00402B24          ; INITIOMGR — driver table + INSTALL_DRIVERS
                                  ;   EmulOp at $36CAA fires here
 $136: jsr  FUN_0041A19C          ; (additional driver init)
 $13C: bsr  FUN_0040076E          ; INITCRSRMGR — unit table + cursor
 $140: bsr  FUN_004005F6          ; (screen init)
 $144: movea.l ($2A6).w,A0        ; A0 = SysZone
 $148: movea.l (A0),A0            ; A0 = bkLim
 $14A: adda.w #$4000,A0           ; A0 = bkLim + 16 KB (zone expansion)
 $14E: _SetApplBase               ; $A057 — expand zone limit
 $150: move.l ($2A6).w,($118).w   ; TheZone = SysZone
 $156: lea ($400,SP),A6           ; A6 = SP + $400
 $15A: lea ($190,SP),A5           ; A5 = SP + $190
 $15E: bsr  FUN_004001DA          ; _InitZone for application zone
 $162: move.l #'WLSC',($CFC).w   ; set warm-start magic
 $16A: bra  FUN_0040080A          ; → BOOTME
```

**Key insight:** The system zone is created at `$EA` with a **hardcoded
6 KB limit** ($1600-$2E00) from ROM data at `$4001CC`. It is expanded
by 16 KB at `$14A` (after INITIOMGR), reaching ~22 KB. Boot block code
is responsible for further expansion via `_SetApplLimit` / `_MaxApplZone`.

---

## 5. INITDISPATCHER (`$004006DA`) — trap dispatcher install

This is the pivotal routine where the A-line trap dispatcher becomes
live. **Before** `INITDISPATCHER` returns, A-line traps (`$Axxx`
opcodes — the Toolbox API) hit exception stubs in `SYSERRINIT` that
all jump to `TODEEPSHIT`. **After** it returns, vector `$28` points
to the real ROM trap dispatcher at `$00402CD6`, and traps route to
their handlers.

Any emulator hook that needs to run trap-dispatching code must install
no earlier than the return from `INITDISPATCHER`, or it will hit the
pre-dispatch stubs and crash.

The routine itself copies a compressed trap-offset table into the
trap dispatch tables at `$0E00` (OS traps) and `$0400` (Toolbox
traps — only 256 entries on SE because SE ships with <1 MB RAM).

---

## 6. BOOTME (`$00400756`) — high-level boot

After `BOOTRETRY`, `BOOTME` runs the high-level OS boot:

```
BOOTME
  → MOUSEINIT          ; $004010A2 - set up cursor
  → INITFS             ; $004010C2 - File Manager init
  → INITEVENTS         ; $00401168 - Event Manager
  → FINDSTARTUPDEVICE  ; $00401189 - locate boot disk
  → LOADDRIVERS        ; $004012D4 - load non-ROM drivers
  → (reads boot blocks, launches System/Finder)
```

`FINDSTARTUPDEVICE` is where emulator disk drivers (such as an
`INSTALL_DRIVERS` EmulOp backing `.Sony` / `.Disk`) take over from
ROM-resident drivers. `patch_rom_classic` installs the EmulOp at the
`.Sound` driver's `_Open` routine at `$36CAA`; when that EmulOp fires,
it installs the host disk-backed drivers into the Unit Table before
the OS starts scanning for a boot volume.

---

## 7. Hardware base addresses (Mac SE)

Mac SE is 24-bit only; all hardware lives in the bottom 16 MB.

| Device   | Base          | Notes |
|----------|---------------|-------|
| VIA1     | `$00EFE1FE`   | ADB, PRAM, 60 Hz timer, sound |
| SCC ch A | `$009FFFF8`   | modem port (read) |
| SCC ch B | `$00BFFFF9`   | printer port (write) |
| IWM      | `$00DFE1FF`   | floppy |
| SCSI     | `$005FF000`   | NCR 5380 |
| ASC      | `$00E80000`   | Apple Sound Chip (SE has ASC) |

The VIA register layout is offset by `$200` per register index (e.g.
`vT2CL` at `+$1000`, `vT2CH` at `+$1200`, `vIER` at `+$1C00`). Because
the SE's base is the byte address `$00EFE1FE`, all VIA register
accesses end at odd byte boundaries — that's not a bug.

---

## 8. See also

- `docs/roms/dissam/se/rom.lst` — build artifact, the annotated listing
- `docs/roms/dissam/PATCHING_APPROACH.md` — why and how to patch these ROMs
- `docs/roms/dissam/common/hardware_map.md` — cross-machine MMIO reference
- `src/core/rom_patches.cpp::patch_rom_classic` — the SE patch function
- `cy384/68k-mac-rom-maps/MacSEROM.lst.txt` — upstream source of the labels
