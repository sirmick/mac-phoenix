# Mac ROM Disassembly & Annotation

Reverse-engineered, annotated disassembly of classic Mac boot ROMs, driven by
Ghidra headless analysis. Purpose: understand each ROM well enough to boot it
in the MacPhoenix emulator — specifically, to know which hardware accesses to
NOP, which locations to patch, and where to install EmulOps.

## Covered ROMs

| Dir | Machines | File | Size | CPU | Base |
|---|---|---|---|---|---|
| `se/`      | Mac SE                                      | `256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM`               | 256K | 68000 | `$00400000` |
| `macii/`   | Mac II (800k v1)                            | `256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM`      | 256K | 68020 | `$40800000` |
| `maciix/`  | Mac II FDHD, IIx, IIcx, **SE/30**           | `256KB ROMs/1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM` | 256K | 68030 | `$40800000` |
| `iici/`    | Mac IIci (universal 32-bit-clean)           | `512KB ROMs/1989-09 - 368CADFE - Mac IIci.ROM`             | 512K | 68030 | `$40800000` |

Notes:
- SE/30, Mac IIx, Mac IIcx, and Mac II FDHD **share the same ROM** (`$97221136`).
  Annotated once under `maciix/`, with machine-specific branches called out in `notes.md`.
- First 4 bytes of every Mac ROM are a checksum (matches the filename hex).
  Bytes 4–7 are the initial PC, which reveals the ROM base address.

## Layout

```
docs/roms/dissam/
  README.md                 ← you are here
  tools/                    ← Ghidra scripts + headless driver (reproducible)
  common/                   ← shared reference: traps, low-mem globals, HW map
  se/                       ← Mac SE
  macii/                    ← Mac II
  maciix/                   ← Mac IIx / IIcx / II FDHD / SE-30
  iici/                     ← Mac IIci
  ghidra/                   ← Ghidra projects (gitignored, rebuild from tools/)
```

Each machine dir contains:
- `rom.lst`       — annotated assembly listing (exported from Ghidra)
- `rom.c`         — decompiler output for key boot functions
- `notes.md`      — machine-specific quirks (checksum, RAM sizing, trap vectors)
- `boot_flow.md`  — reset → Finder walkthrough
- `hw_patches.md` — every hardware access that needs NOP/EmulOp, cross-referenced to `src/core/rom_patches.cpp`

## Rebuilding from ROMs

The annotated `rom.lst` files are **build artifacts** — not committed to
git. They are regenerated locally from the ROM files (which you must
legally own) plus the committed tooling. This keeps Apple's copyrighted
code out of this repository while letting anyone with the ROMs reproduce
the exact same analysis.

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
4. Run annotation scripts (traps, low-mem globals, hardware MMIO, rom_patches.cpp xrefs)
5. Export `rom.lst` + `rom.c` into the machine dir

## Annotation depth

Per project scope, we target **bootability**, not exhaustive coverage:
- **Heavy** annotation around: reset vector, hardware init (VIA/IWM/SCC),
  RAM sizing, ROM checksum, trap dispatcher install, driver install,
  Slot Manager init, SCSI probe, boot block read, video init.
- **Heavy** annotation at every address already touched by
  `src/core/rom_patches.cpp` — cross-referenced automatically.
- **Light** annotation elsewhere — function naming and a one-line purpose.

## Conventions

- Addresses are physical ROM addresses (not file offsets).
  SE: `$00400000` + offset. Others: `$40800000` + offset.
- Assembly syntax is Ghidra's default M68K listing format
  (lowercase mnemonics, uppercase registers: `move.l D0,D1`).
- Labels use `UpperCamelCase` for functions, `ALL_CAPS` for constants.
- Low-memory globals use their canonical Apple names (`MemTop`, `BufPtr`,
  `ROMBase`, `TimeDBRA`, etc.) — see `common/lowmem_globals.md`.
- A-line traps use their canonical `_TrapName` — see `common/traps.md`.
