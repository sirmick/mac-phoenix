# Mac II FDHD / IIx / IIcx / SE/30 boot flow (`$97221136`)

One ROM, four machines: `$97221136` (256 KB, version `$0178`, "15-Jul-88"
in its board declaration data). It is the Mac II rev B ROM with a set
of patches; every routine is at the same offset as in the Mac II ROMs,
so the stage-by-stage walk is shared and lives in
[`../macii/boot_flow.md`](../macii/boot_flow.md), with a `$97221136`
column wherever this ROM differs. This document covers what this ROM
does that the Mac II ROMs don't: telling the four machines apart, the
68030 and its MMU, RAM up to 128 MB, the SuperDrive, the SE/30's
built-in screen, and the patch area that holds it all. The plan this
serves is [`../../MacIIFamily.md`](../../MacIIFamily.md).

Addresses are ROM offsets (`$40800000` + offset; `$800000` + offset in
24-bit mode). Labels are Apple's Mac II map (`MacIIROM.lst.txt`, made
from rev A); the patch area has no names, so its entry points are given
by address. Listing: `maciix/rom.lst` (`tools/export_listing.sh
maciix`).

Run-time facts come from MAME traces of System 6.0.8 booting to the
Finder on `mac2fdhd`, `maciix`, `maciicx` and `macse30`
(`~/storage/tmp/boottrace/`): which patch entries run, from where, and
the values the identification code reads. Everything else is from the
code and marked *(static)*.

---

## 1. The machines

| | Mac II FDHD | IIx | IIcx | SE/30 |
|---|---|---|---|---|
| CPU, `CPUFlag` (`$12F`) | 68020, 2 | 68030, 3 | 68030, 3 | 68030, 3 |
| MMU, `MMUType` (`$CB1`) | HMMU (1) or 68851 (3) | 68030 PMMU (4) | 68030 PMMU (4) | 68030 PMMU (4) |
| VIA1 PA6 | 0 | 0 | 1 | 1 |
| VIA2 PB3 | 1 | 0 | 1 | 0 |
| `BoxFlag` (`$CB3`) | 0 | 1 | 2 | 3 |
| Gestalt machine type (`BoxFlag` + 6) | 6 (shared with the Mac II) | 7 | 8 | 9 |
| Floppy | SWIM, SuperDrive | SWIM | SWIM | SWIM |
| Video | NuBus card | NuBus card | NuBus card | built in, slot `$E` |
| Slots the ROM scans | 9–`$E` | 9–`$E` | 9–`$E` | 9–`$E` |
| MAME | `mac2fdhd` (a 68851) | `maciix` | `maciicx` | `macse30` |

Values read at run time (MAME, the `$3F76A` and `$3F786` reads of §3):

| MAME | VIA1 port A | VIA2 port B | `CPUFlag` | `MMUType` | `BoxFlag` |
|---|---|---|---|---|---|
| `mac2fdhd` | `$81` | `$4F` | 2 | 3 | 0 |
| `maciix` | `$81` | `$07` | 3 | 4 | 1 |
| `maciicx` | `$C1` | `$4F` | 3 | 4 | 2 |
| `macse30` | `$C1` | `$07` | 3 | 4 | 3 |

On rev A and B, VIA1 PA6 picked a "rev 8" I/O layout (`REV8CHK`, see
the Mac II doc §5). This ROM uses PA6 as a machine-ID bit, so `REV8CHK`
(`$73E`) is `ori #4,ccr; rts` (Z set, "normal layout") and each inline
PA6 test (`$1E10`, `$2ACE`, `$2AF4`, `$2C58`, `$2CB4`, `$2CE2`, `$2EB2`)
became a `bra`. The I/O addresses are the Mac II's on all four
machines.

---

## 2. Reset

`$2A` → `$90`, which in this ROM is `jmp $4083F61C` → `bra $3F856`:

```
3F856  move.w  #$2700,sr
       reset
       move.l  #$2000,d0
       movec   d0,cacr        ; WA bit: only a 68030 keeps it
       movec   cacr,d0
       tst.l   d0
       beq.s   1f
       lea     zero(pc),a0
       pmove   (a0),tc        ; 68030: translation off
1:     jmp     $40800096      ; → jmp STARTTEST1
```

From there `STARTTEST1` runs as on the Mac II, except at `$2B0C`, where
it calls `$3F8C8` before the first memory test (§5).

---

## 3. Identifying the machine

`STARTINIT1` calls `WHICHCPU` (`$532`) at `$AA`, as on the Mac II. Here
`WHICHCPU`'s `movec d0,cacr` is replaced by `jmp $4083F604` → `bra
$3F74A`, with D1 = 2 and the illegal-instruction vector already pointed
at `$54C` (68000/68010 land there as before):

```
3F74A  move.w  #$2909,d0
       movec   d0,cacr        ; enable + clear both caches
       movec   cacr,d0
       bclr    #8,d0          ; data cache enable stuck? → 68030
       beq.s   1f
       movec   d0,cacr        ;   data cache off again
       moveq   #3,d1
1:     movea.l a0,a7          ; WHICHCPU's epilogue
       move.l  (a7)+,(a1)
       move.b  d1,d7          ; D7.b = CPUFlag
       moveq   #2,d0
       btst    #6,$50F01E00   ; VIA1 port A, PA6
       beq.s   2f
       addq.w  #2,d0
2:     movea.l #$50F02000,a0  ; VIA2
       move.b  $400(a0),d1    ; save DDRB
       bclr    #3,$400(a0)    ; PB3 an input
       btst    #3,(a0)        ; VIA2 port B, PB3
       beq.s   3f
       addq.w  #1,d0
3:     move.b  d1,$400(a0)
       swap    d7
       clr.w   d7
       move.b  table(pc,d0.w),d7
       swap    d7             ; D7 = BoxFlag << 16 | CPUFlag
       rts
table: dc.b $FF,$04,$01,$00,$03,$02
```

The index starts at 2, so the first two table bytes are never used:

| PA6 | PB3 | Index | `BoxFlag` | Machine |
|---|---|---|---|---|
| 0 | 0 | 2 | 1 | IIx |
| 0 | 1 | 3 | 0 | Mac II (FDHD) |
| 1 | 0 | 4 | 3 | SE/30 |
| 1 | 1 | 5 | 2 | IIcx |

`STARTINIT1` stores `CPUFlag` at `$108` as before, then at `$110`
`swap d7; jsr $4083F624` → `$3F7A4`: `move.b d7,$CB3` (`BoxFlag`) and
`SYSERRINIT`, which returns through `$3F7B2`. `BoxFlag` has to be
written after `FILLWITHONES` (`$100`), hence the round trip in D7.
Rev A/B's `MMU_INIT` clears `$CB3`; this ROM's doesn't.

The only other absolute reference to `BoxFlag` in the ROM is `$3FE8C`,
the `.Sony` patch that picks a drive table (§7). The ROM has no SE/30-
or IIx-specific init beyond that; the SE/30's screen is found by the
Slot Manager like any card. (System software reads `BoxFlag` through Gestalt.)

---

## 4. MMU

`STARTINIT1` `$120` calls `MMU_INIT` at `$3AA8` (rev A/B: `$3AA2`; the
code in `$3A8A`–`$3BA9` was rewritten):

```
3AA8  clr.b   $CB0           ; MMUFlags
      clr.l   $CB4           ; MMUTbl
      clr.l   $CB8
      moveq   #4,d0          ; 68030 PMMU
      move.b  $12F,d1        ; CPUFlag
      subq.b  #3,d1
      bge.s   set            ; 68030
      ...                    ; 68020: VIA2 PB3 = 0 (output);
                             ; pmove under an F-line trap:
                             ; traps → 1 (HMMU), runs → 3 (68851)
set:  move.b  d0,$CB1        ; MMUType
      ...                    ; types 3, 4: $CB4/$CB8 → the CRP/TC
                             ; pairs at $3B7A + (type − 3) × 24
      jSwapMMU ($DBC) = $3A8A
      bra     $3B18          ; SWAPMMUMODE's 32-bit half, as on the Mac II
```

| `MMUType` | 24-bit mode | 32-bit mode |
|---|---|---|
| 1, HMMU | VIA2 PB3 = 0 | VIA2 PB3 = 1 |
| 3, 68851 | CRP `$7FFF0202`:`$40800050` (`NUMAP`), TC `$80F84500` (`$3B7A`) | CRP `$7FFF0202`:`$4083F5A0`, TC 0 (`$3B86`) |
| 4, 68030 | CRP `$7FFF0002`:`$40800050`, TC `$80F84500` (`$3B92`) | CRP `$7FFF0002`:`$4083F5A0`, TC `$80F04D00` (`$3B9E`) |

`NUMAP` (`$50`): 16 page descriptors of 1 MB for 24-bit mode. Entries
0–7 are RAM (`$00000019`..`$00700019`), 8 is the ROM (`$40800019`;
rev A/B have `$40000019`), 9–`$E` are `$F9000059`..`$FE000059` (slot
`s`'s 1 MB → `$Fs000000`, cache inhibited), `$F` is the I/O
(`$50F00059`; rev A/B `$50000059`).

`$3F5A0`: 16 descriptors of 256 MB for 32-bit mode, an identity map
(`$00000019`..`$40000019`, then `$50000059`..`$F0000059` cache
inhibited).

`SWAPMMUMODE` (`$3A8A`): D0 = 0 → 24-bit, else 32-bit; returns the old
mode in D0; does nothing if already in that mode. For type 1 it sets or
clears VIA2 PB3. For 3 and 4 it loads TC from the bytes at `$CB1`
(`MMUType` in the top byte, so the enable bit is clear: translation
off), then CRP and TC from the table, and clears the 68030's caches
(CACR `|= $0808`). `MMU32Bit` (`$CB2`) records the mode.

The mode changes during boot are the Mac II's (Mac II doc §5): 32-bit
from `MMU_INIT` on, 24-bit from `BOOTRETRY` `$166`, 32-bit inside Slot
Manager calls that read card ROMs, and here also around the
warm-start `RAMTEST` (§5).

---

## 5. RAM

All four machines size RAM the same way, through VIA2 PA6/PA7 (bank B
placement) and the size table at `$366E` (1–128 MB; Mac II doc §3).

| Where | What `$97221136` adds |
|---|---|
| `STARTTEST1` `$2B0C` → `$3F8C8` | before the first memory test: VIA2 DDRA and port A bits 6–7 set (`$C0`, bank B at 32 MB), one long read in each MB from 0 to 127 MB, DDRA restored; then D7 = `$E`, A0 = 0, `jmp $36CA` |
| `COMPBOOTSTACK` `$230` → `$3F898` | `MemTop` capped at `$800000` before halving |
| `INITHIMEMGLOBALS` `$2DE` → `$3F8AC` | `$138` = `$FFFF`; `BufPtr` etc. from `MemTop` capped at 8 MB |
| `BOOTRETRY` `$1A0` → `$3F880` | cold boot: `SwapMMUMode(1)`, `RAMTEST`, swap back; `bra $1AE` |
| `BOOTME` `$10F6` → `$3F8A0` | the system heap fraction is taken of `MemTop` capped at 8 MB |

So with more than 8 MB, 24-bit System 6 sees 8 MB, and `RAMTEST` still
covers all of it in 32-bit mode. MAME's RAM options for these machines
are 1, 4, 5, 8, 16, 17, 20, 32, 64, 65, 68, 80 and 128 MB (the `$366E`
table); for the Mac II (rev A/B) 1, 4, 5 and 8.

---

## 6. Slot Manager and video

The Slot Manager is rev B's, with the same patches (patch entries
`$3F5E0`–`$3F600`, §8). `INITSDECLMGR` switches to 32-bit mode (`$439A`
→ `$3F682`) and probes `$F9FFFFFF` to `$FEFFFFFF` (MAME `macse30`: one
read of each, from `$43F4` and `$41FC`). Every `_SlotManager` call goes
through `$3F658`, which runs the routine in 24-bit mode with A0
stripped; the routines that read card ROMs (`sGetcString`,
`sReadStruct`, `sOffsetData`, `sReadPBSize`, `InitsRsrcTable`, the new
selector `$1C`) switch to 32-bit through the stubs at `$408E`–`$40C5`.

**IIx, IIcx, II FDHD:** NuBus cards in slots 9–`$E` (the IIcx has three
connectors, but the ROM scans all six). MAME puts a Display Card 8•24
in slot 9: `ScrnBase` = `$F9900A00`, `ScreenRow` = 80 at boot.

**SE/30:** the built-in screen is slot `$E` as far as the ROM knows.
In MAME its declaration ROM (`se30vrom.uk6`, 8 KB) is at
`$FEFFE000`–`$FEFFFFFF`, the top of slot `$E`'s 32-bit space, and the
frame buffer is at `$FE000000` and mirrored at `$FEE00000`–`$FEEFFFFF`.
The scan finds the format block at `$FEFFFFEC`–`$FEFFFFFF`
(`$2BC7000F` at `$FEFFFFFC`: the end of the test pattern, reserved
byte, byte lanes `$0F`). At boot `ScrnBase` = `$FEE08040`, `ScreenRow`
= 64 (512 one-bit pixels). The main ROM has no SE/30 video code: the
driver comes from the video ROM's declaration data. The VBL interrupt
is gated by VIA1 PB6 (MAME `se30_via_out_b`); in MAME the SE/30's PDS
gives cards the slot interrupts 9–`$E`.

In 24-bit mode `NUMAP` sends `$E00000`–`$EFFFFF` to `$FE000000`–
`$FE0FFFFF`, so 24-bit code reaches the SE/30 frame buffer at
`$E00000` *(static)*; `ScrnBase` itself is the 32-bit address.

**Our emulator.** The scan's addresses only mean something with real
32-bit addressing: under a fixed 24-bit mask all six probes read
`$FFFFFF`. See the Mac II doc §13 for the two ways out (switch the
address mask with `MMU32Bit`, or put rev A's 24-bit arithmetic back).

---

## 7. Floppy: SWIM and the SuperDrive

`.Sony` (`DRVR` 4, resource data at `$2D72C`, same block size as rev
A/B) is the SWIM driver. It doesn't fit: it continues at `$31B20`,
where rev A/B have `MDEF` 0 (the resource map still lists `MDEF` 0
there), through jumps at `$2EEB0`–`$2EEF7`, and four entries in the
patch area:

| Entry | Code | From |
|---|---|---|
| `$3F60C` | `$3FE36` | `$2EEBC` |
| `$3F610` | `$3FE1E` | `$2EEC2` |
| `$3F614` | `$3FE50` | `$2EEC8` |
| `$3F618` | `$3FE8C`: `BoxFlag` = 2 (IIcx) → table `$3F8FC`, else `$3F908`, stored in the driver's globals at `+$130` | `$2EEF2` |

The IWM/SWIM base is `$50F16000` on all four. `patch_rom_ii` replaces
`.Sony` with our driver, which makes all of this moot.

---

## 8. The patch area

`$3F594`–`$3F59F` is a few bytes of filler; `$3F5A0` the 32-bit MMU
table (§4); `$3F5E0` a table of `bra` entries; the code runs to
`$3FEA7`; then filler to the board declaration data at the top
(`$3FF46`–`$3FFFF`: board sResource "Macintosh II", vendor "Apple
Computer", "Rev 1.3", "15-Jul-88"; sResource "CPU_68020_\_MacII";
format block with test pattern `$5A932BC7`, byte lanes `$0F`). Nothing
in the ROM reads the declaration data.

Entries (✓ = executed in the MAME traces):

| Entry | Code | Called from | Does |
|---|---|---|---|
| `$3F5E0` | `$3F638` | Slot Manager stubs `$408E`–`$40B6` | 32-bit mode ✓ |
| `$3F5E4` | `$3F648` | (internal) | 24-bit mode |
| `$3F5E8` | `$3F658` | `SLOTMANAGER` `$4F56` | dispatch in 24-bit mode, A0 stripped, caller's mode restored ✓ |
| `$3F5EC` | `$3F682` | `INITSDECLMGR` `$439A` | `SInfoPtr` (`$CBC`), 32-bit mode ✓ |
| `$3F5F0` | `$3F68E` | `INITSRSRCTABLE` `$49CA` | 24-bit around `_DisposePtr`/`_NewPtrSysClear` ✓ |
| `$3F5F4` | `$3F6B0` | `SGETDRIVER` `$4E06` | `_StripAddress` ✓ |
| `$3F5F8` | `$3F6BE` | `OPEN` `$6A84` | slot driver's device base (`sFindsRsrcPtr`, then selector `$1C` or `$1B`) ✓ |
| `$3F5FC` | `$3F700` | `_SlotManager` selector `$1C` (`$40C0`) | device base in 32-bit form |
| `$3F600` | `$3F6F4` | `SREADSTRUCT` `$3D32` | `_StripAddress` ✓ |
| `$3F604` | `$3F74A` | `WHICHCPU` `$544` | 68030 test, `BoxFlag` (§3) ✓ |
| `$3F608` | `$3F7B4` | `BOOTRETRY` `$166` | §9 ✓ |
| `$3F60C`–`$3F618` | `$3FE36`, `$3FE1E`, `$3FE50`, `$3FE8C` | `.Sony` | §7 (`$3F610`, `$3F618` ✓) |
| `$3F61C` | `$3F856` | reset `$90` | §2 ✓ |
| `$3F620` | `$3F880` | `BOOTRETRY` `$1A0` | `RAMTEST` in 32-bit mode ✓ |
| `$3F624` | `$3F7A4` | `STARTINIT1` `$112` | `BoxFlag`, `SYSERRINIT` ✓ |
| `$3F628` | `$3F898` | `COMPBOOTSTACK` `$230` | 8 MB cap ✓ |
| `$3F62C` | `$3F8AC` | `INITHIMEMGLOBALS` `$2DE` | 8 MB cap ✓ |
| `$3F630` | `$3F8A0` | `BOOTME` `$10F6` | 8 MB cap |
| `$3F634` | `$3F8C8` | `STARTTEST1` `$2B14` | RAM bank setup (§5) ✓ |

Rev B's patch area has the first nine (`$3F600`–`$3F620`, code `$3F624`–
`$3F735`), byte for byte the same code `$14` bytes higher.

---

## 9. BOOTRETRY `$166`: caches

Rev A/B do `moveq #0,d0; _SwapMMUMode; bsr GETPRAM` here. This ROM does
`jsr $4083F608` → `$3F7B4`:

1. OS trap table entry for `_HWPriv` (`$A198`, at `$400 + $98 × 4 =
   $660`) = `$3F7E4`, a 68030 handler: selectors 0–3 (swap/flush
   instruction cache, swap/flush data cache) via the table at `$3F7CC`;
   others return `-502`.
2. `_HWPriv` selector 2 with A0 = 2: data cache on (no effect on a
   68020).
3. `SwapMMUMode(0)`: 24-bit.
4. `jmp GETPRAM`.

---

## 10. What our emulator has to answer

For each profile (MacIIFamily.md step 4), the ROM must see:

| | II FDHD | IIx | IIcx | SE/30 |
|---|---|---|---|---|
| VIA1 PA6 at `$3F76A` | 0 | 0 | 1 | 1 |
| VIA2 PB3 at `$3F786` | 1 | 0 | 1 | 0 |
| CACR test at `$3F74A`/`$3F856` | 68020 | 68030 | 68030 | 68030 |

Either the I/O memory holds those bits when `$3F74A` runs (VIA1 port A
`$F01E00`, VIA2 port B `$F02000` under the 24-bit mask, the bits
`INITVIA` just wrote aside), or the table lookup at `$3F796` is patched
to load the profile's `BoxFlag`.

`MMUType` 4 sends `SWAPMMUMODE` to `pmove`. With the UAE core (no
68030 MMU) that path has to go: `patch_rom_ii`'s NOPs at `$3B24` and
`$3B62` force the HMMU path, which only writes VIA2 PB3. On `mame-68k`
(PMMU emulated) the ROM's own tables could run, with the 32-bit table
at `$3F5A0` doing the real address switch.

Everything else that differs from the Mac II is either replaced by our
drivers (`.Sony`) or harmless (the 8 MB caps, the `_HWPriv` handler).
`patch_rom_ii`'s offsets at `$3F872` (the reset `pmove`), `$3F7C4`
(`_SwapMMUMode` in `$3F7B4`), `$3B24` and `$3B62` are this ROM's; on
rev A and B the same offsets are other code or filler (Mac II doc §13).

---

## See also

- [`../macii/boot_flow.md`](../macii/boot_flow.md): the shared boot
  walk, the ROM diff, `patch_rom_ii` today
- [`../../MacIIFamily.md`](../../MacIIFamily.md)
- MAME `src/mame/apple/macii.cpp`: `iicx_via_in_a`, `via2_in_b`,
  `iix_via2_in_b`, `macse30_map`
