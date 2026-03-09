# Project Goals and Vision

What we're building and why.

---

## Mission Statement

**Build a fast, maintainable Mac emulator using Unicorn M68K CPU with dual-CPU validation for correctness.**

---

## The End Goal

### Primary: Unicorn-Based Emulator

**What**: Mac emulator that uses Unicorn Engine as its CPU

**Why Unicorn**:
- **JIT compilation**: QEMU-based TCG backend compiles M68K to host native code
- **Maintained**: Active upstream project
- **Clean API**: Simple C API, easy to integrate
- **Cross-platform**: Works on Linux, macOS, Windows

**Current State**: Both backends boot Mac OS 7.5.5 to Finder desktop (March 2026). Unicorn is ~10x slower than UAE — 94.7% of time is JIT execution, hooks add only 5.3%. The gap is structural (QEMU TCG M68K condition codes, memory-indirect registers, small basic blocks).

**Target State**:
- Run Mac applications (HyperCard, games, productivity software)
- Performance optimization (currently ~10x slower than UAE interpreter)
- Clean, maintainable codebase

### Secondary: Modern Architecture

**Goals**:
1. **Platform API Abstraction** - Backend-independent core code
2. **Meson Build System** - Fast, cross-platform builds
3. **Modular Design** - Clear separation of concerns
4. **Comprehensive Documentation** - Explain quirks, design decisions
5. **Continuous Validation** - Catch bugs early via dual-CPU mode

**Not Goals**:
- ❌ Rewrite everything from scratch (reference BasiliskII heavily)
- ❌ Support every Mac model (focus on Quadra 650 / 68020 first)
- ❌ Perfect historical accuracy (pragmatic emulation over cycle-accuracy)

---

## Role of Each Backend

### 1. Unicorn: The Future ⭐

**Purpose**: **Primary backend** for end users

**Status**: Boots Mac OS 7.5.5 to Finder desktop (March 2026)

**Completed**:
- ✅ EmulOps (0xAExx for Unicorn, 0x71xx for UAE)
- ✅ A-line/F-line traps (via deferred register updates)
- ✅ Interrupt support (QEMU native delivery with auto-ack)
- ✅ Native trap execution, JIT TB invalidation
- ✅ MMIO infrastructure (VIA/SCC/SCSI/ASC/DAFB stubs)
- ✅ Boot to Finder desktop
- ✅ Performance: auto-ack, goto_tb backward branches, lean hook_block

**Long-term Vision**:
- Eventually, most users will run Unicorn backend only
- Fast enough for daily use
- Stable enough for productivity

### 2. UAE: The Baseline 📊

**Purpose**: Legacy compatibility and validation reference

**Status**: Fully functional, maintained but not the focus

**Why Keep It**:
- ✅ Proven, stable implementation (decades of development)
- ✅ Validation baseline - if Unicorn differs, UAE is usually right
- ✅ Fallback option - if Unicorn has issues, UAE still works
- ✅ Historical reference - understand original BasiliskII design

**Role in Project**:
- **Validation reference**: "Does Unicorn match UAE behavior?"
- **Compatibility fallback**: Users can switch to UAE if needed
- **Code reference**: Understand how BasiliskII solved problems

**Not Going Away**: UAE will be retained indefinitely for legacy support

**Not the Focus**: New features will prioritize Unicorn

### 3. DualCPU: The Validator 🔍

**Purpose**: **Validation tool** for development

**Status**: Fully functional, critical for development

**How It Works**:
- Run UAE and Unicorn in lockstep
- Execute same instruction on both CPUs
- Compare all registers after each instruction
- Stop immediately on divergence

**Achievements**:
- ✅ Caught VBR register bug (uninitialized memory reads)
- ✅ Caught CPU type selection bug (68030 instead of 68020)
- ✅ Revealed interrupt timing differences
- ✅ Validated 514,000+ instructions with zero divergence

**Role in Project**:
- **Development tool**: Catch bugs immediately during Unicorn development
- **Regression testing**: Verify changes don't break existing functionality
- **Understanding divergence**: Analyze why/when UAE and Unicorn differ

**Not for End Users**: DualCPU is ~2x slower (runs both CPUs), only for development

---

## Development Philosophy

### 1. Reference BasiliskII Heavily

**Don't Reinvent**: BasiliskII solved these problems over decades

**Do Understand**: Read BasiliskII code, understand approach, then adapt

**Example**:
- ✅ Use direct addressing (proven fast)
- ✅ Copy EmulOp system (elegant trap mechanism)
- ✅ Reference ROM patches (know what Mac OS expects)
- ❌ Copy UAE CPU verbatim (we're building Unicorn backend)

### 2. Validate Continuously

**Dual-CPU Mode**: Run after every significant change

**Catch Bugs Early**: Better to fail at instruction 100 than 100,000

**Example**:
```bash
# After implementing interrupt support:
./build/mac-phoenix --backend dualcpu --timeout 30 ~/quadra.rom

# If it validates 500k+ instructions → probably correct
# If it diverges at 1k instructions → definitely a bug
```

### 3. Document Everything

**Quirks are Important**: UAE and Unicorn have surprising behavior

**Future You Will Thank You**: 6 months later, why did we do this?

**Example Documentation**:
- Why VBR reads returned garbage (missing Unicorn API)
- Why CPU type enum doesn't match array (Unicorn internals)
- Why interrupt timing differs (wall-clock vs instruction-count)

### 4. Performance Matters

**Hook Optimization**: UC_HOOK_CODE → UC_HOOK_BLOCK (10x improvement)

**JIT-Friendly**: Minimize cache invalidation, reduce hook overhead

**Profile and Measure**: Don't guess, measure actual performance

---

## Roadmap

### Phase 1: Core CPU Emulation ✅ **COMPLETE**

- ✅ Unicorn M68K backend running
- ✅ EmulOp system working (0x71xx and 0xAExx)
- ✅ A-line/F-line traps (via deferred register updates)
- ✅ Interrupt support (60Hz timer with M68K exception frames)
- ✅ Native trap execution (no UAE dependency)
- ✅ Dual-CPU validation (514k+ instructions)
- ✅ Hook optimization (UC_HOOK_BLOCK + UC_HOOK_INTR)
- ✅ JIT TB invalidation workaround (60Hz flush)
- ✅ MMIO infrastructure (uc_mmio_map)

**Outcome**: Unicorn achieves full boot parity with UAE

### Phase 1.5: Boot Progress ✅ **COMPLETE** (March 2026)

**Achievement**: Unicorn boot parity with UAE

**What was done**:
- ✅ Fixed IRQ storm (4-phase fix, 99.997% overhead reduction)
- ✅ Solved JIT TB invalidation (60Hz flush workaround)
- ✅ Implemented deferred register updates for EmulOp handlers
- ✅ Built MMIO infrastructure with uc_mmio_map()
- ✅ Both backends: 87 OS trap entries, 16,879 EmulOps in 30s, identical state

**Current stall**: Both backends stuck in resource chain search at PC=0x0001c3d4. Chain sentinel at [0x01FFF30C] = 0xFF00FF00. ROM is searching for system resources from a SCSI boot disk that doesn't exist yet.

### Phase 2: WebRTC Integration ✅ **COMPLETE**

- ✅ 4-thread in-process architecture
- ✅ All encoders integrated (H.264, VP9, WebP, PNG, Opus)
- ✅ JSON configuration system
- ✅ Mouse/keyboard input via WebRTC data channel

### Phase 3: Performance & Polish 🎯 **CURRENT**

- ✅ Unicorn perf: auto-ack interrupts, goto_tb, lean hook_block (hooks 5.3%, JIT ~10x slower)
- ✅ Web UI mouse/keyboard input working
- ⏳ Application support (HyperCard, classic games)
- ⏳ Stability improvements (long-running sessions)

### Phase 4: Application Support ⏳ **FUTURE**

**Goal**: Run Mac applications successfully

**Examples**: HyperCard stacks, classic games, productivity software

**Requirements**: Full hardware emulation, stable execution (hours)

### Phase 5: SheepShaver Support ⏳ **FAR FUTURE**

**Goal**: Mac OS 9, PowerPC support

---

## Success Metrics

### Q1 2026 - ACHIEVED
- ✅ 500k+ instruction dual-CPU validation (514k+)
- ✅ Boot to Finder with both backends
- ✅ WebRTC streaming with input
- ✅ Unicorn hook overhead minimized (5.3% of execution time)

### Medium-Term (2026)
- ⏳ Run HyperCard successfully
- ⏳ Play one classic game
- ⏳ Stable 30+ minute sessions

### Long-Term
- ⏳ Mac OS 8 support
- ⏳ Performance parity with UAE

---

## Non-Goals

**What We're NOT Trying to Do**:

1. **Cycle-Accurate Emulation** - We're pragmatic, not perfect
2. **Support Every Mac Model** - Focus on Quadra 650 / 68020 first
3. **Rewrite Everything** - Reference BasiliskII, don't reinvent
4. **Replace BasiliskII for Users** - This is a research/learning project
5. **Support Pre-68020 Macs** - 68020+ only (too much work for 68000/68010)

---

## Why This Project Exists

### Technical Goals
- Learn emulator architecture
- Explore dual-CPU validation approach
- Modern build system (Meson) for classic emulator
- Clean abstraction layers (Platform API)

### Practical Goals
- Preserve access to classic Mac software
- Faster emulation via Unicorn JIT
- Maintainable codebase (vs. 30-year-old BasiliskII)

### Research Goals
- Differential testing (UAE vs Unicorn)
- Document quirks and design decisions
- Explore JIT optimization strategies

---

## Contributing

### What We Need Help With
1. **Hardware Emulation** - VIA, SCSI, video details
2. **Performance Optimization** - JIT tuning, profiling
3. **Testing** - Run Mac applications, report issues
4. **Documentation** - Explain Mac OS internals, ROM behavior

### What to Expect
- **Unicorn-first development** - New features target Unicorn
- **Dual-CPU validation** - Major changes need validation testing
- **Documentation required** - Quirks must be documented

---

## Summary

**Unicorn**: ⭐ The future - primary backend, active development
**UAE**: 📊 The baseline - legacy support, validation reference
**DualCPU**: 🔍 The validator - development tool, catch bugs early

**End Goal**: Fast, clean, validated Mac emulator using Unicorn M68K CPU

**Current Status**: Both backends boot to Mac OS 7.5.5 Finder desktop. Unicorn ~10x slower than UAE (JIT bottleneck). Web UI streaming with input working.

**Philosophy**: Reference BasiliskII, validate continuously, document everything
