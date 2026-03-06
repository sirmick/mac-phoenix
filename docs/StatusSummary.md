# mac-phoenix Status Summary

**Date**: March 5, 2026

---

## MILESTONE: Both Backends Boot to Mac OS 7.5.5 Finder Desktop

**Both the Unicorn JIT backend and the UAE interpreter backend boot Mac OS 7.5.5 to the Finder desktop.**

| Metric | UAE | Unicorn |
|--------|-----|---------|
| **Boot to Finder** | ~5s | ~48s |
| **CHECKLOADs** | 2200+ | 2513+ |
| **Performance** | Baseline | ~10x slower |

### Web UI
- WebRTC video streaming (H.264/VP9) working
- Mouse/keyboard input via data channel working
- Playwright e2e tests for input pipeline

---

## Key Achievements

### Boot to Finder (March 2026)
- Framebuffer fix: placed at 0x02110000 outside RAM (avoids WDCB overlap)
- RTR instruction added to Unicorn's QEMU m68k translator
- FPU emulation, SIGSEGV handler, serial null check

### Unicorn Performance (March 2026)
Reduced hook overhead to ~5% of execution time (JIT itself is ~10x slower):
- Auto-ack interrupts in QEMU's `m68k_cpu_exec_interrupt()`
- `goto_tb` enabled for backward branches (loop chaining)
- Lean `hook_block()` — stripped per-block timing, stats, stale TB detector

### Web UI Input (March 2026)
- Mouse/keyboard input via WebRTC data channel binary protocol
- `process_input_message()` → ADB functions
- Browser keycode → Mac ADB scancode conversion
- Playwright e2e test suite (6 tests)

### WebRTC Integration (January 2026)
- 4-thread in-process architecture
- All encoders: H.264, VP9, WebP, PNG, Opus audio
- JSON configuration system

### Earlier Achievements
- A-line/F-line traps via deferred register updates
- JIT TB invalidation via QEMU `notdirty_write()` + STALE-TB detector
- MMIO infrastructure (`uc_mmio_map()` for VIA/SCC/SCSI/ASC/DAFB stubs)
- IRQ storm fix (4-phase, 99.997% overhead reduction)
- 514k+ instruction dual-CPU validation

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

## Phase Status

| Phase | Status | Notes |
|-------|--------|-------|
| Phase 1: Core CPU | **COMPLETE** | All backends working, 514k+ dual-CPU validated |
| Phase 1.5: Boot Parity | **COMPLETE** | Both backends boot to Finder desktop |
| Phase 2: WebRTC | **COMPLETE** | 4-thread architecture, all encoders, input pipeline |
| Phase 3: Performance & Polish | **CURRENT** | Unicorn ~10x slower (94.7% JIT, 5.3% hooks), input working |
| Phase 4: Application Support | FUTURE | HyperCard, classic games |
| Phase 5: SheepShaver | FAR FUTURE | Mac OS 9, PowerPC |

## What's Next

- Application support (HyperCard, classic games)
- Stability improvements (long-running sessions)
- Further Unicorn performance optimization

---

*Last updated: March 5, 2026*
