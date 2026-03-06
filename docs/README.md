# mac-phoenix

Modern Mac emulator with Unicorn M68K CPU backend and dual-CPU validation.

---

## What Is This?

**mac-phoenix** is a clean-room rewrite of the BasiliskII Mac emulator, focused on:

1. **Unicorn M68K CPU** - Fast JIT-compiled 68020 emulation (primary goal)
2. **Dual-CPU Validation** - Run UAE and Unicorn in parallel to catch emulation bugs
3. **Modern Architecture** - Clean platform API, modular design, Meson build
4. **Legacy Support** - UAE backend retained for compatibility

**Current Status**: ✅ Both backends boot Mac OS 7.5.5 to Finder desktop (March 2026)

---

## Quick Start

### Build
```bash
cd mac-phoenix
meson setup build
meson compile -C build
```

### Configure
```bash
# Edit config file
nano ~/.config/mac-phoenix/config.json
```

### Run with Unicorn backend
```bash
CPU_BACKEND=unicorn ./build/mac-phoenix ~/quadra.rom
```

### Run with dual-CPU validation
```bash
CPU_BACKEND=dualcpu ./build/mac-phoenix ~/quadra.rom
```

### Run with custom config
```bash
./build/mac-phoenix --config myconfig.json ~/quadra.rom
```

See **[Commands.md](Commands.md)** for complete build and testing guide.
See **[JsonConfig.md](JsonConfig.md)** for configuration documentation.

---

## Documentation

### Essential Reading (Start Here!)
- **[Architecture.md](Architecture.md)** - How the system fits together (Platform API, backends, memory)
- **[ProjectGoals.md](ProjectGoals.md)** - Vision and end goals (Unicorn-first approach)
- **[Commands.md](Commands.md)** - Build, test, debug, trace commands
- **[JsonConfig.md](JsonConfig.md)** - Configuration system
- **[TodoStatus.md](TodoStatus.md)** - What's done ✅ and what's next ⏳
- **[StatusSummary.md](StatusSummary.md)** - Current project status

### Technical Deep Dives
- **[deepdive/](deepdive/)** - Detailed technical documentation
  - **[cpu/](deepdive/cpu/)** - CPU backend documentation
    - **[UnicornQuirks.md](deepdive/cpu/UnicornQuirks.md)** - ⚠️ **CRITICAL** - PC change limitation
    - **[ALineAndFLineStatus.md](deepdive/cpu/ALineAndFLineStatus.md)** - Trap handling status
    - [UaeQuirks.md](deepdive/cpu/UaeQuirks.md), [CpuBackendApi.md](deepdive/cpu/CpuBackendApi.md), and more
  - [MemoryArchitecture.md](deepdive/MemoryArchitecture.md) - Memory system
  - [InterruptTimingAnalysis.md](deepdive/InterruptTimingAnalysis.md) - Timing analysis
  - [PlatformAPIInterrupts.md](deepdive/PlatformAPIInterrupts.md) - Interrupt abstraction

### Historical Documentation
- **[completed/](completed/)** - Successfully completed implementations and fixes
- **[archive/](archive/)** - Archived docs (obsolete, superseded, or historical)
  - See [archive/README.md](archive/README.md) for details on what's archived and why

---

## Project Vision

**End Goal**: Unicorn-based Mac emulator with:
- Fast JIT execution
- Clean, maintainable codebase
- Validated against proven UAE implementation
- Modern build system and tooling

**Current State**: Both backends boot Mac OS 7.5.5 to Finder desktop (March 2026)

**UAE's Role**: Legacy compatibility and validation baseline (will be retained but Unicorn is the focus)

**Dual-CPU's Role**: Validation tool to ensure Unicorn matches UAE behavior

See **[ProjectGoals.md](ProjectGoals.md)** for detailed vision.

---

## Key Achievements

- ✅ **Both backends boot to Mac OS 7.5.5 Finder** (March 2026)
- ✅ Unicorn M68K backend with JIT (68040 mode)
- ✅ EmulOps (0xAExx for Unicorn, 0x71xx for UAE)
- ✅ A-line/F-line traps via deferred register updates
- ✅ Interrupt support (60Hz timer, QEMU native interrupt delivery)
- ✅ JIT TB invalidation via QEMU `notdirty_write()` + STALE-TB detector
- ✅ MMIO infrastructure (VIA/SCC/SCSI/ASC/DAFB stubs)
- ✅ WebRTC streaming (H.264, VP9, Opus audio)
- ✅ Mouse/keyboard input via WebRTC data channel
- ✅ JSON configuration system
- ✅ Unicorn performance optimizations (auto-ack, goto_tb, lean hook_block)
- ✅ Playwright e2e test framework

See **[TodoStatus.md](TodoStatus.md)** for complete checklist.

---

## Recent Improvements (January-March 2026)

### ✅ Boot to Finder (March 2026)
Both UAE and Unicorn backends boot Mac OS 7.5.5 to Finder desktop:
- UAE: 2200+ CHECKLOADs, reaches Finder in ~5s
- Unicorn: 2513+ CHECKLOADs, reaches Finder in ~48s

Key fixes: framebuffer placement outside RAM (avoids WDCB overlap), RTR instruction
added to QEMU m68k translator, FPU emulation, SIGSEGV handler.

### ✅ Unicorn Performance (March 2026)
Reduced Unicorn hook overhead from ~10x to ~5% of execution time (JIT itself is the bottleneck, ~10x slower):
- Auto-ack interrupts in QEMU's `m68k_cpu_exec_interrupt()`
- `goto_tb` enabled for backward branches (loop chaining)
- Stripped hook_block of per-block perf timing, block stats, stale TB detector

### ✅ Web UI Input (March 2026)
Mouse and keyboard input wired through WebRTC data channel:
- Binary protocol: relative/absolute mouse, buttons, keyboard
- Data channel -> `process_input_message()` -> ADB functions
- Playwright e2e tests verify full-stack input pipeline

### ✅ WebRTC Integration (January 2026)
4-thread in-process architecture with all encoders integrated.

### ✅ IRQ Storm Fixed (January 2026)
4-phase fix: EmulOp encoding, QEMU-style execution loop, deferred register updates,
proper M68K interrupt delivery.

---

## Directory Structure

```
mac-phoenix/
├── src/
│   ├── common/include/    # Shared headers (sysdeps.h, platform.h)
│   ├── core/              # Core Mac managers (emul_op.cpp, adb.cpp, rom_patches.cpp)
│   ├── cpu/               # CPU backends
│   │   ├── uae_cpu/       # UAE M68K interpreter (legacy)
│   │   ├── cpu_unicorn.cpp     # Unicorn backend (primary)
│   │   ├── unicorn_wrapper.c   # Unicorn hooks and interrupt delivery
│   │   ├── unicorn_exec_loop.c # uc_emu_start loop
│   │   └── cpu_dualcpu.c       # Validation backend
│   ├── drivers/           # Video, audio, platform drivers
│   ├── webrtc/            # WebRTC server (signaling + input)
│   ├── webserver/         # HTTP server, API handlers
│   └── config/            # JSON config system
├── client/                # Browser client (HTML, JS, CSS)
├── tests/
│   ├── boot/              # Boot tests
│   └── e2e/               # Playwright e2e tests
├── subprojects/           # Unicorn, libdatachannel, nlohmann_json
├── docs/                  # Documentation (you are here!)
└── meson.build            # Build configuration
```

---

## License

GPL v2 (based on BasiliskII)

## References

- Original BasiliskII: https://github.com/kanjitalk755/macemu
- Unicorn Engine: https://www.unicorn-engine.org/
- M68K Reference: Motorola M68000 Family Programmer's Reference Manual
