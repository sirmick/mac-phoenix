# Unicorn Engine Integration Documentation

This directory contains comprehensive documentation for the Unicorn Engine integration project.

## ✅ UPDATE: EmulOps Successfully Implemented

**A-line EmulOps (0xAE00-0xAE3F) are now working!** See [A_LINE_EMULOPS_IMPLEMENTATION.md](A_LINE_EMULOPS_IMPLEMENTATION.md) for details.

## Documents

### [00_UNICORN_INTEGRATION_MASTER_PLAN.md](00_UNICORN_INTEGRATION_MASTER_PLAN.md)

**THE definitive project design and implementation guide** (~1,850 lines)

This document serves as:
- **Project overview** - Vision, goals, and why we're doing this
- **Design specification** - Architecture, dual-CPU validation, build system
- **Implementation guide** - 12 detailed phases with code examples
- **Reference manual** - Complete binary trace format, Python tools, Meson build

**Key Sections:**

1. **Project Vision** - Building a modern, maintainable Mac emulator
2. **Why Replace the CPU?** - Cross-platform JIT, maintainability, accuracy
3. **Why Unicorn Engine?** - Clean API, proven cores, active upstream
4. **Dual-CPU Validation Strategy** - Binary traces, Python analysis tools
5. **New Build System Architecture** - Meson-based, clean structure
6. **Phase Breakdown** - 12 phases from foundation to production
7. **Timeline & Milestones** - 8-10 weeks to completion

**Read this first** - Everything you need to know is here.

## Quick Start

### Understanding the Plan

```bash
# Read the master plan
less 00_UNICORN_INTEGRATION_MASTER_PLAN.md

# Jump to specific sections:
# - Why Unicorn Engine? (line 158)
# - Dual-CPU Validation Strategy (line 285)
# - New Build System (line 876)
# - Phase Breakdown (line 1223)
```

### Phase 1 - Foundation Setup

```bash
# Create new project directory
mkdir ~/macemu-next
cd ~/macemu-next

# Set up initial structure
mkdir -p src/{common,cpu,drivers,basilisk,sheepshaver,platform/{linux,macos,windows}}
mkdir -p external tests tools docs

# Build Unicorn
cd ~/macemu-dual-cpu/unicorn
mkdir build && cd build
cmake .. -DUNICORN_ARCH="m68k;ppc" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
make -j$(nproc)

# Verify
ls -lh libunicorn.a
# Should see: ~1.5MB library
```

## Key Concepts

### Dual-CPU Validation

Run **both** UAE/KPX and Unicorn CPUs side-by-side:

1. Execute one instruction on UAE/KPX
2. Execute one instruction on Unicorn
3. Compare CPU state (registers, memory, I/O)
4. Abort on first divergence

This proves Unicorn produces **identical** results to the legacy CPUs.

### Binary Trace Format

Record every instruction execution to binary files:

- **CPUStateSnapshot** - 280 bytes per instruction (registers, PC, flags)
- **MemoryOperation** - 26 bytes per memory access
- **IOOperation** - 25 bytes per I/O access

Python tools decode and compare these traces.

### Meson Build System

Modern, fast, cross-platform build:

```bash
# Dual-CPU validation mode
meson setup build -Dcpu_backend=dualcpu -Dtests=true

# Production (Unicorn only)
meson setup build-release -Dcpu_backend=unicorn -Dbuildtype=release

# Fallback (UAE only)
meson setup build-uae -Dcpu_backend=uae
```

### IPC Driver Architecture

All hardware emulation via IPC:

```
BasiliskII Main Process
    ├── Unicorn CPU
    ├── Device Emulation (VIA, SCC, SCSI)
    └── IPC Message Queue
            ├── Video Driver (IPC) → SDL2
            ├── Disk Driver (IPC) → File I/O
            ├── Network Driver (IPC) → TAP/TUN
            └── Audio Driver (IPC) → PulseAudio
```

No in-process device drivers. Clean separation.

## Timeline

```
Week 1:   Phase 1  - Foundation Setup
Week 2:   Phase 2  - Unicorn Wrapper API
Week 2-3: Phase 3  - EmulOp Hook System
Week 3:   Phase 4  - State Capture & Trace Recording
Week 3-4: Phase 5  - Dual-CPU Harness
Week 4:   Phase 6  - Python Trace Analysis Tools
Week 4-5: Phase 7  - Copy BasiliskII Common Code
Week 5-6: Phase 8  - Instruction Validation Tests
Week 6:   Phase 9  - ROM Boot Validation
Week 7:   Phase 10 - IPC Driver Architecture
Week 7-8: Phase 11 - Full BasiliskII Integration
Week 8:   Phase 12 - Production Build

Total: 8-10 weeks to production
```

## Success Criteria

Project is complete when:

- ✅ BasiliskII boots Mac OS 7.5 with Unicorn CPU
- ✅ SheepShaver boots Mac OS 9 with Unicorn CPU
- ✅ All instruction validation tests pass (zero divergences)
- ✅ ROM boot validation passes (100,000+ instructions)
- ✅ Performance is equal or better than UAE/KPX
- ✅ Builds on Linux, macOS, Windows
- ✅ IPC drivers work for video, disk, network, audio
- ✅ Documentation complete
- ✅ CI/CD pipeline working

## Next Steps

1. **Read the master plan** - Understand the full scope
2. **Set up dev environment** - Install Meson, CMake, dependencies
3. **Begin Phase 1** - Create project structure, build Unicorn
4. **Follow the phases** - One phase at a time, test everything
5. **Track progress** - Git commit after each phase completion

---

## Obsolete Documentation

The `obsolete/` folder contains documentation for approaches that were tried but failed:

- **MMIO_IMPLEMENTATION_PLAN.md** - MMIO transport approach (size mismatch problem)
- **MMIO_TRAP_APPROACH.md** - MMIO trap approach (proposed but never implemented)

These are preserved for historical reference.

---

**Last Updated:** January 2025
**Status:** A-line EmulOps working, timer interrupts need implementation for full boot
