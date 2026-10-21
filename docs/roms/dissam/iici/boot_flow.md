# Mac IIci Boot Flow

A map of the Mac IIci ROM (`$368CADFE`, 512 KB, version `$067C`, sub
version `$10F1`, the "Universal" 32-bit-clean ROM) from reset to the
Finder: what the real machine does at each step, and what MacPhoenix
does instead. MacPhoenix boots System 6.0.8 on it to the Finder in about
1.5 s (uae and mame-68k cores; tests `boot_iici`, `boot_iici_mame68k`).

Addresses are ROM offsets (`$01D10`) unless written in full; the ROM's
own address is `$40800000` + offset, and in our 24-bit layout
`$800000` + offset. Routine names are Apple's, from
`tools/vendor/68k-mac-rom-maps/rom-maps/MacIIciROM.lst.txt` (imported
into the listing `iici/rom.lst`, a gitignored build artifact; see §6).
Where Apple's map has no name, the routine is called by its address.

What this is based on:

- the ROM, disassembled (Ghidra listing, and capstone where Ghidra left
  code as data, e.g. `CONFIGURERAM`);
- our boot traced with `--trace-atraps` and a snapshot at the desktop
  (trap order and low memory), and the EmulOp log at `--log-level 2`;
- MAME's `maciici` booting the 6.0.8 System Tools floppy, 8 MB, empty
  PRAM: a trace of the first execution of every ROM routine, every
  A-trap, the I/O and slot accesses, and low memory at key points
  (`docs/roms/mame/boottrace/README.md`); and MAME booting without a
  disk to read the MMU globals (§6).

Times written "MAME 5.38 s" are emulated seconds from reset in that
trace, so the step is runtime-confirmed on the real ROM; "ours" means
seen in our own trace. The rest is read from the code. The Mac II
family's walk, for comparison: `../macii/boot_flow.md` (Mac II rev A
and B) and `../maciix/boot_flow.md` (`$97221136`: IIx, IIcx, SE/30).

---

## 1. Overview

### ROM identity

| Offset | Value | What |
|---|---|---|
| `$00` | `$368CADFE` | checksum (also the reset SSP) |
| `$04` | `$4080002A` | reset PC: the ROM's own address, not the overlay copy |
| `$08` | `$067C` | ROM version: `patch_rom_32` (`ROM_VERSION_32`) |
| `$0A`, `$0E` | `jmp $8C` | start entries |
| `$12` | `$10F1` | sub version |
| `$1A` | `$0007EC10` | offset of the ROM resource map |
| `$1E` | `jmp $1240` | `GOOFYDOEJECT` |
| `$22` | `$000514E0` | offset of the compressed trap table (`DISPOFF`) |
| `$26` | `jmp $2310` | `CRITICAL` → `CRITERR` (sad Mac) |
| `$2A` | `jmp $8C` | reset entry |
| `$40` | `$00080000` | ROM size (the Slot Manager uses it to find slot 0's declaration data) |
| `$7FFEC` | format block | slot 0 declaration data: length `$13E6`, test pattern `$5A932BC7` |

`set_machine_profile()` (`src/config/machine_profile.cpp`) picks the
`iici` profile by this checksum, ahead of the version word, because the
Quadra 650's ROM is also `$067C`.

### The machine, real and ours

| | Real IIci | MacPhoenix `iici` profile |
|---|---|---|
| CPU | 68030 + 68882 | 68030 + FPU (`cpu_type` 3, `fpu` true) |
| RAM | up to 128 MB, two banks | 8 MB max (`max_ram`), one bank |
| Addressing | 24 or 32-bit (PRAM `$8A`), 68030 PMMU, MMU type 4 | 24-bit only: `GuestMemory` masks addresses to 24 bits; MMU type 1 |
| ROM | `$40800000` | Mac `$800000` (`$40800000` & `$FFFFFF`) |
| I/O | `$50F00000`… (decoder table, §4) | redirected to ScratchMem (`$888000`); ASC to a fake block |
| Video | RBV on-board video, slot 0, frame buffer carved out of bank A (`ScrnBase` `$FBB08000`) | our slot-0 declaration ROM and driver (`slot_rom.cpp`), frame buffer at `$890000` |
| NuBus | slots `$C`, `$D`, `$E` | none (`nuBusInfo` and the probe patched) |
| Gestalt machine | 11 (box flag 5) | 11 (`model_id` 5, `--model-id` overrides) |
| Startup disk | SCSI | `.Disk` driver, refnum -33 (`disk_refnum`) |

### Address map

On the real machine the ROM is overlaid at 0 at reset so the 68030 can
fetch its vectors, but the reset PC is already `$4080002A`, so code runs
from the ROM's real address from the first instruction. After
`INITMMU` the PMMU maps RAM, ROM (`$40800000`), I/O (`$50F00000`) and
slot space (`$Fs000000`); in 24-bit mode the 24-bit view is RAM at 0,
ROM at `$800000`, I/O at `$F00000`, slots `$9`–`$E` at `$900000`–`$EFFFFF`.

Ours is that 24-bit view, flat, with no MMU (`src/core/cpu_context.cpp`,
`init_m68k()`):

| Mac address | What |
|---|---|
| `$000000`–`$7FFFFF` | RAM (8 MB); BootGlobs at the top (`$7FFFE4`) |
| `$800000`–`$87FFFF` | ROM (patched copy) |
| `$880000`–`$88FFFF` | scratch block; `ScratchMem` = `$888000` |
| `$890000`– | frame buffer (8 MB area; 640×480×32 ends at `$9BC000`) |

The whole block is mapped at Mac 0 and the address mask is 24 bits, so
the ROM's 32-bit constants wrap onto it: `$40800000` → `$800000`,
`$50F14000` (ASC) → `$F14000`, in the unused part of the frame buffer
area. Nothing else in the boot reaches `$Fxxxxx`, because the decoder
bases are redirected (§2.2).

### Phase diagram

```
reset $2A ── jmp $8C
  $8C   real: SR=$2700, reset, MMU off, JUMPINTOROM $2E00 (GETHARDWAREINFO, INITVIAS)
        ours: EMUL_OP_RESET, jmp $BA ............................. [ROM init]
  $B4   real: RUNDIAGS $8E0 → STARTTEST1 $434E4 → ROMCHK $435AA
        (ROM checksum, chime, SIZEMEMORY, RAM test, TESTRBV, ...) ── skipped
  $B8   STARTINIT1
          GETHARDWAREINFO / INITVIAS ............................ NOPed
          VALIDATEPRAM $B27C
          WHICHCPU $7C0 (faked), WHICHBOARD $7F0, CONFIGURERAM $A70
          INITMMU $42FE (3 patches)
  $10E    EMUL_OP_PATCH_BOOT_GLOBS ................................ [boot globs]
          SETUPHWBASES, INITSCC/INITIWM/INITSCSI (RTS), FILLWITHONES,
          SETUPHWBASES, INITMMUGLOBALS $4B0A, SYSERRINIT $25F0,
          SETUPTIMEK $800 (faked), INITHIMEMGLOBALS $590
  $1A6  BOOTRETRY (the restart point; MYBOOT returns here on a failed mount)
          INITGLOBALVARS, INITINTHANDLER, INITDISPATCHER, INITMMUTRAP,
          GETPRAM/_InitUtil, INITMEMMGR, _SwapMMUMode(24), SETUPSYSAPPZONE,
          INITRSRCMGR, NMINIT, INITTIMEMGR, INITSHUTDOWNMGR,
          INITSLOTS $1210 (Slot Manager: finds our slot-0 ROM), INITDTQUEUE,
          INITSCSIMGR $9490,
          INITIOMGR $10F0: _Open .Sony, EMUL_OP_INSTALL_DRIVERS ........ [drivers]
                           FROVIDEO (our video driver), SERD
          INITADB $A802, INITCRSRMGR, INITGESTALT, INITRELIABILITY,
          TEGLOBALINIT, DRAWBEEPSCREEN $530
  $2FE    WarmStart = 'WLSC' ....................................... [warm start]
  $322  bra BOOTME/MYBOOT $1D10
          FINDSTARTUPDEVICE $1350 → boot blocks (bbVersion $17)
          _MountVol, _InitResources (System file; ROvr 0, ROv# $067C)
          SysError(dsGreeting): "Welcome to Macintosh"
          System data-fork code $1EA4: PTCH 0, PTCH $067C, ptch
          INIT 0..35 from the System file, incl. INIT 31 ............... [extensions]
          FinderName ← bbShellName, _Launch "Finder" .................... [Finder]
  Finder event loop idle ............................................ [desktop]
```

The bracketed names are `src/core/boot_progress.cpp`'s phases
(`/api/status` `boot_phase`). Two of them don't correspond to an
address here: `boot blocks` is the first `'boot'` resource, which only
System 7's boot blocks load (6.0.8 never reaches it: the ROM's own
`MYBOOT` does the work, §2.13), and `warm start` is noticed at the
first `CHECKLOAD` after `'WLSC'` is written. `checkload_count` runs
into the hundreds of thousands on 6.0.8 (something in the System
calls `GetResource` in a loop), so it isn't a progress measure here.

Timings, ours: everything up to the `MYBOOT` INIT loop takes about
10 ms; a System Folder INIT then shows "Please name your Macintosh
computer:" (`DLOG` 555), which `boot_progress.cpp`'s dialog
auto-dismiss closes after about 1 s (it does so even with
`--no-dismiss-shutdown-dialog`); the Finder is idle at 1.5 s. MAME's IIci, from the 800K floppy:

| Step | MAME (s) | Instructions |
|---|---|---|
| `STARTINIT1` (after 5.2 s of diagnostics, 3.4 s of it `MOD3TEST`) | 5.38 | 21.15 M |
| `BOOTRETRY` | 5.44 | 21.27 M |
| `INITSLOTS` | 5.45 | 21.33 M |
| `INITIOMGR` (after a 0.5 s SCSI bus reset) | 6.00 | 23.61 M |
| `INITADB` → `INITCRSRMGR` (ADB takes 1.7 s) | 6.12 → 7.82 | |
| `BOOTME` | 7.87 | 30.46 M |
| `WAITFORINTERNAL` → next device (the ROM's internal-disk timeout) | 9.4 → 25.6 | |
| "Welcome to Macintosh" (`DSERRORHANDLER`) | 28.66 | 110.1 M |
| System data-fork code (`$1EA4`) | 28.99 | |
| `_Launch` Finder (`$2036`) | 33.29 | 125.6 M |

1228 distinct ROM routines run before the Finder. Ours spends no time
in any of these waits.

---

## 2. Stage by stage

### 2.1 Reset entry (`$2A` → `$8C`)

Real:

```
$8C  move   #$2700,SR
$90  reset
$92  move.l #$2000,D0 / movec D0,CACR / movec CACR,D0 / tst.l D0
$A2  beq    $AC              ; CACR bit 13 reads back only on a 68030
$A4  lea    (-$1C,PC),A0     ; $8A: the long $000046FC, TC with E clear
$A8  pmove  (A0),TC          ; MMU off (68030 only)
$AC  lea    (6,PC),A6 / jmp JUMPINTOROM   ; returns to $B4
$B4  bra.w  RUNDIAGS
```

`JUMPINTOROM` (`$2E00`) sets SP to `$2600` and, through VBR, a
bus-error vector for the probes, calls `GETHARDWAREINFO` (D2 = 0:
probe), moves execution
to the ROM's real address if it isn't there already, calls `INITVIAS`,
and pokes two more devices if present (`BasesValid` bits 18 and 19).
`RUNDIAGS` (`$8E0`) runs the startup tests (`STARTTEST1` → `ROMCHK`:
ROM checksum, the chime `BOOTBEEP6`, `SIZEMEMORY`, `MOD3TEST`, address
line, SCC, VIA, SCSI, ASC, PRAM, `TESTRBV` `$45264`, `TESTSWIM`,
`FLOAT_TEST`, `TESTPGC`), which builds the RAM chunk table, and enters
`STARTINIT1` at `$B8` with D2 = 0.

MacPhoenix: `patch_rom_32` writes `EMUL_OP_RESET` + `jmp $408000BA` over
`$8C`–`$93`. The handler (`src/core/emul_op.cpp`, `M68K_EMUL_OP_RESET`)
resets timers, audio and Ethernet and stands in for everything up to
`STARTINIT1`:

- BootGlobs at `RAMSize - $1C` (`$7FFFE4`), the 4 KB below the top of
  RAM cleared, with a one-bank chunk table `{0, RAMSize}, -1`;
- D0 = `BasesValid`, D1 = `ExtValid`, D2 = the `$10` long of the
  ProductInfo (`$DC000505`; bit 28 = FPU, set or cleared from
  `FPUType`), A0 = decoder table, A1 = ProductInfo, A6 = BootGlobs,
  A7 = `$10000`: what `GETHARDWAREINFO` and the diagnostics hand on;
- SR `$2000`. The real ROM stays at `$2700` until `BOOTRETRY` `$23A`.

`$BA` skips `STARTINIT1`'s `moveq #0,D2`, so D2 arrives as the
ProductInfo long. VBR stays 0 (the 68030's reset value): the vectors are
in low memory from `SYSERRINIT` on, as on the real machine after boot.

### 2.2 Identifying the IIci: `GETHARDWAREINFO`, ProductInfo, decoder table

`GETHARDWAREINFO` (`$2F18`) walks a table of ProductInfo records
(the "UniversalInfo" of `patch_rom_32`; the list starts at `$32D4`).
With D2 = 0 it probes the hardware and matches the ID bits it reads
against each record's mask/match pair. What the probes see on MAME's
IIci (MAME 0.00003 s, `hwprobes.tsv`):

- a walking bit written to VIA1's IER (`$50F01C00`) and read back
  through `$50F21C00` (reads 0: no copy there) and `$50F41C00` (echoes
  every value: VIA1 repeats there), which tells the address decoders
  apart;
- `tst.b` of `$50F26000` (RBV) and `$50F24000` (VDAC);
- VIA1 port A `$C7` and port B `$08`, read with their DDRs cleared:
  the ID bits;
- VIA2's port A at `$50F02000` reads 0 (there is no VIA2).

It settles on the record `patch_rom_32` also uses: at the Finder MAME
has `$DD8` (ProductInfo) = `$408036C0`, `$DD0` = `$773F`, `$DD4` = 0,
the values our RESET hands over. It returns A0 = decoder table, A1 = ProductInfo, D0 =
`BasesValid`, D1 = `ExtValid` (from the record, or from the decoder
table's `-$10` long when the record's is 0), D2 = the `$10` long.

Records in this ROM:

| ProductInfo | Box flag (Gestalt) | Decoder kind | Notes |
|---|---|---|---|
| `$36C0` | 5 (11, IIci) | 5 | ID match `$46000000` under mask `$56000000`; the one `patch_rom_32` uses |
| `$3700` | 5 (11, IIci) | 5 | ID match `$56000000`, `ExtValid` 1 |
| `$3800` | 6 (12) | 5 | |
| `$37C0` | 7 (13, IIfx) | 6 | the IIfx's OSS/IOP decoder |
| `$3740`, `$3780` | 9 (15), 10 (16) | 5 | |
| `$3840`, `$3880`, `$38C0` | `$FD` | 4, 5, 6 | fallbacks per decoder kind (4 is the Mac II family's) |
| `$3900` | `$FD` | 0 | unknown decoder |

The IIci record, `$36C0`:

| Offset | Field | Value |
|---|---|---|
| `+$00` | DecoderInfoPtr | `-$234` → decoder table at `$348C` |
| `+$04` | RamInfoPtr | → `$396A` |
| `+$08` | VideoInfoPtr | → `$3982` (frame buffer `$FBB08000`) |
| `+$0C` | NuBusInfoPtr | → `$39C2` |
| `+$10` | HwCfgWord | `$DC00` (SCSI, clock, FPU, MMU, ADB); copied to `HWCfgFlags` `$B22` by `INITGLOBALVARS` |
| `+$12` | ProductKind (box flag) | 5 → `BoxFlag` `$CB3` |
| `+$13` | DecoderKind | 5 |
| `+$14` | ROM85 word | `$3FFF` → `ROM85` `$28E` |
| `+$16` | DefaultRSRCs | 1 |
| `+$18` | BasesValid | `$0000773F` |
| `+$1C` | ExtValid | 0 |
| `+$20`, `+$24` | ID mask, match | `$56000000`, `$46000000` |

The decoder table (`$348C`, one long per `BasesValid` bit, in Apple's
DecoderInfo order) is in §4. `SETUPHWBASES` (`$910`) copies the valid
entries into low memory through the `(bit, global)` table at `$94A`.

What `patch_rom_32` does to these (`src/core/rom_patches.cpp`):

- finds the record by the signature `DC 00 05 05 3F FF 01 00` (the
  `$10`–`$17` bytes) in `$3400`–`$3C00`: first hit `$36D0`, so
  `UniversalInfo` = `$36C0`;
- NuBusInfo (`$39C2`, a byte per slot 0–15): keeps slot 0's `$03`,
  writes `$08` to slots 1–15 (the IIci's are `$00`×8, `$01 $01 $08`,
  `$07 $07 $07`, `$00`). Bit 2 marks a NuBus connector: Gestalt's
  `'nubs'`/`'slt1'`/`'slot'` code (`$167FC`) counts them, so our IIci
  reports none. The slot scan itself is stopped by a separate patch
  (§2.6);
- ProductKind: `--model-id` if given, else the profile's 5 (the value
  already there);
- every decoder entry named in the `$94A` table → `ScratchMem`, except
  the ASC's (global `$CC0`);
- DefaultRSRCs = 4 ("FPU optional") only when there's no FPU (not the
  IIci profile).

### 2.3 `INITVIAS` and the rest of `STARTINIT1`'s head

`STARTINIT1` (`$B8`):

| Address | Real | Ours |
|---|---|---|
| `$C2` | `jmp GETHARDWAREINFO` | NOP NOP |
| `$C6`–`$E3` | A4 = decoder `+8` (VIA1); save VIA1 port bit 6; `INITVIAS` (`$2E8C`: VIA1, VIA2 and RBV registers from the ProductInfo's tables; on MAME, RBV IER `$7F`, `RBufB` `$8F`, monitor `$40`, slot IER `$FF`); restore the bit on decoder 5 | 15 NOPs |
| `$E4`–`$F6` | `VALIDATEPRAM` (`$B27C`) via `jmp` with A6 return; MAME 5.384 s (38 ms: it rewrote the empty PRAM) | runs (§2.12) |
| `$FE` | `WHICHCPU` (`$7C0`): D7 = CPU type; MAME 5.422 s | `moveq #3,D7; rts` |
| `$102` | `WHICHBOARD` (`$7F0`): D7 high word = box flag (`$12,A1`) | runs |
| `$106` | `CONFIGURERAM` (`$A70`) | runs |
| `$10A` | `INITMMU` (`$42FE`); on MAME it calls `GETRBVSIZE` and returns 9.8 ms later | runs, patched |
| `$10E` | `movea.l (-$14,A4),A6` (top of memory) | `EMUL_OP_PATCH_BOOT_GLOBS`, NOP |

`CONFIGURERAM` moves the stack into the middle of the first bank,
records the chunk table's size at BootGlobs `-8`, and dispatches on the
decoder kind (D2's low byte) to set up the memory controller. Kind 4
(Mac II GLUE) and 6 (IIfx) do work; kind 5, the IIci's, returns at once.
It has no BootGlobs-clearing loop, so `patch_rom_32`'s `clear_globs_dat`
signature isn't found here and that patch is skipped.

### 2.4 `INITMMU` (`$42FE`)

```
$42FE  link   A5,#-$74
$4302  bsr    $4392        ; save regs; MMU type ($44BC); PRAM $8A; sizes
$4306  bsr    $4538        ; per-decoder-kind RAM and video layout
$430A  cmpi.b #1,(-$1A,A6) ; MMU type 1 (HMMU): no tables
$4310  bne.b  $431E        ; else build tables, load CRP/TC, enable caches
$4312  movem.l (-$18,A5),D0-D2/D7/A0-A1 ; A4 = A6 (BootGlobs); rts
```

- `$44BC` sets the MMU type (BootGlobs `-$1A`) from the CPU type through
  the table at `$44B8`: 68000/010 → 0, 68020 → 1 (a probe at `$44D0` can
  make it 3), 68030 → 4. A real IIci gets 4.
- `$43D4` reads PRAM `$8A` with `STARTREADPRAM` → `RDXBYTE` (`$445DC`)
  into BootGlobs `-$19`: bit 0 = 32-bit addressing (bit 4 is cleared
  when bit 0 is).
- `$4538` jumps on the decoder kind; for kind 5 (`$45A6`), if
  `BasesValid` bit 13 (RBV) is set, it calls `GETRBVSIZE` (`$37E`) and
  takes the frame buffer out of the first bank.
- With type ≠ 1 the rest builds the 32-bit tables (record at BootGlobs
  `-$42`) and, when booting 24-bit, the 24-bit ones (`-$2E`), loads CRP
  and TC and enables the caches.

`patch_rom_32`:

| Site | Patch | Effect |
|---|---|---|
| `$44BC` (`init_mmu_dat`, 512 KB form `0C 47 00 03 62 00 FE`) | NOP×4 over `cmpi.w #3,D7; bhi`, `moveq #0,D0`+NOP over `move.b $44B8(PC,D7),D0` | MMU type 0 |
| `$45AA` (`init_mmu2_dat`, `08 06 00 0D 67`, first of 7 hits) | `beq` → `bra` | no RBV frame buffer carved out of RAM |
| `$4310` (`init_mmu3_dat`) | NOP over the `bne` | always the no-tables path: A4 = BootGlobs, return |

So A4 = BootGlobs = our `$7FFFE4` from here on (`STARTINIT1` stores it
at `$DDC`).

### 2.5 BootGlobs and `PATCH_BOOT_GLOBS`

BootGlobs, as used by this ROM (A4, `$DDC`):

| Offset | What |
|---|---|
| `0`… | chunk table `{start, size}`…, `-1` |
| `-$04` | `'WLSC'` copy (`BOOTRETRY` `$30E`) |
| `-$08` | size of the chunk table (`CONFIGURERAM`) |
| `-$0C` | offset to the first `BufPtr`: `-$42` (`INITMMU` `$43B8`) |
| `-$14` | top of usable memory → `MemTop` (A6 at `$10E`) |
| `-$18` | → `$1EF0` (`INITHIMEMGLOBALS`) |
| `-$19` | addressing flags from PRAM `$8A`; bit 0 = start in 32-bit mode |
| `-$1A` | MMU type |
| `-$2E`, `-$42` | 24-bit and 32-bit CRP/TC records (`$CB4`, `$CB8`) |

`EMUL_OP_PATCH_BOOT_GLOBS` (`emul_op.cpp`, `ROMVersion == ROM_VERSION_32`
branch) writes `-$14` = `RAMBaseMac + RAMSize` and A6 = the same (the
instruction it replaces), and on a 24-bit profile:

- `-$1A` = 1: the Mac II's HMMU type;
- `-$19` bit 0 cleared: not 32-bit mode, whatever PRAM said.

(On the Quadra: type 0 and bit 0 set.) These two bytes decide what
`INITMMUGLOBALS` (`$4B0A`) installs:

```
$CB0 MMUFlags = 0;  $CB2 MMU32Bit = 1
$CB8 = BootGlobs-$42;  $CB4 = BootGlobs-$2E
if -$19 bit 0:  $DBC = $4B82            ; moveq #1,D0; rts: always 32-bit
else:           $CB1 MMUType = -$1A
                $DBC = by type: 0,2,5,6 → rts; 1 → $4C20; 3 → $4B86; 4 → $4BD8
```

`$DBC` is the `_SwapMMUMode` routine (`INITMMUTRAP` `$4AFE` installs it
as trap `$A05D`, and `BOOTRETRY` calls it directly before that).
`$4BD8` (type 4, the real IIci's) switches with `pmove` to the CRP in
`$CB4`/`$CB8`. `$4C20` (type 1) only flips VIA2 port B bit 3 (the Mac
II's address-mode line) through `$CEC`, records the mode in `MMU32Bit`,
clears the instruction cache, and ORs the ASC's `$804` byte into itself
when it's nonzero. On our machine `$CEC` is ScratchMem and the ASC byte
is 0, so it records the mode and nothing else, which is right for a
flat 24-bit memory. `INITHIMEMGLOBALS` reads the same bit: in 24-bit
mode it clamps memory above 8 MB (`MMUCLEANUPFOR8MB`).

Our desktop vs MAME's IIci (both 8 MB, 6.0.8):

| Global | Ours | MAME |
|---|---|---|
| `MMUType` `$CB1` | 1 | 4 |
| `MMU32Bit` `$CB2` | 0 | 0 |
| `BoxFlag` `$CB3` | 5 | 5 |
| `jSwapMMU` `$DBC` | `$804C20` | `$40804BD8` |
| `MemTop` | `$800000` | `$800000` |
| `FinderName` | `Finder` | `Finder` |

The story (commit `2c395a4c`): with the Quadra's values the IIci booted
6.0.8 with bit 0 set, so `$DBC` was the always-32-bit routine,
`MMUType` was never written and `MMU32Bit` stayed 1 after `BOOTRETRY`'s
`_SwapMMUMode(0)`. System 6 doesn't run in 32-bit mode: the Finder came
up, said "Finder is older version" and relaunched forever. Comparing
low memory with MAME's IIci on the same System (`docs/roms/mame/lowmem.lua`)
showed `MMU32Bit` 1 against 0 and `FinderName` unset (normally `MYBOOT`
copies it from the boot blocks' `bbShellName` at `$1FF8`, §2.13).
Type 1 rather than the real 4: type 4's swap would load PMMU tables
that we don't build and don't emulate.

### 2.6 The rest of `STARTINIT1`

| Address | Call | Ours |
|---|---|---|
| `$118`–`$120` | `$DD0` = D0 (`BasesValid`), `$DD4` = D1, `$DD8` = A1 (ProductInfo) | |
| `$124` | `SETUPHWBASES` (`$910`) | runs: copies the (redirected) bases |
| `$128` | `INITSCC` (`$A2E`) | RTS (`init_scc_dat` `08 38 00 01 0D D1 67 04`, the first of 3 in the ROM) |
| `$12C` | `INITIWM` (`$9C0`), the SWIM | RTS |
| `$130` | `INITSCSI` (`$9A0`): clear 5380 registers | RTS |
| `$14C` | `FILLWITHONES` `$100`–`$2000`, keeping `$CFC` and `$DD0`–`$DD8` | runs |
| `$160` | `SETUPHWBASES` again (the fill wiped them) | runs |
| `$164`–`$176` | `CPUFlag` `$12F`, `BoxFlag` `$CB3`, `MemTop`, `BufPtr` (BootGlobs-`$42`), `$DDC` | |
| `$17A` | `INITMMUGLOBALS` (`$4B0A`) | runs (§2.5) |
| `$182` | `SYSERRINIT` (`$25F0`): exception vectors | runs |
| `$186` | CACR = `$3919`: both caches on, burst, write-allocate | runs |
| `$190` | `ENABLEEXTCACHE` (`$54BA`), the cache-card slot | NOP NOP |
| `$194` | `DISABLEINTSOURCES` (`$9F4C`): VIA1/RBV IER = `$7F` | RTS at `$9F4C` |
| `$19E` | `SETUPTIMEK` (`$800`): time DBRA loops against VIA timer 2 | replaced: `TimeDBRA`, `TimeSCCDBRA`, `TimeSCSIDBRA`, `TimeRAMDBRA` = 10000 (MAME's IIci measures `TimeDBRA` `$103F`) |
| `$1A2` | `INITHIMEMGLOBALS` (`$590`): `$1EF8`/`$1EF4`/`$1EF0`, 8 MB clamp, PWM and sound buffers below `BufPtr` (`$B0A`, `$312`, `SoundBase` `$266`) | runs; falls through into `BOOTRETRY` |

On MAME this whole stretch runs in order between 5.432 s
(`SETUPHWBASES`) and 5.437 s (`INITHIMEMGLOBALS`), 4 ms of it
`SETUPTIMEK`.

### 2.7 `BOOTRETRY` (`$1A6`)

The order is confirmed by MAME's first-execution trace and by our
A-trap trace. The MAME column is when the call first reaches its
target (blank: no newly entered routine there, or entered earlier from
elsewhere).

| Address | Call | Target | Ours | MAME (s) |
|---|---|---|---|---|
| `$1A6` | `move #$2700,SR` | | | |
| `$1AA` | `moveq #1,D0; jsr ($DBC)` | `_SwapMMUMode` to 32-bit | records the mode | 5.437 |
| `$1B2` | `INITGLOBALVARS` | `$600`: `ROMBase`, `ROM85`, `HWCfgFlags`, `SETUPHWBASES`, `jFetch`/`jStash`/`jIODone`, VBL queue, cursor vars, `JGNEFilter` |  | 5.437 |
| `$1B6` | `DISABLEINTSOURCES` | `$9F4C` | RTS | |
| `$1C0` | `INITINTHANDLER` | `$9E56`: level-1 dispatch table `$192`: one-second `$B140`, VBL `$A296` |  | 5.437 |
| `$1CA` | `INITDISPATCHER` | `$9A96`: unpacks the trap table at ROM+`$514E0` into `$E00` (Toolbox) and `$400` (OS); line-A vector `$28` = `$99B0` |  | 5.437 |
| `$1D4` | `INITMMUTRAP` | `$4AFE`: `_SetOSTrapAddress($A05D, ($DBC))` | first traced trap | 5.439 |
| `$1D8` | `GETPRAM` | `$780`: `_InitUtil` (`$C5CC`), key repeat, caret and double-click times | §2.12 | 5.439 |
| `$1DC` | `INITMEMMGR` | `$11A0` → `INITMEMVECT` `$CC60`: Memory Manager vectors at `$1E00`/`$1F00`, locations 0 and 4 | `$CCAA`: `move.l #ScratchMem,D0` instead of `ROMBase+$10000` | 5.444 |
| `$1F0` | `moveq #0,D0; _SwapMMUMode` | to 24-bit | | |
| `$1F4` | `SETUPSYSAPPZONE` | `$4B0`: `_InitZone` system heap at `$2000`, `ApplZone`, `COMPBOOTSTACK`, `_SetApplLimit` |  | 5.444 |
| `$1F8` | `INITSWITCHERTABLE` | `$760` |  | 5.444 |
| `$1FC` | `INITRSRCMGR` | `$11D0` → `_InitResources` (`$1AA32`): the ROM resource map |  | 5.444 |
| `$200` | `NMINIT` | `$1CC60` |  | 5.453 |
| `$20A` | `INITTIMEMGR` | `$B0C6` | returns at `$B0E2`, before the VIA1 timer setup | 5.453 |
| `$214` | `INITSHUTDOWNMGR` | `$580`: `_ShutDown(0)` |  | 5.454 |
| `$218` | `INITSLOTS` | `$1210`, §2.8 |  | 5.454 |
| `$222` | `INITDTQUEUE` | `$11F0` |  | 5.502 |
| `$226` | `ENABLEONESECINTS` | `$9FBA` | NOP×5 (`lea` at `$226`) | 5.502 |
| `$230` | `ENABLE60HZINTS` | `$9F9A` | NOP×5 (`lea` at `$230`) | 5.502 |
| `$23A` | `move #$2000,SR` | the first interrupt is taken here (`INTHND`, `ONESECINT`, `VBLINT` on MAME) | | 5.502 |
| `$23E` | `INITVIDGLOBALS` | `$BE0`: just `rts` in this ROM |  | 5.503 |
| `$242` | `COMPBOOTSTACK` | `$490`: SP = halfway between the system heap and `BufPtr`, less `$400` | replaced, same result, then `EMUL_OP_FIX_MEMSIZE` (`$1EF8` = `RAMSize`, `$1EF4` keeps its offset) | |
| `$26C` | `INITQUEUE` (`$999E`) | drive queue `$308` | | |
| `$27A` | `INITSCSIMGR` | `$9490`, §2.11 |  | 5.503 |
| `$284` | `INITIOMGR` | `$10F0`, §2.9 | `EMUL_OP_INSTALL_DRIVERS` | 6.004 |
| `$288` | `INITADB` | `$A802`, §2.10 |  | 6.122 |
| `$292` | `INITCRSRMGR` | `$1060` |  | 7.822 |
| `$296` | `_NewPtrSysClear($FC)` → `BasicGlob` | | | |
| `$2AC` | `INITGESTALT` | `$1693C` |  | 7.825 |
| `$2B6` | `INITRELIABILITY` | `$C860` |  | 7.829 |
| `$2C0` | `TEGLOBALINIT` | `$21F6E` |  | 7.831 |
| `$2CA`–`$2E2` | `_SetApplBase`(end of the system heap + `$4000`), `TheZone`/`ApplZone`/`HeapEnd` | | | |
| `$2EE` | `ENABLESLOTINTS` | `$9FC6` | NOP×5 | 7.831 |
| `$2F8` | `DRAWBEEPSCREEN` | `$530`: `_InitGraf`, `_OpenCPort`, the grey desktop with rounded corners |  | 7.831 |
| `$2FC` | `$326` | KeyMap = Command-Option-P-R (table at `$366`): `"Gone"` over XPRAM's `'NuMc'`, `SysParam` invalid, `_WriteParam`, `_InitUtil` (both PRAMs re-initialised), XPRAM `$B8`–`$BB` kept, restart at `$8C` |  | 7.874 |
| `$2FE` | `WarmStart` `$CFC` = `'WLSC'` | | `boot_progress`: warm start | |
| `$306`–`$316` | `_SwapMMUMode(32)`, BootGlobs-4 = `'WLSC'`, `_SwapMMUMode` back | | | |
| `$318` | `INITOVERPATCH` | `$474D0`: on 20 September 1989 (`Time` `$A13C8380`–`$A13DD4FF`) with Command-Option-C-I held, the Easter egg: a picture of the IIci team (the map's name notwithstanding) |  | 7.874 |
| `$322` | `bra BOOTME` | `$1D10` |  | 7.874 |

### 2.8 Slot Manager and our slot-0 declaration ROM

`INITSLOTS` (`$1210`) calls `INITSDTBL` (`$6F94`), `INITVBLQS`
(`$A350`) and `STARTSDECLMGR` (`$5DE0`); an error there is sad Mac
`$1F` via `CRITICAL`. `STARTSDECLMGR`: `INITJMPTBL` (`$6D60`),
`INITSLOTMGR` (`$5E20`), `INITSRSRCTABLE` (`$60A0`), `INITPRAMRECS`
(`$61A0`), `PRIMARY_INIT` (`$6242`).

`INITSLOTMGR` first calls `$5E48`, which builds the sInfo records:

- slots `$E`..`$1`: a byte read at the top of each slot's space
  (`$FEFFFFFF`, `$FDFFFFFF`, … `$F1FFFFFF`, at `$5E8A`) under a
  bus-error handler (`PINSTALLBUS`/`PRESTOREBUS`, which switch to 32-bit
  mode around it). A slot whose read faults stays at status `$FED4`
  (−300, empty);
- slot 0: the declaration data's top is `ROMBase` + (`ROMBase`+`$40`) − 1,
  the last byte of the main ROM (`$5EDC`).

Then for slots `$E`..`0` it gets each sInfo record (`_SlotManager`
`$2F`) and, for the slots whose status isn't negative, looks for a
declaration ROM (`$5F2A`): the byte-lanes byte at the top of the slot
(`$5F90`, up to 4 addresses, `$5FAA`), the format block, the test
pattern `$5A932BC7`. A slot without one gets status `$FECB`.

On MAME the first probe of an empty slot reads 0 instead of faulting
(`slots.txt`: `$FEFFFFFF` down to `$F1FFFFFF` from `$5E8A`, then 4
reads each from `$5FAA`), so every slot goes through `$5F2A` and fails
on the byte lanes. The built-in video is slot 0, from the main ROM.

On the real IIci slot 0 is the on-board video: the ROM's last `$13E6`
bytes are its declaration data (format block at `$7FFEC`). MacPhoenix:

- `InstallSlotROM()` (`src/core/slot_rom.cpp`, called from
  `patch_rom_32`) writes our declaration ROM over the end of the main
  ROM: board sResource ("Basilisk II Slot ROM"), a video sResource per
  mode with a small driver whose Open/Control/Status are EmulOps
  (`VIDEO_OPEN` etc.), a CPU sResource, an Ethernet sResource; format
  block, CRC (`ChecksumSlotROM()`), test pattern `$5A932BC7`. The
  Slot Manager finds it as slot 0 by the rule above, with nothing
  else to change;
- `nubus_dat` (`45 FA 00 0A 42 A7 10 11`, at `$5E84`) NOPs the probe
  read, its record and the slot count (`$5E8A`–`$5E8F`): no slot space
  is touched and slots 1–`$E` keep status `$FED4`, so `$5F2A` runs for
  slot 0 only. (In our memory a read there would return 0 anyway and
  fail the byte lanes, as on MAME; this ROM doesn't need a bus error to
  call a slot empty.)

### 2.9 I/O Manager and drivers (`INITIOMGR` `$10F0`)

```
$10F0  bsr INITIOPMGR ($5262)          ; the IIfx's IOP manager; runs here too (trap installs)
$10F4  BasesValid bit 16: ENABLESWIMIOPINTS; bit 17: SCCIOPBYPASS   (not on the IIci)
$1112  UnitNtryCnt = $40; UTableBase = _NewPtrSysClear($200)
$1138  _Open ".Sony"                    ; DRVR 4
$1142  _Open ".Sound"                   ; DRVR 3
$1144  movea.l SonyVars,A1; move.l ($130,A1),-(SP); bsr FROVIDEO ($1000); move.l (SP)+,D0
$1152  BoxFlag 3 (SE/30): put the long back into SonyVars
$1166  GetResource('SERD',0) and call it
```

MacPhoenix:

- `.Sony` (`DRVR` 4 at `$6C3E0`, `$4170` bytes) is overwritten at patch
  time: `sony_driver[]` at `+0`, `disk_driver[]` (.Disk) at `+$100`,
  `cdrom_driver[]` (.AppleCD) at `+$200`, the `CheckLoad` stub at
  `+$300`, icons at `+$400`–`+$A00`, the PutScrap and GetScrap stubs at
  `+$C00`/`+$D00`. The ROM's own `_Open ".Sony"` at `$1138` opens ours
  (`EMUL_OP_SONY_OPEN`, the first `CHECKLOAD`: `DRVR` 4);
- `$1142`: `EMUL_OP_INSTALL_DRIVERS` replaces the `.Sound` `_Open`
  (`boot_progress`: drivers). The handler: `SoundActive` `$27E` = 0
  (the `.Sound` open would have cleared it; commit `001cb5f5`),
  `InstallDrivers(A0)` (`rom_patches.cpp`): Microseconds (`$A093`) and
  DebugUtil (`$A08D`) installed by `_SetOSTrapAddress`; `.Sony` is
  already in the unit table, so it's left alone; `.Disk` at refnum -33
  and `.AppleCD` installed with `_DrvrInstallRsrvMem` and opened
  (`DISK_OPEN` adds the drives to the drive queue). Then the PutScrap
  and GetScrap patches (`_SetToolTrapAddress`), and a 4 KB fake ASC
  register block (`_NewPtrSysClear`, version `$0F` at `+$800`) whose
  address goes into `ASCBase` `$CC0`;
- `$1144`–`$1150`: the SonyVars save and restore NOPed, the
  `bsr FROVIDEO` kept (video, §2.9.1);
- `SERD` 0 (`$6AB90`): `+12` = `EMUL_OP_SERD; rts`, which installs our
  `.AIn`/`.AOut`/`.BIn`/`.BOut` (copied to `+$100`–`+$400`) with refnums
  -6 to -9 (`InstallSERD()`).

The `.netBOOT` RTS at `$1256` is a Quadra site; in this ROM `$1256` is
zero padding after `GOOFYDOEJECT`'s `jmp`, never executed.

#### 2.9.1 Video (`FROVIDEO` `$1000`)

`OPNVIDDEFLT` (`$D00`): `_GetVideoDefault` (`$A080`, PRAM), the sResource
it names (`_SlotManager` `$16`); if that's a display/video sResource,
`RDVIDPARAM` (`$BF0`: mode, base via `FINDDEVBASE`, gamma), `OPENSDRVR`
(`$CA0`: open the sResource's driver), `INITVIDDEFLT` (`$D50`:
`GDevice`, `INITDEFGAMMA` `$E40`). Otherwise `FROVIDEO` searches the
display/video sResources (`$15`), and with none `INITDUMMYSCREEN`
(`$F40`).

On ours it finds our slot-0 sResource, `_Open`s our driver
(`VIDEO_OPEN`; video driver code in `src/drivers/video/`), and
`FINDDEVBASE` (`$5B40`) returns the `minorBaseOS` long unmasked
(patch at `$5B78`: NOP NOP, `move.l D1,D2`, `bra $5BDE`; the real code
ANDs it to 24 bits and ORs in the slot's `$Fs` base). `ScrnBase` =
`$890000`. Monitors offers 1 to 32 bits per pixel; 640×480×32 (1.2 MB)
fits below 16 MB in this layout.

### 2.10 ADB (`INITADB` `$A802`)

Runs, with VIA1 writes NOPed (`$A8A8`: `move.b #$84,$1C00(A1)`; `$A662`
and `$A670`: shift-register setup in the ADB code after `ADBOP`). The
byte check at `$A8A8` (non-zero here) picks this set, the 512 KB one.
`_ADBOp` (`$A3DC`) is replaced by `adbop_patch[]` (`EMUL_OP_ADBOP`,
`src/core/adb.cpp`), which answers the bus scan: keyboard at address 2
(register 3 `$6205`), mouse at 3 (`$6301`). `MYBOOT` later runs
`INITADBDRVR` (`$A7B0`, the `ADBS` 2 resource from the System). On
MAME `INITADB` takes 1.7 s (6.12 to 7.82 s), talking to the real
transceiver; ours returns at once.

### 2.11 SCSI

`INITSCSI` (`$9A0`) is an RTS. `INITSCSIMGR`/`SCSIINIT` (`$9490`) runs:
globals, PRAM, a Time Manager task, `_SCSIDispatch(0)` (reset). `_SCSIDispatch`
(`$7460`, `SCSIOLD`) is `EMUL_OP_SCSI_DISPATCH; move.l A1,A7; jmp (A0)`.
At boot `SCSILOAD` (`$71F0`) looks for driver partitions on each ID
(14 dispatches) and finds none: our disks come through `.Disk`, not
SCSI.

### 2.12 PRAM and XPRAM

Paths in this ROM:

- `ClkNoMem` (trap `$A053`, `$B1E4`, called with A5 as the return):
  every PRAM, XPRAM and clock access once the trap table exists, and
  `VALIDATEPRAM` before. `_ReadXPRam`/`_WriteXPRam` (`$B186`/`$B190`)
  go through `CLKRW` (`$B16A`) → `($54C)` = `$B1E4`. Patched to
  `EMUL_OP_CLKNOMEM; jmp (A5)` (its first word isn't `jmp (A5)`, so the
  1 MB form isn't used). The handler (`emul_op.cpp`) decodes the
  command in D1: XPRAM `[reg]`, classic PRAM `$08`–`$0B`/`$10`–`$1F`,
  the clock from the host time;
- `RDXBYTE` (`$445DC`) and `U_RDXBYTE` (`$43148`): the diagnostics' and
  `INITMMU`'s direct reads. Both are patched to `EMUL_OP_READ_XPRAM;
  jmp (A6)` (the "ROM10" and "ROM11" signatures both match this ROM),
  which returns `XPRAM[D1]` raw.

`$8A` (addressing mode): `CLKNOMEM` reads clear bits 0 and 2 on 24-bit
profiles (System 6 won't start in 32-bit mode) and set them on 32-bit
ones; writes are forced to 32-bit only on 32-bit profiles. The raw
`RDXBYTE` read in `INITMMU` doesn't filter, which is one reason
`PATCH_BOOT_GLOBS` clears BootGlobs `-$19` bit 0 itself.

XPRAM isn't persisted here (`LoadXPRAM`/`SaveXPRAM` are the null
drivers'): every boot starts from `XPRAMSetDefaults()` plus the boot
device (`emulator_init.cpp`: drive 1, refnum -33 at `$78`–`$7B`).
`VALIDATEPRAM` (`$B2FE`) reads the `'NuMc'` signature at XPRAM `$0C` a
byte at a time with `lsl.l #8,D2` between the reads, so `CLKNOMEM` must
replace only D2's low byte, as the real `ClkNoMem` does. Until
2026-10-21 the EmulOp set all of D2, the signature never matched, and
the ROM reset XPRAM at every boot: it wrote `'NuMc'`, zeroed `$20`–`$FF`
and `$00`–`$07` and wrote its defaults at `$76`–`$89` (266 `CLKNOMEM`
calls before `PATCH_BOOT_GLOBS`; 8 now, 310 in the whole boot to the
desktop against 568). It didn't stop the boot only because the ROM's
default startup device, drive -1, refnum -33 (`$FFDF`), is ours too.
Before the fix `_InitUtil` (`$C5CC`) also took its re-initialisation
path in the A-trap trace, where MAME's reads the signature back and
writes nothing (no `_WriteXPRam` from it in `traps.tsv`); not
rechecked since.

### 2.13 Boot device, boot blocks, System file, INITs, Finder (`BOOTME`/`MYBOOT` `$1D10`)

`FINDSTARTUPDEVICE` (`$1350`): `EMBARKONSEARCH` (`_GetDefaultStartup`,
`_GetOSDefault`), `LOADSLOTDRVRS`, `LOADDRIVERS` → `SCSILOAD`,
`WAITFORINTERNAL` (`_InternalWait`, the PRAM timeout), then the drive
queue: `FINDNEXTCANDIDATE`, `SELECTDEVICE`, `CHECKMOUSEEJECT`,
`GETSTARTUPINFO` (`_Read` of the boot blocks), `SHOWSUCCESS`
(`HAPPYMAC`). Returns with A6 = the boot blocks.

`MYBOOT` (A6 = boot blocks; 6.0.8's header: `bbVersion` `$17`,
`bbSysName` "System", `bbShellName` "Finder", `bbDbg1Name` "Macsbug",
`bbDbg2Name` "Disassembler", `bbScreenName` "StartUpScreen",
`bbHelloName` "Finder", `bbScrapName` "Clipboard File", 10 FCBs, 20
events):

| Address | What |
|---|---|
| `$1D24` | `bbVersion`'s high byte `$44`, or its top bits `$C0`: `jsr 2(A6)`, the boot blocks' own code (System 7). 6.0.8's `$0017`: the ROM does it all |
| `$1D3C` | `_InitEvents`(`bbCntEvts`), `_InitFS`(`bbCntFCBs`) |
| `$1D52` | `_MountVol` the boot drive; failure → `$203E`: mark the drive bad, `bra BOOTRETRY` |
| `$1D60` | `_GetVolInfo`; `$2142`: `_HFSDispatch` (`OpenWD` on the blessed folder) + `_SetVol` (`BootDrive` `$210` = the WD refnum, `$8023` here and on MAME) |
| `$1D70` | `bbSysName` → `SysResName` `$AD8`; `_InitResources` opens the System file, and from inside it the first System resources load: `ROvr` 0 and `ROv#` 1660 (the ROM resource overrides for `$067C`; MAME and ours); failure → `_UnmountVol`, retry |
| `$1D98` | `GetResource('DSAT',0)` → `DSAlertTab` `$2BA`; `_InitFonts` |
| `$1DAE` | `_OpenResFile(bbScreenName)`: a startup screen if there is one |
| `$1E5C` | `SysError($28)` (dsGreeting): "Welcome to Macintosh", when there's no startup screen (MAME 28.66 s) |
| `$1E62`–`$1E94` | debuggers: `bbDbg1Name`, `bbDbg2Name` loaded below `BufPtr` and called; `SysError(-10/-11)` adds "MacsBug installed" etc. |
| `$1E98`–`$1EA4` | the System file's data fork, read past its first 512 bytes into the system heap and called (`jsr (A1)` at `$1EA4`; MAME 28.99 s). 6.0.8's loads `PTCH` 0 and `PTCH` 1660 (`$067C`), which load the `'ptch'` resources and patch traps. MAME's trace calls this step "bootblocks" |
| `$1EAC` | `_SlotManager($32)`; `INITADBDRVR` (`$A7B0`), `MOUSEINIT` (`$21A0`), `OPENSLOTS` (`$2240`) (MAME 32.30, 32.40, 32.46 s) |
| `$1EC6` | `DSAT` 2; `_Control` to `.Sony` (refnum -5, csCode 9: track cache); `_InitApplZone`; `_RDrvrInstall` |
| `$1F02`–`$1F60` | INITs: `GetResource('INIT', n)` for n = 0..35, each found one detached and called (`jsr (A0)` at `$1F52`): 13 in 6.0.8's System, among them 0, 1, 3–7, 10, 11, 22, 30 and 31; INIT 31 opens the System Folder's INIT, RDEV and cdev files |
| `$1F62` | `_ClosePort`, `_PurgeMem`; system heap size from the boot blocks (`$86`–`$90`) |
| `$1FD4` | `_SetApplBase`, `_InitApplZone`; `bbScrapName` → `$970` (`ScrapTag` `$96C`) |
| `$1FF8` | `bbShellName` → `FinderName` `$2E0` |
| `$2004`–`$2036` | `_GetVol`; `bbHelloName` if the file exists, else `bbShellName`: `_Launch` at `$2036` (MAME 33.29 s; `LAUNCH` `$BE0A` is entered from the System's patch of the trap) |
| `$2038` | neither: `SysError($29)`, `_Debugger` |

On ours the first `INIT` resource sets the `extensions` phase, `CurApName`
= "Finder" the `Finder` phase, and the Finder going idle `desktop`
(`IDLE_TIME`, from the `SynchIdleTime` patch in `rsrc_patches.cpp`, or
the window heuristic). Every resource the ROM or System loads passes
`$1B8F4` (`movea.l $7F0,A0; jmp (A0)`, the jump to `VCHECKLOAD`
`$1B8FA`), which `patch_rom_32` points at the stub at `.Sony`+`$300`:
the real `CheckLoad`, then `EMUL_OP_CHECKLOAD`, which counts it for
`boot_progress` and lets `rsrc_patches.cpp` patch resources by
signature.

After the Finder starts, the first `.Sony` periodic action (`accRun`,
`SonyControl` csCode 65, `src/core/sony.cpp`) mounts the remaining
volumes and calls `PatchAfterStartup()`: ExtFS, the text feed
(`/api/text`) and the bridge trap.

### 2.14 Interrupts and the Time Manager

The 60.15 Hz host timer raises a level-1 interrupt. `INTHND` (`$9B00`)
would AND VIA1's IFR and IER and dispatch through the table at `$192`;
`$9BC4` is patched to `moveq #2,D0` + 4 NOPs, so every level-1
interrupt goes to `VBLINT` (`$A296`), where `addq.l #1,Ticks` and the
VIA1 flag write are replaced by `EMUL_OP_IRQ; tst.l D0; beq` to the
`rts` at `$A294`. The handler counts `Ticks` and, once `'WLSC'` is
written, runs the host Time Manager, video and other interrupt work;
D0 ≠ 0 lets the ROM's VBL processing continue at `$A2A0`.

`_InsTime` (`$AEEC`), `_RmvTime` (`$AE90`), `_PrimeTime` (`$AF1A`) are
EmulOps (`INSTIME`, `RMVTIME`, `PRIMETIME`, the latter two at IPL 7),
followed at `$AF26` by the Microseconds stub and at `$AF2A` by the
DebugUtil stub. `_PowerOff` (`$9E16`) is `EMUL_OP_SHUTDOWN`.
`ENABLEASCINTS`' write to `$1C13` from `$CEC` (`$A026`) is an RTS.

### 2.15 Sound

The ROM's `.Sound` (`DRVR` 3, `$70570`) is never opened (§2.9). Its
`_Open` sets `SoundBase` = `ASCBase` (`$705D4`; MAME's IIci has
`SoundBase` `$50F14000`); ours keeps the RAM buffer
`INITHIMEMGLOBALS` set (`$7FFCA2`). `ASCBase` is the real `$50F14000`
(masked `$F14000`) until `INSTALL_DRIVERS` replaces it with the fake
register block. `_SysBeep` isn't hooked on `$067C` ROMs (the SE's hook
is in `patch_rom_classic`).

---

## 3. `patch_rom_32` in the IIci ROM

Every write `patch_rom_32` makes, where it lands in this ROM, in source
order. Signature searches are limited to the range in the source; "not
found" patches are skipped (a few are fatal if missing, and all of
those are found). Diffing the patched ROM in a snapshot (`rom.bin`)
against the original shows exactly these sites, plus the slot ROM and
a few single bytes the system writes into ROM resources at run time
(`$72DC5` and others).

| Site | Apple routine | Patch | Effect |
|---|---|---|---|
| `$36D0` | ProductInfo `$36C0` | (signature) | `UniversalInfo` = `$36C0` |
| `$39C3`–`$39D1` | NuBusInfo | slots 1–15 = `$08` | no NuBus connectors (Gestalt) |
| `$36D2` | ProductKind | `model_id` | 5 unless `--model-id` |
| `$3494`… | decoder table | 15 entries → ScratchMem (all but ASC) | §4 |
| `$36D6` | DefaultRSRCs | 4, only without an FPU | not on the profile |
| `$8C` | reset | `EMUL_OP_RESET; jmp $BA` | §2.1 |
| `$C2` | `STARTINIT1`+`$A` | NOP×2 | no `GETHARDWAREINFO` |
| `$C6` | `STARTINIT1`+`$E` | NOP×15 | no `INITVIAS` |
| `$7C0` | `WHICHCPU` | `moveq #3,D7; rts` | CPU type from the profile |
| — | `CONFIGURERAM` | `clear_globs_dat` not found | nothing to skip in this ROM |
| `$44BC` | `$4392`'s MMU-type code | NOP×4, `moveq #0,D0`, NOP | MMU type 0 (then `PATCH_BOOT_GLOBS` writes 1) |
| `$45AA` | `$4538`, decoder kind 5 | `beq` → `bra` | no RBV frame buffer taken from RAM |
| `$4310` | `INITMMU`+`$12` | NOP | no MMU tables |
| `$445DC` | `RDXBYTE` | `EMUL_OP_READ_XPRAM; jmp (A6)` | raw XPRAM read |
| `$43148` | `U_RDXBYTE` | `EMUL_OP_READ_XPRAM; jmp (A6)` | raw XPRAM read |
| — | (1 MB form) | `read_xpram3` | 1 MB ROMs only |
| `$B1E4` | `CLKNOMEM` | `EMUL_OP_CLKNOMEM; jmp (A5)` | PRAM, XPRAM, clock |
| `$10E` | `STARTINIT1`+`$56` | `EMUL_OP_PATCH_BOOT_GLOBS`; NOP | §2.5 |
| `$A2E` | `INITSCC` | RTS | |
| — | `$4232` | bytes aren't `$50F1A101` | Quadra only |
| `$9C0` | `INITIWM` | RTS | |
| `$9A0` | `INITSCSI` | RTS | |
| — | ASC init | `init_asc_dat` not found in `$4000`–`$5000` | |
| `$190` | `STARTINIT1`+`$D8` | NOP×2 | no `ENABLEEXTCACHE` |
| `$9F4C` | `DISABLEINTSOURCES` | RTS | |
| `$800` | `SETUPTIMEK` | 4 × `move.w #10000`, RTS | |
| — | `$50A`, `$CCB4`, `$DBA2` | `REAL_ADDRESSING` only | not compiled |
| `$CCAA` | `INITMEMVECT`+`$4A` | `move.l #ScratchMem,D0` | locations 0 and 4 → ScratchMem |
| `$B0E2` | `INITTIMEMGR`+`$1C` | `movem.l (SP)+,D0-D5/A0-A4; rts` | no VIA1 timer setup |
| — | ModelID reads at `$5FFFFFFC` | both signatures not found | |
| end of ROM (from about `$7FABA`) | slot 0 declaration data | `InstallSlotROM()` | §2.8 |
| `$5E8A` | `$5E48` (in `INITSLOTMGR`) | NOP×3 | no slot probe |
| `$226` | `BOOTRETRY`+`$80` | NOP×5 | no `ENABLEONESECINTS` |
| `$230` | `BOOTRETRY`+`$8A` | NOP×5 | no `ENABLE60HZINTS` |
| `$490` | `COMPBOOTSTACK` | recomputed + `EMUL_OP_FIX_MEMSIZE`; RTS | |
| — | `fix_memsize2` | not found in `$4C000`–`$4C080` | |
| `$1256` | after `GOOFYDOEJECT` | RTS | lands in padding (Quadra's `.netBOOT` site) |
| `$1142` | `INITIOMGR`+`$52` | `EMUL_OP_INSTALL_DRIVERS` | replaces `_Open .Sound` |
| `$1144` | `INITIOMGR`+`$54` | NOP×4, skip 2 words, NOP | no SonyVars save/restore |
| `$A8A8` | `INITADB`+`$A6` | NOP×3 | no VIA1 IER write |
| `$A662`, `$A670` | ADB code after `ADBOP` | NOP×5, skip 2, NOP×2 | no VIA1 ACR/SR writes |
| `$2EE` | `BOOTRETRY`+`$148` | NOP×5 | no `ENABLESLOTINTS` |
| `$5B78` | `FINDDEVBASE`+`$38` | NOP×2, `move.l D1,D2`, `bra $5BDE` | device base = `minorBaseOS` |
| — | frame base (1 MB form) | 1 MB only | |
| `$A026` | `ENABLEASCINTS`+`$18` | RTS | no VIA2/RBV IER write |
| — | `via2b`, BlockMove/SANE `PTEST`, `MemoryDispatch` | not found / 1 MB only | |
| — | `.EDisk` | no `DRVR` 51 in this ROM | |
| `$6C3E0` | `.Sony` (`DRVR` 4) | our drivers, stubs, icons | §2.9 |
| `$6AB9C`, `$6AC90`… | `SERD` 0 | `EMUL_OP_SERD`; serial drivers | |
| `$A3DC` | `ADBOP` | `adbop_patch[]` (54 bytes) | |
| `$AEEC` | `INSTIME` | `EMUL_OP_INSTIME; rts` | |
| `$AE90` | `RMVTIME` | `EMUL_OP_RMVTIME` at IPL 7 | |
| `$AF1A` | `PRIMETIME` | `EMUL_OP_PRIMETIME` at IPL 7; then Microseconds `$AF26`, DebugUtil `$AF2A` | |
| `$7460` | `SCSIOLD` (`_SCSIDispatch`) | `EMUL_OP_SCSI_DISPATCH; move.l A1,A7; jmp (A0)` | |
| `$1B8F4` | before `VCHECKLOAD` | `jmp` to `.Sony`+`$300` | `CHECKLOAD` |
| `$9E16` | `POWEROFF` | `EMUL_OP_SHUTDOWN` | |
| `$6CFE0`, `$6D0E0` | (in `.Sony`) | PutScrap, GetScrap stubs | installed by `INSTALL_DRIVERS` |
| — | `PACK` 4 | one only | warning only without an FPU |
| `$9BC4` | `INTHND`+`$C4` | `moveq #2,D0`, NOP×4 | every level-1 interrupt is VBL |
| `$A296` | `VBLINT` | NOP×2, `EMUL_OP_IRQ`, `tst.l D0`, `beq $A294` | |

`docs/roms/dissam/tools/patch_sites.py` compares these sites with the
Quadra ROM (byte for byte, by signature, by routine name); its "-"
verdicts for the IIci mean "bytes differ from the Quadra's", not "not
applied". The fixed offsets are written regardless, and the table above
is what they hit.

---

## 4. Hardware bases

The IIci decoder table (`$348C`), the global each entry feeds through
`$94A`, and the value at the desktop:

| Bit | Device (DecoderInfo) | IIci address | Valid | Global | Ours | MAME |
|---|---|---|---|---|---|---|
| 0 | ROM | `$40800000` | yes | (`ROMBase` is set from the PC) | | |
| 1 | diagnostic ROM | `$58000000` | yes | | | |
| 2 | VIA1 | `$50F00000` | yes | `VIA` `$1D4` | `$888000` | `$50F00000` |
| 3 | SCC read | `$50F04000` | yes | `SCCRd` `$1D8` | `$888000` | `$50F04000` |
| 4 | SCC write | `$50F04000` | yes | `SCCWr` `$1DC` | `$888000` | `$50F04000` |
| 5 | IWM (the SWIM) | `$50F16000` | yes | `IWM` `$1E0` | `$888000` | `$50F16000` |
| 6 | PWM | 0 | no | `$B0A`, `$312` | RAM buffer | |
| 7 | sound | 0 | no | `SoundBase` `$266` | RAM buffer | `$50F14000` |
| 8 | SCSI (5380) | `$50F10000` | yes | `$C00` | `$888000` | `$50F10000` |
| 9 | SCSI DACK (pseudo-DMA) | `$50F12000` | yes | `$C04` | `$888000` | `$50F12000` |
| 10 | SCSI handshake | `$50F06000` | yes | `$C08` | `$888000` | `$50F06000` |
| 11 | VIA2 | `$50F02000` | no | `VIA2` `$CEC` | | |
| 12 | ASC | `$50F14000` | yes | `ASCBase` `$CC0` | fake block | `$50F14000` |
| 13 | RBV | `$50F26000` | yes | `VIA2` `$CEC` | `$888000` | `$50F26000` |
| 14 | VDAC | `$50F24000` | yes | | | |
| 15 | SCSI DMA | `$50F18000` | no | `$C00`–`$C08` | | |
| 16 | SWIM IOP | `$50F1E020` | no | `$1E0` | | |
| 17 | SCC IOP | `$50F0C020` | no | `$1D8`, `$1DC` | | |
| 18 | OSS | 0 | no | `$CEC` | | |

`BasesValid` `$773F` = bits 0–5, 8–10, 12–14. The RBV stands in for
VIA2 (its registers at small offsets: `$13` is the interrupt enable),
and the VIA2 global points at it. `patch_rom_32` rewrites every entry
the `$94A` table names (2–11, 13, 15–18) with `ScratchMem`, valid or
not; on our IIci that is Mac `$888000`. The ASC's is left alone and
replaced later (§2.15).

---

## 5. Pitfalls and lessons

- **A ROM's version word doesn't name the machine.** The IIci and the
  Quadra 650 are both `$067C`; the IIci needs the Mac II's 24-bit
  layout. The profile is chosen by checksum.
- **BootGlobs `-$19` bit 0 is start-in-32-bit-mode.** Set, this ROM
  installs a `_SwapMMUMode` that never leaves 32-bit mode and leaves
  `MMUType` unwritten. On a 24-bit profile it must be clear, and the
  MMU type must have a swap routine that works without tables (1). The
  boot phase reached "Finder" either way: the IIci test checks for
  the Finder's Trash label in `/api/text`, not the phase.
- **Compare low memory with MAME.** The 32-bit-mode bug was found by
  diffing a handful of globals at the Finder against MAME's IIci.
  `MMUType` and `jSwapMMU` differ on purpose (1 and `$4C20` against 4
  and `$4BD8`); `BufPtr` is higher on ours (no RBV frame buffer taken
  from RAM, §2.4, among other things); `SoundBase` differs (§2.15).
- **Patch sites from another ROM need checking here.** Quadra fixed
  offsets land on the same routines in this ROM almost everywhere
  (same `STARTINIT1`/`BOOTRETRY` layout), but `$1256` lands in padding
  and the 1 MB-only patches don't apply. `patch_sites.py` shows which
  are byte-identical.
- **Skipped `_Open`s leave their side effects undone.** Skipping the
  `.Sound` open left `SoundActive` at `$FF` (World Builder games waited
  forever; `INSTALL_DRIVERS` clears it) and leaves `SoundBase` at the
  RAM buffer.
- **EmulOps must keep the registers the ROM code relies on.**
  `CLKNOMEM` used to set all of D2 where the real routine replaces its
  low byte; `VALIDATEPRAM`'s 4-byte compare failed and XPRAM was reset
  at every boot (§2.12), unnoticed because the ROM's default startup
  device matches ours.
- **Slot 0 needs no slot hardware.** The Slot Manager finds slot 0's
  declaration data at the end of the main ROM; replacing those bytes
  is the whole video hookup. The `$0178` ROMs have no slot 0
  (`../../MacIIFamily.md`).

---

## 6. Reproducing

Listing (Ghidra 12, PyGhidra; one Ghidra process at a time on this box):

```bash
docs/roms/dissam/tools/bootstrap.sh            # once: vendor repos + PyGhidra venv
docs/roms/dissam/tools/export_listing.sh iici  # -> docs/roms/dissam/iici/rom.lst
```

Ghidra leaves some code as data (`CONFIGURERAM`, `INITVIDGLOBALS`).
capstone (`python3 -m pip show capstone`) disassembles any range of the
raw ROM:

```python
import capstone
import os
rom = open(os.path.expanduser("~/storage/roms/iici.rom"), "rb").read()
md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_030)
for i in md.disasm(rom[0xA70:0xBE0], 0x40800A70):
    print("%05x  %s %s" % (i.address - 0x40800000, i.mnemonic, i.op_str))
```

(capstone doesn't print index scales: `move.w $4B4E(PC,D0.w),D0` at
`$4B40` is `D0.w*2`.)

Patch sites against the Quadra: `python3 docs/roms/dissam/tools/patch_sites.py`
(`-v` for the bytes of the misses). The ProductInfo records:
`print_rom_info()` in `rom_patches.cpp` (`PrintROMInfo`) lists them, or
walk the list at `$32D4` (each entry is an offset from its own address).

Our boot, traced (a copy of the image, never the original):

```bash
cp --reflink=auto ~/storage/images/6.0.8.img ~/storage/tmp/boot.img
./build/mac-phoenix --backend uae --config /dev/null --ram 8 --port 19651 \
    --log-level 2 --trace-atraps --disk ~/storage/tmp/boot.img \
    ~/storage/roms/iici.rom > ~/storage/tmp/iici.log 2>&1 &
curl -s -X POST localhost:19651/api/emulator/start
curl -s -X POST -d '{"condition":"text=Trash","timeout":30}' localhost:19651/api/wait
curl -s -X POST -d '{"name":"iici"}' localhost:19651/api/snapshot
```

`--log-level 2` logs every EmulOp (the order in §1 comes from it). The
snapshot (`~/storage/snapshots/iici/`) has `ram.bin` (low memory),
`rom.bin` (the patched ROM: diff it against the original for §3) and
`atraps.tsv`: every (trap, caller PC) pair in first-call order (`seq`
column); map the PCs (`$800000` + offset) through Apple's map to get
the routine order of §2.7 and §2.13. Without the web server,
`--no-webserver --deterministic --snapshot-at NAME@SECS` writes the same.

MAME (`docs/roms/mame/`): `bin/maciici maciici -rompath roms -hard
DISK.hd -video none -sound none -nothrottle -autoboot_script lowmem.lua`
(a raw image needs a `.hd` name; RAM is `-ram 8m`, lower case: MAME
refuses `8M`; without `-ram` the IIci gets 2 MB; `run.sh` passes `8m`).
`lowmem.lua` prints `MMU32Bit`, `FinderName` and others after
`LOWMEM_SECS`; add `readv_u8(0xcb1)` (`MMUType`) and `readv_u32(0xdbc)`
for the MMU comparison of §2.5. The first-execution routine trace used
for §1 and §2.7 (`routines.tsv`, `flow.txt`, `lowmem-finder.txt`) comes
from `docs/roms/mame/boottrace/` (`run_trace.sh maciici`, then
`analyze.py`), a tracer patched into MAME's 68k core that names each
newly executed routine with Apple's labels.

---

## 7. See also

- `src/core/rom_patches.cpp` — `patch_rom_32`, `InstallDrivers`, `InstallSERD`
- `src/core/emul_op.cpp` — `RESET`, `PATCH_BOOT_GLOBS`, `CLKNOMEM`, `INSTALL_DRIVERS`, `IRQ`
- `src/config/machine_profile.cpp` — the `iici` profile
- `src/core/slot_rom.cpp` — the slot-0 declaration ROM
- `src/core/boot_progress.cpp` — boot phases
- `tests/test_boot_iici.sh` — `boot_iici`, `boot_iici_mame68k`
- `../PATCHING_APPROACH.md`, `../common/hardware_map.md`, `../../MacIIFamily.md`, `../../mame/README.md`
