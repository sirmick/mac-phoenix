# Mac Hardware MMIO Map (SE, II, IIx/IIcx/SE-30, IIci)

Consolidated map of memory-mapped I/O regions across the four ROMs
covered under `docs/roms/dissam/`. Use this to identify what a given
ROM address is trying to talk to when you see an MMIO access in a
boot trace.

**Emulator implication:** any instruction whose effective address
resolves into one of these ranges is a hardware access. To get the
ROM past it, you need one of:
1. **SIGSEGV skip** — let a fault handler advance PC past the
   instruction. Default today for unhandled reads.
2. **Dummy bank** — route the region to a scratch memory bank that
   reads back zeros or ones. The Mac II `$50F14000`-style "safe
   defaults" pattern.
3. **NOP patch** in `rom_patches.cpp` — when the access is in a
   well-known probe routine, patch the BSR/JSR out.
4. **EmulOp hook** — install a trap opcode that transfers to a host
   handler that returns a realistic value (needed for timers, ADB
   events, read-back-for-write bits).

---

## Memory address spaces

The four machines use two fundamentally different address layouts:

### 24-bit machines (SE)

ROM and RAM in the bottom 16 MB of address space:

| Region | Range | Notes |
|--------|-------|-------|
| RAM | `$000000..$3FFFFF` | 1–4 MB, depends on SIMMs |
| ROM (shadow) | `$400000..$43FFFF` | 256 KB, also mirrored at `$600000..$63FFFF` |
| SCC read/write | `$9FFFF8..$BFFFF9` | Serial (Z8530) |
| IWM | `$DFE1FF` | Floppy |
| VIA1 | `$EFE1FE` | ADB, timers, sound, PRAM |
| SCSI | `$5FF000..$5FFFFF` | NCR 5380 |
| ASC | `$E80000..$E9FFFF` | Apple Sound Chip (SE/Plus have ASC) |

### 32-bit machines (Mac II, IIx, IIcx, SE/30, IIci)

ROM shadowed in the upper 1 GB, hardware in a dedicated `$50F00000`
block (for 32-bit-clean ROMs), NuBus slots at `$Fx000000`:

| Region | Range | Notes |
|--------|-------|-------|
| RAM | `$00000000..$(SIZE)` | Up to 128 MB |
| ROM | `$40800000..$4087FFFF` (IIci) / `$40800000..$4083FFFF` (II/IIx) | |
| Hardware (decoded) | `$50F00000..$50FFFFFF` | See table below |
| NuBus slots | `$F0000000..$FEFFFFFF` | Slots `$0..$E`, 16 MB each, SE/30 has PDS slot only |
| NuBus super slots | `$60000000..$EFFFFFFF` | Extended slot space |

The **`$50F00000` hardware range** is the key region on 32-bit
machines. It's decoded into device banks at fixed offsets, and is
different between Mac II and IIci (Mac IIci uses "RBV" / IOP
architecture).

---

## `$50F00000` device bank layout

### Mac II / IIx / IIcx / SE-30 (version `$0178` ROMs)

| Bank | Base | Device |
|------|------|--------|
| 00 | `$50F00000` | VIA1 (sysinfo: ADB, PRAM, 60 Hz tick source) |
| 02 | `$50F02000` | VIA2 (NuBus interrupts, slot status) |
| 04 | `$50F04000` | SCC read (Z8530 ch A/B read) |
| 06 | `$50F06000` | SCC write (Z8530 ch A/B write) |
| 10 | `$50F10000` | SCSI base (NCR 5380) |
| 12 | `$50F12000` | SCSI pseudo-DMA channel |
| 14 | `$50F14000` | ASC (Apple Sound Chip) — IIx onwards; Mac II has this too on rev B |
| 16 | `$50F16000` | IWM (Mac II) / SWIM (IIx+) floppy |
| 18 | `$50F18000` | IWM rev 8 probe (Mac II uses this to detect controller type) |

### Mac IIci (version `$067C` ROM)

IIci adds on-board video (RBV) and changes some bases:

| Bank | Base | Device |
|------|------|--------|
| 00 | `$50F00000` | VIA1 |
| 02 | `$50F02000` | VIA2 |
| 04 | `$50F04000` | SCC read |
| 06 | `$50F06000` | SCC write |
| 10 | `$50F10000` | SCSI base |
| 12 | `$50F12000` | SCSI pseudo-DMA |
| 14 | `$50F14000` | ASC |
| 16 | `$50F16000` | SWIM (IIci is IWM-free) |
| 24 | `$50F24000` | **RBV** — on-board video/memory controller |
| 26 | `$50F26000` | **OSS** — Nubus interrupt controller (some revisions) |

The **RBV** ("RAM-based Video") is IIci's integrated video controller.
It lives in the same decoded-space region the Slot Manager would scan,
but it's not on a real slot — it's hardwired on the motherboard and
responds to pseudo-slot `$E`. The IIci ROM knows about this and calls
`TESTRBV` (`$40843782`) + `U_TESTRBV` to initialize it.

---

## VIA1 register layout

VIA1 is the most-referenced hardware on every Mac. Registers are
spaced at `$200` bytes (see Rockwell 6522 datasheet) and mapped into
the VIA base. For Mac II/IIci at `$50F00000`:

| Offset | Register | Common uses |
|--------|----------|-------------|
| `$0000` | vBufB   | bit 0=rTCCLK, 1=rTCData, 2=rTCEnb, 3=SCCReset, 4=Mode32 (IIci) |
| `$0200` | vBufA   | bit 7=SCCWReq, 6-4=Volume, 3=SndPg2, 2-0=Video |
| `$0400` | vDIRB   | data direction B |
| `$0600` | vDIRA   | data direction A |
| `$0800` | vT1CL   | timer 1 counter low (reload) |
| `$0A00` | vT1CH   | timer 1 counter high |
| `$0C00` | vT1LL   | timer 1 latch low |
| `$0E00` | vT1LH   | timer 1 latch high |
| `$1000` | vT2CL   | timer 2 counter low (used by SETUPTIMEK for DBF calibration) |
| `$1200` | vT2CH   | timer 2 counter high |
| `$1400` | vSR     | shift register (keyboard on pre-ADB machines) |
| `$1600` | vACR    | auxiliary control register |
| `$1800` | vPCR    | peripheral control register |
| `$1A00` | vIFR    | interrupt flag register |
| `$1C00` | vIER    | interrupt enable register (bit 7=enable, bits 6-0=source mask) |
| `$1E00` | vBufANH | Port A, no handshake |

The Mac SE uses the same layout but at base `$EFE1FE`. (Yes, `$EFE1FE`
is the full byte address — VIA registers are at `$EFE1FE` +
`(offset*$200) + 1` on 24-bit machines due to byte-addressing
peculiarities of the Mac I/O bus.)

**Critical for emulation**: writes to `$1200` (vT2CH) disable timer 2
interrupts on real hardware. If our emulator silently ignores those
writes, timer 2 can still fire — see the `tick_inhibit` fix for SE in
`docs/roms/dissam/se/boot_flow.md` §3.

---

## VIA2 register layout (Mac II and later only)

Same 6522 register format but different register function mapping:

| Offset | Register | Common uses |
|--------|----------|-------------|
| `$0000` | vBufB   | bit 0=CDIS (Cache Disable), 1=vBusLk, 2=PowerOff |
| `$0200` | vBufA   | NuBus slot interrupt status |
| `$1800` | vPCR    | peripheral control — configured by INITVIA |
| `$1A00` | vIFR    | interrupt flags (slot interrupts, Ethernet, sound) |
| `$1C00` | vIER    | interrupt enable — `#$02` in Mac II BOOTRETRY enables slot interrupts |

NuBus slot interrupts show up as bits `0..5` in VIA2's IFR. The Mac II
BOOTRETRY writes `$50F02000 $1C00 = $02` to enable the slot IRQ
pathway, then later `$82` to finalize it.

---

## SCC (Zilog Z8530) layout

Serial controller, two channels (A=modem, B=printer). Mapped with a
**read / write split**:

| Access | Mac II/IIci | SE |
|--------|-------------|-----|
| Read  | `$50F04000` | `$9FFFF8` |
| Write | `$50F06000` | `$BFFFF9` |

Channel A and B are interleaved at different byte offsets within each
base. SE's SCC at `$9FFFF8` is critical for several boot polling loops
that MacPhoenix hasn't handled yet — see SE `$22F4` and `$22FC`.

Reading `Read Register 0` (status) bit 0 = Rx Character Available.
This is the bit polled by the SE stuck-loop at `$22FC`.

---

## IWM / SWIM (floppy)

| Machine | Base | Chip |
|---------|------|------|
| SE | `$DFE1FF` | IWM |
| Mac II | `$50F16000` | IWM rev 1 or IWM rev 8 (see `$50F18000` probe) |
| IIx, IIcx, SE/30 | `$50F16000` | SWIM (Sander-Woz Integrated Machine) |
| IIci | `$50F16000` | SWIM |

The IWM's register map is complex (16 "state" registers addressed via
a combination of address lines). For emulation purposes, all four
ROMs NOP the `INITIWM` BSR during boot; we never touch real floppy
I/O during the boot path.

---

## SCSI (NCR 5380)

| Machine | Base | Notes |
|---------|------|-------|
| SE | `$5FF000` | direct-mapped |
| Mac II | `$50F10000` | direct |
| IIci | `$50F10000` + pseudo-DMA at `$50F12060` | |

Registers at `$10`, `$20`, `$30`, `$40` within base (the Mac ROM
writes zeros to all four in `INITSCSI` to reset the chip — see SE's
`INITSCSI` at `$004004CE`).

---

## ASC (Apple Sound Chip)

| Machine | Base | Notes |
|---------|------|-------|
| SE | `$E80000` | |
| Mac II (rev B+), IIx, IIci | `$50F14000` | |
| IIci | `$50F14000` | |

The ASC is mostly safe to ignore during early boot — its only boot
touch is `BOOTBEEP`, which we NOP on all four ROMs (see §2 of each
`boot_flow.md`).

---

## Patching strategy by access type

When you see a hardware access in a boot trace, categorize it:

| Access pattern | Right fix |
|----------------|-----------|
| `tst.b (N,A0)` / `btst` on VIA/SCC status bit | SIGSEGV skip or dummy bank returning the bit you want |
| `move.b #imm,(N,A0)` to VIA timer counter | NOP the writes (they'd-disable interrupts) and use `tick_inhibit` |
| Polling loop (`LAB: btst...; bne.b LAB`) | Patch to BRA over the loop |
| Init routine (entire BSR target is probing HW) | NOP the BSR at the caller |
| ROM-resident driver `_Open` | EmulOp hook |
| NuBus slot decl-ROM read | NOP the slot manager init |

---

## Cross-reference: where patches live

`src/core/rom_patches.cpp` functions and their target hardware:

| Function | ROM | Patches at MMIO range |
|----------|-----|-----------------------|
| `patch_rom_classic` | SE | `$EFE1xx` (VIA1), `$9FFFF8` (SCC), `$5FFxxx` (SCSI) |
| `patch_rom_ii` | Mac II / IIx / IIcx / SE-30 | `$50F00xxx` (VIA1), `$50F02xxx` (VIA2), `$50F16xxx` (IWM/SWIM) |
| `patch_rom_32` | IIci | `$50F00xxx..$50F26xxx`, `$50F24xxx` (RBV) |
| `patch_rom_iici` | IIci | MMU init trampoline, Slot Manager skip, INITIOPMGR |

---

## See also

- Per-ROM boot walks: `../se/boot_flow.md`, `../macii/boot_flow.md`,
  `../iici/boot_flow.md`
- Inside Macintosh: *Devices* (Ch. 1 — VIA, SCC), *Operating System
  Utilities* (Ch. 8 — Trap Manager)
- Rockwell R6522 VIA datasheet
- Zilog Z8530 SCC datasheet
- Declaration ROMs and NuBus — *Designing Cards and Drivers for
  the Macintosh Family*, 3rd ed.
