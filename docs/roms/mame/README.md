# MAME Reference Emulation

MAME is our **ground truth** for classic Mac boot behavior. When
MacPhoenix diverges from a known-good boot trace, the first stop is
MAME's milestone logger showing what register state the real ROM
expects at the same PC. This directory holds scripts to build, run,
and instrument MAME for differential debugging.

## What lives here

```
docs/roms/mame/
  README.md              ← you are here
  bootstrap.sh           ← build MAME (or symlink ~/mame) into bin/
  run.sh                 ← unified runner for all 5 machines
  bin/                   ← gitignored: MAME binaries
  src/                   ← gitignored: MAME source (only if bootstrapping fresh)
  log/                   ← gitignored: console logs (MAME_LOG=1)
```

## Quick start

```bash
cd docs/roms/mame
./bootstrap.sh                   # links ~/mame if present, else clones+builds
./run.sh macse                   # windowed, throttled, System 7.5.5
./run.sh maciici                 # Mac IIci
MAME_DEBUG=1 ./run.sh macii      # launch with debugger
MAME_THROTTLE=0 ./run.sh maciix  # unthrottled, 30s auto-exit
```

## Covered machines

Three MAME binaries cover all 5 machines we care about:

| Machine | Binary | CPU | Source file | Notes |
|---------|--------|-----|-------------|-------|
| `macse`    | `macse`    | 68000 | `src/mame/apple/mac128.cpp` | Mac SE |
| `macsefd`  | `macse`    | 68000 | `src/mame/apple/mac128.cpp` | Mac SE FDHD |
| `macii`    | `macii`    | 68020 | `src/mame/apple/macii.cpp`  | Mac II |
| `maciix`   | `macii`    | 68030 | `src/mame/apple/macii.cpp`  | Mac IIx |
| `maciicx`  | `macii`    | 68030 | `src/mame/apple/macii.cpp`  | Mac IIcx |
| `macse30`  | `macii`    | 68030 | `src/mame/apple/macii.cpp`  | Mac SE/30 |
| `maciici`  | `maciici`  | 68030 | `src/mame/apple/maciici.cpp`| Mac IIci + RBV |

The IIci has its own file because it integrates the RBV (RAM-Based
Video) controller and the IOP (I/O Processor) architecture that the
earlier Mac II family lacks.

## ROMs

MAME expects its own named/hashed ROM files under
`bin/roms/<machine>/`. The standard filenames differ from the ones
under `~/storage/roms/`. The bootstrap script will symlink
`bin/roms -> ~/mame/roms/` if that directory exists (it does on this
machine); otherwise you need to stage ROM files yourself.

Minimum ROM files per machine:
- `macse`:    `342-0701-a.rom` or similar SE ROM dump
- `macii`:    `9779d2c4.rom` (rev B, 256K) **or** `97851db6.rom` (rev A)
- `maciix`:   `97221136.rom` (shared with IIcx, SE/30, II FDHD — filename
              may be `342-0639-a.rom` + `342-0640-a.rom` depending on
              MAME version)
- `macse30`:  same shared image as `maciix`
- `maciici`:  `341-0735.um11`, `341-0736.um12`, `342-0733.um9`,
              `342-0734.um10` (4× 128K chips) + `342s0440-b.bin` (1K ADB
              modem PIC)

Check the exact filenames by running:
```bash
./bin/macse -listroms macse
./bin/macii -listroms maciix
./bin/maciici -listroms maciici
```

## Disk images

`run.sh` auto-selects a default disk image per machine:

| Machine | Default disk |
|---------|-------------|
| SE / II / IIx / IIcx / SE-30 / IIci | `~/storage/images/macos-7.5.5.img` |

Override with `MAME_DISK=/path/to/image.img ./run.sh macii`.

For IIci specifically, the existing setup uses `mac608.chd` (Mac OS
6.0.8 CHD) — if you have that, pass it with:
```bash
MAME_DISK=~/storage/images/mac608.chd ./run.sh maciici
```

## Instrumentation

MAME is invaluable because you can add arbitrary logging inside
`m68kcpu.cpp::execute_run()` to watch exactly what a working ROM does.
Two instrumentation styles:

### 1. Milestone logger (recommended for boot tracking)

Add to `src/devices/cpu/m68000/m68kcpu.cpp` around the top of
`execute_run()` (~line 900):

```cpp
#include <cstdio>

struct MS { uint32_t pc; const char *name; int max_hits; int hits; };
static MS ms[] = {
    {0x4080008c, "ResetInit",       1, 0},
    {0x408000b8, "STARTINIT1",      1, 0},
    {0x408001a6, "BOOTRETRY",       5, 0},
    {0x408010f0, "INITIOMGR",       1, 0},
    {0x40801142, "InstallDrivers",  1, 0},
    // ...add more from docs/roms/dissam/iici/boot_flow.md...
};
static FILE *mslog = nullptr;

// inside execute_run(), after fetching PC:
uint32_t pc = m_pc;
for (auto &m : ms) {
    if (pc == m.pc && m.hits < m.max_hits) {
        if (!mslog) mslog = fopen("/tmp/mame_boot_milestones.log", "w");
        m.hits++;
        fprintf(mslog, "[%s] hit#%d pc=%08x  D0=%08x D1=%08x D7=%08x "
                       "A0=%08x A6=%08x SP=%08x\n",
                m.name, m.hits, pc, m_dar[0], m_dar[1], m_dar[7],
                m_dar[8], m_dar[14], m_dar[15]);
        fflush(mslog);
    }
}
```

Rebuild: `./bootstrap.sh` → rebuilds the subtarget you touched.

**Milestone address source**: use the labeled symbols from
`docs/roms/dissam/<machine>/rom.lst`. Any `UPPERCASE:` label is a
candidate — grep the listing for the function you want to trace.

### 2. RAM dumps at specific PCs

For capturing the full memory state at a milestone (useful when
MacPhoenix crashes at the same point and you need to diff memory):

```cpp
if (pc == 0x40801d52 /* MountVol */) {
    FILE *f = fopen("/tmp/mame_ram_mountvol.bin", "wb");
    for (uint32_t a = 0; a < 0x800000; a += 4) {
        uint32_t v = m68ki_read_32(a);  // IMPORTANT: uses MMU
        fwrite(&v, 4, 1, f);
    }
    fclose(f);
}
```

**⚠️ MMU caveat for 68030 machines (IIx, SE/30, IIci):** virtual ≠
physical addresses during boot. Always use `m68ki_read_32()` which
goes through the CPU's address translation, not `space.read_dword()`
which reads raw RAM.

### 3. Per-instruction PC trace

For the finest grained trace (slow, multi-GB file):

```cpp
static FILE *pclog = nullptr;
if (!pclog) pclog = fopen("/tmp/mame_pc_trace.log", "w");
// gate to just the ROM and a short window
if (pc >= 0x40800000 && pc < 0x40900000 && m_icount > 0) {
    fprintf(pclog, "%08x\n", pc);
}
```

Compare against MacPhoenix's equivalent trace with
`tools/diff_pc_traces.py` (see `reference_mame_iici.md` in project
memory).

## Debugging tips

- **Launch MAME's built-in debugger**: `MAME_DEBUG=1 ./run.sh macii`.
  Gives you `bp` (breakpoint), `wp` (watchpoint), `g` (go),
  `over` (step over), `trace <file>` (PC trace).

- **Compare PC at divergence**: when MacPhoenix crashes at address X,
  start MAME, `bp X` in the debugger, `g` to run. When MAME hits the
  same PC, inspect `d0..d7/a0..a7/sr` and compare to MacPhoenix's
  register state at that crash.

- **Set watchpoints on low-memory globals**: `wp 0x0d00,4,rw` watches
  `TimeDBRA` for reads/writes. Common globals in
  `docs/roms/dissam/common/hardware_map.md` and
  `vendor/classic-mac-rom-ghidra-tools/lomem_globals.txt`.

- **Dump CPU state at exception**: inside the 68K exception handler in
  `m68kcpu.cpp`, log `m_pc`, `m_dar[]`, and the vector offset to see
  where MacPhoenix diverges.

## Cross-referencing with the disassembly

Every address MAME logs or the debugger stops at can be looked up in
the MacPhoenix disassembly:

```bash
grep "^40801142" docs/roms/dissam/iici/rom.lst
# shows: 40801142  some instruction  ; possibly PATCH-WIP marker
```

The disassembly's labeled symbols + cross-references form the **left
column** of a differential debugging session. MAME is the **right
column**: what actually happens there on real hardware. When the two
diverge, you have a precise target for the next patch.

## Relationship to ~/mame

If you already have a MAME tree under `~/mame` with built `macse`,
`macii`, and `maciici` binaries (as this project's author does), the
`bootstrap.sh` fast path just **symlinks** those binaries into
`docs/roms/mame/bin/`. No disk space wasted, no rebuild needed.

To force a fresh in-tree build (e.g. to instrument without touching
`~/mame`):

```bash
EXTERNAL_MAME=/nonexistent ./bootstrap.sh
```

The script will clone MAME into `docs/roms/mame/src/` (shallow, ~500
MB) and build the three subtargets (~15-40 min depending on CPU).

## See also

- `docs/roms/dissam/` — disassembly & annotation of the same ROMs
- `docs/roms/dissam/common/hardware_map.md` — MMIO reference
- Project memory: `reference_mame_iici.md`, `reference_mame_macii.md`
- `tools/diff_pc_traces.py` — trace comparison utility
