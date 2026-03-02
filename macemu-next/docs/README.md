# macemu-next

Modern Mac emulator with Unicorn M68K CPU backend and dual-CPU validation.

---

## What Is This?

**macemu-next** is a clean-room rewrite of the BasiliskII Mac emulator, focused on:

1. **Unicorn M68K CPU** - Fast JIT-compiled 68020 emulation (primary goal)
2. **Dual-CPU Validation** - Run UAE and Unicorn in parallel to catch emulation bugs
3. **Modern Architecture** - Clean platform API, modular design, Meson build
4. **Legacy Support** - UAE backend retained for compatibility

**Current Status**: ✅ Unicorn boot parity with UAE achieved (March 2026) -- both backends stall at same point awaiting SCSI disk emulation

---

## Quick Start

### Build
```bash
cd macemu-next
meson setup build
meson compile -C build
```

### Configure
```bash
# Generate default config
./build/macemu-next --save-config

# Edit config file
nano ~/.config/macemu-next/config.json
```

### Run with Unicorn (primary backend)
```bash
CPU_BACKEND=unicorn ./build/macemu-next ~/quadra.rom
```

### Run with dual-CPU validation
```bash
CPU_BACKEND=dualcpu ./build/macemu-next ~/quadra.rom
```

### Run with custom config
```bash
./build/macemu-next --config myconfig.json ~/quadra.rom
```

See **[Commands.md](Commands.md)** for complete build and testing guide.
See **[JSON_CONFIG.md](JSON_CONFIG.md)** for configuration documentation.

---

## Documentation

### Essential Reading (Start Here!)
- **[Architecture.md](Architecture.md)** - How the system fits together (Platform API, backends, memory)
- **[ProjectGoals.md](ProjectGoals.md)** - Vision and end goals (Unicorn-first approach)
- **[Commands.md](Commands.md)** - Build, test, debug, trace commands
- **[JSON_CONFIG.md](JSON_CONFIG.md)** - Configuration system
- **[TodoStatus.md](TodoStatus.md)** - What's done ✅ and what's next ⏳
- **[STATUS_SUMMARY.md](STATUS_SUMMARY.md)** - Current project status

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

**Current State**: Unicorn boot parity with UAE (March 2026) -- both stall awaiting SCSI disk

**UAE's Role**: Legacy compatibility and validation baseline (will be retained but Unicorn is the focus)

**Dual-CPU's Role**: Validation tool to ensure Unicorn matches UAE behavior

See **[ProjectGoals.md](ProjectGoals.md)** for detailed vision.

---

## Key Achievements

- ✅ **Unicorn boot parity with UAE** (March 2026) -- both backends reach identical state
- ✅ Unicorn M68K backend working (68020 with JIT)
- ✅ EmulOps (0x71xx and 0xAExx) -- Illegal instruction traps
- ✅ A-line/F-line traps -- **WORKING** via deferred register updates
- ✅ Interrupt support (60Hz timer with M68K exception frames)
- ✅ JIT TB invalidation workaround (60Hz flush)
- ✅ MMIO infrastructure (uc_mmio_map for hardware registers)
- ✅ 87 OS trap table entries (identical between backends)
- ✅ 16,879 EmulOps dispatched in 30s (including 2,046 SCSI searches)
- ✅ UAE backend fully functional
- ✅ Dual-CPU validation infrastructure (514k+ instructions)

See **[TodoStatus.md](TodoStatus.md)** for complete checklist.

---

## Current Limitation: No SCSI Boot Disk

Both backends stall at the same point: a resource chain search at PC=0x0001c3d4. The ROM is looking for system resources from a SCSI boot disk. The chain sentinel at [0x01FFF30C] = 0xFF00FF00 in both backends -- the resource list is empty because there's no disk to load from.

**To progress further**, the emulator needs:
1. SCSI disk emulation (System file provides resources)
2. More complete VIA emulation (timers, slot interrupts)
3. Video framebuffer initialization
4. ADB hardware responses

---

## Recent Improvements (January-March 2026)

### ✅ Boot Parity Achieved (March 2026)
Both Unicorn and UAE reach identical boot state:
- 87 OS trap table entries
- $0b78 boot progress = 0xfd89ffff
- Same TopMapHndl, SysMapHndl values
- Both stall at same resource chain search

### ✅ JIT TB Invalidation Solved
Mac OS heap overwrites RAM patch code. QEMU's JIT cache retains stale translations. Fixed with 60Hz `uc_ctl_flush_tb()` workaround.

### ✅ A-Line/F-Line Traps Working
Previously broken due to Unicorn's PC limitation. Solved via deferred register updates -- register writes are queued during hook callbacks and applied at block boundaries.

### ✅ IRQ Storm Fixed (January 2026)
4-phase fix: corrected EmulOp encoding, QEMU-style execution loop, deferred register updates, proper M68K interrupt delivery. 99.997% overhead reduction.

### ✅ MMIO Infrastructure
Hardware registers use `uc_mmio_map()` (required because JIT bypasses `UC_HOOK_MEM_READ` for `uc_mem_map_ptr` regions). VIA/SCC/SCSI/ASC/DAFB stubs implemented.

---

## Directory Structure

```
macemu-next/
├── src/
│   ├── common/include/    # Shared headers (sysdeps.h, platform.h)
│   ├── core/              # Core Mac managers (emul_op.cpp, xpram.cpp)
│   ├── cpu/               # CPU backends
│   │   ├── uae_cpu/       # UAE M68K interpreter (legacy)
│   │   ├── cpu_unicorn.cpp     # Unicorn backend (primary)
│   │   ├── cpu_dualcpu.cpp     # Validation backend
│   │   └── unicorn_wrapper.c   # Unicorn API wrapper
│   └── tests/             # Unit and boot tests
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
