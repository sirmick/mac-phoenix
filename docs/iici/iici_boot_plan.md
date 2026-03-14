# IIci Boot Implementation Plan

Starting point: commit `84f99352` (pre-IIci, clean Quadra-working codebase).

## Background: How the Quadra Boots (What Already Works)

Understanding the Quadra boot path is essential because the IIci must follow the
same pattern. The Quadra boot works because:

1. `VideoInit()` creates monitor descriptors with framebuffer base, modes, slot IDs
2. `PatchROM()` calls `InstallSlotROM()` which builds a declaration ROM containing
   a video driver (with EmulOp traps for Open/Control/Status/Close) and copies it
   to the **end of the Mac ROM image** (`ROMBaseHost + ROMSize - slot_rom_size`)
3. `patch_rom_32()` inserts INSTALL_DRIVERS EmulOp at ROM+$1142 and
   REPAIR_DRIVERS EmulOp at ROM+$134E
4. The Quadra ROM's Slot Manager scans NuBus slots, finds the declaration ROM,
   opens the video driver, sets up GDevices, and boot proceeds normally
5. The heap never gets corrupted because the Slot Manager succeeds

## What Went Wrong (8 Commits of Wasted Work)

We added IIci ROM patches but **never got the video driver installed via the Slot
Manager**. The Slot Manager's system board scan at $5E48 couldn't find our
declaration ROM, so it took error/fallback paths. These error paths:

- Left driver DCEs uninitialized or partially initialized
- Caused the ROM to re-run initialization passes (CHECKLOADs repeated)
- The driver reinit loop (`CLR.L $8(A2)` at ROM $1AEDE) zeroed DCE fields that
  overlapped with heap block headers at address $3938
- This created zero-size heap blocks ($40000000 = tag $40, size $000000)
- Every Memory Manager routine (at least 5 different ROM code paths) that walks
  the heap looped forever on these zero-size blocks
- We then spent 8 commits adding: heap walk/repair in newcpu.cpp, per-instruction
  watchpoints, zero-size block skip patches at 5 ROM offsets, zcbFree recalculation,
  memWZErr suppression, exception vector periodic repair, manual ApplZone creation,
  heap coalescing trampolines, and 900+ lines of debug traces

**None of this was necessary.** The fix is: make the Slot Manager find the video driver.

### The Rule

> If you find yourself patching heap manager internals, you have a missing driver
> or missing hardware init. Stop. Fix the actual cause.

---

## IIci vs Quadra: Key Differences

| | Quadra 650 | IIci |
|---|---|---|
| CPU | 68040 | 68030 |
| ROM | 1MB ($100000) | 512KB ($80000) |
| ROM guard | `ROMSize > 0x80000` | `ROMSize <= 0x80000` |
| Addressing | 32-bit clean | **24-bit** (high byte = MM flags) |
| ROMBaseMac | $40800000 | $00800000 |
| Slot Manager | NuBus auto-discovery | **Slot $0 only, needs trampoline** |
| Video | NuBus card sRsrc | Built-in (RBV framebuffer) |
| Interrupts | VIA2 + AMIC | VIA1 + RBV |
| VIA handler offset | ROM+$9BC4 | ROM+$9BC0 |
| ADB controller | Cuda | Egret (not emulated) |
| jIODone | Clears busy bit | **Does NOT clear busy bit** |
| Init passes | Single pass | **Two passes** (INSTALL_DRIVERS runs twice) |

---

## Trap: 24-Bit Addressing

This is the single most pervasive issue. The IIci Memory Manager stores flags
in the high byte of handles and master pointers:

```
Byte 3 (high):  Lock | Purge | Resource | flags
Bytes 2-0:      Actual 24-bit address
```

Every piece of code that dereferences a Mac handle or pointer on the IIci must
mask to 24 bits:

```cpp
// WRONG — accesses $80003938 instead of $003938
uint32_t addr = ReadMacInt32(handle);

// RIGHT — strip flag bits
uint32_t addr = ReadMacInt32(handle) & 0x00FFFFFF;
```

This applies to:
- All driver EmulOp handlers (A1 = DCE pointer has flags in high byte)
- Any code reading master pointers from the heap
- Unit table entries (driver handles)
- GDevice list pointers
- Anything from `ReadMacInt32()` that came from the Mac Memory Manager

The UAE emulator runs in 68040/32-bit mode, but Mac OS on the IIci uses 24-bit
conventions throughout. The ROM code itself handles this correctly, but our
host-side EmulOp handlers must do it manually.

**Where masking is already implemented:** `emul_op.cpp` lines 119-130 masks A1
for all driver opcodes when `ROMSize <= 0x80000`.

---

## IIci Memory Map (8MB RAM)

```
$000000 - $0001FF   Exception vectors + low-memory globals
$000200 - $000FFF   System globals (trap dispatch tables, FS vars, etc.)
$001000 - $001FFF   Scratch area
$002000 - $0XXXXX   System heap zone (zone header at $2000)
$0XXXXX - $7FFFFF   Application zone + stack (SP starts near top)
$800000 - $87FFFF   ROM (512KB)
$900000 - $FFFFFF   I/O space (VIA, RBV, SCC, SCSI, ASC)
```

**Zone header layout** (at zone base, e.g. $2000):

| Offset | Field | Notes |
|--------|-------|-------|
| $00 | bkLim | End of usable heap (**NOT offset $04**) |
| $04 | purgePtr | |
| $08 | hFstFree | |
| $0C | zcbFree | Total free bytes |
| $34 | heapData | First heap block starts here |

**Heap block header:** 4 bytes — `[tag:8][size:24]`. Size masked via low-mem
global at $031A. Tag: $00=free, $40=non-relocatable, $80+=relocatable.
Minimum block size is 12 bytes.

---

## Implementation Steps

### Step 1: Config + ROM Detection

**Files:** `emulator_config.cpp`, `emulator_config.h`

Add model preset:
```cpp
{"iici", cpu_type=3, modelid=5, fpu=false, ram_mb=8, 640x480}
```

- `cpu_type=3` = 68030
- `modelid=5` → productKind in ROM UniversalInfo (model_id = 5+6 = 11)
- `--model iici` CLI flag applies this preset
- `--arch m68k` (default for IIci)

ROM detection: `CheckROM()` must accept 512KB ROMs. Guard all ROM-size-dependent
code with `ROMSize <= 0x80000` (IIci) vs `ROMSize > 0x80000` (Quadra).

### Step 2: Slot Manager — Video Driver in Slot $0

**Files:** `rom_patches.cpp`, `slot_rom.cpp`

This is the **most critical step**. Get this right and most downstream issues vanish.

**The problem:** The IIci ROM's SInit routine at $5E48 allocates slot structures
and sets slot $0's status to $FED4 (negative = skip). Without intervention, the
Slot Manager never scans slot $0 and never finds our video driver.

**The solution:** A 14-byte trampoline at ROM+$0D42 (dead padding area):

```asm
; Trampoline at $0D42:
CLR.W    4(A1)           ; status = 0 (positive = scan this slot)
MOVE.L   #$0087FFFF,16(A1)  ; base address = end of ROM
RTS
```

Patch $5ED0 to call it:
```asm
; Was: MOVE.W #$FED4,4(A1) — mark slot $0 as skip
; Now: BSR.W  $0D42        — call trampoline to enable slot $0
;      NOP
```

**Also required:**
- Disable all NuBus slots (1-15) via UniversalInfo nubus table — we only want slot $0
- `InstallSlotROM()` places declaration ROM at `ROMBaseHost + ROMSize - slot_rom_size`
  which for 512KB ROM means the top byte is at Mac address $0087FFFF — exactly where
  the trampoline points

**Verification:** After boot starts, the Slot Manager should find the declaration ROM,
parse the sResource directory, locate the video driver sResource, and call the driver's
Open routine (which triggers our VIDEO_OPEN EmulOp).

### Step 3: ROM Patches (`patch_rom_iici()`)

**File:** `rom_patches.cpp`

Create `patch_rom_iici()` called from `patch_rom_32()` when `ROMSize <= 0x80000`.

**Hardware skips** (chips we don't emulate):

| Address | What | Patch |
|---------|------|-------|
| $14CE, $766A, $C09C, $193EC | Timer wait loops | NOP (0x4E71) |
| $71F6 | SCSI bus scan | NOP |
| $A8E0 | ADB/Egret bus scan | Skip (BRA over) |
| $706E | ASC sound chip init | Skip |
| $42922 | Egret manager | Skip |
| $2E570 | Sound Manager | Skip |
| $2EDCA | SANE FP infinite loop | Fix |

**Boot flow patches:**

| Address | What | Patch |
|---------|------|-------|
| $1D36 | Warm start branch | BNE → BRA (single-pass boot) |
| $16AA | Happy Mac display | Skip |
| $285A | VBL drawing | Skip |
| $284A/$2856 | InitDialogs/DrawMenuBar | Replace with stack cleanup (ADDQ.L #4,A7) |
| $0566-$057A | InitPalettes/InitWindows/DrawControls | NOP with stack fixup |

**Exception/interrupt handling:**

| Address | What | Patch |
|---------|------|-------|
| $26E2/$26F4 | Illegal instruction handler | Prevent infinite re-entry |
| $26E8 | IRQ handler | EmulOp(IRQ) + TST.L D0 + RTE |
| $2708 | Auto-vector redirect | Point to $26E8 |
| $281E | CritError | MOVEQ #0,D0 / RTS (suppress crash dialog) |

**Memory/filesystem:**

| Address | What | Patch |
|---------|------|-------|
| $050E | System zone limitPtr | $3800 → $200000 (14KB → 2MB) |
| $4B82 | GrowZone stub | Return 0 instead of 1 |
| $F65E | FS dispatch | MOVE.L A0,($0362) replacing BSR |
| $F662 | FSBusy | NOP |
| $EFE2 | jIODone spin loop | NOP (IIci drivers are synchronous) |

**Video/display:**

| Address | What | Patch |
|---------|------|-------|
| $2C1F2, $2C3B0 | GDevice list walks | Skip (may be removable once video works) |

### Step 4: EmulOp Handlers

**File:** `emul_op.cpp`

All IIci-specific code guarded by `ROMSize <= 0x80000`.

#### 24-Bit Address Masking

At the top of every driver EmulOp dispatch (SONY_OPEN through SERIAL_CLOSE):
```cpp
if (ROMSize <= 0x80000) {
    r->a[1] &= 0x00FFFFFF;  // Strip MM flag bits from DCE pointer
}
```

#### DRIVER_DONE Macro

The IIci's jIODone never clears the Device Manager busy bit. Without this,
the second call to any driver returns "driver busy" (-1).

```cpp
#define DRIVER_DONE(base, result) do { \
    if (ROMSize <= 0x80000) { \
        WriteMacInt16(base + 0x10, result);  /* ioResult */ \
        uint16_t flags = ReadMacInt16(r->a[1] + 4); \
        WriteMacInt16(r->a[1] + 4, flags & ~0x0080);  /* clear busy */ \
        /* Also write to 24-bit masked address */ \
        uint32_t clean = r->a[1] & 0x00FFFFFF; \
        WriteMacInt16(clean + 4, ReadMacInt16(clean + 4) & ~0x0080); \
    } \
} while(0)
```

Apply after every driver EmulOp return.

#### INSTALL_DRIVERS Handler

Runs at ROM+$1142. On IIci, this runs **twice** (passes 1 and 2).

**Pass 1:** Save state
- Save driver DCE handles for .Sony/.Disk/.CDROM
- Save drive queue
- Save RAM snapshot $0000-$4FC8 (MEMCOPY at $38570 will overwrite it)

**Pass 2:** Restore state after MEMCOPY
- Restore RAM $0000-$4FC8
- Restore Device Manager vectors at $6D4/$6DC
- Clear unit table slots
- Restore drive queue
- Restore driver DCE handles
- Call _InitFonts and _MountVol directly

**Both passes:**
- Redirect auto-vector interrupts to IRQ handler at ROM+$26E8
- Allocate FCB array and WDCBs (file system structures)
- **Pre-initialize $02BA dispatch table** (16 entries → safe RTS). Without this,
  every Toolbox trap that enters the $26A0 dispatch framework loops forever.
  This is a fundamental IIci boot requirement.

#### REPAIR_DRIVERS Handler

Runs at ROM+$134E (called via BSR redirect at $1D10/$1D12).

- Redirect Vec25 ($64) autovector to IRQ handler
- Repair Device Manager vectors $6D4/$6DC
- Patch jIODone to skip BGT spin loop
- Repair driver DCEs (dCtlDriver, dCtlFlags, dCtlRefNum)
- Restore unit table entries destroyed by MEMCOPY

#### VIDEO_OPEN Handler

The video driver code lives in the system heap (loaded from the declaration ROM's
sResource). During VIDEO_OPEN, `Execute68kTrap(NewPtrSysClear)` can trigger heap
compaction that **overwrites the driver's own executable code**.

After the allocation returns, verify the driver header is intact. If the drvrOpen
offset at A2+0x2A is not $0032, the code has been corrupted. Repair by rewriting
the entire driver from the slot_rom template (offsets $32-$6E).

### Step 5: Verify and Test

**Expected boot sequence if everything works correctly:**
1. ROM init → timer/SCSI/ADB skips → reaches INSTALL_DRIVERS
2. INSTALL_DRIVERS pass 1 → saves state, inits $02BA dispatch table
3. Slot Manager SInit → trampoline at $0D42 enables slot $0 → finds declaration ROM
4. Video driver Open called → VIDEO_OPEN EmulOp → framebuffer configured
5. CHECKLOADs proceed (each resource loaded once, no repeats)
6. INSTALL_DRIVERS pass 2 → restores state after MEMCOPY
7. REPAIR_DRIVERS → fixes DCEs after warm start
8. Boot continues to desktop

**If CHECKLOADs repeat** (same resources load twice), the Slot Manager didn't find
the video driver. Check: trampoline address, declaration ROM placement, sRsrc format.

**If the heap gets corrupted**, the video driver isn't opening correctly. Check:
VIDEO_OPEN handler, DRIVER_DONE busy bit clearing, 24-bit masking.

**Do NOT add newcpu.cpp debug traces or heap patches.** Use fprintf in EmulOp
handlers and ROM patch code instead.

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/config/emulator_config.cpp` | IIci model preset, --model flag |
| `src/config/emulator_config.h` | Model preset struct (if needed) |
| `src/core/rom_patches.cpp` | `patch_rom_iici()` — all ROM patches + slot $0 trampoline |
| `src/core/emul_op.cpp` | IIci EmulOp handlers, 24-bit masking, DRIVER_DONE |
| `src/core/slot_rom.cpp` | Verify works with 512KB ROM / 24-bit addresses |
| `src/main.cpp` | Model-aware startup (if needed) |

**Do NOT modify:**
- `src/cpu/uae_cpu/newcpu.cpp` — zero per-instruction hooks
- `src/common/include/memory.h` — no watchpoint hooks
- Any heap management code

---

## Key Constants and Addresses

| Constant | Value | Notes |
|----------|-------|-------|
| IIci ROM size | $80000 (512KB) | Guard: `ROMSize <= 0x80000` |
| IIci ROMBaseMac | $00800000 | With 8MB RAM |
| Slot ROM top byte | $0087FFFF | `ROMBaseMac + ROMSize - 1` |
| Trampoline address | ROM+$0D42 | Dead padding, 14 bytes available |
| SInit status patch | ROM+$5ED0 | BSR.W to trampoline |
| INSTALL_DRIVERS | ROM+$1142 | Same offset as Quadra |
| REPAIR_DRIVERS | ROM+$134E | BSR redirect from $1D10 |
| IRQ handler | ROM+$26E8 | EmulOp + TST.L D0 + RTE |
| VIA handler (IIci) | ROM+$9BC0 | vs Quadra's $9BC4 |
| System zone base | $002000 | Zone header here |
| bkLim | zone+$00 | **NOT zone+$04** (that's purgePtr) |
| zcbFree | zone+$0C | Free byte count |
| Size mask | $031A | Low-mem global for block size masking |
| $02BA dispatch table | $02BA | 16 entries, must be pre-initialized |
| Warm start skip | ROM+$1D36 | BNE → BRA |
| Zone limitPtr | ROM+$050E | $3800 → $200000 |

---

## Traps to Avoid

1. **bkLim is at zone+$00, not zone+$04.** We wasted hours reading purgePtr
   instead of bkLim. The Mac zone header has bkLim as its first field.

2. **Don't debug heap corruption — fix missing drivers.** If the heap is corrupt,
   something upstream failed to initialize. The heap itself is fine.

3. **Don't add newcpu.cpp traces.** They create 900+ line diffs, slow the emulator,
   and make it impossible to find real issues in the noise.

4. **The driver reinit loop at ROM $1AEDE (`CLR.L $8(A2)`)** zeros DCE fields that
   can overlap heap blocks. This is normal ROM behavior — it only causes problems
   when the Slot Manager failed to set up drivers properly.

5. **CHECKLOADs repeating = Slot Manager failure.** The IIci loads PACK 4,5,7 then
   loads them again. This means the ROM re-entered initialization because the first
   pass didn't complete (video driver not found → error path → restart init).

6. **The IIci's INSTALL_DRIVERS runs twice.** Pass 1 saves state, pass 2 restores
   after MEMCOPY. The Quadra runs it once. Track pass count in a static variable.

7. **EmulOp encoding differs by backend.** UAE uses $71xx (MOVEQ overload), Unicorn
   uses $AExx (A-line trap). `platform_make_emulop()` handles this, but the slot ROM
   driver code embeds EmulOps directly — make sure `slot_make_emulop()` uses the
   correct encoding for the active backend.

8. **VIDEO_OPEN can corrupt its own driver code.** The driver is heap-resident.
   `NewPtrSysClear` during Open can trigger compaction that moves/overwrites the
   driver block. Always verify and repair driver code after any heap allocation
   inside VIDEO_OPEN.

9. **24-bit masking is needed everywhere on IIci.** Every `ReadMacInt32` of a Mac
   handle or pointer must be masked with `& 0x00FFFFFF`. This includes DCE pointers,
   unit table entries, GDevice list pointers, and anything from the Memory Manager.

10. **Slot $0 trampoline must point to `ROMBaseMac + ROMSize - 1`.** For 512KB ROM
    with 8MB RAM, that's $0087FFFF. The declaration ROM is placed at the end of the
    ROM image by `InstallSlotROM()`. If the trampoline points elsewhere, the Slot
    Manager reads garbage.
