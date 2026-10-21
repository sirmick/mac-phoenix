# Mac II boot flow (the `$0178` ROMs)

Reset to Finder on the Mac II ROMs, rev A (`$97851DB6`) and rev B
(`$9779D2C4`), with the Mac II FDHD / IIx / IIcx / SE/30 ROM
(`$97221136`) alongside, because all three run the same code at the same
offsets apart from a short list of patches. What only `$97221136` does
(machine identification, the 68030 MMU, the SuperDrive, the SE/30's
screen) is in [`../maciix/boot_flow.md`](../maciix/boot_flow.md). The
plan this serves is [`../../MacIIFamily.md`](../../MacIIFamily.md).

Addresses are ROM offsets; the ROM runs at `$40800000` (`$800000` in
24-bit mode). Labels are Apple's, from `MacIIROM.lst.txt` (MPW 3.5 ROM
maps), which was made from **rev A** (see "Apple's map" below). Listings:
`macii/rom.lst` (rev A), `maciib/rom.lst` (rev B), `maciix/rom.lst`
(`$97221136`), all from `tools/export_listing.sh`.

How the facts were checked: byte diffs of the three ROMs, disassembly
(Ghidra listings, cross-checked with capstone and `m68k-linux-gnu-objdump`
for the PMMU instructions), and MAME traces of System 6.0.8 booting to
the Finder (`~/storage/tmp/boottrace/`: `macii` = rev B, `macii -bios
original` = rev A, `mac2fdhd`, `maciix`, `maciicx`, `macse30`). MAME's
`macii` and `mac2fdhd` have a 68851 (`MMUType` 3); its `maciihmu` has
the HMMU. The routine order in §3–§12 is the order the rev B trace
executes them in; what was only read in the code is marked
*(static)*.

---

## 1. The three ROMs

| | Rev A | Rev B | `$97221136` |
|---|---|---|---|
| File (`~/storage/roms/256KB ROMs/`) | `1987-03 - 97851DB6 - MacII (800k v1).ROM` | `1987-12 - 9779D2C4 - MacII (800k v2).ROM` | `1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM` |
| Machines | Mac II | Mac II | Mac II FDHD, IIx, IIcx, SE/30 |
| MAME | `macii -bios original` | `macii`, `maciihmu` | `mac2fdhd`, `maciix`, `maciicx`, `macse30` |
| Version word (`$08`) | `$0178` | `$0178` | `$0178` |
| Date in ROM | "Sat, Feb 21, 1987" (`$3FFEE`) | "September 1, 1987" (board sResource) | "15-Jul-88" (board sResource) |
| Patch area (top of ROM) | none (filler) | `$3F600`–`$3F735` | `$3F5A0`–`$3FEA7` |
| Board declaration data (top `$C8`/`$BA` bytes) | none | "Macintosh II", "Rev 1.2" | "Macintosh II", "Rev 1.3" |

**No routine moved.** The three images differ by in-place patches only:
each changed routine keeps its address and either has instructions
rewritten or jumps into a patch area at the top of the ROM. Every Apple
label therefore names the same routine in all three (1,498 of the map's
1,503 names verify byte for byte on rev B, 1,489 on `$97221136`; the
rest sit next to a patch).

Byte agreement: rev A vs rev B 98.98%; rev B vs `$97221136` 95.78%.

### Rev A → rev B (all Slot Manager and 32-bit slot addressing)

| Offset | Routine | Change in rev B |
|---|---|---|
| `$0000`–`$003F` | header | checksum, and the longs at `$2E`–`$3F` |
| `$07FC`–`$0819` | `RDVIDPARAM` | screen base from `_SlotManager` `sFindDevBase` (`$1B`) instead of computing `(spsBaseOffset & $FFF00000) + slot base` |
| `$3D32` | `SREADSTRUCT` | address passed through `_StripAddress` (patch `$3F6E0`) |
| `$3E53`–`$3E6B` | `SREADFHEADER` | format block address `$FsFFFF00` (was `$F00FFF00` + slot nibbles, a 24-bit-compatible form) |
| `$408E`–`$40FB` | `SMACBOOT` | removed; the space holds stubs that switch to 32-bit mode before `sGetcString`, `sReadStruct`, `sOffsetData`, `sReadPBSize`, `InitsRsrcTable`, and the entry for new selector `$1C` |
| `$42A1` | CRC check (`$428E`) | mask `$FFFFFF` (was `$0FFFFF`) |
| `$439A`–`$43D9`, `$4499` | `INITSDECLMGR` body (`$436A`) | runs in 32-bit mode; probes `$FsFFFFFF` (was `$FssFFFFF`), steps `$1000000` (was `$1100000`) |
| `$49CA`–`$49DB` | `INITSRSRCTABLE` | 24-bit mode around `_DisposePtr` / `_NewPtrSysClear` |
| `$4E06` | `SGETDRIVER` | `_StripAddress` |
| `$4F56`–`$4FAF` | `SLOTMANAGER`, `SDMJUMPTABLE` | dispatcher wrapped (§8); selector `$1A` (`sMacBoot`) dropped, `$1C` added |
| `$6A84` | `OPEN` | slot driver's `dCtlDevBase` via `_SlotManager` (patch `$3F6AA`) |
| `$19006`–`$1901F` | after `GETDEVPIXMAP` | device base from the slot parameter block instead of recomputed |
| `$3F594`– | top of ROM | patch area, board declaration data |

### Rev B → `$97221136`

| Offset | Routine | Change |
|---|---|---|
| `$0000`–`$0095` | header, `NUMAP`, reset | checksum; `NUMAP` ROM entry `$40800019` (was `$40000019`), I/O `$50F00059` (was `$50000059`); `$90` jumps to the patch area |
| `$0110`–`$0123` | `STARTINIT1` | stores `BoxFlag`; calls `MMU_INIT` at its new address `$3AA8` |
| `$0166`–`$01AD` | `BOOTRETRY` | 68030 cache handler for `_HWPriv`; `RAMTEST` in 32-bit mode |
| `$0230`, `$02DE` | `COMPBOOTSTACK`, `INITHIMEMGLOBALS` | `MemTop` capped at 8 MB |
| `$0544` | `WHICHCPU` | 68030 test and machine identification |
| `$0744` | `REV8CHK` | always "not rev 8" |
| `$10F6` | `BOOTME` | system heap fraction from `MemTop` capped at 8 MB |
| `$1E10`, `$2ACE`, `$2AF4`, `$2C58`, `$2CB4`, `$2CE2`, `$2EB2` | `CRITERR`, `STARTTEST1`, test manager | each inline `btst #6` of VIA1 port A now branches as if clear |
| `$2B0C`–`$2B19` | `STARTTEST1` | RAM bank configuration before the memory test |
| `$3A8A`–`$3BA9` | `SWAPMMUMODE`, `MMU_INIT`, `COMPMMUTYPE` | rewritten for four MMU types (HMMU, 68851, 68030) |
| `$3D37`, `$4092`–`$40C5`, `$439E`, `$49CE`, `$4E0A`, `$4F5A`, `$6A88` | Slot Manager | same patches as rev B, patch area moved by `-$14` |
| `$2D735`–`$2F007` | `.Sony` (`DRVR` 4) | SWIM driver for the SuperDrive |
| `$31B21`–`$32325` | `MDEF` 0's slot | more `.Sony` code (the map still lists `MDEF` 0 here) |
| `$3F594`– | top of ROM | 32-bit MMU table, patch area, board declaration data |

The resource map (`$2765C`, located through the long at `$1A`) lists the
same 41 resources at the same offsets in all three.

### Apple's map

`MacIIROM.lst.txt` is rev A's: it puts `SMACBOOT` at `$408E`, where rev
A has `movem.l d3/a2-a4,-(a7)` and rev B and `$97221136` have a table of
`jsr`/`jmp` stubs. Names that do not hold outside rev A: `SMACBOOT`
(gone), `SDMJUMPTABLE` contents (§8), `MMU_INIT` (moved to `$3AA8` in
`$97221136`), `SWAPMMUMODE`/`COMPMMUTYPE` (rewritten in place in
`$97221136`), `_DRVR_4_.Sony` and `_MDEF_0_` (different code in
`$97221136`). `load_rom.py` borrows the map for `maciib` and `maciix`
and drops any name whose 16 bytes differ from rev A.

---

## 2. Header and reset

| Offset | Bytes | Meaning |
|---|---|---|
| `$00` | checksum | `$97851DB6` / `$9779D2C4` / `$97221136`; also the reset SSP |
| `$04` | `$4080002A` | reset PC |
| `$08` | `$0178` | ROM version |
| `$0A`, `$0E` | `jmp $90` | the other two header entry points both go to the reset code |
| `$1A` | `$00027658` | locates the ROM resource map |
| `$1E` | `jmp GOOFYDOEJECT` (`$E92`) | |
| `$22` `DISPOFF` | `$00026EE2` | offset of the packed trap table `INITDISPATCHER` unpacks |
| `$26` `CRITICAL` | `jmp CRITERR` (`$1DF8`) | the sad Mac |
| `$2A` | `jmp $90` | reset entry |
| `$40`–`$4B` | `$D0A`, `$64FA`, `$D98` | the offsets of `INITDISPATCHER`, `EMT1010` and `BADTRAP` *(static; no boot code reads them)* |
| `$50` `NUMAP` | 16 longs | the 24-bit page table for the 68851 / 68030 (§5) |

Reset path:

| Step | Rev A, rev B | `$97221136` |
|---|---|---|
| `$2A` | `jmp $90` | `jmp $90` |
| `$90` | `move.w #$2700,sr` / `reset` | `jmp $4083F61C` → `bra $3F856`: `move.w #$2700,sr`, `reset`, write `$2000` to CACR and read it back; non-zero (68030) → `pmove` 0 to TC (MMU off); `jmp $40800096` |
| `$96` | `jmp STARTTEST1` (`$2A14`) | same |

The CPU starts with the ROM overlaid at 0 (VIA1 PA4 high); `STARTTEST1`
runs from `$4080xxxx` and turns the overlay off.

---

## 3. STARTTEST1 (`$2A14`): diagnostics and RAM sizing

The power-on test owns the machine until it jumps to `STARTINIT1`.

1. `SP = $2000`, `VBR = $2806` (the vector table that starts the
   diagnostics module, labelled `MEMTEST`; its reset PC is
   `$40802A14`).
2. `$58000000` holds `$AAAA5555`? Then a test box is attached: jump
   through `$58000004` *(static)*.
3. VIA1 (`$50F00000`): DDRA (`+$600`) = `$3D`; port A (`+$1E00`) bits
   3 and 4 cleared (bit 4 is the ROM overlay). PA0 is toggled and PA1
   read back; if they follow each other, bit `$1A` of D7 is set
   ("test mode": a failure then enters the test manager instead of
   the sad Mac) *(static)*.
4. ROM checksum, `$36EC`: sums the words from `$40800004` for
   `$1FFFE` words and compares with the long at `$0`. `CKSUMBR`
   (`$370C`) is the `beq` that decides; a mismatch sets D6 =
   `$FFFF`, and `$2AB6` sends the boot to the failure path.
5. Reads of the SCSI (`$50F10010`) and IWM (`$50F16000`+`$1C00`,
   `+$1A00`) registers. On rev A/B, VIA1 PA6 set selects the "rev 8"
   addresses (`$50F1C000` for the IWM, §5); `$97221136` always takes
   the normal ones.
6. Memory tests and sizing, each returning through A6 with D6 = 0 on
   success:
   - `$36CA` with D7 = `$E`: data-line test at 0. In `$97221136` this
     call goes through `$3F8C8` first, which sets VIA2 PA6/PA7 (`$C0`,
     "bank B at 32 MB"), reads one long in each of the first 128 MB,
     restores DDRA and continues to `$36CA`.
   - `$3714`: memory test of `$0`–`$3FF`.
   - `$35A4`: RAM sizing. Tries the bank configurations in the table
     at `$3664` (`$20`/`$C0`, `$08`/`$80`, `$02`/`$40`, `$01`/`$00`:
     bank-B address in MB / VIA2 PA7–6) until a pattern written at
     the bank-B address doesn't alias address 0, then probes the sizes
     listed at `$366E` (1, 2, 4, 5, 8, 16, 17, 20, 32, 64, 65, 68, 80,
     128 MB). Returns D6 = top of RAM.
   - `SP = D6`; `$36CA` again just below it; `$3944`.
7. Success, and not in test mode: copy 64 vectors from `$2806` to `$0`,
   `VBR = 0`, `SR = $2500`, `A6 = SP` (= top of RAM),
   `jmp STARTINIT1`.

Failure path `$2C3C` *(static)*: play an error chime (`ERRORBEEP1`), then
`TMRESTART` (`$2E96`): `ERRORBEEP4`, `VBR = $2806`, and the serial test
manager loop (SCC init at `$33FA`, polls at `$3296` for `*` commands).
It does not return.

Today's `patch_rom_ii` gets to `STARTINIT1` through this failure path:
its patched ROM fails the checksum, `$2C64` is redirected to `$2E96`,
the chime at `$2EBE` is NOPed and `$2ECE` jumps to `$9A` (§13). The
memory tests, the sizing and the vector copy never run.

---

## 4. STARTINIT1 (`$9A`)

| Offset | Code | |
|---|---|---|
| `$9A` | `bsr INITVIA` (`$6F2`) | VIA1: ORA `$01`, DDRA `$3F`, ORB `$07`, DDRB `$87`, IER `$7F`. VIA2 (`$50F02000`): DDRA `$C0`, ORB `$05`, DDRB `$80` (PB3 an input), IER `$7F` |
| `$9E` | `bsr INITSCC` (`$7B4`) | reset both channels from the table at `$796` |
| `$A2` | `bsr INITIWM` (`$6AA`) | IWM mode register to `$17` |
| `$A6` | `bsr INITSCSI` (`$66C`) | clear the 5380's registers |
| `$AA` | `bsr WHICHCPU` (`$532`) | D7.b = CPU; `$97221136` also identifies the machine (§5) |
| `$AE` | `RAMTEST` (`$2BBC`) | A0 = A6 − `$300` .. A1 = A6: the top `$300` bytes |
| `$BA` | ASC base | A3 = `$50F14000`, or `$50F18000` if `REV8CHK` says rev 8 |
| `$D0` | `bsr BOOTBEEP` (`$5E4A`) | the startup chime, D0 = `$28` |
| `$D6` | boot stack | A1 = `$40000`, or A6 if RAM is smaller; cold boot (`$CFC` ≠ `'WLSC'`) runs `RAMTEST` from SP to A1 |
| `$F2` | `SP = A1` | |
| `$100` | `FILLWITHONES` (`$220`) | `$100`–`$1DFF` = `$FF`, keeping `$CFC` |
| `$108` | `CPUFlag` (`$12F`) = D7.b; `MemTop` (`$108`) = A6 | |
| `$110` | `SYSERRINIT` (`$201C`) | exception vectors `$08`–`$FC`, clears `$C2C`–`$CAF` and `$120`. Rev A/B: `lea $118(pc),a6; jmp`. `$97221136`: `swap d7; jsr $3F624` stores `BoxFlag` first (§5) |
| `$118` | `bsr SETUPTIMEK` (`$560`) | §4.1 |
| `$11C` | `bsr VIATIMERENABLES` (`$74C`) | VIA2 T1 free-running, latch `$196E`, PB7 output (the 60.15 Hz tick that reaches VIA1 CA1); VIA1 PCR low nibble cleared, IER = `$83` (CA1 and CA2) |
| `$120` | `jsr MMU_INIT` | `$3AA2` in rev A/B, `$3AA8` in `$97221136` (§5) |
| `$124` | diagnostic hook *(static)* | `$F80080` = `~$AA5555AA`? Then `jmp ($F80084)` with A1 = `$142` |
| `$13E` | `bsr INITHIMEMGLOBALS` (`$2DE`) | `BufPtr` (`$10C`) = `MemTop` − `$300` (and `$266`, `$312`, `$B0A`); `$97221136` caps `MemTop` at 8 MB here |
| | falls into `BOOTRETRY` | |

There is no `WHICHBOARD` routine in these ROMs; `$97221136`
identifies the machine inside `WHICHCPU` (§5).

### 4.1 SETUPTIMEK (`$560`)

Counts `dbra` iterations until VIA1 Timer 2 interrupts (one-shot
mode, T2 = `$030F`, IER = `$A0`, the level-1 vector `$64` pointed at
a local handler), three times:

| Loop body | Result |
|---|---|
| plain `dbra` | `TimeDBRA` (`$D00`) |
| `btst #0` of the SCC (`$50F04000`) | `TimeSCCDB` (`$D02`) |
| `btst #0` of SCSI `+$40` (`$50F10040`) | `TimeSCSIDB` (`$DA6`) |

MAME: `TimeDBRA` 2616, `TimeSCCDB` 1121, `TimeSCSIDB` 1046 on the
68020 machines (1207 and 1121 on the 68030 ones). The loops are bounded
by `dbra` (65,536 passes); with no interrupt each ends with the value
0. `INSTALL_DRIVERS` (`emul_op.cpp`) already puts 10000 in `$D00` and
`$D02` when they are 0 on 24-bit machines; `$DA6` is not set.

---

## 5. Machine type, CPU and MMU

### WHICHCPU (`$532`)

Points the illegal-instruction vector (`$10`) at `$54C`, then
`movec d0,cacr`:

| CPU | Result |
|---|---|
| 68000 | illegal → D1 = 0 |
| 68010 | illegal, exception frame vector offset `$10` → D1 = 1 |
| 68020 | executes → D1 = 2 |

D7.b = D1 → `CPUFlag`. In `$97221136` the `movec` is replaced by a jump
to `$3F74A`, which adds the 68030 test (CACR bit 8 sticks → 3) and the
machine identification; see [`../maciix/boot_flow.md`](../maciix/boot_flow.md) §3.

`BoxFlag` (`$CB3`, Gestalt machine type − 6): rev A/B clear it in
`MMU_INIT` (`clr.b $CB3`), so it is 0, "Mac II". `$97221136` sets it
from VIA1 PA6 and VIA2 PB3 (0 = II / II FDHD, 1 = IIx, 2 = IIcx,
3 = SE/30).

### REV8CHK (`$73E`)

`btst #6` of VIA1 port A (`$50F01E00`). In rev A/B, PA6 set means a
"rev 8" board with a different I/O layout *(static; MAME reads PA6 = 0)*:

| Global | Normal (PA6 = 0) | Rev 8 (PA6 = 1) |
|---|---|---|
| `IWM` (`$1E0`) | `$50F16000` | `$50F1C000` |
| `SCSIDMA` (`$C04`) | `$50F12000` | `$50F14000` |
| `SCSIHsk` (`$C08`) | `$50F06000` | `$50F08000` |
| `ASCBase` (`$CC0`) | `$50F14000` | `$50F18000` |

`$97221136` uses PA6 as a machine-ID bit instead, so its `REV8CHK` is
`ori #4,ccr; rts` (always "normal"), and every inline PA6 test
(`$1E10`, `$2ACE`, `$2AF4`, `$2C58`, `$2CB4`, `$2CE2`, `$2EB2`) branches
the same way.

### MMU_INIT and SWAPMMUMODE

Rev A/B (`$3AA2`):

1. Clear `MMUFlags` (`$CB0`), `MMU32Bit` (`$CB2`), `BoxFlag`
   (`$CB3`), `$CB4`, `$CB8`; `jSwapMMU` (`$DBC`) = `SWAPMMUMODE`
   (`$3A8A`).
2. `COMPMMUTYPE` (`$3ADE`): VIA2 PB3 = 0 and made an output; execute a
   `pmove` with the F-line vector caught. It traps → `MMUType`
   (`$CB1`) = 1 (the HMMU); it runs → 3 (a 68851).
3. 68851: `$CB4` = `NUMAP` (`$40800050`), `$CB8` = `$40`.
4. `SwapMMUMode(1)`: the rest of `STARTINIT1` runs in **32-bit mode**.

`SWAPMMUMODE` (D0 = 0 → 24-bit, else 32-bit; returns the old mode):

| `MMUType` | 32-bit | 24-bit |
|---|---|---|
| 1, HMMU | VIA2 PB3 = 1 | VIA2 PB3 = 0 |
| 3, 68851 | `pmove` 0 → TC (no translation) | CRP = `$7FFF0202`:`NUMAP`, TC = `$80F84500` |

`MMU32Bit` (`$CB2`) records the mode. With the HMMU, 24-bit mode maps
`$000000`–`$7FFFFF` to RAM, `$800000`–`$8FFFFF` to `$40800000`,
`$s00000`–`$sFFFFF` (s = 9..E) to `$Fs000000`–`$Fs0FFFFF`, and `$Fxxxxx`
to `$50Fxxxxx` (MAME `hmmu_translate_addr`). `NUMAP` gives the 68851
the same map (16 entries of 1 MB: RAM, ROM, slots `$F9000000`..
`$FE000000`, I/O).

`$97221136` rewrote all three for a fourth type, 4 = 68030 PMMU, with
CRP/TC pairs for each mode at `$3B7A`; see the IIx doc §4.

Mode changes during boot (MAME `macii`):

| Where | Mode |
|---|---|
| `MMU_INIT` (`STARTINIT1` `$120`) | 32-bit |
| `BOOTRETRY` `$148`: `jsr ($DBC)` with D0 = 1 | 32-bit (for retries) |
| `BOOTRETRY` `$166` (rev A/B: `moveq #0,d0; _SwapMMUMode`) | 24-bit, from here on |
| Rev B / `$97221136`: `INITSDECLMGR` and the Slot Manager routines that read card ROMs | 32-bit while they run |
| `LVL1INT` (`$607C`) | 24-bit for the handler, then back |

---

## 6. Hardware bases

None of these ROMs has a decoder table: the bases are constants in the
code. Low-memory globals are filled by `INITGLOBALVARS` and its helpers;
much of the early code (STARTTEST1, `SETUPTIMEK`, `INITVIA`,
`VIATIMERENABLES`, `SWAPMMUMODE`, `BOOTRETRY`) uses the constants
directly.

| Device | Address | Global | Set by |
|---|---|---|---|
| VIA1 | `$50F00000` | `VIA` `$1D4` | `INITVIAGLOBALS` `$6E8` |
| VIA2 | `$50F02000` | none | constants only |
| SCC | `$50F04000` | `SCCRd` `$1D8`, `SCCWr` `$1DC` (both the same) | `INITSCCGLOBALS` `$780` |
| SCSI 5380 | `$50F10000` | `SCSIBase` `$C00` | `INITSCSIGLOBALS` `$634` |
| SCSI DMA | `$50F12000` | `SCSIDMA` `$C04` | " |
| SCSI handshake | `$50F06000` | `SCSIHsk` `$C08` | " |
| ASC (sound) | `$50F14000` | `ASCBase` `$CC0` | `INITGLOBALVARS` `$300` |
| IWM / SWIM | `$50F16000` | `IWM` `$1E0` | `INITIWMGLOBALS` `$692` |
| ROM | `$40800000` | `ROMBase` `$2AE` | `INITGLOBALVARS` |

(Rev 8 alternatives in §5.) In 24-bit mode the I/O block is `$F00000`–
`$FFFFFF`. Registers are `$200` apart: VIA register n at `n × $200`.

In our emulator the 24-bit mask turns `$50Fxxxxx` into `$Fxxxxx`, which
is ordinary memory in the 32 MB allocation (reads return what was last
written). So nothing faults; the ROM's hardware probes read back their
own writes or zeros.

---

## 7. BOOTRETRY (`$142`)

Restartable: `BOOTME` branches back here when a boot fails.

| Offset | Code | |
|---|---|---|
| `$142` | `SR = $2700` | |
| `$146` | `moveq #1,d0; jsr ($DBC)` | 32-bit mode |
| `$14E` | VIA2 IER = `$02` | |
| `$15A` | `INITGLOBALVARS` (`$300`) | `ROMBase`, `ASCBase`, `ROM85` (`$28E` byte `$3F`), `HWCfgFlags` (`$B22`) = `$DC00`, the device-base helpers above, `JMPTBLINIT` for the I/O jump table at `$8F4`, queues, `INITCRSRVARS`, `GNEFILTER` |
| `$15E` | `INITXVECTTABLES` (`$4B6`) | interrupt dispatch tables (`$192` level 1, `$1B2`, `$2BE`, `$D70`), autovectors `$64` `INTHND`, `$68`/`$6C` `VIA2INT`, `$70` `LVL2INT` (SCC), `$74` `SPURIOUS`, `$78` `POWEROFF` |
| `$162` | `INITDISPATCHER` (`$D0A`) | §7.1 |
| `$166` | rev A/B: `moveq #0,d0; _SwapMMUMode; bsr GETPRAM` | 24-bit mode. `$97221136`: `jsr $3F608` (68030 `_HWPriv`, then the same) |
| `$16A` | `GETPRAM` (`$478`) | `_InitUtil` (PRAM via the VIA1 RTC lines), then key repeat, caret and double-click times from `SysParam` |
| `$16E` | `INITMEMMGR` (`$E04`) | `Lo3Bytes` (`$31A`) = `$FFFFFF`, `MinStack` `$400`, `DefltStack` `$6000` |
| `$172` | `SETUPSYSAPPZONE` (`$23E`) | `_InitZone` `$1E00`..`$3600`, 64 master pointers; `SysZone` = `ApplZone` = `TheZone`; `ApplLimit` from `COMPBOOTSTACK` (`MemTop`/2 − `$400`, `$97221136` caps `MemTop` at 8 MB) |
| `$176` | `INITSWITCHERTABLE` (`$464`) | |
| `$17A` | `INITRSRCMGR` (`$E22`) | `_InitResources` for the ROM map |
| `$17E` | `INITTIMERMGR` (`$E80`) | Time Manager queue |
| `$182` | `INITADBVARS` (`$2CA`) | ADB globals (`$CF8`), shift-register handler `FDBSHIFTINT` |
| `$186` | `INITSLOTS` (`$E32`) | §8 |
| `$18A` | `SR = $2000` | interrupts on |
| `$18E` | `INITADB` (`$6D80`, also labelled `KBDMNGR`) | §10 |
| `$192` | `INITVIDGLOBALS` (`$7E6`) | just `rts` |
| `$1A0` | cold boot: `RAMTEST` | `$97221136`: `jsr $3F620`, the same in 32-bit mode |
| `$1AE` | `COMPBOOTSTACK` | SP; `_SetApplLimit` |
| `$1BA` | `INITQUEUE` (`$6072`) on `DrvQHdr` (`$308`) | |
| `$1C2` | `jsr SCSISTUFF` (`$26636`) | SCSI Manager |
| `$1C8` | `INITIOMGR` (`$D9E`) | §9 |
| `$1CC` | `INITCRSRMGR` (`$BB8`) | cursor globals, `CRSRVBLTASK`, `CRSRCORE` |
| `$1D0` | `_SetApplBase` (system heap end + 16 KB) | |
| `$1F4` | VIA2 IER = `$82` | |
| `$200` | `DRAWBEEPSCREEN` (`$27C`) | `_InitGraf`, `_OpenCPort`, paints the grey screen |
| `$204` | `$CFC` = `'WLSC'` | warm-start flag |
| `$20C` | `bra BOOTME` (`$E96`) | §11 |

### 7.1 INITDISPATCHER (`$D0A`)

Fills the Toolbox table (`$E00`–`$1DFF`, 1024 entries) and the OS table
(`$400`–`$7FF`, 256 entries) with `BADTRAP` (`$D98`), then unpacks the
compressed table at `ROMBase + DISPOFF` (`$26EE2`) into them. The A-line
vector (`$28`) is `ATRAP68010` (`$64F2`) for a 68010, `EMT1010`
(`$64FA`) for a 68000 and `DSPATCH` (`$64BA`, also `ATRAP68020`) for a
68020 or 68030. From here A-traps work.

---

## 8. Slot Manager (`INITSLOTS`, `$E32`)

`INITSLOTS`: `SINTCORE` (slot interrupt queues), `INITVBLQS`,
`INITSPTBL`, `INITQUEUE` (`$D92`), `$D9C` = `DTCORE`, then
`STARTSDECLMGR` (`$4152`) with D0 bit 2 set on a warm start. A non-zero
result goes to `CRITICAL` with D7 = `$1F` (sad Mac).

`STARTSDECLMGR`: allocates `$D0` bytes for the `_SlotManager` jump
table (`$DB8`) from `SDMJUMPTABLE` (`$4F5C`, `$34` word offsets), then
calls, in order:

| `_SlotManager` | Routine | Error on failure |
|---|---|---|
| `$20` | `INITSDECLMGR` (`$41DC` → `$436A`) | `$B` |
| `$29` | `INITSRSRCTABLE` (`$48D6`) | `$C` |
| `$25` | `INITPRAMRECS` (`$4A78`) | `$D` |
| `$21` | `SPRIMARYINIT` (`$44B4`) | `$E` |

`SLOTMANAGER` (`$4F44`, the `$A06E` trap): `cmpi.w #$34,d0`, look up
`($DB8)`, jump. Rev B and `$97221136` jump instead to a wrapper (`$3F644`
/ `$3F658`): save `MMU32Bit`, switch to 24-bit, `_StripAddress` A0,
call, restore the caller's mode. The routines that read card ROMs switch
themselves to 32-bit (the stubs at `$408E`–`$40BF`).

### 8.1 INITSDECLMGR (`$436A`): the slot scan

1. Free any old `sInfo` array, allocate `$80` bytes
   (`_NewPtrSysClear`), `SInfoPtr` (`$CBC`) = it. Rev B/`$97221136`:
   then switch to 32-bit mode (`$3F66E` / `$3F682`).
2. A bus-error handler (`SBUSERRPROC`, `$4F28`) is installed through
   the record at the head of the array; each slot's record (16 bytes)
   gets a timeout word `$64` at `+$C`.
3. For slot D3 = 9 to `$E`, A5 = the slot's last byte:

   | | Slot 9 | Step | Slot `$E` |
   |---|---|---|---|
   | Rev A | `$F99FFFFF` | `+$1100000` | `$FEEFFFFF` |
   | Rev B, `$97221136` | `$F9FFFFFF` | `+$1000000` | `$FEFFFFFF` |

   Rev A scans in 24-bit mode (`BOOTRETRY` `$166` switched before
   `INITSLOTS`), so its address is `$sFFFFF`, the top of the slot's
   1 MB, which the MMU sends to `$Fs0FFFFF`: MAME rev A reads
   `$F90FFFFC`..`$FE0FFFFC` (68851, `NUMAP`). Rev B and `$97221136`
   switch to 32-bit mode for the scan and read `$F9FFFFFC`..
   `$FEFFFFFC`.
4. Read `(A5)` with the bus-error vector (`$08`) swapped; a bus error
   → status `-300` (`smEmptySlot`, `$FED4`).
5. Byte lanes: for D2 = 3 down to 0, `$41E0` reads `(A5)`; the byte
   must have its low nibble equal to the complement of its high
   nibble, and bit D2 of the lane mask; then `+$9` = lanes, `+$A` =
   low byte of A5. Then `_SlotManager $13` (`sReadFHeader`) and the
   test pattern `$5A932BC7` at `+$E` of the header. A5 − 1 on a miss;
   after four misses `-309` (`smBLFieldBad`).
6. Header checks (`$4244`): format `1`, ROM revision ≤ 9, reserved 0,
   directory offset non-zero in the low 24 bits with a zero top byte.
   Then CRC (`$428E`) over the length; mismatch `-301`.
7. `_SlotManager $2C` (`sCalcsPointer`) twice: the directory → the
   record's `+$0`. Errors `-304`.

The SE/30's built-in video is found here, as slot `$E` (IIx doc §6).

Empty slots: with all slots empty `INITSDECLMGR` still succeeds; the
rest of the boot runs without a screen (§9.1). MAME's Display Card 8•24
answers only at the top of its 16 MB, so rev A sees slot 9 empty.

---

## 9. INITIOMGR (`$D9E`) and the drivers

| Offset | Code |
|---|---|
| `$D9E` | `UnitNtryCnt` (`$1D2`) = `$40`; `_NewPtrSysClear($200)` (at `$DAC`) → `UTableBase` (`$11C`) |
| `$DC4` | `_Open` `.Sony` (`DRVR` 4, `$2D72C`) |
| `$DCE` | `_Open` `.Sound` (`DRVR` 3, `$2F010`) |
| `$DD0` | `FROVIDEO` (`$B58`), §9.1 |
| `$DD8` | `ROMMapInsert` (`$B9E`) = `$FFFF`, `_GetResource('SERD', 0)`, call it (`$2AB58`: installs the serial drivers) |

The `.Sony` driver is the IWM driver in rev A/B and the SWIM driver in
`$97221136` (same resource offset; its code spills into `MDEF` 0's
space). `.Sound` drives the ASC. AppleTalk (`.MPP`, `.ATP`, `.XPP`) is
opened later by the System.

### 9.1 Video: FROVIDEO (`$B58`)

1. `OPNVIDDEFLT` (`$928`): `_GetVideoDefault` (PRAM slot and
   sResource), `sRsrcInfo` (`$16`); it must be category/type
   `$0003`/`$0001` (display, video) with driver software `$0001`
   (Apple). Then `RDVIDPARAM` (`$7E8`), `OPENSDRVR` (`$8D0`),
   `INITVIDDEFLT` (`$974`).
2. Otherwise walk `sNextTypesRsrc` (`$15`) for the first such sResource
   and do the same.
3. None: `INITDUMMYSCREEN` (`$AA0`): a 32×32 one-bit "screen" in a
   `_NewPtr` block, a GDevice for it, and the boot carries on blind.
   MAME rev A with the 8•24 in slot 9 does this (`ScrnBase` = `$2FE4`,
   `ScreenRow` = 4) and still reaches the Finder; rev B finds the card
   (`ScrnBase` = `$F9900A00`, `ScreenRow` = 80).

`RDVIDPARAM`: the default mode, its parameter block (`sGetBlock`),
`ScrnBase` (`$824`), `ScreenRow` (`$106`), the screen size (`$C20`,
`$C22`, `ScrVRes`/`ScrHRes` at `$102`/`$104`), `CrsrBase` (`$898`),
`_AttachVBL` for the slot. Rev A computes the base itself; rev B and
`$97221136` ask `sFindDevBase`.
`OPENSDRVR`: `sReadDrvrName` (`$19`), then `_Open` with the slot
parameters (`$A200`). `INITVIDDEFLT`: `_NewGDevice`, `MainDevice`
(`$8A4`), `DeviceList` (`$8A8`), `TheGDevice` (`$CC8`), gamma.
Other video cards are opened later by `OPENSLOTS` (§11).

---

## 10. ADB, PRAM, clock

`INITADB` (`$6D80`): VIA1 PCR = 0, IER = `$84` (shift register),
DDRB |= `$30` (ADB state lines ST0/ST1), sets bit 5 of `$15D(ADBBase)`,
starts the bus probe and waits at `$6DD8` with interrupts on until the
shift-register interrupts (`FDBSHIFTINT`, `$7002`) clear the bit. Then
`$77F4` and `$7834` build the device table. `INITADBDRVR` (`$7724`) and
`MOUSEINIT` run later from `BOOTME`. `_ADBOp` is `ADBOP` (`$72FA`).

PRAM and clock: `INITUTIL` (`$DBE8`) through `CLKNOMEM` (`$DDD6`),
`READXPRAM`/`WRITEXPRAM`, bit-banged on VIA1 port B (PB0 data, PB1
clock, PB2 enable).

---

## 11. Boot device and boot blocks

`BOOTME` (`$E96`) starts with `FINDSTARTUPDEVICE` (`$151C`):

1. `EMBARKONSEARCH`: `_GetDefaultStartup`, `_GetOSDefault`; default
   device or "any".
2. `LOADSLOTDRVRS` (`$166C`): a slot boot device: `sFindsRsrcPtr`,
   `sExec` its boot record.
3. `LOADDRIVERS` → `SCSILOAD` (`$7AD4`): load drivers from SCSI
   disks' partition maps.
4. `WAITFORINTERNAL` (`$15EA`): wait up to 20 s (`_InternalWait`) for
   the internal SCSI disk (ID from `$C2F`), whose driver refnum is
   `-33 − ID`.
5. Walk the drive queue (`$308`) with `ISITFLOPORDEF` / `ISITANYTHING`;
   `GETSTARTUPINFO` reads 1 KB of boot blocks (`_Read`) and wants
   `'LK'`; `REACTTOFAILURE`, `VISUALUPDATE` (the disk icons) on
   failure; `SHOWSUCCESS` (happy Mac).

The rest of `BOOTME` uses the boot blocks (A6):

| Offset | Code |
|---|---|
| `$EBE` | executable boot blocks: `jsr 2(a6)` |
| `$EC2` | `_InitEvents` (`bbCntEvts` `$7C`), `_InitFS` (`bbCntFCBs` `$7A`) |
| `$ED8` | `_MountVol`; failure → `$11AE` (eject, `BOOTRETRY`) |
| `$F00` | `SysResName` (`$AD8`) = `bbSysName`; `_InitResources` (opens the System file) |
| `$F30` | `_InitFonts`; startup screen: `_OpenResFile(bbScreenName)`, `_GetPicture`, `_DrawPicture`; else `_SysError(40)`, "Welcome to Macintosh" |
| `$FF4` | debuggers (`bbDbg1Name`, `bbDbg2Name`) |
| `$1038` | `INITADBDRVR`, `MOUSEINIT`, `OPENSLOTS` (`$1370`: every slot driver, video ones added with `ADDVIDDEVICE`) |
| `$1056` | `DSAT` 2 → `DSAlertTab`; `.Sony` track cache on |
| `$1074` | `_InitApplZone`, `_RDrvrInstall` |
| `$107C` | `INIT` resources 0..35 from the System file (INIT 31 runs the INITs) |
| `$10D4` | system heap size from `bbSysHeapSize` or, for newer boot blocks, `MemTop` × `bbSysHeapFract` + `bbSysHeapExtra` (`$97221136`: `MemTop` capped at 8 MB); `_SetApplBase`, `_InitApplZone` |
| `$1154` | scrap name, `FinderName` (`$2E0`) = `bbShellName` |
| `$117C` | `_GetVol` → `BootDrive` (`$210`); working directory |
| `$118A` | `bbHelloName` if present, else the Finder: `_GetFileInfo`, `_Launch`; neither → `_SysError(41)` |

MAME `macii` (rev B) with 6.0.8: `CurApName` = "Finder" at 38 s.

---

## 12. Interrupts in the running system

| Level | Source | Vector | Handler |
|---|---|---|---|
| 1 | VIA1 (60.15 Hz on CA1, one-second CA2, ADB shift register) | `$64` | `LVL1INT` (`$607C`), table at `$192` |
| 2 | VIA2 (slot IRQs, SCSI) | `$68` | `VIA2INT` (`$622E`), `SLOTINT` (`$6284`) |
| 4 | SCC | `$70` | `LVL2INT` (`$60D0`) |
| 6 | power switch | `$78` | `POWEROFF` (`$62E6`) |
| 7 | NMI | `$7C` | `NMIEXCP` (`$2114`) |

`LVL1INT` switches to 24-bit mode first if `MMU32Bit` is set.

---

## 13. `patch_rom_ii` today, and what each stage needs

`patch_rom_ii` (`src/core/rom_patches.cpp`) was written against
`$97221136` and applied unchanged to all three ROMs. Trial runs
(`--config /dev/null --ram 8`, 6.0.8 disk, 2026-10-21): rev A and rev B
stay in phase "pre-reset"; A6 holds `$00800118`, the return address
`STARTINIT1` gave `SYSERRINIT`, and the CPU is running wild. `$97221136`
reaches phase "drivers" (the `INSTALL_DRIVERS` EmulOp) and then runs
wild too; the screen stays black.

Since no routine moved, one fixed offset per site is right for all
three ROMs, provided the patcher checks the bytes it expects there
first: the contents differ wherever the ROMs differ (§1). Apple's names
say what each site is.

What it does, and what is wrong with it:

| Patch | Comment says | What the site is |
|---|---|---|
| `$3F872` NOP | `pmove` clearing TC at reset | right for `$97221136`; filler text in rev A/B, never executed |
| `$2C64` jmp `$2E96`, `$2EBE` NOP, `$2ECE` jmp `$9A` | skip BOOTBEEP / "ADB scan" | the checksum-failure path and the serial test manager; it reaches `STARTINIT1` without the vector copy and `VBR = 0` |
| VBR writes `$2A20`, `$2BD2`, `$2C30`, `$2D08`, `$2ECA` NOP | keep VBR = 0 | `STARTTEST1` (`$2A20`), `RAMTEST` (`$2BD2` and its restore at `$2C30`), `TESTMANAGER` (`$2D08`), `TMRESTART` (`$2ECA`) |
| `$2B14` 3 NOPs, `$2B36`/`$2B4E`/`$2B62`/`$2B76` NOPs | "hardware probes" | `$2B14` is `$97221136`'s bank-config call (in rev A/B: the end of `lea $2B1A(pc),a6` and half of `jmp $36CA`); the rest are the memory tests and **RAM sizing** (`$35A4`). All are after the checksum, so on the failure path they never run anyway, and A6 is never the RAM size |
| `$9A`,`$9E`,`$A2`,`$A6`,`$C0`,`$D0` NOP | hardware init | `INITVIA`/`SCC`/`IWM`/`SCSI`, `REV8CHK`, `BOOTBEEP` |
| `$AE` `movea.l #RAMSize,a6`, then `$2246` | fix A6, "movea.l A6,A1" | stands in for the skipped sizing. `$2246` is `movea.l d6,a1`, not `a6`, and A0 (`A6 − $300`) is no longer set, so the first `RAMTEST` gets the wrong range |
| `$118`–`$13B` block | `clr.w ($CB1).w`, `move.l #ScratchMem,($1D4).w` … | **mis-encoded**: `$4279`/`$23FC` are the absolute-*long* forms, so each 2-byte address swallows the next instruction. It also replaces `SETUPTIMEK`, `VIATIMERENABLES`, `MMU_INIT` and the diagnostic hook |
| `$13E` NOP | | `INITHIMEMGLOBALS` (`BufPtr` never set) |
| `$3F7C4` NOP | `_SwapMMUMode` "in INITDISPATCHER" | in `$97221136`'s `$3F7B4` (after `INITDISPATCHER`); filler in rev A/B |
| `$3B24`, `$3B62` NOP | SwapMMUMode always via VIA2 | right for `$97221136`'s `SWAPMMUMODE`; in rev A/B each is the low word of `lea $50F02000,a0` in the HMMU path, so PB3 is written at `$50F04E71` |
| `$146`–`$14C` NOP | "uninitialised ($DBC)" | `jsr jSwapMMU`, unset because `MMU_INIT` was skipped |
| `$14E` NOP ×5 | VIA2 IER | the two instructions are 12 bytes; the last word, `$1C00`, is left and runs as `move.b d0,d6` |
| `$4F54` `beq` → `bra` | "`_SetTrapAddress` handler" | `SLOTMANAGER`: every `_SlotManager` call now fails |
| SERD `_Open`s `$2ABF2`, `$2ABFA`, `$2AC56`, `$2AC84` → `rts` | | inside `SERD` 0 |
| `$2F02A` `INSTALL_DRIVERS` | `.Sound` `_Open` | right (`.Sound` header `+$1A`) |
| `$1F4`–`$203` `INSTALL_DRIVERS` | | replaces VIA2 IER and `DRAWBEEPSCREEN` |
| `$186` NOP | skip slot scan | `INITSLOTS` |
| `$1A0` 3 NOPs (twice) | "ADB init" | the warm-start `RAMTEST` test; in rev A/B it cuts an 8-byte `cmpi.l` in half |
| `$1CC`–`$1F3` NOP | "video globals" | `INITCRSRMGR`, `_SetApplBase`, `ApplZone`/`HeapEnd` setup |
| `$DC4`, `$DCE` NOP | | `_Open .Sony`, `_Open .Sound` in `INITIOMGR` (not "INITCRSRMGR") |
| `$DD0` NOP | "cursor manager" | `FROVIDEO`: no video at all, not even the dummy screen |
| `$DD8`–`$DF3` NOP | SERD | right |
| `$6DCC` NOP ×3 | "IOP polling in INITIOMGR" | `INITADB`'s bus-probe flag; the Mac II has no IOP |
| `$DAC` `PATCH_BOOT_GLOBS` | "INITCRSRMGR" | `INITIOMGR`'s `_NewPtrSysClear` for the unit table |
| `serd_offset` = `$2AB5C` | `SERD` | `SERD` 0 starts at `$2AB58`; `+12` lands on its second `pea` (the SE patcher's `+12` is the first) |
| `.Sony` at `$2D72C`, `vCheckLoad` `$13078`, `LVL1INT` `$608E`, trap-located patches | | right in all three |

### Stage by stage

| Stage | Needs |
|---|---|
| Reset | `$97221136`: keep `$3F856` if the core has a 68030 MMU (`mame-68k`), else NOP the `pmove` |
| STARTTEST1 | `CKSUMBR` (`$370C`) `beq` → `bra` so the patched ROM passes; RAM sizing either emulated (VIA2 PA6/PA7 bank placement, aliasing above the RAM) or `$35A4` replaced by an EmulOp returning D6 = RAM top. Keep the vector copy and `VBR = 0` at `$2B8A`–`$2BAA`. VIA1 PA0→PA1 must not loop back (test mode) |
| STARTINIT1 | the device inits can run against plain memory; `BOOTBEEP` NOP; `REV8CHK` must say "normal" (VIA1 PA6 = 0 on rev A/B) |
| WHICHCPU / `BoxFlag` | rev A/B: nothing; `$97221136`: VIA1 PA6 and VIA2 PB3 per profile (IIx doc) |
| SETUPTIMEK | supply `TimeDBRA`/`TimeSCCDB`/`TimeSCSIDB` (`$D00`, `$D02`, `$DA6`) |
| MMU_INIT | let it run; `MMUType` 1 (HMMU) on the Mac II and II FDHD |
| `SWAPMMUMODE` | 24-bit only is enough until the slot scan (below) |
| INITSLOTS | run it, with a card: see below |
| INITADB | the existing NOP of the flag at `$6DCC`, or a VIA1 shift register that answers |
| INITIOMGR | `.Sony` replaced; `.Sound` hook; `SERD` patch at `$2AB64` (`$2AB58 + 12`) |
| FROVIDEO | let it run: it finds the card's video sResource and opens its driver |
| BOOTME | as for the SE: our disk drivers in the drive queue, refnum `-33` |

### The slot scan and the fake card

- **Rev A** probes `$FssFFFFF`. Under our 24-bit mask that is `$sFFFFF`,
  so a declaration ROM placed with its format block ending at
  `$EFFFFF` (slot `$E`) is found as it is.
- **Rev B and `$97221136`** probe `$FsFFFFFF` in 32-bit mode. Under a
  fixed 24-bit mask every slot reads `$FFFFFF`, the same byte. Either
  the CPU really switches to 32-bit addressing when `MMU32Bit` / VIA2
  PB3 says so (the HMMU emulation in MacIIFamily.md), with the card's
  ROM at `$FEFFFFxx` and its frame buffer in `$FExxxxxx`, or the
  address arithmetic is put back to rev A's form: `SREADFHEADER`
  `$3E50`, the CRC mask at `$429E`, `INITSDECLMGR` `$43CA`/`$43D6`/
  `$4496`, and the device-base code for selector `$1C` in the patch
  area (rev B `$3F720`, `$97221136` `$3F734`).
- The scan reads with the bus-error vector swapped; empty slots read 0
  in our memory and fail the byte-lane check without a bus error.
- `slot_rom.cpp` writes its block at the top of the main ROM. In rev B
  and `$97221136` that space holds the board's declaration data, which
  no ROM code reads.

---

## 14. See also

- [`../maciix/boot_flow.md`](../maciix/boot_flow.md): `$97221136`'s own
  code (IIx, IIcx, SE/30, II FDHD)
- [`../../MacIIFamily.md`](../../MacIIFamily.md): the plan
- [`../iici/boot_flow.md`](../iici/boot_flow.md),
  [`../se/boot_flow.md`](../se/boot_flow.md)
- [`../common/hardware_map.md`](../common/hardware_map.md)
- MAME `src/mame/apple/macii.cpp` (VIA port wiring, RAM banks),
  `src/devices/cpu/m68000/m68kmmu.h` (`hmmu_translate_addr`)
