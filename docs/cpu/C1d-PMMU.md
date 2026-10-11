# C1d: Virtual Memory on the 68k machine (PMMU), where it stands

Status 2026-10-11: **assessed, not started.** The `mame-68k` core can
emulate a 68040 PMMU (C1c: `cpu::Config::mmu` selects MAME's PMMU
initialisation), but nothing turns it on, and the 68k ROM machine is built
in a way that keeps Mac OS from ever using it. This document records why,
what the memory map looks like, and the options. The milestone and its
gate are in [PLAN.md](PLAN.md) (C1d, straggler S14).

## What Mac OS sees today

Measured on the Quadra 650 ROM, System 7.5.5, `--backend uae` (the same
ROM patches apply on `--core mame-68k`), with a MacPerl Gestalt probe run
through the guest suite:

| Gestalt selector | Value | Meaning |
|---|---|---|
| `proc` | 5 | 68040 |
| `fpu ` | 3 | 68040 FPU |
| `mmu ` | 0 | **no MMU** |
| `vm  ` | 0 | Virtual Memory off |
| `mach` | 20 | Quadra 700 (not the 650 the ROM is for) |
| `addr` | 7 | 32-bit addressing |

With `mmu ` = 0, System 7.5.5 does not offer Virtual Memory at all.

## How Mac OS decides there is a PMMU

1. **The ROM identifies the machine at reset.** The Quadra ROM's start-up
   code works out which machine it is on from its hardware tables, sizes
   the RAM banks and records the CPU type and MMU mode in low-memory
   globals (the MMU flags at `$CB1`, among others).
2. **`InitMMU` builds the translation tables** from that description:
   RAM, ROM, I/O and NuBus space, at the real Quadra addresses, and turns
   translation on.
3. **Gestalt `mmu `** answers from what the ROM recorded (none, 68851,
   68030, 68040).
4. **Virtual Memory** (System 7's VM, set in the Memory control panel and
   started at boot) requires a supported MMU type and then works from the
   tables the ROM built, adding its own paging and a backing-store file.
   This step is from Apple's documented design, not measured here.

Items 1 to 3 are what this machine changes.

## What the ROM machine changes

`src/core/rom_patches.cpp` (Basilisk II's approach) patches the MMU out:

* **`InitMMU`, three patches**: "no MMU present, don't choke on unknown
  CPU types" (NOPs and `moveq #0,d0`), "no RBV present" (a branch), and
  "don't init MMU" (a NOP). Translation is never enabled.
* **No `PTEST`** on the 68040: BlockMove and SANE are patched to avoid it.
* **`SwapMMUMode`** and the 24-bit paths are forced on the older ROMs.
* **Hardware is not where a Quadra has it**: the VIA, SCC and IWM base
  pointers in low memory point at ScratchMem, and hardware access is done
  by host calls (EmulOps) patched into the ROM.

## The memory map

The 68k ROM machine allocates one contiguous host block; a Mac address is
the offset into it (`MEMBaseDiff`):

| Region | Mac address | Contents |
|---|---|---|
| RAM | 0 to RAM size | the Mac's memory (64 MB by default) |
| ROM | right after RAM (`0x02000000` with 32 MB) | the 1 MB ROM |
| ScratchMem | after the ROM | 64 KB standing in for hardware registers |
| Framebuffer | after ScratchMem | 8 MB |

A real Quadra 650 has its ROM at `0x40800000`, on-board I/O at
`0x50F00000`, NuBus slot space from `0xF9000000`, and RAM in banks the
memory controller reports. Mac OS runs on the emulator's layout only
because nothing looks at the real addresses: hardware access is patched,
and the MMU that would map the real layout is never set up.

## What a Virtual Memory boot would need

Three things, whichever route:

1. **Gestalt and the low-memory MMU flags** must say 68040 MMU.
2. **Valid translation tables** describing the memory the Mac actually
   has, installed before VM starts, with translation on.
3. **The PMMU running** on `mame-68k` (`Config::mmu`), so translated
   accesses, page faults and `PFLUSH`/`PTEST`/`MOVEC TC/URP/SRP` behave.
   MAME's 68040 PMMU does this; it has had no workload here (S14).

## Options

**A. Synthetic MMU tables for the current layout.** Keep the ROM patches;
at the point `InitMMU` would run, build 68040 tables (identity for RAM,
ROM, ScratchMem and the framebuffer; invalid elsewhere), load URP/SRP/TC,
and set the MMU flags so Gestalt reports a 68040 MMU.
*For:* no change to the rest of the machine. *Against:* VM may make
assumptions the synthetic tables don't meet (table layout, where the ROM
keeps them, the physical-memory description in the boot globals); each
one found by trial. Unknown size, likely days.

**B. A hardware-accurate Quadra 650 memory map.** ROM at `0x40800000`,
I/O at `0x50F00000`, the memory controller's registers, unpatched
`InitMMU`. Mac OS then does everything itself, as on QEMU's q800 machine.
*For:* faithful; VM and anything else that relies on the real map just
works. *Against:* it is a different machine model: device emulation for
the VIA, the memory controller and more, and every ROM patch that assumes
the current layout revisited. Weeks.

**C. Park it.** Nothing in the current work needs VM. The PMMU stays
available in `mame-68k` for a future machine.

## Recommendation

Park (C) unless VM is wanted for a specific reason; if it is, try A first
as a spike with a clear stop point (Gestalt reports an MMU, the Memory
control panel offers VM, VM starts), and only consider B if A hits a wall
that needs the real map.
