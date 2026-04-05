# Mac IIx / IIcx / SE-30 / II FDHD — Boot Flow

These four machines share the same ROM image (`$97221136`, version
`$0178`). The factual boot walkthrough is in
[`../macii/boot_flow.md`](../macii/boot_flow.md) — everything in
`STARTINIT1`, `BOOTRETRY`, `INITDISPATCHER`, and `INITIOMGR` is
identical at the same offsets because the four machines run the
same code.

What differs between them is determined at runtime inside the same
ROM by:

| Probe routine | What it selects |
|---|---|
| `WHICHCPU` | 68020 (Mac II) vs 68030 (IIx/IIcx/SE-30) → MMU availability |
| `WHICHBOARD` | reads box ID → dispatches to per-model init |
| IWM rev-check at `$BA` | IWM (Mac II) vs SWIM (IIx/IIcx/SE-30) floppy controller |
| Slot Manager | NuBus probe (II/IIx/IIcx) vs PDS only (SE-30) |
| Video base | NuBus card (II/IIx/IIcx) vs built-in compact display (SE-30) |

The disassembly in `rom.lst` (build artifact) is produced from the
`$97221136` image and is valid for all four machines. Symbols
imported from `cy384/68k-mac-rom-maps/MacIIROM.lst.txt` are verified
per-offset against the original Mac II ROM (`$97851DB6`) at load
time; symbols in divergent regions are skipped. See the `VERIFY_WINDOW`
logic in `tools/load_rom.py`.

Patching status: version `$0178` is **not covered** by
`src/core/rom_patches.cpp` today. See
[`../PATCHING_APPROACH.md`](../PATCHING_APPROACH.md) §"The $0178 gap"
for the recommended shape of a future `patch_rom_ii` function.
