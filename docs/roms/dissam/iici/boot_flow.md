# Mac IIci Boot Flow

Walk of the Mac IIci ROM (`$368CADFE`, 512 KB, version `$067C` — the
"Universal 32-bit clean" ROM) from reset to the launch of the first
non-ROM driver, annotated with the labels imported from
`cy384/68k-mac-rom-maps/MacIIciROM.lst.txt`. All ROM addresses below
are physical post-overlay (`$40800000 + offset`).

> **WIP status (2026-04-04).** The IIci path (`patch_rom_32` +
> `patch_rom_iici` in `src/core/rom_patches.cpp`) does *not* boot to
> Finder. It progresses far into init but crashes somewhere after MMU
> setup. Every reference below marked `PATCH-WIP` is an attempted
> workaround, not a verified fix. Companion listing:
> `docs/roms/dissam/iici/rom.lst` — 115 patch sites, 2,081 imported
> symbols.

---

## 1. Reset vector

| Offset | Content                  | Purpose |
|--------|--------------------------|---------|
| `$0000` | `36 8C AD FE`            | Apple checksum (matches filename) |
| `$0004` | `40 80 00 2A`            | Initial PC |
| `$0008..$0029` | JMP-table header  | Dispatch offsets to various init phases |
| `$002A` | `jmp $4080008c`          | Into cold-init trampoline |
| `$008C` | `ResetInit` (unlabeled)  | Real cold-init entry |

Bytes at `$8C`:
```
40800090  move  #$2700,SR      ; mask all interrupts
40800094  reset                 ; 68K hardware reset
40800096  move.l #$2000,D0     ; cache enable value
4080009c  movec  D0,CACR        ; enable 68030 inst cache
408000a0  movec  CACR,D0        ; verify
408000a4  tst.l  D0             ;
408000a6  beq    $408000ac      ; skip PMOVE if no cache
408000a8  lea    (-$1c,PC),A0
408000ac  pmove.l (A0),TC       ; load MMU translation control
                                ; → `PATCH-WIP 1301` installs EmulOp here
```

### Critical patches at the top

| Addr | cpp line | What it tries to do |
|------|----------|---------------------|
| `$8C` (BEFORE `move #$2700`) | `patch_rom_32:1301` | Install `EmulOp(RESET)` + JMP to `$BA` — bypasses the real reset path entirely |
| `$B4` | `patch_rom_iici:2262` | Change JMP target from `$BA` to `$B4` (a BRA.W $08E0) |

Both are the foundation of our "skip hardware init" strategy. If they
don't work, nothing downstream can.

---

## 2. STARTINIT1 (`$408000B8`) — the bypassed original path

This is where real hardware would init. On IIci the flow is:

```
STARTINIT1:
  moveq   #0,D2
  movem.l D5-D7/A5-A6,-(SP)
  lea     (6,PC),A6
  jmp     GETHARDWAREINFO     ; $40802f18 — NOPed      <- PATCH-WIP 1308
  movea.l (8,A0),A4
  moveq   #$40,D4
  and.b   (A4),D4
  lea     (6,PC),A6
  jmp     INITVIAS            ; $40802e8c — NOPed      <- PATCH-WIP 1313
  ...
  bsr     WHICHCPU            ; probe 68030 vs 68040
  bsr     WHICHBOARD          ; read box ID
  bsr     CONFIGURERAM        ; walks RAM rows
  bsr     INITMMU             ; $408042FE — critical   <- PATCH-WIP 2917/2919
  ; At this point the BootGlobs pointer (-0x14,A4) is critical:
  movea.l (-$14,A4),A6       ;                          <- PATCH-WIP 1428
  ...
  bsr     SETUPHWBASES        ; populate hardware base pointer table
  bsr     INITSCC             ; $40800A2E               <- PATCH-WIP (skipped)
  bsr     INITIWM             ; $408009C0               <- PATCH-WIP 1450
  bsr     INITSCSI            ; $408009A0               <- PATCH-WIP 1454
  ...
  bsr     SETUPHWBASES        ; second pass after MMU on
  move.b  D7,(CPUFlag)
  ...
  bsr     INITMMUGLOBALS
  jmp     SYSERRINIT          ; install system error stubs
  move.l  #$3919,D0
  movec   D0,CACR             ; enable data cache
  bsr     ENABLEEXTCACHE      ; IIci L2 cache          <- PATCH-WIP 1467
  ...
  bsr     SETUPTIMEK          ; calibrate DBF loop count
  bsr     INITHIMEMGLOBALS    ; ≥1MB memory globals
```

### Why most of this is NOPed

MacPhoenix has no real VIA/SCC/IWM/SCSI/MMU hardware. Any routine that
probes these will either hang in a polling loop or dereference a junk
register value. The strategy in `patch_rom_32` is:

1. **NOP the probes** — `GETHARDWAREINFO`, `INITVIAS`, `INITSCC`,
   `INITIWM`, `INITSCSI`, `ENABLEEXTCACHE`, `ENABLEPARITYPATCH`.
2. **Fake the results** that the downstream code expects (hardware
   base address table, BootGlobs).
3. **Let MMU init run for real** — 68030 MMU is emulated and the
   translation tables matter for 24-bit compatibility mode later.

The `PATCH-WIP 2917/2919` pair at `INITMMU` replaces the `LINK A5,#-$74`
with a BRA.W to a C trampoline, so we can substitute our own MMU state.

---

## 3. INITMMU (`$408042FE`) — still runs

Unlike VIA/SCC/etc., MMU init is not NOPed. The 68030 PMMU is emulated
and the page tables matter for correct address translation when the
ROM later tries to use 24-bit compatibility traps. Structure:

```
INITMMU:
  link.w  A5,#-$74
  bsr     FUN_40804392        ; read RAM config from CPUFlag/decoderInfo
  bsr     FUN_40804538        ; build translation control word
  cmpi.b  #1,(-$1A,A6)
  bne     LAB_4080431E
  ; Single-bank fast path
  ...
LAB_4080431E:
  ; Multi-bank: build segment descriptors
  bsr     FUN_4080463E        ; fill segment table
  bsr     FUN_4080476C        ; fill page table
  bsr     FUN_40804480
  bsr     FUN_408043FE
  pmove.d (-$8,A3),CRP         ; load CPU root pointer
  ...
  pflusha                      ; flush MMU TLB
  movec   CACR,D5
  ori.w   #$808,D5            ; enable data cache + burst
  movec   D5,CACR
  jmp     A6                   ; jump to post-MMU code
```

This is one of the most fragile regions — any error in the page table
construction causes protection faults later. If IIci boot diverges
here, suspect that `CPUFlag` / `decoderInfoPtr` is wrong relative to
what our fake hardware setup provides.

---

## 4. BOOTRETRY (`$408001A6`) — restartable OS init

After STARTINIT1 jumps through `SETUPTIMEK` and `INITHIMEMGLOBALS`, we
reach BOOTRETRY. This is the high-level init chain, analogous to SE's
BOOTRETRY but with IIci-specific additions:

```
BOOTRETRY:
  move    #$2700,SR
  moveq   #1,D0
  movea.l (DAT_DBC),A0
  jsr     (A0)                  ; call via indirect pointer
  jsr     INITGLOBALVARS
  ; indirect calls to ROM-relative thunks
  jsr     $4080a03e              ; sub-init A
  jsr     $40809f56              ; sub-init B
  jsr     $40809c9c              ; sub-init C
  bsr     INITMMUTRAP            ; install _HWPriv MMU traps
  bsr     GETPRAM                ; read PRAM
  bsr     INITMEMMGR             ; create SysZone
  btst    #0,(DAT_1EFC)
  ...
  bsr     SETUPSYSAPPZONE
  bsr     INITSWITCHERTABLE
  bsr     INITRSRCMGR
  jsr     $4081ca60              ; Resource scan
  jsr     $4080aebc
  bsr     INITSHUTDOWNMGR        ; <- IIci-specific
  jsr     $40800ff8
  bsr     INITDTQUEUE            ; Deferred Task Queue
  jsr     $40809d94
  jsr     $40809d6a              ; [PATCH-WIP 1594] Don't EnableParityPatch
  move    #$2000,SR              ; drop to interrupt level 2
  bsr     INITVIDGLOBALS
  bsr     COMPBOOTSTACK          ; set boot stack to $2000
  ; Warm-boot magic check
  move.b  (DAT_1EFC),-(SP)
  btst    #2,(DAT_1EFC)
  ...
  bsr     INITIOMGR              ; <<< driver install, see §6
  bsr     INITCRSRMGR
  ; ... more init ...
```

### Patches applied in BOOTRETRY

- `rom_patches.cpp:1594` — `Don't EnableParityPatch/Enable60HzInts` at
  `$40800230` — the parity handler probes RAM control registers.
- `rom_patches.cpp:1277` — `ScratchMem` detection in `SETUPHWBASES`
  (data table of decoder-info indices).
- `rom_patches.cpp:2290` — fixes `$02D6-$02E4` so `TheZone`/`ApplZone`
  propagation works without original SysZone layout.

---

## 5. Slot Manager Init (`$40805E20`) — IIci-specific

IIci has 3 NuBus slots **plus** an on-board pseudo-slot for the RBV
video controller. `INITSLOTMGR` walks slots `$E..$1` looking for valid
declaration ROMs. Structure:

```
INITSLOTMGR:
  movem.l  D1/A1-A2,-(SP)
  bsr      FUN_40805E48          ; seed slot iteration state
  moveq    #$E,D1                ; start at slot $E
LAB_40805E2A:
  move.b   D1,($31,A0)           ; slot number
  moveq    #$2F,D0
  movea.l  (A0),A1               ; A1 = slot base
  tst.w    (4,A1)                ; probe slot declaration ROM
  bmi      LAB_40805E3E          ; no card
  bsr      FUN_40805F2A          ; init card
LAB_40805E3E:
  dbf      D1,LAB_40805E2A
  movem.l  (SP)+,D1/A1-A2
  rts
```

### Critical patch: skip slot scan

`rom_patches.cpp:2780` at `$40805E8C` — the inner write loop in
`FUN_40805E48` is neutered by patching its CLR.L so entries stay zero,
making the rest of the scan skip all slots. **This is aggressive** —
any Nubus declarations (including the IIci's own RBV pseudo-slot) are
lost, which is why RBV init has separate handling.

There are several related `PATCH-WIP` sites in this vicinity (`2295`,
`2311`, `2317`, `2326`, `2332`, `2349`, `2358`, `2367`) that all deal
with the slot manager / NuBus scan being bypassed.

---

## 6. INITIOMGR + INITIOPMGR (`$408010F0`) — driver install

The IIci layers two levels of I/O manager init:

1. **`INITIOMGR`** — classic Mac driver table install (same shape as
   SE's `INITIOMGR`): walks ROM `DRVR` resources, calls `_Open` on
   each, populates the Unit Table.

2. **`INITIOPMGR`** — IOP (I/O Processor) manager init, IIci-specific.
   This one initializes the Apple SWIM II and ADB Transceiver as
   IOP-accelerated devices.

Patches at `$10F0`:

```
INITIOMGR:
  bsr     INITIOPMGR          ; [PATCH-WIP 2430] JMP trampoline
                              ; [PATCH-WIP 2464] BTST bytes
408010F4  btst  #0,(DAT_DD0+1)
408010FA  beq   LAB_40801106
...
```

The BSR at `$10F0` is replaced with a JMP to a 6-byte trampoline that
eats into the next instruction (`$10F6`), hence the dual PATCH-WIP
comments. This is the driver install entry point.

---

## 7. OPENSDRVR (`$40800CA0`) — individual driver open

Called by `INITIOMGR` for each `DRVR` resource. Responsible for
allocating a Unit Table entry and calling the driver's `DRVROpen`.

BSR sites: `$40800D36`, `$40801040`, `$408022A4` — these are
per-driver open calls in sequence (.Sony, .Sound, .ATalk, etc.).

Current IIci boot crashes somewhere between `INITIOPMGR` and
`OPENSDRVR`; exact point varies per run depending on stack contents.
This is the **next investigation area**.

---

## 8. Hardware bases (Mac IIci)

| Device | Base | Notes |
|--------|------|-------|
| VIA1  | `$50F00000` | ADB, PRAM, PM-FPU interrupt |
| VIA2  | `$50F02000` | NuBus, RBV interrupts, Ethernet |
| SCC R | `$50F04000` | Serial read |
| SCC W | `$50F06000` | Serial write |
| IWM   | `$50F16000` | SWIM (Sander-Woz IM) |
| SCSI  | `$50F10000` | NCR 5380, pseudo-DMA at `$50F12060` |
| RBV   | `$50F24000` | Video controller (on-board pseudo-slot) |
| ASC   | `$50F14000` | Apple Sound Chip |
| PRAM  | via VIA1    | 256 bytes battery-backed |

The universal decoder info table (referenced via A4 in STARTINIT1)
contains offsets into the `UniversalInfo` ROM structure that resolve
to these bases at runtime. `SETUPHWBASES` uses it to populate the
low-memory globals table.

`SETUPHWBASES` at `$40800910` is the central site — read this routine
carefully if you're debugging hardware base issues. It walks a table
at `-$1c,PC` of `(bit, lmg_offset)` pairs.

---

## 9. Complete PATCH-WIP site summary

115 cross-references across the IIci listing. Rough distribution:

| Category | Count | Offset range | Purpose |
|----------|-------|--------------|---------|
| Reset/entry shim | ~5 | `$8C`–`$C0` | EmulOp(RESET), skip probe |
| Hardware probe NOPs | ~12 | `$C2`–`$134` | VIAs, SCC, IWM, SCSI |
| MMU init trampoline | ~4 | `$42FE`–`$4306` | LINK replaced with BRA |
| `SETUPHWBASES` data | ~3 | `$94A`–`$950` | fake decoder info |
| Slot Manager skip | ~10 | `$5E20`–`$5FCC` | neuter NuBus scan |
| IOP manager hook | ~4 | `$10F0`–`$10FA` | INITIOPMGR JMP trampoline |
| Parity/cache disables | ~8 | `$188`–`$230` | skip ENABLEEXTCACHE, etc. |
| Driver table entries | ~6 | various | `.Sony`/`.Sound`/`.AIn`/`.AOut` installers |
| Misc one-off fixes | ~63 | various | see listing |

All still `PATCH-WIP` — none verified.

---

## 10. Current known blockers (as of HEAD)

From the project memory (`memory/project_iici_boot.md`) and recent
commits:

1. **`_InitGraf` reached** — we get past `INITIOPMGR`, past driver
   install, into the early QuickDraw init.
2. **24-bit address masking crash** — a bus error on an address that
   would be valid in 24-bit mode but invalid in 32-bit mode.
3. **MMU trap `_HWPriv`** — our implementation of the `_HWPriv` trap
   for Nubus slot access is incomplete.
4. **`$40844B94`** — `#0x50f00000,A0` direct VIA1 access in
   TestRBV/RBV paths, even after the Slot Manager is neutered.

The first productive fix is almost certainly to handle the specific
MMU-mode crash at the 24-bit boundary. The listing entries for
`TESTRBV` (`$40843782`) and `U_TESTRBV` (around `$408437E2`) are the
places to study.

---

## 11. See also

- Listing: `docs/roms/dissam/iici/rom.lst` (regenerate with
  `tools/export_listing.sh iici`)
- Patches: `src/core/rom_patches.cpp` — `patch_rom_32` and
  `patch_rom_iici`
- Memory: `memory/project_iici_boot.md` (stored project notes)
- Related: `docs/iici/`, `docs/IIci_Boot_Status.md`,
  `docs/iici_boot_plan.md` (older manual notes)
- SE walkthrough: `docs/roms/dissam/se/boot_flow.md` (similar structure,
  simpler hardware)
