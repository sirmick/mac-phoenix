# Mac SE Boot Flow

Walk of the Mac SE ROM (`$B2E362A8`, 256 KB, version `$0276`) from power-on
reset to Finder launch, annotated with the labels imported from
`cy384/68k-mac-rom-maps/MacSEROM.lst.txt`. Every ROM address below refers
to the physical post-overlay layout (`$00400000 + offset`). The
**companion listing** lives at `docs/roms/dissam/se/rom.lst` — regenerate
it from the ROM via `tools/export_listing.sh se`.

> **WIP status (2026-04-04).** The SE path in `src/core/rom_patches.cpp`
> (`patch_rom_classic`) does *not* currently boot to Finder. It does reach
> `INSTALL_DRIVERS` as of commit `2a0ddd3f`, which is a major milestone.
> Every patch referenced below is marked `PATCH-WIP` in the listing and
> should be treated as "attempted, not verified." Expect revisions.

---

## 1. Reset vector

The CPU boots into the ROM overlay (ROM temporarily mapped at `$00000000`).
Byte layout of the overlay:

| File offset | Content                          | Purpose |
|-------------|----------------------------------|---------|
| `$0000`     | `B2 E3 62 A8`                    | Apple ROM checksum (matches filename) |
| `$0004`     | `00 40 00 2A`                    | Initial PC (M68K reads as reset vector) |
| `$0008–$0029` | JMP-table / dispatch header    | Apple ROM header (mostly data) |
| `$002A`     | `ResetEntry` — first executed byte | → jumps into `STARTTEST1` |

After the CPU latches the initial PC, the very first instruction runs at
**`$0040002A`**. Control rapidly hops through a few trampolines into
**`STARTINIT1`** at `$00400048`, which is the real entry point for the
init sequence.

*(Note: the disassembly around `$40002A` looks garbled in the listing
because Ghidra brute-force disassembles the header bytes. The real flow
enters `STARTINIT1`.)*

---

## 2. STARTINIT1 — cold hardware init (`$00400048`)

This is the first thing that runs with a sensible environment. It
initializes the peripherals in a fixed order before the OS heap exists.

```
STARTINIT1:
  bsr INITVIA       ; VIA1: ADB, timers, system interrupt source
  bsr INITSCC       ; Serial Communication Controller (modem/printer ports)
  bsr INITIWM       ; Integrated Woz Machine (floppy controller)     <- PATCH-WIP 1047
  bsr INITSCSI      ; NCR 5380 SCSI controller
  bsr WHICHCPU      ; Probe 68000 vs 68020 vs 68030 (SE is 68000)
  ...               ; stack setup, A6 = MemTop guess
  jsr RAMTEST       ; Walking-ones RAM sizing                        <- PATCH-WIP 1082
  bsr BOOTBEEP      ; Startup chime                                  <- PATCH-WIP 1052
  ...
  jsr RAMTEST       ; Second pass                                     <- PATCH-WIP 1084
  lea (0x100),A0
  lea ($1600),A1
  bsr FILLWITHONES  ; Zero/fill low-memory globals region
  ...
  lea (0x6,PC),A6
  jmp SYSERRINIT    ; Install system error trap handler table
  bsr SETUPTIMEK    ; *** TIMER CALIBRATION — critical, see §3 ***
  bsr VIATIMERENABLES
  ...
  ; Jump into BOOTRETRY (the restartable part)
```

### Patches applied in STARTINIT1

| Addr | Patch file:line | Effect |
|------|-----------------|--------|
| `$50` | `rom_patches.cpp:1047` | Skip `INITIWM` — no real IWM hardware to drive |
| `$64` | `rom_patches.cpp:1082` | Skip first `RAMTEST` — walking-ones test hangs without real RAM controller |
| `$6A` | `rom_patches.cpp:1052` | Skip `BOOTBEEP` — skips startup chime |
| `$86` | `rom_patches.cpp:1084` | Skip second `RAMTEST` |

All four are **`PATCH-WIP`**: the skip is plausible but neither the skip
order nor the fall-through side effects are verified.

---

## 3. SETUPTIMEK (`$0040041C`) — the CRITICAL timer calibration

This routine measures how many 68000 DBF loop iterations happen in one
VIA timer tick, so `MicroSeconds()` and other time-sensitive ROM code can
calibrate itself. It's the site of **the biggest SE debugging
breakthrough in `2a0ddd3f`**.

```
SETUPTIMEK:
  move   SR,-(SP)          ; save interrupt mask
  move.l ($0064),-(SP)     ; save vector table entry 0x64 (Level-1 autovector)
  movea.l #$EFE1FE,A1      ; A1 = VIA1 base
  bclr.b #5,($1600,A1)     ; clear VIA1 IFR bit 5
  move.b #$FF,($1200,A1)   ; VIA1 T2 counter low  — $042E  ← disables T2 interrupt
  move.b #$A0,($1C00,A1)   ; VIA1 IER             — $0434  ← arms T2 enable bit
  andi   #$F8FF,SR         ; enable interrupts
  ...
  ; DBF loop — count iterations until T2 timeout
  dbf  D0,LAB_00400458
  ...
  move.w D0,(TimeDBRA).w   ; store result at $0D00
```

### Why this matters for the emulator

On real hardware, the write to `$1200` (VIA T2CL) disables pending T2
interrupts, and the write to `$1C00` (VIA IER) arms T2 fresh, then
`andi #$F8FF,SR` enables CPU interrupts. No stray tick fires between
those steps.

On MacPhoenix the VIA is **not emulated as a real device**: the writes
to `$1200` / `$1C00` are silent no-ops. Meanwhile our 60 Hz timer thread
keeps firing Level-1 IRQs regardless. The first tick after `andi` would
land on whatever lives at vector `$64` — which during this window points
to the `SYSERRINIT` exception stubs at `$1384–$13xx` that all jump to
`TODEEPSHIT` (the built-in crash handler).

### Fix (commit `2a0ddd3f`)

`patch_rom_classic` sets `tick_inhibit = true` before letting the ROM
run, and a new EmulOp (installed at `INSTALL_DRIVERS`, i.e. well past
`INITDISPATCHER`) clears it once a real trap dispatcher is installed and
interrupt routing is safe.

> `SETUPTIMEK`'s DBF loop then completes its ~65,536 iterations and
> returns normally — measured at emulator instruction `#599868` from
> power-on in the commit log.

**No `PATCH-WIP` markers here**, because the fix is structural (CPU
timer inhibit flag) not a ROM byte-patch. If you're trying to reproduce
or extend the SE boot, this whole region is what you're studying.

---

## 4. VIATIMERENABLES (`$004004B0` vicinity)

Reconfigures VIA1 T1 for the eventual 60 Hz system tick. Short routine;
nothing to patch here. After it returns, execution reaches the
`SYSERRINIT` trampoline and branches into `LAB_004000B0` → falls through
to `BOOTRETRY`.

---

## 5. BOOTRETRY (`$004000D2`) — restartable boot sequence

This is the main init chain. It is designed to be re-entered on certain
non-fatal failures (corrupted PRAM, failed boot device, etc.), which is
why it's separate from `STARTINIT1`.

```
BOOTRETRY:
  move #$2700,SR             ; disable interrupts for init
  bsr INITGLOBALVARS         ; zero/init LowMemGlobals
  bsr INITXVECTTABLES        ; exception vector $00..$3FF
  bsr INITDISPATCHER         ; <<< trap dispatcher install
  bsr GETPRAM                ; read parameter RAM via VIA
  bsr INITMEMMGR             ; Memory Manager — creates SysZone
  bsr SETUPSYSAPPZONE        ; ApplZone in SysZone
  bsr INITSWITCHERTABLE      ; multitasking stub
  bsr INITRSRCMGR            ; Resource Manager — reads ROM resources
  bsr INITTIMERMGR           ; Time Manager
  bsr INITADBVARS            ; ADB Manager globals
  move #$2000,SR             ; interrupts back on (level-2 mask)
  jsr INITADB                ; scan ADB bus for keyboard/mouse    <- PATCH-WIP 1057 (loop skip)
  bsr INITVIDGLOBALS
  ...
  cmpi.l #'WLSC',(DAT_CFC)   ; warm-start magic check
  beq   LAB_00400122         ; warm start: skip big RAM test
  jsr   RAMTEST              ;                                     <- PATCH-WIP 1066
LAB_00400122:
  bsr COMPBOOTSTACK          ; Compute boot stack pointer
  ...
  jsr INITQUEUE
  jsr INITSCSIMGR
  bsr INITIOMGR              ; .Sony, .Sound etc driver table install
  bsr INITCRSRMGR
  ...
  bsr DRAWBEEPSCREEN         ; blank screen with pattern
  move.l #'WLSC',(DAT_CFC)   ; set warm-start magic for next boot
  bra BOOTME
```

### Critical milestone: INITDISPATCHER ($004006DA)

**After `INITDISPATCHER` returns, the A-line trap vector at `$28` points
to the real ROM trap dispatcher at `$402CD6`.** Before this, A-line
traps hit `TODEEPSHIT` exception stubs. This is the exact point where
our `tick_inhibit` flag is safe to clear — and where the `INSTALL_DRIVERS`
EmulOp fires, since `INITIOMGR` installs `.Sound` right after.

---

## 6. INSTALL_DRIVERS — the breakthrough point

From `INITIOMGR` (`$0040076E`):

```
INITIOMGR:
  moveq #$40,D0
  move.w D0,(UnitNtryCnt)   ; 64 unit table entries
  ...
```

`INITIOMGR` walks the ROM resource list for `DRVR` resources and calls
`_Open` on each. On SE, this hits:

- `$00400798` — `_Open ".Sound"` sequence (MacPhoenix intercepts this)
- `$00436CAA` — inside the `.Sound` driver's own `DRVROpen` routine
  (`SoundDCE` write)

The MacPhoenix EmulOp installed by the fix fires here, clears
`tick_inhibit`, and logs `[BOOT] INSTALL_DRIVERS EmulOp fires!`.

### Current known stuck point after this

Per the commit, boot reaches **`$004022FC`** next — a second SCC
polling loop (distinct from the one at `$004022F4` already patched).
Disassembly of that region:

```
004022EC  move.w #$8000,D5
004022F0  btst.l #17,D7         ; test hardware flag in D7
004022F4  beq.b  LAB_00402350    ; <- already patched, jumps past SCC probe
004022F6  lea    ($9FFFF8),A2    ; SCC channel B base
004022FC  btst.b #0,(2,A2)       ; *** STUCK HERE — polls SCC RR0 ***
00402302  beq.b  LAB_00402350
...
```

This loop polls the SCC's Read Register 0 (status) bit 0 (Rx Character
Available). Without a real SCC, the bit never changes and the loop
hangs. The existing patch at `$22F4` skips the first SCC probe; a
parallel patch at `$22FC` (or a BRA to `LAB_00402350` directly) is
likely needed.

---

## 7. BOOTME → BOOTING ($00400756 onward)

Once `BOOTRETRY` returns, `BOOTME` runs the high-level boot:

```
BOOTME
  → MOUSEINIT       ; $004010A2 - set up cursor
  → INITFS          ; $004010C2 - File Manager init
  → INITEVENTS      ; $00401168 - Event Manager
  → FINDSTARTUPDEVICE ; $00401189 - locate boot disk
  → LOADDRIVERS     ; $004012D4 - load non-ROM drivers
  → (reads boot blocks, launches System/Finder)
```

We have not yet reached this territory on SE. Once `$22FC` (and any
subsequent SCC/polling blockers) are resolved, the next milestone is
**reaching `FINDSTARTUPDEVICE`**, where the emulator's ExtFS or disk
driver takes over.

---

## 8. Hardware base addresses (Mac SE)

Used throughout the boot sequence; useful for pattern-matching in the
listing and for planning NOP patches.

| Device     | Base          | Notes |
|------------|---------------|-------|
| VIA1       | `$00EFE1FE`   | ADB, PRAM, 60 Hz timer, sound |
| SCC ch. A  | `$009FFFF8`   | modem port (read) |
| SCC ch. B  | `$00BFFFF9`   | printer port (write) |
| IWM        | `$00DFE1FF`   | floppy |
| SCSI       | `$005FF000`   | NCR 5380 |
| ASC / snd  | `$00E80000`   | Apple Sound Chip (SE has ASC) |

Any instruction whose effective address resolves into these ranges is a
hardware access. For each one we want the emulator to survive, we
either:
1. Let it fall through to a SIGSEGV handler that skips the instruction
   (current approach for most MMIO reads)
2. Replace with NOPs via `patch_rom_classic`
3. Install an EmulOp that returns sensible values

The listing's `PATCH-WIP` comments flag sites where option 2 has been
attempted.

---

## 9. Complete patch site index (as of HEAD)

All 24 SE patch sites from `patch_rom_classic`, sorted by ROM offset:

| Offset | Purpose | cpp line |
|--------|---------|----------|
| `$0050` | Skip `INITIWM` | 1047 |
| `$0064` | Skip `RAMTEST` (first) | 1082 |
| `$006A` | Skip `BOOTBEEP` | 1052 |
| `$0086` | Skip `RAMTEST` (second) | 1084 |
| `$011E` | Skip `RAMTEST` (BOOTRETRY) | 1066 |
| `$0508` | Don't loop in ADB init | 1057 |
| `$0798` | `.Sound _Open` — EmulOp hook | (new in 2a0ddd3f) |
| `$1044` | `ClkNoMem` patch | 1061 |
| `$1C40` | BRA.S $1C60 (orig) | 1022 |
| `$1C6C` | `4A86` → `7C00` force Z=1 | 1042/1044 |
| `$1D12` | BCLR #16,D7 (was BSET) | 1097 |
| `$1D50` | BRA.W (was BEQ.W) | 1078 |
| `$1E3C` | BRA.W (was BEQ.W) | 1066 |
| `$1E38` | Don't jump into debugger | 1037 |
| `$22F4` | BRA past SCC probe #1 | 1108 |
| `$22FC` | **MISSING** — SCC probe #2, current blocker | — |
| `$36CAA` | `.Sound` DRVROpen EmulOp | (new in 2a0ddd3f) |
| `$798`–`$7FA` | `.Sound`/`.Sony` unit table entries | 1143/1155 |
| `$1144`–`$114C` | NOP sled after BOOTBEEP skip | 1633–1646 |
| `$1220`–`$1230` | Level-1 / 60 Hz handlers | 1220/1231 |

> The row for `$22FC` is the **next thing to fix**. See §6.

---

## 10. Investigation map

When debugging a new blocker, use this decision tree:

1. **What's the current PC?** Grep the listing for the address.
2. **Is it inside a named routine?** Scroll back to the preceding
   `UPPERCASE:` label — that's your function name.
3. **Is there a `PATCH-WIP` comment nearby?** A failed existing patch is
   more likely than an unknown bug.
4. **Hardware access?** Cross-reference §8. MMIO reads need either
   SIGSEGV-skip, NOP patch, or EmulOp.
5. **Trap dispatch failure?** If before `INITDISPATCHER` (anything
   strictly before `$006DA` through the BOOTRETRY chain), A-line traps
   crash via `TODEEPSHIT`. Use `tick_inhibit` or avoid the trap.
6. **Stuck in a polling loop?** Examine `btst`/`tst`/`cmp` patterns
   around the PC. Every one is a candidate for BRA patching.

---

## See also

- Listing: `docs/roms/dissam/se/rom.lst` (build artifact — run
  `tools/export_listing.sh se` to generate)
- Patches: `src/core/rom_patches.cpp` — search for `patch_rom_classic`
- Tick inhibit / INSTALL_DRIVERS: `src/core/emul_op.cpp`,
  `src/cpu/uae_cpu/newcpu.cpp`
- Commit `2a0ddd3f` — the SETUPTIMEK / tick_inhibit breakthrough
- `cy384/68k-mac-rom-maps/MacSEROM.lst.txt` — source of the labels
