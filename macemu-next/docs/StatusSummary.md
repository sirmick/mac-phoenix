# macemu-next Status Summary

**Date**: March 1, 2026

---

## MILESTONE: Unicorn Boot Parity with UAE

**Both the Unicorn JIT backend and the UAE interpreter backend reach the identical boot state and stall at the same point.** The stall is NOT a Unicorn bug -- it is a shared emulator limitation (no SCSI boot disk).

---

## Current Boot State (Both Backends)

| Metric | Value |
|--------|-------|
| **Boot progress ($0b78)** | 0xfd89ffff (identical) |
| **OS Trap Table** | 87 RAM handler entries (identical) |
| **TopMapHndl ($A50)** | 0x00002074 (identical) |
| **SysMapHndl ($A54)** | 0x00002074 (identical) |
| **EmulOps in 30s** | 16,879 dispatched |
| **SCSI dispatches** | 2,046 (searching for boot disk) |
| **Stall point** | PC=0x0001c3d4 (resource chain search) |
| **Chain sentinel** | [0x01FFF30C] = 0xFF00FF00 (both backends) |

### Boot Sequence Timeline

1. **RESET EmulOp** (0x7103/0xAE03) -- Sets up boot globals, registers
2. **CLKNOMEM x336** (0x7104/0xAE04) -- XPRAM/RTC initialization
3. **ROM initialization** -- Ticks-waiting loop at PC=0x020014ca (~4 seconds)
4. **OS Trap Table setup** -- 87 entries installed (A001-A0FF range)
5. **Resource Manager** -- Searches for boot resources, stalls on empty chain

### Why Both Backends Stall

The resource chain search at PC=0x0001c3d4 follows a linked list starting at A3=0x01FFF30C. The first entry points to 0xFF00FF00 (dummy fill pattern), immediately breaking the chain. The ROM is looking for system resources that would normally come from a SCSI boot disk.

---

## Key Achievements (Since January 2026)

### 1. A-Line/F-Line Traps -- WORKING

Previously documented as "BROKEN" due to Unicorn's PC limitation (GitHub issue #1027). **Now fully working** via the deferred register update mechanism:

- EmulOp handlers run inside UC_HOOK_INTR callbacks
- Register writes are deferred and applied at block boundaries via `apply_deferred_updates_and_flush()`
- PC changes take effect correctly after hook returns
- All 87 OS trap table entries populated (matching UAE exactly)

### 2. JIT Translation Block Invalidation -- SOLVED (workaround)

**Root cause**: Mac OS heap overwrites RAM containing patch code. QEMU's JIT cache retains stale compiled translations. Executing stale code crashes at PC=0x00000002.

**Workaround**: `uc_ctl_flush_tb()` on every 60Hz timer tick in `hook_block`.

**Proper fix needed**: Investigate QEMU's `TLB_NOTDIRTY` / `tb_invalidate_phys_page_range()` mechanism.

### 3. MMIO Infrastructure

Hardware register emulation via `uc_mmio_map()` (not `UC_HOOK_MEM_READ`, which JIT bypasses for `uc_mem_map_ptr` regions):

- VIA1/VIA2 stubs (0x50F00000-0x50F03FFF)
- SCC/SCSI/ASC/DAFB stubs
- NuBus gap regions (return 0)
- Unmapped memory handler

### 4. IRQ Storm Fix

4-phase fix eliminating 99.997% of IRQ polling overhead:
1. Fixed IRQ EmulOp encoding (0x7129 not 0xAE29)
2. QEMU-style execution loop with adaptive batch sizing
3. Deferred register update mechanism
4. Proper M68K interrupt delivery with exception frames

### 5. Performance Optimization

- Hook-based timer: poll every 4096 blocks (not every block)
- Cached environment variable lookups
- Batch execution (count=1000)
- 60Hz TB flush (workaround for self-modifying code)

---

## EmulOp Dispatch Summary (30-second run)

| EmulOp | Count | Description |
|--------|-------|-------------|
| BLOCK_MOVE | 4,361 | Memory block operations |
| SCSI_DISPATCH | 2,046 | SCSI manager calls (searching for disk) |
| IRQ | 1,797 | Interrupt handling |
| DISK_PRIME | 1,358 | Disk read/write attempts |
| CLKNOMEM | 574 | XPRAM/RTC access |
| ADBOP | 417 | ADB keyboard/mouse |
| CHECKLOAD | 283 | Resource loading checks |
| Other | 6,043 | RESET, PATCH_BOOT_GLOBS, etc. |
| **Total** | **16,879** | |

---

## Memory Map (Unicorn Backend)

| Region | Address Range | Size | Content |
|--------|--------------|------|---------|
| RAM | 0x00000000-0x01FFFFFF | 32MB | Host buffer, shared with UAE |
| ROM | 0x02000000-0x020FFFFF | 1MB | Writable (for patching) |
| Dummy | 0x02100000-0x030FFFFF | 16MB | 0xFF00FF00 fill pattern |
| NuBus Gap 1 | 0x03100000-0x50EFFFFF | ~1.2GB | dummy_bank (returns 0) |
| MMIO | 0x50F00000-0x50F3FFFF | 256KB | VIA/SCC/SCSI stubs |
| NuBus Gap 2 | 0x50F40000-0xEFFFFFFF | ~2.5GB | dummy_bank (returns 0) |
| High Mem | 0xF0000000-0xFEFFFFFF | 240MB | Zeroed |
| Trap Gap | 0xFF000000-0xFF000FFF | 4KB | Unmapped (EmulOp detection) |
| High Mem 2 | 0xFF001000-0xFFFFFFFF | ~16MB | Zeroed |

---

## Architecture Summary

### Three CPU Backends

- **UAE**: Interpreter, fully functional, legacy baseline
- **Unicorn**: JIT (QEMU-based), primary development focus, boot parity achieved
- **DualCPU**: Runs both in lockstep for validation

### Key Technical Details

- **Deferred Register Updates**: EmulOp handlers run inside UC_HOOK_INTR. Register writes inside hooks don't persist in QEMU. Solution: defer updates, apply at block boundaries.
- **SR requires uint32_t***: `uc_reg_write` for SR needs `uint32_t*` not `uint16_t*` (QEMU internal representation).
- **MMIO via uc_mmio_map()**: `UC_HOOK_MEM_READ` does NOT work for `uc_mem_map_ptr` regions (JIT bypasses hooks). Must use `uc_mmio_map()` for hardware registers.
- **EmulOp Encoding**: `make_emulop()` generates 0xAExx for Unicorn, 0x71xx for UAE.

---

## What's Needed to Boot Further

The resource chain stall is a shared emulator issue. To progress:

1. **SCSI disk emulation** -- System file provides resources the ROM is searching for
2. **More complete VIA emulation** -- Interrupt sources, timers, slot interrupts
3. **Video framebuffer** -- Display initialization
4. **ADB hardware responses** -- Keyboard/mouse detection

---

## Phase Status

| Phase | Status | Notes |
|-------|--------|-------|
| Phase 1: Core CPU | **COMPLETE** | All backends working, 514k+ dual-CPU validated |
| Phase 1.5: Boot Progress | **COMPLETE** | Unicorn boot parity with UAE achieved |
| Phase 2: WebRTC | **COMPLETE** | 4-thread architecture, all encoders integrated |
| Phase 3: Hardware Emulation | **NEXT** | SCSI, VIA, Video needed for further boot |
| Phase 4: Boot to Desktop | FUTURE | Requires Phase 3 |
| Phase 5: Application Support | FUTURE | |

---

*Last updated: March 1, 2026*
