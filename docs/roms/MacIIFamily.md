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
- **Rev B vs `97221136`:** 95.8% identical. They differ only in early
  init (`$0000`–`$6FFF`: hardware probing for the 68030 machines and
  the SE/30), the floppy and sound driver area (`$2D000`–`$32FFF`: the
  SuperDrive) and the CPU setup at `$3F000`.

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

It also **skips the NuBus slot scan** (the `bsr` at `$186`) and NOPs the
video globals at `$1CC`–`$1F3`. No video driver is ever installed, so
it could never show a picture. Treat it as notes, not a base: rewrite
it.

## Apple's map

`tools/vendor/68k-mac-rom-maps/rom-maps/MacIIROM.lst.txt` (fetched by
`docs/roms/dissam/tools/bootstrap.sh`) has 1,503 names. It fits all
three ROMs; `load_rom.py` verifies 1,489 of them on `97221136`. 1,370
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

Listings: `docs/roms/dissam/tools/export_listing.sh macii` and
`... maciix` write `macii/rom.lst` and `maciix/rom.lst` (gitignored,
about 2 minutes each).

## Video: no slot 0, so a NuBus card

On the Quadra and the IIci, our declaration ROM (`slot_rom.cpp`) sits at
the top of the main ROM and the Slot Manager finds it as slot 0. **The
`$0178` Slot Manager has no slot 0.** `INITSLOTS` (`$E32`) →
`STARTSDECLMGR` → `INITSDECLMGR` (`$436A`) loops slots 9 to `$E` only
(`moveq #9,D3` … `cmpi.w #$E,D3`). For each slot it probes the top of
the slot's space (`$F9FFFFFF`, `$FAFFFFFF`, …; `$s FFFFF` in 24-bit
mode) on 4 byte lanes. It reads under a temporary bus-error vector and
wants Apple's test pattern `$5A932BC7`.

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

The `97221136` ROM tells these apart by probing hardware (VIA2 ID bits
and the like) in its early init. That code is in the region where it
differs from the Mac II ROM; we'd answer it per profile.

All are 24-bit machines with these ROMs (not 32-bit clean; System 7
would need MODE32), so the flat 24-bit layout the IIci uses fits them:
RAM up to 8 MB, ROM at `$800000`, the card's space in `$9xxxxx`–`$Exxxxx`.
The current `profile_macii` puts ScratchMem and the framebuffer at
`$900000`, which is slot 9's space. That's fine if the card lives in
slot `$E`.

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
