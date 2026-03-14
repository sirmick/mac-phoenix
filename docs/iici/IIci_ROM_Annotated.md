# Mac IIci ROM Annotated Map

ROM Version: `$067c` (ROM_VERSION_32 — 32-bit clean Mac II family)
ROM Size: 512KB (`0x80000`)
Model: Mac IIci (productKind = 5, model ID 11)
Checksum: `$368CADFE` (first 4 bytes)

This document maps every significant region of the IIci 512KB ROM as used by
MacPhoenix. Regions are marked with their **status**:
- **LIVE** — executed as-is during emulated boot
- **PATCHED** — modified by `rom_patches.cpp` / `patch_rom_iici()` at load time
- **DEAD** — never executed (hardware-dependent code replaced or NOPped out)
- **DATA** — not code; tables, strings, parameter blocks

---

## Memory Layout Context

```
Mac address space during boot:
  $00000000 - $01FFFFFF   RAM (32 MB)
  $02000000 - $0207FFFF   ROM (512 KB, mapped here by emulator)
  $02100000 - $0210FFFF   ScratchMem (64 KB, fake hardware bases)
  $02110000 - $024FFFFF   FrameBuffer (4 MB)

Low memory globals (selected):
  $0028   Vec 4 — Illegal Instruction vector
  $0064   Vec 25 — Level 1 auto-vector (VIA interrupt)
  $010C   BufPtr
  $0114   HeapEnd
  $0118   TheZone
  $011C   UTableBase
  $012F   CPUFlag
  $016A   Ticks (60Hz counter)
  $01D2   UnitNtryCnt
  $02A6   CurStackBase
  $02AA   ApplZone
  $0308   VBLQueue
  $030A   DrvQHdr
  $06D4   jDrvr (Device Manager dispatch vector)
  $06DC   jIODone (I/O completion vector)
  $0CB2   CrsrCouple (cursor coupling flag)
  $0CBC   JVBLTask
  $0DD0-$0DDC   Boot scratch (device info, table ptrs)
```

---

## ROM Header ($0000-$0043)

| Offset | Size | Content | Notes |
|--------|------|---------|-------|
| $0000 | 4 | `$368CADFE` | ROM checksum |
| $0004 | 4 | `$40800000` | Entry point / version tag |
| $0008 | 2 | `$067C` | ROM version (ROM_VERSION_32) |
| $000A | 2 | — | Sub-version |
| $001A | 4 | Resource map offset | Points to ROM resource fork |
| $001E | 4 | — | (reserved) |
| $0022 | 4 | Trap table offset | Compressed A-trap dispatch table |
| $0044-$005B | — | Decoder info offsets | Hardware address table pointers |

**Status: DATA**

---

## Exception Vectors & Reset ($005C-$00B3)

### $005C-$008B: PMMU / CACR setup
```
$005C: MOVEC SFC,D2          ; Save source function code
$0062: ANDI.W #$FEFE,D0      ; Mask CACR bits
$0066: MOVEC D2,SFC          ; Restore
$006C-$0088: PMMU setup      ; 68030 MMU configuration
```
**Status: DEAD** — No MMU in emulator. Patched by InitMMU NOPs.

### $008C-$0093: Reset entry point
```
$008C: MOVE.W #$2700,SR      ; Supervisor mode, all IRQs masked
$0090: RESET                 ; Bus reset
$0092: MOVE.L #$2000,D0      ; CACR value
```
**Status: PATCHED** — Replaced with:
```
$008C: EmulOp(RESET)         ; → emul_op.cpp M68K_EMUL_OP_RESET
$008E: JMP $020200BA         ; Jump to boot code at $00BA
```
The RESET EmulOp sets up BootGlobs, register state (D0/D1/D2/A0/A1/A6/A7),
clears the SR interrupt mask, and triggers timer interrupts.

### $00B4: Boot code continues
```
$00B4: BRA $000008E0         ; Jump to StartInit1 (late boot)
```
**Status: LIVE**

---

## Boot Initialization ($00B8-$01F0)

### $00BA-$010A: Early device/memory init
```
$00BA: MOVEQ #0,D2
$00BC: MOVEM.L D5-D7/A5/A6,-(SP)
$00C2: LEA ...,A6 / JMP $2F18    ; → GetHardwareInfo
$00C6-$00DE: VIA/hardware init calls
$00E4: MOVEM save/restore
$00EC-$00F2: LEA $B190,A5 / JMP (-8,PC,A5)  ; Indirect call
$00FE: BSR $07C0              ; → SetupTimeK (CPU speed)
$0102: BSR $07F0              ; → secondary timing
$0106: BSR $0A70              ; → misc init
$010A: BSR $42FE              ; → hardware detection
```
**Status: PATCHED** — `$00C2` (GetHardwareInfo) and `$00C6-$00DE` (VIA init)
are NOPped out by `patch_rom_32()`. The emulator doesn't have real VIA/RBV
hardware. `$00C2` = 2 NOPs. `$00C6` = 15 NOPs.

### $010E: Patch BootGlobs
```
$010E: (original boot globs setup)
```
**Status: PATCHED** — Replaced with `EmulOp(PATCH_BOOT_GLOBS)` + NOP.
Sets MemTop, disables MMU flags.

### $0190: EnableExtCache
```
$0190: BSR ...               ; Enable external cache
```
**Status: PATCHED** — 2 NOPs. No cache hardware.

---

## Subroutine Stubs ($0480-$09FF)

### $0480-$048E: Memory fill loop
```
$0480: MOVE.L A1,D0          ; end address
$0482: SUB.L  A0,D0          ; length = end - start
$0484: LSR.L  #2,D0          ; count = length / 4
$0486: MOVEQ  #-1,D1         ; pattern = $FFFFFFFF
$0488: MOVE.L D1,(A0)+       ; fill loop
$048A: SUBQ.L #1,D0
$048C: BNE    $0488
$048E: RTS
```
**Status: LIVE** — Memory clearing utility used by InitZone and heap setup.

### $0490-$04AF: CompBootStack
```
$0490: (original code)
```
**Status: PATCHED** — Replaced with:
```
$0490: MOVE.L $010C,D0       ; BufPtr
$0494: ADD.L  $02A6,D0       ; + CurStackBase
$0498: LSR.L  #1,D0          ; / 2
$049A: BCLR   #0,D0          ; align
$049E: SUBI.W #$400,D0       ; - 1KB margin
$04A2: MOVEA.L D0,A0
$04A4: EmulOp(FIX_MEMSIZE)
$04A6: RTS
```
Computes boot stack pointer midway between heap end and stack base.

### $0500-$0510: InitZone parameter block
```
$050A: 0000 2000             ; startPtr  = $00002000 (system zone base)
$050E: 0000 3800             ; limitPtr  = $00003800 (original: 14KB zone)
```
**Status: PATCHED** — `patch_rom_iici()` changes limitPtr from `$3800` to
`$10000` (64KB), giving emulated drivers room. Original 14KB was barely
enough for real hardware drivers; emulated `.Sony`, `.Disk`, `.AppleCD`,
video, serial, and ExtFS drivers need ~40KB.

**Status: DATA** (parameter block, not code)

---

## SetupTimeK — CPU Speed ($07C0-$082E)

```
$07C0: (original CPU timing loop)
```
**Status: PATCHED** — Replaced entirely with:
```
$0800: MOVE.W #10000,$0D00   ; TimeDBRA
$0806: MOVE.W #10000,$0D02   ; TimeSCCDBRA
$080C: MOVE.W #10000,$0B24   ; TimeSCSIDBRA
$0812: MOVE.W #10000,$0CEA   ; TimeRAMDBRA
$0818: RTS
```
Hardcodes timing constants. Original code calibrated against real VIA timers.

**Status: DEAD** (original code at $07C0-$07FF never executes)

---

## Boot Phase Dispatch ($08E0-$0930)

### $08E0: StartInit1
The main boot code at `$00B4` branches here. This is the "warm start" path
that initializes memory management, the heap, and system zones.

### $0910: Device table installer
```
$0910: MOVE.L $0DD0,D0       ; Device info flags
$0914: MOVEA.L $0DD8,A0      ; Device table pointer
$0918: ADDA.L (A0),A0        ; Offset into table
$091A: LEA    +46(PC),A2     ; Dispatch table
$091E: MOVE.W (A2)+,D3       ; Entry: bit number
$0920: BMI    $0930           ; End of table (negative = done)
$0922: MOVEA.W (A2)+,A3      ; Target low-memory global
$0924: BTST   D3,D0          ; Is this device present?
$0926: BEQ    $091E           ; No — skip
$0928: LSL.W  #2,D3           ; Index * 4
$092A: MOVE.L (A0,D3.W),(A3) ; Install handler from table
$092E: BRA    $091E           ; Next entry
```
**Status: LIVE** — Installs device handler addresses from the UniversalInfo
decoder table into low-memory globals (VIA base, SCC base, etc.).
Hardware bases are redirected to ScratchMem by `patch_rom_32()`.

---

## Hardware Init Stubs ($09A0-$0A70)

### $09A0: InitSCSI
```
$09A0: (SCSI chip init)
```
**Status: PATCHED** — Replaced with `RTS`. No SCSI hardware.

### $09C0: InitIWM
```
$09C0: (IWM floppy controller init)
```
**Status: PATCHED** — Replaced with `RTS`. No IWM hardware.

### $09F4C: DisableIntSources
```
$09F4C: (masks VIA interrupt sources)
```
**Status: PATCHED** — Replaced with `RTS`. Emulator handles interrupts via EmulOp.

---

## SCC Init ($0A58)

```
$0A58: BTST #1,$0DD1         ; Check SCC present flag
$0A5E: BEQ  ...              ; Skip if not present
```
**Status: PATCHED** — Replaced with `RTS`. No SCC (serial) hardware.

---

## Slot Manager ($0D10-$0D90)

### $0D18: Slot scanning entry
```
$0D18: (original: A06E trap — _AddSlotIntQElement)
$0D1A: (NuBus slot probing loop)
```
**Status: PATCHED** — `patch_rom_iici()` replaces with:
```
$0D18: MOVEQ #-1,D0          ; Return error (no slots)
$0D1A: NOP
```
Without bus error support, NuBus slot reads return valid-looking data and
the scanner would loop forever. IIci has 3 NuBus slots but we emulate
video through a slot ROM injected by `InstallSlotROM()`.

---

## Driver Installation ($1130-$1180)

### $1138: .Sony driver open
```
$1138: (original .Sony open call)
```
**Status: PATCHED** — Replaced with `NOP`. The original .Sony driver code
polls IWM hardware. Our replacement drivers are installed at $1142.

### $1142: INSTALL_DRIVERS
```
$1142: (original: _Open trap for .Sound)
```
**Status: PATCHED** — Replaced with `EmulOp(INSTALL_DRIVERS)`.
This is the critical hook where MacPhoenix installs all emulated drivers:
- `.Sony` (floppy) — refnum -1
- `.Disk` (hard disk) — refnum -33
- `.AppleCD` (CD-ROM) — refnum -62
- Video driver (via slot ROM)
- Serial drivers (via SERD resource)
- ExtFS driver (shared folder)
Also sets up ApplZone (app heap), fixes system zone bkLim, saves Device
Manager vectors ($6D4/$6DC), and redirects Level 1 auto-vector to IRQ handler.

### $1144-$1150: SonyVars access
```
$1144: (SonyVars initialization)
```
**Status: PATCHED** — 5 NOPs. No floppy hardware state to initialize.

---

## REPAIR_DRIVERS Hook ($134E)

```
$134E: (was NOP padding)
```
**Status: PATCHED** — `EmulOp(REPAIR_DRIVERS)` inserted here.

### Why this exists:
ROM+$38570 performs a large memory copy (`MEMCOPY`) that overwrites ~20KB of
low memory ($0000-$4FC8), corrupting:
1. Driver DCE (Device Control Entry) fields — dCtlDriver, dCtlFlags
2. Device Manager dispatch vectors — $6D4 (jDrvr), $6DC (jIODone)
3. Heap zone headers

The `REPAIR_DRIVERS` EmulOp (emul_op.cpp) restores:
- Saved $6D4/$6DC vectors
- Patches jIODone to skip its spin loop (BGT at $BB8C → $BB92)
- Repairs .Sony, .Disk, .AppleCD DCE fields (driver pointer, flags, refnum)
- Dual-writes DCE flags for 24-bit flagged handles

### $1D10: BSR that triggers repair
```
$1D10: BSR.W $1350           ; Originally BSR.W $1352
```
**Status: PATCHED** — Offset decremented by 2 to hit EmulOp at $134E.

---

## Timer Wait Loops (DEAD on IIci)

These loops poll `Ticks` ($016A) which only increments via 60Hz interrupts.
During early boot with IPL=7 (interrupts masked), they spin forever.

### $14CA-$14CE: Wait loop 1
```
$14C6: ADD.L  $016A,D0       ; target = ticks + delay
$14CA: CMP.L  $016A,D0       ; current vs target
$14CE: BCC    $14CA           ; loop
```
**Status: PATCHED** — BCC at $14CE → NOP.

### $7650-$766A: Wait loop 2
```
$7648: MOVE.L $016A,D4
$764C: ADD.L  +8(A6),D4      ; target = ticks + timeout
$7650: BSR    $77CC
...
$766A: BHI    $7650           ; loop
```
**Status: PATCHED** — BHI at $766A → NOP.

### $C098-$C09C: Wait loop 3
```
$C092: MOVE.L $016A,D0
$C096: ADD.L  A0,D0
$C098: CMP.L  $016A,D0
$C09C: BGT    $C098           ; loop
```
**Status: PATCHED** — BGT at $C09C → NOP.

### $193E8-$193EC: Wait loop 4
```
$193E8: (tick comparison)
$193EC: BHI $193E8
```
**Status: PATCHED** — BHI at $193EC → NOP.

---

## Exception Handling Framework ($25F0-$2720)

### $25F0: Vector table setup
Called during boot to install exception vectors into low memory. Maps:
- Vec 4 ($10) → Illegal Instruction handler
- Vec 25 ($64) → Level 1 auto-vector (VIA/timer)
- Vec 26-30 ($68-$78) → Other auto-vectors

### $26A0: Boot dispatch framework
The IIci ROM uses a phase-based dispatch at $26A0 for boot initialization.
Each phase returns via `JMP (A6)` (A6 = return continuation). This
framework cannot handle timer interrupts — it's boot-only.

### $26E2-$26E7: Illegal instruction skip handler
```
$26E2: (was padding)
```
**Status: PATCHED** — `patch_rom_iici()` installs:
```
$26E2: ADDQ.L #2,2(SP)       ; Advance PC past illegal insn
$26E6: RTE                    ; Return from exception
```
When ROM dispatch RTEs to garbage RAM, the illegal instruction handler
skips the bad opcode instead of re-entering dispatch infinitely.

### $26E8-$26ED: IRQ handler
```
$26E8: (was padding)
```
**Status: PATCHED** — `patch_rom_iici()` installs:
```
$26E8: EmulOp(IRQ)           ; Process 60Hz/1Hz/ADB/serial interrupts
$26EA: TST.L  D0             ; Did we handle an interrupt?
$26EC: RTE                    ; Return from exception
```
This is the main interrupt entry point for the IIci. Level 1 auto-vector
($64) is redirected here by `INSTALL_DRIVERS` EmulOp.

### $26F4: Illegal instruction dispatch redirect
```
$26F4: (was BSR.S in dispatch table)
```
**Status: PATCHED** — Replaced with `BRA.S $26E2` to route illegal
instructions to the skip handler.

### $2708: Auto-vector redirect
```
$2708: (was handler for auto-vectors 25-30)
```
**Status: PATCHED** — Replaced with `BRA.S $26E8` to route all
hardware interrupts to the IRQ EmulOp.

---

## Boot Dispatch ($2E00-$2F20)

### $2E00: Stack/vector initialization
```
$2E00: MOVEA.W #$2600,A7     ; Init supervisor stack
$2E04: MOVEA.L A6,A4         ; Save boot context
$2E06: LEA    +124(PC),A0    ; Vector table pointer
$2E0C: (vector setup loop)
```
**Status: LIVE** — Called from boot entry via `JMP $2E00`.

### $2F18: GetHardwareInfo
```
$2F18: (hardware detection routine)
```
**Status: DEAD** — Called from $00C2 which is NOPped.

---

## UniversalInfo ($3400-$3C00)

Located by scanning for signature `$DC000505 3FFF0100`.

```
UniversalInfo structure (at base-$10):
  +$00: decoderInfoPtr    (offset to address decoder table)
  +$04: (reserved)
  +$08: (reserved)
  +$0C: nuBusInfoPtr      (offset to NuBus slot table)
  +$10: HWCfgFlags/IDs
  +$12: productKind        ← patched to config modelid
  +$14: (reserved)
  +$16: defaultRSRCs       ← patched to 4 if no FPU
  +$18: AddrMapFlags
  +$1C: UnivROMFlags
```

### NuBus info table
**Status: PATCHED** — First byte set to $03, remaining 15 bytes set to $08.
Disables all NuBus slot scanning. Video is provided via injected slot ROM.

### Decoder info table
**Status: PATCHED** — Hardware base addresses (VIA, SCC, ASC, IWM, SCSI)
redirected to ScratchMem so hardware register accesses hit inert memory
instead of unmapped addresses.

**Status: DATA**

---

## ASC Sound Chip Init ($7050-$7080)

### $706E: ASC init convergence point
```
$706E: CLR.B  +$801(A3)      ; Clear ASC registers
$7072: CLR.B  +$807(A3)
$7076: MOVE.W (A4)+,D0       ; Config word
$7078: LSL.W  #2,D0          ; Scale
$707A: TST    +$800(A3)      ; Check ASC type
$707E: BNE    $7082
$7080: LSR.W  #5,D0          ; Adjust for no ASC
$7082: MOVE.B D0,+$806(A3)   ; Store config
```
**Status: PATCHED** — `$706E` replaced with `JMP (A6)`. ASC hardware
doesn't exist in emulator. Five entry points ($7052, $7058, $705E,
$7064, $706A) all converge at $706E.

---

## SCSI Bus Scan ($71F0-$7220)

### $71F0-$71F6: SCSI probe
```
$71F0: BTST   #7,$0B22       ; Test SCSI interrupt flag
$71F6: BNE    $71FC           ; Branch if device found
```
**Status: PATCHED** — BNE at $71F6 → NOP. Without SCSI hardware, the scan
loops through timeouts forever.

---

## VIA Interrupt Handler ($9B60-$9BD4)

### $9B64: Level 1 interrupt entry
```
$9B64: LEA    +90(PC),A3     ; A3 → $9BC0 (VIA handler)
$9B68: TST.B  $0CB2          ; CrsrCouple flag
$9B6C: BNE    $9B8A           ; If set, take cursor-busy path
$9B6E: JSR    (A3)            ; Call VIA handler
$9B70: (return from interrupt)
```
**Status: PATCHED** —
- `$9B6C` BNE → NOP (prevents crash via uninitialized $0DBC)
- `$9BC0` (VIA handler) replaced with:
```
$9BC0: MOVEQ  #2,D0          ; Always claim 60Hz interrupt
$9BC2: EmulOp(IRQ)           ; Process all interrupt types
$9BC4: RTS                    ; Return to $9B70
```
The original handler reads VIA hardware registers to determine interrupt
source. Since hardware bases point to ScratchMem, it finds no interrupt
and re-triggers infinitely. The patch claims 60Hz and delegates to EmulOp.

---

## InitADB ($A8E0-$A920)

### $A8E0: ADB bus scan
```
$A8E0: BSR    $A8F8           ; Call init
$A8E2: ANDI.W #$F8FF,SR      ; Unmask interrupts
$A8E6: BTST   #5,+349(A3)    ; Poll Egret status
$A8EC: BNE    $A8E6           ; Spin until ready
```
**Status: PATCHED** — `$A8E0` replaced with `RTS`. The Egret ADB controller
doesn't exist in emulation; the poll at $A8E6 spins forever. ADB
mouse/keyboard is handled by the emulator's `adb.cpp` directly.

---

## Device Manager ($B5F0-$BBB0)

### $B608: Driver dispatch entry
```
$B608: BSET   #7,+5(A1)      ; Set busy bit in dCtlFlags
$B60C: (dispatch to driver Open/Prime/Control/Status/Close)
```
**Status: LIVE** — The busy bit set here is critical for IIci. The emulator's
`DRIVER_DONE` macro must clear it because jIODone ($BB8C) never does.

### $BB8C: jIODone (I/O completion)
```
$BB8C: MOVE.W +16(A0),D0     ; Read ioResult from param block
$BB90: BGT    $BB8C           ; *** SPIN LOOP *** if result > 0
$BB92: EXT.L  D0              ; Sign-extend
$BB94: MOVE.W D0,+16(A0)     ; Write back
$BB96: RTS
```
**Status: PATCHED** — The BGT spin loop at $BB90 is the root cause of
"driver busy" hangs on IIci. For emulated synchronous drivers, ioResult
is already set by the EmulOp handler. The `REPAIR_DRIVERS` EmulOp patches
$6DC to skip the spin:
```
$6DC → $BB92                  ; Skip MOVE.W/BGT, go straight to EXT.L
```

### $EFE2: Inline jIODone copy
```
$EFDE: MOVE.W +16(A0),D0     ; Read ioResult
$EFE2: BGT    $EFDE           ; *** SPIN LOOP ***
$EFE4: EXT.L  D0
```
**Status: PATCHED** — BGT at $EFE2 → NOP. Same spin pattern duplicated
in a different code path.

---

## Heap Coalescing ($E140-$E6A0)

### Path 1 ($E670-$E688): Forward coalescing
```
$E674: CMPA.L +48(A6),A3     ; Compare with purgePtr
$E678: BNE    $E67E
$E67A: MOVE.L A0,+48(A6)     ; Update purgePtr
$E67E: ADD.L  (A3),D0        ; Accumulate block size
$E680: ADDA.L (A3),A3        ; Advance pointer by block size
$E682: TST.B  (A3)           ; Test tag byte
$E684: BEQ    $E674           ; If free (tag=0), continue coalescing
$E686: MOVE.L D0,(A0)        ; Store combined size
```
**Status: PATCHED** — Zero-size blocks cause ADDA.L to add 0 and A3 never
advances → infinite loop. `patch_rom_iici()` redirects $E682 to trampoline:
```
$A526: TST.B  (A3)           ; Test tag byte
$A528: BNE.S  $A538           ; Non-free → exit normally
$A52A: TST.L  (A3)           ; Check full header word
$A52C: BNE.S  $A534           ; Non-zero size → coalesce
$A52E: MOVEA.L A2,A3         ; Zero size! Force A3 = bkLim to exit
$A530: BRA.W  $E686           ; → store result
$A534: BRA.W  $E674           ; → continue coalescing
$A538: BRA.W  $E686           ; → store result
```

### Path 2 ($E158-$E16A): Secondary coalescing
```
$E158: MOVEA.L A1,A0
$E15A: ADDA.L D1,A1
$E15C: TST.B  (A0)
$E15E: BNE    $E16A
$E160: CMPA.L (A6),A0
$E162: BCC    $E16A
$E164: ADD.L  (A0),D1        ; *** SAME BUG: zero-size → infinite ***
$E166: MOVE.L D1,(A1)
$E168: BRA    $E158
```
**Status: PATCHED** — $E164 redirected to trampoline:
```
$A540: TST.L  (A0)           ; Check size
$A542: BNE.S  $A54A          ; Non-zero → coalesce
$A544: BRA.W  $E16A           ; Zero → exit loop
$A54A: ADD.L  (A0),D1        ; (original) accumulate size
$A54C: MOVE.L D1,(A1)        ; (original) store combined
$A54E: BRA.W  $E158           ; → loop back
```

---

## GrowZone ($E4AE-$E520)

### $E4AE: Grow-zone handler entry
```
$E4AE: MOVEM.L D0/A0/A2/A3,-(SP)
$E4B2: MOVEA.L A0,A3
$E4B4: MOVEA.L (A6),A0       ; A6 = zone header, (A6) = bkLim
$E4B6: MOVE.L  A3,D0
$E4B8: SUB.L   A0,D0         ; New extent
$E4BA: MOVE.L  #$0C,(A3)     ; 12-byte sentinel
$E4C0: BSR     $DDEE         ; Adjust zone
$E4C4: BSR     $E91C         ; Post-growth fixup
$E4C8: MOVE.L  A3,(A6)       ; Update bkLim
```
**Status: PATCHED** — `patch_rom_iici()` redirects $E4AE to trampoline:
```
$A554: CMPI.L #$40000,(A6)   ; bkLim >= 256KB?
$A55A: BCS.S  $A560           ; Below → grow normally
$A55C: RTS                    ; At limit → refuse
$A560: MOVEM.L D0/A0/A2/A3,-(SP)  ; (original)
$A564: BRA.W  $E4B2           ; Continue body
```
Caps system zone at 256KB to prevent unbounded growth that would
collide with ApplZone at $20000.

---

## Heap Clearing ($D40C-$D420)

### $D40C-$D420: Clear heap area
```
$D40C: BTST   #9,D1
$D410: BEQ    $D420           ; Skip if bit clear
$D412: (clear loop: CLR.W (A0)+ / SUBQ / BGT)
$D420: (continue)
```
**Status: PATCHED** — BEQ at $D410 → `BRA $D420` (always skip).
This routine clears memory in the range $4538-$4644 which overlaps with
the slot ROM video driver installed by `InstallDrivers()`. Without this
patch, the video driver is wiped out.

---

## Egret Manager Init ($42922)

```
$42922: MOVEA.L A7,A1         ; Setup
$42924: SUBA.W #$180,A4       ; Allocate scratch
$42928: LEA    +6(PC),A6
$4292C: JMP    -5422(PC)       ; → Egret hardware init
```
**Status: PATCHED** — `$42922` replaced with:
```
$42922: MOVEQ  #0,D0          ; Return success
$42924: RTS                    ; Skip Egret init entirely
```
The Egret is the power management/ADB controller chip on the IIci.
Not emulated; ADB is handled directly by `adb.cpp`.

---

## SANE FP Shift Bug ($2EDCA)

### $2EDCC: Shift loop exit
```
$2EDCA: (shift loop)
$2EDCC: BLE    $2EE26         ; Exit when D1 <= 0
```
**Status: PATCHED** — BLE → BRA. When D0=0 (uninitialized), D1 never
decreases and the loop runs forever. Forcing unconditional exit.

---

## Sound Manager Loop ($2E5F6)

### $2E5F6: Linked-list traversal
```
$2E5F6: MOVE.L $08A8,D4       ; Sound Manager queue head
$2E5FA: (traverse linked list)
...
$2E6A6: (after loop)
```
**Status: PATCHED** — `$2E5F6` replaced with `BRA.W $2E6A6` (skip loop).
The list at $08A8 has corrupt/circular entries because Sound Manager
was never properly initialized on the emulated IIci.

---

## Memory Copy ($38570)

```
$38570: (large block copy: source → $0000-$4FC8)
```
**Status: LIVE** (the copy itself runs) — But it corrupts everything the
emulator installed in low memory. This is why `REPAIR_DRIVERS` exists
at $134E, called after the copy completes.

---

## ROM Resources

Driver resources are found via `find_rom_resource()` and overwritten:

| Resource | Type/ID | Original | Replacement |
|----------|---------|----------|-------------|
| .Sony | DRVR 4 | IWM floppy driver | EmulOp-based disk driver |
| .Disk | — | (installed at sony+$100) | EmulOp-based disk driver |
| .AppleCD | — | (installed at sony+$200) | EmulOp-based CD driver |
| SERD 0 | SERD 0 | Hardware serial init | EmulOp-based serial |
| Icons | — | (at sony+$400-$AFF) | Floppy/disk/CD icons |
| vCheckLoad | — | at $1B8F4 | Patched to call CHECKLOAD EmulOp |

---

## Trampoline Code Summary

`patch_rom_iici()` installs trampolines in unused ROM space ($A526-$A56F):

| Address | Purpose | Redirected from |
|---------|---------|-----------------|
| $A526-$A53B | Heap coalesce fix (path 1) | $E682 |
| $A540-$A551 | Heap coalesce fix (path 2) | $E164 |
| $A554-$A567 | GrowZone 256KB cap | $E4AE |

And patched code in ROM padding:

| Address | Purpose |
|---------|---------|
| $26E2-$26E7 | Illegal instruction skip handler |
| $26E8-$26ED | IRQ EmulOp handler |

---

## EmulOp Insertion Points (IIci)

| ROM Offset | EmulOp | Purpose |
|------------|--------|---------|
| $008C | RESET | Boot initialization |
| $010E | PATCH_BOOT_GLOBS | Fix MemTop, MMU flags |
| $0490+ | FIX_MEMSIZE | Correct RAM size globals |
| $1142 | INSTALL_DRIVERS | Install all emulated drivers |
| $134E | REPAIR_DRIVERS | Fix DCEs after MEMCOPY |
| $26E8 | IRQ | Interrupt handler |
| $9BC2 | IRQ | VIA handler (secondary path) |

---

## Dead Code Summary (never executes on IIci in MacPhoenix)

| Region | Size | Original Purpose | Why Dead |
|--------|------|-----------------|----------|
| $005C-$008B | ~48B | PMMU/CACR setup | No MMU emulated |
| $00C2-$00DE | ~28B | GetHardwareInfo + VIA init | NOPped; no VIA/RBV |
| $07C0-$07FF | ~64B | CPU timing calibration | Replaced with constants |
| $09A0 | ~32B | InitSCSI | RTS; no SCSI chip |
| $09C0 | ~32B | InitIWM | RTS; no IWM chip |
| $09F4C | ~16B | DisableIntSources | RTS; no VIA |
| $0A58 | ~32B | InitSCC | RTS; no SCC |
| $0D18-$0D90 | ~120B | Slot Manager scan | Returns error; no NuBus |
| $1138 | 2B | .Sony hardware open | NOP |
| $1144-$1150 | ~14B | SonyVars init | NOPped |
| $2F18+ | ~256B | GetHardwareInfo body | Never called |
| $42922+ | ~256B | Egret init | RTS |
| $706E+ | ~64B | ASC init | JMP (A6) |
| $A8E0+ | ~64B | ADB bus scan (Egret) | RTS |
| $BB8C-$BB90 | 6B | jIODone spin loop | Skipped via $6DC patch |
| $EFDE-$EFE2 | 6B | Inline jIODone spin | NOP |
| $2EDCC | 2B | SANE shift loop exit | Forced unconditional |
| $2E5F6-$2E6A5 | ~176B | Sound Manager list walk | BRA past |
| $D412-$D41E | ~14B | Heap clearing ($4538-$4644) | BRA past |
| Timer loops | ~8B ea | 4 tick-polling loops | NOPped (4 locations) |

**Total dead code: ~1.3 KB** out of 512 KB ROM — the remaining ~99.7% is
either live boot code, data tables, or ROM resources.

---

## IIci-Specific Workarounds (not needed for Quadra)

These exist only because the IIci ROM (1989) predates:
- 32-bit clean Memory Manager (24-bit handle flags in high byte)
- Proper Device Manager busy-bit clearing
- Interrupt handling independent of VIA hardware

| Workaround | Code Location | Why |
|------------|--------------|-----|
| 24-bit DCE masking | emul_op.cpp:88-92 | HLock sets $80 in handle high byte |
| DRIVER_DONE macro | emul_op.cpp:619-629 | Clear busy bit + dual-write for flagged handles |
| REPAIR_DRIVERS | emul_op.cpp:507-598 | MEMCOPY at $38570 corrupts drivers |
| Auto-vector redirect | emul_op.cpp:432-438 | ROM VIA init overwrites vectors |
| ApplZone creation | emul_op.cpp:325-361 | ROM assumes ApplZone=SysZone |
| Zone bkLim fix | emul_op.cpp:371-410 | ROM adjusts bkLim past our limit |
| Heap coalesce fixes | patch_rom_iici() | Zero-size blocks → infinite loop |
| GrowZone cap | patch_rom_iici() | Unbounded zone growth |
