# Mac ROM Disassembly & Annotation

Reverse-engineered, annotated disassembly of classic Mac boot ROMs,
driven by Ghidra headless analysis. Purpose: understand each ROM well
enough to patch it via the signature-driven approach in
`src/core/rom_patches.cpp` (`patch_rom_classic` and `patch_rom_32`).

Read [`PATCHING_APPROACH.md`](PATCHING_APPROACH.md) before making any
emulator changes — it explains how the Quadra/Basilisk-II strategy
cooperates with the ROM's own `UniversalInfo` structures, rather than
fighting them with hardcoded address patches.

## Covered ROMs

| Dir       | Machines                                    | File                                                            | Size | CPU   | Base         |
|-----------|---------------------------------------------|-----------------------------------------------------------------|------|-------|--------------|
| `se/`     | Mac SE                                      | `256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM`                    | 256K | 68000 | `$00400000`  |
| `macii/`  | Mac II (800k v1)                            | `256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM`           | 256K | 68020 | `$40800000`  |
| `maciix/` | Mac II FDHD, IIx, IIcx, **SE/30**           | `256KB ROMs/1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM`  | 256K | 68030 | `$40800000`  |
| `iici/`   | Mac IIci (Universal 32-bit-clean)           | `512KB ROMs/1989-09 - 368CADFE - Mac IIci.ROM`                  | 512K | 68030 | `$40800000`  |

Notes:
- SE/30, Mac IIx, Mac IIcx, and Mac II FDHD **share the same ROM**
  (`$97221136`). Annotated once under `maciix/`, with machine-specific
  branches called out in the boot-flow walkthroughs.
- First 4 bytes of every Mac ROM are a checksum (matches the filename
  hex). Bytes 4–7 are the initial PC, which reveals the ROM base.

## Layout

```
docs/roms/dissam/
  README.md                 ← you are here
  PATCHING_APPROACH.md      ← the Basilisk-II / Quadra strategy (read first)
  tools/                    ← Ghidra scripts + headless driver (reproducible)
  common/                   ← shared reference: traps, low-mem globals, HW map
  se/                       ← Mac SE
  macii/                    ← Mac II
  maciix/                   ← Mac IIx / IIcx / II FDHD / SE-30
  iici/                     ← Mac IIci
  ghidra/                   ← Ghidra projects (gitignored, rebuild from tools/)
```

Each machine dir contains:
- `rom.lst`       — annotated assembly listing (build artifact)
- `boot_flow.md`  — factual reset → Finder walkthrough

## Rebuilding from ROMs

The annotated `rom.lst` files are **build artifacts** — not committed
to git. They are regenerated locally from the ROM files (which you
must legally own) plus the committed tooling. This keeps Apple's
copyrighted code out of this repository while letting anyone with the
ROMs reproduce the exact same analysis.

Prereq:
- Ghidra 12+ with `analyzeHeadless` available
- ROMs under `~/storage/roms/` (or override via `ROM_ROOT` env var)
- Java 21 in `PATH` (Ghidra snap bundles one under
  `/snap/ghidra/35/usr/lib/jvm/java-21-openjdk-amd64`)

```bash
# One-time: fetch upstream reversing-tool repos (not committed)
./docs/roms/dissam/tools/bootstrap.sh

# Generate listings for all ROMs
./docs/roms/dissam/tools/export_listing.sh

# ...or a single ROM
./docs/roms/dissam/tools/export_listing.sh iici
```

This will:
1. Create a Ghidra project under `docs/roms/dissam/ghidra/<machine>/`
2. Import the raw ROM at its base address with the correct M68K variant
3. Run auto-analysis
4. Run annotation scripts (low-mem globals, ROM symbol map, patch
   cross-references)
5. Export `rom.lst` into the machine dir

## Annotation depth

Every routine that the `patch_rom_*` signature scans locate, and the
boot-time init chain from reset vector through driver install, is
annotated with Apple's own labels from
`cy384/68k-mac-rom-maps`. Low-memory globals (`MemTop`, `TimeDBRA`,
`ROMBase`, …) are imported from `gm-stack/classic-mac-rom-ghidra-tools`
so references to `$0100`-`$0D00` resolve to symbolic names.

## Conventions

- Addresses are physical ROM addresses (not file offsets). SE:
  `$00400000` + offset. Others: `$40800000` + offset.
- Assembly syntax is Ghidra's default M68K listing format (lowercase
  mnemonics, uppercase registers: `move.l D0,D1`).
- Labels use `UpperCamelCase` for functions, `ALL_CAPS` for constants.
- Low-memory globals use their canonical Apple names — see
  `common/hardware_map.md`.
- A-line traps use their canonical `_TrapName`.
