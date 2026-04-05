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
peripherals in a fixed order before the OS heap exists.

```
STARTINIT1:
  bsr  INITVIA        ; VIA1: ADB, timers, system interrupt source
  bsr  INITSCC        ; Z8530 SCC — serial controller
  bsr  INITIWM        ; Integrated Woz Machine (floppy controller)
  bsr  INITSCSI       ; NCR 5380 SCSI controller
  bsr  WHICHCPU       ; Probe 68000 vs 68020 (SE is 68000)
  ...                 ; stack setup, A6 = MemTop guess
  jsr  RAMTEST        ; Walking-ones RAM sizing
  bsr  BOOTBEEP       ; Startup chime
  ...
  jsr  RAMTEST        ; Second pass
  lea  ($100),A0
  lea  ($1600),A1
  bsr  FILLWITHONES   ; Zero/fill low-memory globals region
  ...
  lea  (6,PC),A6
  jmp  SYSERRINIT     ; Install system error trap handler table
  bsr  SETUPTIMEK     ; Timer calibration (see §3)
  bsr  VIATIMERENABLES
  ...                 ; Fall into BOOTRETRY
```

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

```
BOOTRETRY:
  move #$2700,SR             ; disable interrupts for init
  bsr  INITGLOBALVARS        ; zero/init LowMemGlobals
  bsr  INITXVECTTABLES       ; exception vectors $00..$3FF
  bsr  INITDISPATCHER        ; trap dispatcher install (see §5)
  bsr  GETPRAM               ; read parameter RAM via VIA
  bsr  INITMEMMGR            ; Memory Manager — creates SysZone
  bsr  SETUPSYSAPPZONE       ; ApplZone in SysZone
  bsr  INITSWITCHERTABLE     ; multitasking stub
  bsr  INITRSRCMGR           ; Resource Manager — reads ROM resources
  bsr  INITTIMERMGR          ; Time Manager
  bsr  INITADBVARS           ; ADB Manager globals
  move #$2000,SR             ; interrupts back on (level-2 mask)
  jsr  INITADB               ; scan ADB bus for keyboard/mouse
  bsr  INITVIDGLOBALS
  ...
  cmpi.l #'WLSC',(DAT_CFC)   ; warm-start magic check
  beq    LAB_00400122        ; warm start: skip big RAM test
  jsr    RAMTEST             ; cold start: full walking-ones test
LAB_00400122:
  bsr  COMPBOOTSTACK         ; Compute boot stack pointer
  ...
  jsr  INITQUEUE
  jsr  INITSCSIMGR
  bsr  INITIOMGR             ; .Sony, .Sound etc driver table install
  bsr  INITCRSRMGR
  ...
  bsr  DRAWBEEPSCREEN        ; blank screen with pattern
  move.l #'WLSC',(DAT_CFC)   ; set warm-start magic for next boot
  bra  BOOTME
```

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
