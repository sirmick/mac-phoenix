# Mac II, IIx, IIcx, SE/30: the `$0178` ROMs

Status (2026-10-19): **investigated, not started.** None of these
machines boots. This is what the investigation found and the plan for
making them boot. It follows the method that got the IIci to System 6
(commits `2c395a4c`, `ccba0302`): name ROM patch sites with Apple's
maps, find them by name or signature, and compare against MAME running
the real ROM.

## The ROMs

All three are ROM version `$0178`, 256 KB, and all match MAME's CRCs:

| ROM (`~/storage/roms/256KB ROMs/`) | Checksum | Machines | MAME |
|---|---|---|---|
| `1987-03 - 97851DB6 - MacII (800k v1).ROM` | `97851DB6` | Mac II rev A | `macii -bios original` |
| `1987-12 - 9779D2C4 - MacII (800k v2).ROM` | `9779D2C4` | Mac II rev B | `macii`, `maciihmu` (no 68851) |
| `1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM` | `97221136` | II FDHD, IIx, IIcx, SE/30 | `mac2fdhd`, `maciix`, `maciicx`, `macse30` |

Also in storage, also matching MAME:

- `se30vrom.uk6`: the SE/30's 8 KB built-in video ROM (MAME `se30vrom`).
- `341-0868.BIN`: the Macintosh Display Card 8•24 ROM. This is the card
  MAME puts in slot 9 of a Mac II or IIx by default (`mdc824`, file
  `3410868.bin`).

So MAME references can be run for all four machines.

How alike the three ROMs are, byte for byte:

- **Rev A vs rev B:** 99.0% identical bytes; 6 of 64 4 KB blocks differ.
- **Rev B vs `97221136`:** 95.8% identical. They differ in early init
  (`$0000`–`$6FFF`: machine identification, the 68030 and its MMU, RAM
  over 8 MB), the `.Sony` driver (`$2D735`–`$2F007`, the SuperDrive,
  spilling into `MDEF` 0's space at `$31B21`–`$32325`) and the patch
  area at the top of the ROM (`$3F594`–).
- No routine moved in any of the three: the differences are in-place
  patches, many of them jumps into the patch area. Rev A → B is all
  Slot Manager (32-bit slot addressing). The diff by routine is in
  `docs/roms/dissam/macii/boot_flow.md` §1.

All three share the trap table layout: 7 of 8 sampled trap addresses
equal Apple's Mac II map. **One patcher can cover all three.**

## What happens today

On the current code, booting the hand-made `system-6.0.8` disk with
`--ram 8`:

| ROM | Result |
|---|---|
| `97221136` | Phase "drivers", then stalls with a black screen |
| `9779D2C4` | Never reaches the reset EmulOp ("pre-reset") |
| `97851DB6` | Never reaches the reset EmulOp ("pre-reset") |

`patch_rom_ii` (`src/core/rom_patches.cpp`, 19 WIP commits from April
2026, `9511d0e0`..`8fc6d973`) is fixed-offset NOPs written for the
`97221136` ROM: `$3F872`, `$2C64`, `$2ECE`, the VBR writes in
`FUN_40802A14`, and so on. It applies them to whichever `$0178` ROM it
gets, which is why both Mac II revisions die at once.

It also **skips the NuBus slot scan** (the `bsr` at `$186`) and NOPs
`FROVIDEO` (`$DD0`, in `INITIOMGR`), the call that opens the video
driver, along with `INITCRSRMGR` and the heap setup at `$1CC`–`$1F3`. No
video driver is ever installed, so it could never show a picture. Its
other faults (a mis-encoded block at `$118`, sites that are a different
instruction in rev A/B) are listed in `docs/roms/dissam/macii/boot_flow.md` §13. Treat it as notes, not a base: rewrite
it.

## Apple's map

`docs/roms/dissam/tools/vendor/68k-mac-rom-maps/rom-maps/MacIIROM.lst.txt` (fetched by
`docs/roms/dissam/tools/bootstrap.sh`) has 1,503 names. It was made from
rev A and fits all three ROMs; `load_rom.py` verifies 1,498 of them on
rev B and 1,489 on `97221136`. 1,370
of the 1,503 names also appear in the IIci map, so knowledge carries
over by name.

What is **not** there is the "universal ROM" machinery that
`patch_rom_32` leans on: `GetHardwareInfo`, `UniversalInfo` and the
decoder tables, `InitMMU`, `InitVIAs`, `FindDevBase`, `RdXByte`,
`InitMemVect`, `InitTimeMgr`, `StartBoot`. The early-boot patches are
new work, not ports.

Mapping `patch_rom_32`'s 57 patch sites from the IIci onto rev B
(`docs/roms/dissam/tools/patch_sites.py --ref ~/storage/roms/iici.rom
--ref-map MacIIciROM.lst.txt --target <rev B> --target-map
MacIIROM.lst.txt`): 9 carry over through the trap table (ADBOp,
InsTime/RmvTime/PrimeTime, SCSIDispatch, PowerOff, Get/PutScrap,
ClkNoMem) and 1 byte for byte (`VBLINT`). The rest need finding in this
ROM. 20 of the 32 routines they live in exist under the same name.

Listings: `docs/roms/dissam/tools/export_listing.sh macii`, `... maciib`
and `... maciix` write `macii/rom.lst` (rev A), `maciib/rom.lst` (rev B)
and `maciix/rom.lst` (gitignored, about 2 minutes each).

## Video: no slot 0, so a NuBus card

On the Quadra and the IIci, our declaration ROM (`slot_rom.cpp`) sits at
the top of the main ROM and the Slot Manager finds it as slot 0. **The
`$0178` Slot Manager has no slot 0.** `INITSLOTS` (`$E32`) →
`STARTSDECLMGR` → `INITSDECLMGR` (`$436A`) loops slots 9 to `$E` only
(`moveq #9,D3` … `cmpi.w #$E,D3`). For each slot it probes the top of
the slot's space on 4 byte lanes, reading under a temporary bus-error
vector, and wants Apple's test pattern `$5A932BC7`. Rev A scans in
24-bit mode at `$F99FFFFF`, `$FAAFFFFF`, … (`$sFFFFF` under the mask).
Rev B and `97221136` switch to 32-bit mode for the scan and probe
`$F9FFFFFF`, `$FAFFFFFF`, …, which under a fixed 24-bit mask all read
`$FFFFFF`: for those two ROMs the card is only found with real 32-bit
addressing, or with the scan's address arithmetic put back to rev A's.

So these machines need a **fake NuBus video card**:

- the declaration ROM at the top of one slot's space, e.g. slot `$E`
  (`$EFFFxx` in the flat 24-bit map; `slot_rom.cpp` writes the same
  format block, so it needs a placement option, not a new ROM);
- the framebuffer inside that slot's 1 MB (`$E00000`–), with
  `minorBaseOS` as the offset from the slot base. There is no
  `FindDevBase` to patch here, so the offset has to be real.

Empty slots read as zero in our memory, which fails the test pattern,
so no bus errors need emulating.

In 24-bit mode a minor slot is 1 MB. At 640×480 that allows 8 bits per
pixel (System 6's Color QuickDraw) or 16 with the 32-Bit QuickDraw
INIT; 32 needs 1.2 MB. Millions would need the machine to really
switch to 32-bit addressing.

On the Mac II the address mode is the HMMU (AMU), driven by VIA2 PB3:
the bit `_SwapMMUMode`'s HMMU variant sets and clears (IIci ROM
`$4C20`; the IIci profile already uses that variant, see
`PATCH_BOOT_GLOBS` in `emul_op.cpp`). Emulating that bit, with the
address mask following it instead of a fixed 24-bit mask, would be
faithful. It would also let the IIci run System 7 in 32-bit mode with
more than 8 MB. That is a separate, later step.

The **SE/30** is the `97221136` ROM with a built-in 512×342 mono screen
that appears as slot `$E` (its video ROM is `se30vrom.uk6`). Our slot-`$E`
card can be faithful (mono, 512×342) or a colour card, like the
third-party colour cards real SE/30s took.

## Machine differences to keep in mind

| | Mac II | IIx | IIcx | SE/30 |
|---|---|---|---|---|
| CPU | 68020 + 68881 | 68030 + 68882 | 68030 + 68882 | 68030 + 68882 |
| MMU | HMMU (AMU), or a 68851 | 68030 PMMU | 68030 PMMU | 68030 PMMU |
| Floppy | 800K (IWM) | SuperDrive (SWIM) | SuperDrive | SuperDrive |
| Video | NuBus card | NuBus card | NuBus card | Built-in, slot `$E` |
| ROM | rev A or B | `97221136` | `97221136` | `97221136` |

The `97221136` ROM tells these apart in `WHICHCPU`'s patch (`$3F74A`):
a CACR test for the 68030, then VIA1 PA6 and VIA2 PB3 give `BoxFlag`
(`$CB3`): 0 II FDHD (PA6 0, PB3 1), 1 IIx (0, 0), 2 IIcx (1, 1),
3 SE/30 (1, 0); MAME reads exactly these. We'd answer it per profile
(`docs/roms/dissam/maciix/boot_flow.md` §3, §10).

All are 24-bit machines with these ROMs (not 32-bit clean; System 7
would need MODE32), so the flat 24-bit layout the IIci uses fits them:
RAM up to 8 MB, ROM at `$800000`, the card's space in `$9xxxxx`–`$Exxxxx`.
With `profile_macii`, ScratchMem is at `$848000` and the IPC frame
buffer at `$850000`, just above the 256 KB ROM; a 640×480 frame buffer
at 32 bits per pixel runs into slot 9's space (`$900000`–). That's fine
if the card lives in slot `$E`.

## Plan

1. **MAME references.** Build MAME's `macii.cpp` driver
   (`docs/roms/mame/src`: `make SUBTARGET=... SOURCES=src/mame/apple/macii.cpp`,
   **`-j4`**, a bigger build OOM-kills the session). Stage the ROM
   files under `docs/roms/mame/bin/roms/` (`macii/9779d2c4.rom`,
   `maciix/97221136.rom`, `macse30/se30vrom.uk6`, `nb9:mdc824/3410868.bin`).
   Boot the 6.0.8 800K floppy (exported from the Legacy Recovery CD, as
   for the IIci) on `macii`, `maciix` and `macse30`. Record low memory
   with `docs/roms/mame/lowmem.lua` (it uses `readv_*`, logical
   addresses) and a trace of which ROM routines run.
2. **Rewrite `patch_rom_ii`**, anchored on Apple's routine names and
   byte signatures and steered by the MAME traces. Keep fixed offsets to
   the reset-vector zone. Reuse `patch_rom_32`'s generic trap and driver
   patches. The early hardware probing is the hard part: redirect the
   hardware bases (VIA1, VIA2, SCC, IWM/SWIM, SCSI, ASC) to ScratchMem
   and answer the probes, rather than NOPing their callers.
3. **The fake NuBus video card** (above): a slot placement option in
   `slot_rom.cpp`, and the framebuffer in that slot in the 24-bit layout.
4. **Profiles, tests, UI.** Machine profiles chosen by ROM checksum (as
   for the IIci): Mac II (both revisions), and the `97221136` ROM as IIx
   by default, with IIcx and SE/30 as variants (they share the ROM, so
   picking one needs a setting). Then `boot_*` tests that check the
   desktop is drawn, web UI profiles, and a 6.0.8 bake made on one of
   these machines (the baked SE 6.0.8 lacks Color and Monitors).

Expect roughly two to three times the IIci's effort, most of it in
step 2.

## Reproducing the findings

```bash
# Patch sites, IIci -> Mac II rev B
python3 docs/roms/dissam/tools/patch_sites.py \
    --ref ~/storage/roms/iici.rom --ref-map MacIIciROM.lst.txt \
    --target "$HOME/storage/roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM" \
    --target-map MacIIROM.lst.txt

# Trial boot of a $0178 ROM (watch /api/status boot_phase)
./build/mac-phoenix --config /dev/null --backend uae --ram 8 --port 19600 \
    --disk COPY-OF-system-6.0.8.img "$HOME/storage/roms/256KB ROMs/1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM"
```
