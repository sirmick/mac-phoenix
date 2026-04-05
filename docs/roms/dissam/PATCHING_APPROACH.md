# ROM Patching Approach

> Read this before touching `src/core/rom_patches.cpp` or writing new
> ROM patches. It captures the strategy that works for classic Mac
> ROMs — inherited verbatim from Basilisk II upstream — and calls out
> the specific parts of it that are hardcoded-by-necessity vs
> signature-scanned.

## The two canonical patch functions

`src/core/rom_patches.cpp` provides two patch functions, dispatched
by the `ROMVersion` word at ROM offset `$8`:

| Function | ROM versions | CPU | Strategy |
|---|---|---|---|
| `patch_rom_classic` | `$0276` (Mac Plus, SE, Classic, Mac 128/512) | 68000 | Small, 24-bit ROMs. Entirely fixed-offset patches — these ROMs are too old and too small for `UniversalInfo`-style dispatch. |
| `patch_rom_32` | `$067C` (Mac II rev B, IIci, IIsi, IIvx, LC III/475, Quadra 605/650/800/900/950, Performa 475/630…) | 68020/030/040 | **Mixed: signature-driven for the bulk of the work, fixed-offset for load-bearing early-boot code.** |

Version `$0178` (Mac II original, IIx, IIcx, SE/30) is currently **not
covered** — see §"The $0178 gap" below.

Both functions are derived from Basilisk II upstream
(`legacy/BasiliskII/src/rom_patches.cpp`) and are essentially verbatim
copies with one adaptation for the Unicorn backend: every
`M68K_EMUL_OP_X` constant is wrapped in `platform_make_emulop(...)`
so the emitted opcode is `0x71xx` for UAE or `0xAExx` for Unicorn
(QEMU TCG treats `0x71xx` as a legal `MOVEQ` and won't trap, so the
Unicorn path needs the A-line encoding).

## The signature-driven core

The most valuable pattern in `patch_rom_32` is locating the
`UniversalInfo` structure by byte signature and then patching
through the ROM's own data layout:

```cpp
static const uint8 universal_dat[] =
    {0xdc, 0x00, 0x05, 0x05, 0x3f, 0xff, 0x01, 0x00};
if ((base = find_rom_data(0x3400, 0x3c00, universal_dat,
                          sizeof(universal_dat))) == 0)
    return false;
UniversalInfo = base - 0x10;
```

Consequences:
- **One function handles many ROMs.** The signature is identical
  across every `$067C` ROM Apple shipped, so the same patch function
  covers Mac II rev B, IIci, IIsi, IIvx, Quadra variants, LC III/475,
  and Performa 475/630.
- **ROM revisions are transparent.** If Apple moved a structure a
  few bytes to add a field, the hardcoded offset breaks but the
  signature scan still finds it.

Everything downstream works off `UniversalInfo` offsets:

| Field | Offset | Used for |
|---|---|---|
| `decoderInfoPtr` | `+0`  | walk the hardware-base LMG table at `$94A` and point every base at `ScratchMem` |
| `nuBusInfoPtr`   | `+12` | mark all NuBus slots as empty (first byte `0x03`, rest `0x08`) |
| `productKind`    | `+18` | write the configured model ID (`m68k.modelid`) |
| `defaultRSRCs`   | `+22` | set to `4` ("FPU optional") when `FPUType == 0` |

Additional signature scans in `patch_rom_32` locate:

- `clear_globs_dat`    — BootGlobs-clearing loop (NOP)
- `init_mmu_dat` / `init_mmu2_dat` / `init_mmu3_dat` — MMU init
  sub-routines (partial NOP to bypass RBV probe / unknown CPU check)
- `read_xpram_dat` / `read_xpram2_dat` / `read_xpram3_dat` — PRAM
  read routines (replaced with `EMUL_OP_READ_XPRAM`)
- `init_scc_dat`       — SCC init (RTS early)
- `init_asc_dat`       — ASC init (JMP past hardware touch)
- `model_id_dat` / `model_id2_dat` — reads of `$5FFFFFFC` (NOP)
- `nubus_dat`          — NuBus probe (NOP)
- `lea_dat` at `$226` / `$230` / `$2EE` — interrupt enable `LEA` sites
- `fix_memsize2_dat`   — physical RAM size fixup
- `frame_base_dat`     — frame buffer base mangling (RTS)
- `via2_dat` / `via2b_dat` — VIA2 writes (RTS)
- `bmove_dat` / `ptest2_dat` — 68040/060 PTEST usage (BlockMove EmulOp)
- `memdisp_dat`        — MemoryDispatch unimplemented trap

Trap-based replacements via `find_rom_trap()`:

- `_ClkNoMem` (`$A053`)       — `EMUL_OP_CLKNOMEM`
- `_ADBOp` (`$A07C`)          — replaced with `adbop_patch[]`
- `_InsTime` / `_RmvTime` / `_PrimeTime` (`$A058`/`$A059`/`$A05A`) — Time Manager EmulOps
- `_SCSIDispatch` (`$A815`)   — `EMUL_OP_SCSI_DISPATCH`
- `_PowerOff` (`$A05B`)       — `EMUL_OP_SHUTDOWN`
- `_PutScrap` / `_GetScrap` (`$A9FE`/`$A9FD`) — clipboard EmulOps

Resource-based replacements via `find_rom_resource()`:

- `'DRVR' 4`  (.Sony)   — overwritten with `sony_driver[]` + host disk/cdrom drivers
- `'DRVR' 51` (.EDisk)  — scan range zeroed
- `'SERD' 0`             — serial driver SERD patched + ain/aout/bin/bout stubs
- `'PACK' 4`             — double-definition check (FPU availability)

## The fixed-offset load-bearing patches

`patch_rom_32` is **not** purely signature-driven. It has a handful
of hardcoded offsets that target stable early-boot addresses. These
are identical across every `$067C` ROM because the Apple reset-vector
layout is fixed, but they *are* fixed offsets and should be
understood as such:

| Offset | Size | Purpose |
|---|---|---|
| `$00008C` | 8 B  | Reset shim: `EMUL_OP_RESET` + `JMP $BA` |
| `$0000C2` | 4 B  | NOP `JMP GETHARDWAREINFO` |
| `$0000C6` | 30 B | 15 NOPs over the `JMP INITVIAS` block |
| `$00010E` | 4 B  | `EMUL_OP_PATCH_BOOT_GLOBS` |
| `$000190` | 4 B  | NOP `bsr ENABLEEXTCACHE` |
| `$0007C0` | 6 B  | Fake `WHICHCPU` result |
| `$000800` | 32 B | Fake `SETUPTIMEK` — hardcodes `TimeDBRA`/`TimeSCCDBRA`/`TimeSCSIDBRA`/`TimeRAMDBRA` to `10000` |
| `$0009A0` | 2 B  | RTS to short-circuit `INITSCSI` |
| `$0009C0` | 2 B  | RTS to short-circuit `INITIWM` |
| `$000490` | 24 B | Synthesized `CompBootStack` replacement with `EMUL_OP_FIX_MEMSIZE` |
| `$00B0E2` | 6 B  | NOP VIA writes in `InitTimeMgr` |
| `$005B78` | 8 B  | `GetDevBase` frame-buffer stub |
| `$009F4C` | 2 B  | RTS — skip `DisableIntSources` |
| `$009BC4` | 10 B | VIA Level-1 handler stub |
| `$00A296` | 12 B | 60 Hz handler → `EMUL_OP_IRQ` |
| `$00CCAA` | 6 B  | Redirect `$0000` handle to `ScratchMem` |
| `$01142` / `$01144` | 8 B | `EMUL_OP_INSTALL_DRIVERS` + NOP SonyVars access |
| `$1B8F4` | 6 B  | `vCheckLoad` trampoline |
| `$0A8A8` / `$0A662` / `$0B2C6A` / `$0B2D2E` | varies | NOP VIA writes in `InitADB` (selected by runtime byte check) |
| `$04232` | 10 B | NOP `$50F1A101` access (ROM32-only, byte-guarded) |

These are load-bearing. Touching them without a byte-level
verification that the target ROM has the same layout is dangerous.
The `InitADB` block (`$A8A8` et al.) is the one example of *guarded*
fixed-offset patching — a runtime byte check selects between ROM10/11
and ROM22/23/26/27/32 offset sets. That's the minimum discipline for
any fixed-offset patch that isn't in the reset-vector zone.

## Rules for adding new patches

1. **Signature-scan by default.** `find_rom_data(start, end, sig,
   len)` with a distinctive byte pattern is always preferable to a
   hardcoded offset. A 6–10 byte signature with a narrow search range
   is almost always unique within a ROM.

2. **Trap table lookups are better than address lookups.** If the
   target is a trap handler, use `find_rom_trap(0xAxxx)` so the patch
   survives any ROM revision that moved the trap dispatcher.

3. **Resource-based lookups beat both** when the target is a DRVR,
   SERD, or named resource. `find_rom_resource(FOURCC, id)` handles
   arbitrary ROM layouts.

4. **Fixed offsets are acceptable only in the reset-vector zone**
   (`$0..$FF`, roughly — up to the point where the ROM has had a
   chance to identify itself via `UniversalInfo`). Anything later
   than that should be scanned.

5. **Guard fixed offsets with a runtime byte check** if you must use
   them elsewhere. Check that the bytes at the target still look
   like what you expect before writing over them.

6. **Patch through data structures, not code.** If the goal is "make
   the ROM think a device isn't present", find the ROM's own "is
   device present?" field (`nuBusInfoPtr`, `productKind`,
   `defaultRSRCs`, etc.) rather than NOP-ing the code that reads it.

7. **Never add a new EmulOp at a hardcoded ROM offset** without
   documenting why a signature scan wasn't possible. The comment
   should name the specific ROM revision the offset was derived
   from.

## The one non-baseline deviation in our tree

Our `patch_rom_32` has exactly one non-Basilisk addition beyond
cosmetic Unicorn adaptations: **the `RTS` inserted at ROM offset
`$1256`** to short-circuit a `.netBOOT` open call that hangs under
Unicorn. This is a fixed offset, derived from one specific ROM
revision, and is the single patch in the file that *should* be
revisited when restarting the effort. See
`src/core/rom_patches.cpp` line ~1390 and the TODO note in the
surrounding comment. The right fix is either a signature scan for
the `.netBOOT` pascal string + `_GetNamedResource` pattern, or
resolving the underlying Unicorn hang so the patch isn't needed at
all.

## The `$0178` gap

There is **no `patch_rom_ii`** in our tree. Version `$0178` ROMs
(Mac II original, IIx, IIcx, SE/30) therefore cannot boot today.
Basilisk II upstream also does not ship a `patch_rom_ii` — classic
Basilisk targets Mac II rev B (`$067C`) and later, not the original
`$0178` hardware.

A future `patch_rom_ii` should mirror the structure of `patch_rom_32`:

1. **Locate the `$0178` equivalent of `UniversalInfo` by signature
   scan.** The `$0178` ROMs have a similar structure but it lives at
   a different offset with a different byte signature. The
   `maciix/rom.lst` disassembly (which covers the shared
   IIx/IIcx/SE30/II-FDHD image) is where to look for the target
   pattern.
2. **Walk the equivalent of the `$94A` LMG hw-base table.** Same
   idea, different location in a `$0178` ROM.
3. **Reuse the same DRVR/SERD/PACK resource patching,** same trap
   replacements, same `patch_emulops_for_aline` pass.
4. **Derive the equivalent fixed-offset early-boot patches** from
   the `$0178` reset layout. The positions of `STARTINIT1`,
   `CompBootStack`, `SETUPTIMEK`, and the VIA interrupt handlers
   will all be different from `$067C`. The `maciix/boot_flow.md`
   walkthrough names them with their `$0178` offsets.

**Do not** hand-craft a list of address-specific patches for each
individual init routine by reading the disassembly. The
signature-driven approach is the one Basilisk II has proved on the
`$067C` family for 20+ years, and the same discipline should carry
over to `$0178`: locate ROM data structures by byte signature, patch
*through* them rather than around them, and keep fixed offsets
confined to the reset-vector zone where the ROM header layout is
stable by definition.

## References

- `src/core/rom_patches.cpp::patch_rom_32` — canonical implementation
- `src/core/rom_patches.cpp::patch_rom_classic` — SE/Plus/Classic
- `src/core/rom_patches.cpp::find_rom_data` — the signature scanner
- `legacy/BasiliskII/src/rom_patches.cpp` — upstream reference (in-tree)
- Inside Macintosh: *Devices*, chapter 1 (Slot Manager, hardware
  base discovery via `UniversalInfo`)
- `se/boot_flow.md`, `macii/boot_flow.md`, `iici/boot_flow.md` —
  factual ROM walks for each covered ROM
- `common/hardware_map.md` — MMIO reference across machines
