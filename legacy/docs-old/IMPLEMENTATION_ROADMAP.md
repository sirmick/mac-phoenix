# Phoenix-Mac - Master Implementation Roadmap
**Project:** Phoenix-Mac (Classic Macs, Modern Hardware)
**Version:** 2.2
**Last Updated:** December 27, 2024
**Status:** Planning Complete - Ready for Implementation

---

## Executive Summary

This roadmap consolidates **two major modernization initiatives** into a coordinated plan:

1. **Unicorn CPU Integration** - Replace UAE/KPX with modern, cross-platform Unicorn Engine
2. **Build System Modernization** - Migrate to Meson, remove legacy platforms

~~3. **Server Refactoring**~~ ✅ **COMPLETE** (server.cpp successfully refactored into modular components)

**Total Timeline:** 10-14 weeks (2.5-3.5 months)
**Team Size:** 1-2 developers
**Risk Level:** Medium-High (critical path through Unicorn integration)

---

## Visual Timeline

```
┌─────────────────────────────────────────────────────────────────────┐
│                    MACEMU MODERNIZATION ROADMAP                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│ Weeks 1-2:  Phase 1 - Foundation (Clean Slate)                     │
│             ├─ Create phoenix-mac/ directory structure             │
│             ├─ Add Unicorn git submodule                           │
│             ├─ Copy minimal main.cpp from BasiliskII               │
│             ├─ Create Unicorn adapter from scratch                 │
│             └─ First instruction execution                         │
│                                                                     │
│ Weeks 3-4:  Phase 2 - Core Integration                             │
│             ├─ Unicorn: Wrapper API & EmulOps                      │
│             └─ Meson: BasiliskII prototype                         │
│                                                                     │
│ Weeks 5-6:  Phase 3 - Dual-CPU Validation                          │
│             ├─ Unicorn: Harness & State Comparison    ← CRITICAL   │
│             └─ Platform Cleanup: Delete obsolete code              │
│                                                                     │
│ Weeks 7-8:  Phase 4 - Instruction Validation                       │
│             ├─ Unicorn: All M68K instruction tests    ← CRITICAL   │
│             └─ Meson: Cross-compilation setup                      │
│                                                                     │
│ Weeks 9-10: Phase 5 - ROM Boot Testing                             │
│             ├─ Unicorn: Mac ROM boot sequence         ← GO/NO-GO   │
│             └─ Meson: Full build parity                            │
│                                                                     │
│ Weeks 11-12: Phase 6 - BasiliskII Integration                      │
│             ├─ Unicorn: Full integration & testing                 │
│             └─ Platform: Directory restructure                     │
│                                                                     │
│ Weeks 13-14: Phase 7 - Production & Release                        │
│             ├─ Unicorn: Performance optimization                   │
│             ├─ Meson: Full autotools replacement                   │
│             ├─ SheepShaver: Apply Unicorn to PPC                   │
│             ├─ Documentation: User guides & migration              │
│             └─ CI/CD: Automated testing pipeline                   │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Initiative Breakdown

### Initiative 1: Unicorn CPU Integration

**Goal:** Replace legacy UAE (M68K) and KPX (PPC) with modern Unicorn Engine

**Why:**
- ✅ Cross-platform (x86, ARM, RISC-V) via QEMU TCG backend
- ✅ Clean API designed for embedding
- ✅ Active upstream (Unicorn2 based on QEMU 5.0)
- ✅ Both M68K and PPC in one library (~1MB combined)
- ✅ No stubs or complex linking (unlike raw QEMU)

**Timeline:** Weeks 1-12 (critical path)

**Documentation:** [unicorn/00_UNICORN_INTEGRATION_MASTER_PLAN.md](unicorn/00_UNICORN_INTEGRATION_MASTER_PLAN.md)

**Approach:** Clean slate - build fresh `phoenix-mac/` project, pull in BasiliskII components incrementally

**Phases:**

| Week | Phase | Deliverable | Risk |
|------|-------|-------------|------|
| 1-2 | Clean Slate Setup | New project structure, Unicorn submodule, minimal main.cpp, first execution | Low |
| 3-4 | Wrapper & EmulOps | Full Unicorn adapter, illegal instruction hooks, pull in ROM system | Medium |
| 5-6 | Dual-CPU Harness | Side-by-side UAE/Unicorn validation (UAE from old project) | **Critical** |
| 7-8 | Instruction Tests | All M68K opcodes validated | **Critical** |
| 9-10 | ROM Boot | Mac ROM boots to desktop, pull in devices as needed | **GO/NO-GO** |
| 11-12 | Full Integration | Complete basilisk/ build, delete old project | High |

**Success Criteria:**
- ✅ Zero instruction divergences in dual-CPU tests
- ✅ Mac OS 7/8 boots to desktop
- ✅ Performance within 2x of UAE JIT
- ✅ Works on x86-64 and ARM64

---

### Initiative 2: Build System Modernization

**Goal:** Replace autotools with Meson, remove legacy platforms, modernize dependencies

**Why:**
- ✅ Autotools configure.ac is 2,000+ lines of spaghetti
- ✅ Support only modern platforms (Linux, macOS, Windows)
- ✅ Meson is fast (Ninja backend), cross-platform
- ✅ QEMU/Unicorn already use Meson (easier integration)
- ✅ Better dependency management

**Timeline:** Weeks 2-14 (parallel with Unicorn)

**Documentation:** [MASSIVE_REFACTOR_PLAN.md](../MASSIVE_REFACTOR_PLAN.md)

**Phases:**

| Week | Phase | Deliverable | Dependencies |
|------|-------|-------------|--------------|
| 2-3 | Prototype | web-streaming builds with Meson | None |
| 4-5 | BasiliskII | BasiliskII builds with Meson | Unicorn Phase 2 |
| 6-7 | Platform Cleanup | Delete AmigaOS, BeOS, etc. | None |
| 8-9 | Directory Restructure | core/, platform/, utils/ layout | Phase 6-7 |
| 10-11 | Autotools Deprecation | Meson is default | All builds working |
| 12-14 | Full Cutover | Delete autotools files | All tests passing |

**Success Criteria:**
- ✅ Builds on Linux, macOS, Windows
- ✅ Cross-compilation works (Linux → Windows)
- ✅ Clean build < 5 minutes
- ✅ 40-50% reduction in source files

---

## Critical Path Analysis

### Critical Dependencies

```
┌─────────────────────────────────────────────────────────────────┐
│                      CRITICAL PATH                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Week 1-2:  Unicorn Build & Setup                              │
│                     ↓                                           │
│  Week 3-4:  Unicorn Wrapper API                                │
│                     ↓                                           │
│  Week 5-6:  Dual-CPU Harness          ← BLOCKER FOR ALL        │
│                     ↓                                           │
│  Week 7-8:  Instruction Validation    ← BLOCKER FOR ROM BOOT   │
│                     ↓                                           │
│  Week 9-10: ROM Boot Testing          ← GO/NO-GO DECISION      │
│                     ↓                                           │
│  Week 11-12: BasiliskII Integration                            │
│                     ↓                                           │
│  Week 13-14: Production Hardening                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### GO/NO-GO Decision Points

**Week 6 - Dual-CPU Harness Complete:**
- ❓ Can we run UAE and Unicorn side-by-side?
- ❓ Do simple programs execute identically?
- ❓ Is state comparison accurate?
- **Decision:** If NO → investigate divergences, may need 1-2 extra weeks

**Week 8 - Instruction Validation Complete:**
- ❓ Do all M68K instruction categories pass?
- ❓ Zero divergences in arithmetic, logic, branching?
- ❓ Exception handling works correctly?
- **Decision:** If NO → may indicate fundamental Unicorn issue, consider fallback

**Week 10 - ROM Boot Testing:**
- ❓ Does Mac ROM boot to desktop?
- ❓ Can we run classic Mac applications?
- ❓ Is performance acceptable (< 2x slower than UAE JIT)?
- **Decision:** This is the **FINAL GO/NO-GO**. If fails, Unicorn integration may be infeasible.

---

## Parallel Work Streams

### Independent Streams (Can Run Concurrently)

**Stream A: Unicorn Integration** (Critical Path)
- Weeks 1-12: Full focus on CPU replacement
- Requires: 1 developer with CPU emulation knowledge

**Stream B: Build System** (Moderate Priority)
- Weeks 2-14: Can prototype early, full migration after Unicorn proven
- Requires: Build system expertise

### Recommended Resource Allocation

**1 Developer:**
- Focus on **Stream A** (Unicorn) as primary work
- Do **Stream B** (Meson) during Unicorn waiting periods (compilation, testing)
- Defer full Meson migration until Week 10+ (after Unicorn GO/NO-GO)

**2 Developers:**
- Dev 1: **Stream A** (Unicorn) - full time
- Dev 2: **Stream B** (Meson) - full time, with platform cleanup

---

## Risk Management

### High-Risk Items

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Unicorn instruction divergences | **Critical** | Medium | Dual-CPU harness catches immediately |
| Unicorn performance too slow | High | Low | Profile and optimize, fallback to UAE |
| ROM boot fails | **Critical** | Medium | Binary search with checkpoints |
| Meson build complexity | Medium | Medium | Keep autotools until proven |

### Contingency Plans

**If Unicorn Fails (Week 10):**
1. Keep UAE/KPX as default
2. Make Unicorn experimental (`--enable-unicorn` flag)
3. Continue with Meson refactor independently

**If Meson Migration Stalls:**
1. Keep autotools as primary build
2. Meson as alternative (`make meson-build`)
3. Revisit after Unicorn stabilizes

---

## Testing Strategy

### Continuous Testing

**Every Phase:**
- ✅ Compile successfully (zero errors)
- ✅ Unit tests pass (where applicable)
- ✅ Integration tests pass
- ✅ Git commit with clear message

### Major Test Points

**Week 6: Dual-CPU Tests**
```bash
./dualcpu --test simple_programs.bin --mode lockstep
# Expected: 100,000 instructions, zero divergences
```

**Week 8: Instruction Validation**
```bash
./dualcpu --test all_instructions.bin --mode lockstep
# Expected: 500,000+ instructions covering all M68K opcodes
```

**Week 10: ROM Boot**
```bash
./BasiliskII --rom MacII.ROM --disk System7.dsk --cpu unicorn
# Expected: Boot to Finder, run applications
```

**Week 12: Performance Benchmark**
```bash
./benchmark.sh --cpu uae --iterations 10
./benchmark.sh --cpu unicorn --iterations 10
# Expected: Unicorn within 2x of UAE
```

### Regression Testing

After each major phase, run full test suite:
```bash
make test-all
# Includes:
# - Unit tests (CPU, memory, devices)
# - Integration tests (ROM boot, application launch)
# - System tests (full desktop usage)
# - Performance benchmarks
```

---

## Documentation Deliverables

### For Developers

1. **Architecture Overview** - [ARCHITECTURE.md](ARCHITECTURE.md) (updated)
2. **Unicorn Integration Guide** - [unicorn/00_UNICORN_INTEGRATION_MASTER_PLAN.md](unicorn/00_UNICORN_INTEGRATION_MASTER_PLAN.md)
3. **Build System Guide** - [BUILD_MODERNIZATION.md](BUILD_MODERNIZATION.md) (new)
4. **Quick Start Guides** - [quickstart/](quickstart/) (new)

### For Users

1. **Migration Guide** - [MIGRATION.md](../MIGRATION.md) (new)
   - Upgrading from old builds
   - Config file migration
   - Breaking changes

2. **Build Instructions** - [BUILD.md](../BUILD.md) (updated)
   - Meson build process
   - Platform-specific requirements
   - Troubleshooting

3. **Performance Guide** - [PERFORMANCE.md](../PERFORMANCE.md) (new)
   - CPU backend selection
   - Codec tuning
   - Optimization tips

### For Maintainers

1. **Release Checklist** - [RELEASE.md](../RELEASE.md) (new)
2. **CI/CD Setup** - [.github/workflows/](../.github/workflows/) (updated)
3. **Contribution Guide** - [CONTRIBUTING.md](../CONTRIBUTING.md) (updated)

---

## Success Metrics

### Code Quality

- ✅ Source file reduction: 40-50% (delete obsolete platforms, UAE/KPX)
- ✅ Average file size: < 500 lines (except autogenerated)
- ✅ Test coverage: > 60% (new modules)
- ✅ Build time: < 5 minutes clean, < 30 seconds incremental

### Functionality

- ✅ Mac OS 7/8/9 boot successfully
- ✅ Classic applications run without regression
- ✅ Video/audio/input work via web UI
- ✅ Networking (SLIRP) functional

### Performance

- ✅ Unicorn CPU: < 2x slower than UAE JIT
- ✅ Frame encoding: < 16ms per frame (60fps capable)
- ✅ Audio latency: < 100ms
- ✅ Input lag: < 20ms

### Platform Support

- ✅ Linux x86-64: Full support
- ✅ Linux ARM64: Full support (new with Unicorn!)
- ✅ macOS x86-64: Full support
- ✅ macOS ARM64: Full support (Apple Silicon)
- ✅ Windows x86-64: Full support

---

## Resource Requirements

### Hardware

**Development Machine:**
- x86-64 or ARM64 CPU
- 16GB+ RAM (for compiling QEMU/Unicorn)
- 50GB+ disk space (builds, ROMs, disk images)

**Testing Machines:**
- Linux x86-64 (primary)
- macOS ARM64 (Apple Silicon testing)
- Raspberry Pi 4 (ARM64 testing) - optional

### Software

**Build Tools:**
- GCC 9+ or Clang 10+
- Meson 0.60+
- Ninja
- CMake 3.14+ (for Unicorn build)

**Dependencies:**
- QEMU 5.0+ (Unicorn subproject)
- libdatachannel
- libyuv, opus, openh264, SvtAv1Enc

**Development:**
- Git
- GDB or LLDB
- Python 3.8+ (for trace analysis tools)

### Time Commitment

**Full-Time (40 hours/week):**
- 10-14 weeks = 400-560 hours total

**Part-Time (20 hours/week):**
- 20-28 weeks = 5-7 months

**Breakdown by Initiative:**
- Unicorn Integration (clean slate): 70% (280-390 hours)
  - Includes pulling in BasiliskII components as needed
- Build Modernization (Meson-native): 30% (120-170 hours)
  - Built-in from day 1, no migration needed!

---

## Communication Plan

### Status Updates

**Weekly:**
- Summary of completed work
- Blockers and risks
- Next week's goals

**Milestone Reviews:**
- Week 6: Dual-CPU harness demo
- Week 10: ROM boot demo ← **GO/NO-GO**
- Week 14: Final release readiness

### Issue Tracking

Use GitHub Issues with labels:
- `unicorn-integration` - Unicorn CPU work
- `build-system` - Meson migration
- `blocker` - Blocks critical path
- `go-no-go` - Decision point

---

## Implementation Strategy

### Clean Slate Approach

Rather than trying to retrofit Unicorn into the existing BasiliskII codebase, we'll **build fresh from the ground up**, pulling in BasiliskII components as needed. This approach:

1. ✅ Starts with clean directory structure
2. ✅ Avoids autotools complexity entirely
3. ✅ Uses Meson from day one
4. ✅ Pulls in only what's needed (no legacy cruft)
5. ✅ Makes incremental progress visible

### Directory Creation First

```bash
# Create new clean project structure
phoenix-mac/
├── meson.build                    # Root build file
├── meson_options.txt              # Build options
├── .gitmodules                    # Unicorn submodule
│
├── basilisk/                      # Fresh BasiliskII
│   ├── meson.build
│   └── src/
│       ├── core/                  # Platform-independent (copy from BasiliskII as needed)
│       │   ├── main.cpp           # Start with Unix/main_unix.cpp
│       │   ├── cpu/               # Unicorn adapter (new)
│       │   ├── rom/               # ROM patches (copy when needed)
│       │   ├── devices/           # Device emulation (copy when needed)
│       │   └── macos/             # Mac OS support (copy when needed)
│       ├── platform/              # Platform-specific
│       │   ├── linux/             # Extract from Unix/
│       │   └── ...
│       └── utils/                 # Cross-platform (sigsegv, vm_alloc)
│
├── unicorn/                       # Git submodule
│
└── web-streaming/                 # Existing (already refactored)
```

### Incremental Build-Up Process

**Step 1: Scaffold** (Day 1)
```bash
# Create directory structure
mkdir -p phoenix-mac/{basilisk/src/{core/{cpu,rom,devices,macos},platform/{linux,macos,windows},utils},docs}

# Add Unicorn submodule
cd phoenix-mac
git init
git submodule add https://github.com/unicorn-engine/unicorn.git

# Create root meson.build
cat > meson.build << 'EOF'
project('phoenix-mac', ['c', 'cpp'],
  version: '2.0.0',
  default_options: ['cpp_std=c++17', 'warning_level=2']
)

# Subprojects
unicorn_proj = subproject('unicorn')
unicorn_dep = unicorn_proj.get_variable('unicorn_dep')

# Build targets
subdir('basilisk')
EOF

git add .
git commit -m "Initial project scaffold with Unicorn submodule"
```

**Step 2: Minimal BasiliskII** (Day 2-3)
```bash
# Copy minimal main file from original BasiliskII
cp ../BasiliskII/src/Unix/main_unix.cpp basilisk/src/core/main.cpp

# Create basilisk/meson.build with minimal compilation
cat > basilisk/meson.build << 'EOF'
basilisk_sources = files('src/core/main.cpp')

basilisk_exe = executable('phoenix-basilisk',
  basilisk_sources,
  dependencies: [unicorn_dep],
  install: true
)
EOF

# Try to build (will fail, but shows what's missing)
meson setup build
meson compile -C build
```

**Step 3: Add Dependencies Incrementally** (Day 3-5)

As the build fails, pull in only what's needed:

```bash
# Build fails: "sysdeps.h not found"
cp ../BasiliskII/src/include/sysdeps.h basilisk/src/core/

# Build fails: "video.h not found"
cp ../BasiliskII/src/include/video.h basilisk/src/core/

# Build fails: "rom_patches.cpp undefined"
cp ../BasiliskII/src/rom_patches.cpp basilisk/src/core/rom/

# Update meson.build to include new files
# ... repeat until minimal build succeeds
```

**Step 4: Unicorn Adapter** (Day 5-7)

Create fresh Unicorn wrapper (don't copy UAE):

```bash
# Create new Unicorn adapter
cat > basilisk/src/core/cpu/unicorn_cpu.h << 'EOF'
#pragma once
#include <unicorn/unicorn.h>
#include <stdint.h>

struct UnicornCPU {
    uc_engine *uc;
    // CPU state
    uint32_t pc;
    uint32_t dregs[8];
    uint32_t aregs[8];
    uint16_t sr;
};

// Lifecycle
UnicornCPU* unicorn_cpu_create(void);
void unicorn_cpu_destroy(UnicornCPU* cpu);

// Memory
bool unicorn_map_ram(UnicornCPU* cpu, uint64_t addr, void* ptr, size_t size);
bool unicorn_map_rom(UnicornCPU* cpu, uint64_t addr, void* ptr, size_t size);

// Execution
void unicorn_execute_one(UnicornCPU* cpu);
void unicorn_execute_until(UnicornCPU* cpu, uint32_t end_pc);

// Register access
uint32_t unicorn_get_dreg(UnicornCPU* cpu, int reg);
void unicorn_set_dreg(UnicornCPU* cpu, int reg, uint32_t val);
// ... etc
EOF

# Implement adapter
nano basilisk/src/core/cpu/unicorn_cpu.cpp
```

### Pull-In Strategy

**When to copy from BasiliskII:**

1. **Immediately needed:**
   - `main.cpp` (entry point)
   - `sysdeps.h` (type definitions)
   - `cpu_emulation.h` (memory access macros)

2. **After minimal build:**
   - ROM patching system (`rom_patches.cpp`, `rsrc_patches.cpp`)
   - Memory management (`vm_alloc.cpp`, `sigsegv.cpp`)
   - EmulOp system (`emul_op.cpp`, `emul_op.h`)

3. **After ROM boots:**
   - Device emulation (ADB, serial, SCSI)
   - Mac OS support files
   - Preferences system

4. **Never copy:**
   - ❌ UAE CPU (`uae_cpu/`, `uae_cpu_2021/`)
   - ❌ Native CPU (`native_cpu/`)
   - ❌ Autotools files (`configure.ac`, `Makefile.am`)
   - ❌ Obsolete platforms (AmigaOS, BeOS, etc.)

### Meson-First Development

**Every file added gets meson.build entry:**

```meson
# basilisk/src/core/meson.build
core_sources = files(
  'main.cpp',
  'cpu/unicorn_cpu.cpp',
  'rom/rom_patches.cpp',
  'macos/emul_op.cpp',
  # Add as needed
)

core_inc = include_directories('.')

core_lib = static_library('basilisk-core',
  core_sources,
  include_directories: core_inc,
  dependencies: [unicorn_dep]
)
```

**Platform-specific files separate:**

```meson
# basilisk/src/platform/linux/meson.build
if host_machine.system() == 'linux'
  platform_sources = files(
    'sys_linux.cpp',
    'timer_linux.cpp',
  )
endif
```

## Next Steps (Immediate)

### Week 1 Kickoff - Clean Slate Approach

**Day 1: Project Scaffold**
1. Create `phoenix-mac/` directory structure
2. Add Unicorn as git submodule
3. Create root `meson.build`
4. Create `basilisk/meson.build` skeleton
5. **Git Commit:** "Initial project scaffold with Unicorn submodule and Meson build"

**Day 2: Minimal Main**
1. Copy `main_unix.cpp` → `basilisk/src/core/main.cpp`
2. Copy minimal headers (`sysdeps.h`, `cpu_emulation.h`)
3. Attempt first build (will fail, but shows dependencies)
4. **Git Commit:** "Add minimal main.cpp from BasiliskII"

**Day 3: Unicorn Adapter**
1. Create `unicorn_cpu.h` and `unicorn_cpu.cpp`
2. Implement basic CPU lifecycle (create, destroy)
3. Implement register access functions
4. Build Unicorn submodule
5. **Git Commit:** "Implement basic Unicorn CPU adapter"

**Day 4: First Execution**
1. Create test program (simple M68K instructions)
2. Map test code into Unicorn memory
3. Execute one instruction
4. Verify PC advances correctly
5. **Git Commit:** "Unicorn executes first M68K instruction"

**Day 5: Pull In ROM System**
1. Copy `rom_patches.cpp` and dependencies
2. Add to `meson.build`
3. Fix includes and build errors
4. Load real ROM file
5. **Git Commit:** "Add ROM patching system from BasiliskII"

**End of Week 1:**
- ✅ Clean project structure
- ✅ Unicorn integrated and executing code
- ✅ Meson build working
- ✅ ROM system available
- ✅ Ready for Phase 2 (EmulOps and dual-CPU)

---

## Appendix: File Structure (Target State)

### Fresh Project: phoenix-mac/

```
phoenix-mac/                       # NEW clean project
├── .git/
├── .gitmodules                    # Unicorn submodule reference
├── meson.build                    # Root build file
├── meson_options.txt              # Build configuration
├── README.md                      # Project overview
├── BUILD.md                       # Build instructions
│
├── basilisk/                      # Fresh BasiliskII (Meson-native)
│   ├── meson.build
│   └── src/
│       ├── core/                  # Pulled from BasiliskII as needed
│       │   ├── main.cpp           # FROM: BasiliskII/src/Unix/main_unix.cpp
│       │   ├── cpu/               # NEW: Unicorn adapter
│       │   │   ├── unicorn_cpu.h
│       │   │   ├── unicorn_cpu.cpp
│       │   │   └── cpu_state.h    # State capture for dual-CPU
│       │   ├── rom/               # FROM: BasiliskII/src/
│       │   │   ├── rom_patches.cpp
│       │   │   ├── rsrc_patches.cpp
│       │   │   └── slot_rom.cpp
│       │   ├── devices/           # FROM: BasiliskII/src/
│       │   │   ├── adb.cpp
│       │   │   ├── serial.cpp
│       │   │   ├── scsi.cpp
│       │   │   └── cdrom.cpp
│       │   ├── macos/             # FROM: BasiliskII/src/
│       │   │   ├── emul_op.cpp
│       │   │   ├── macos_util.cpp
│       │   │   └── prefs.cpp
│       │   └── ipc/               # FROM: BasiliskII/src/IPC/
│       │       ├── ipc_protocol.h
│       │       └── ipc_client.cpp
│       │
│       ├── platform/              # Extracted from BasiliskII/src/Unix/
│       │   ├── linux/
│       │   │   ├── sys_linux.cpp
│       │   │   └── timer_linux.cpp
│       │   ├── macos/
│       │   │   ├── sys_darwin.cpp
│       │   │   └── timer_macos.cpp
│       │   └── windows/
│       │       ├── sys_windows.cpp
│       │       └── timer_windows.cpp
│       │
│       └── utils/                 # FROM: BasiliskII/src/CrossPlatform/
│           ├── vm_alloc.cpp
│           ├── sigsegv.cpp
│           └── video_blit.cpp
│
├── sheepshaver/                   # Future: Same pattern as basilisk/
│
├── web-streaming/                 # Symlink to ../macemu-dual-cpu/web-streaming
│   │                              # (Already refactored, Meson build added)
│   └── meson.build
│
├── unicorn/                       # Git submodule
│   ├── CMakeLists.txt
│   ├── include/unicorn/
│   └── ...                        # Full Unicorn Engine source
│
├── dualcpu/                       # Dual-CPU validation harness
│   ├── meson.build
│   ├── harness.cpp                # Runs UAE + Unicorn side-by-side
│   ├── state_compare.cpp          # Compare CPU states
│   └── trace_writer.cpp           # Binary trace recording
│
├── tools/                         # Python trace analysis
│   ├── compare_traces.py
│   ├── trace_view.py
│   └── find_divergence.py
│
├── docs/
│   ├── ARCHITECTURE.md
│   ├── IMPLEMENTATION_ROADMAP.md  # THIS FILE
│   └── unicorn/
│       └── 00_UNICORN_INTEGRATION_MASTER_PLAN.md
│
└── .github/workflows/
    ├── build-all-platforms.yml
    └── unicorn-validation.yml
```

### Original Project: macemu-dual-cpu/ (Reference)

```
macemu-dual-cpu/                   # EXISTING - kept for reference
├── BasiliskII/                    # Source of truth for copying files
├── SheepShaver/                   # Source of truth for copying files
├── web-streaming/                 # Already refactored, symlink from new project
├── qemu/                          # Abandoned (using Unicorn instead)
├── qemu-cpu/                      # Abandoned (using Unicorn instead)
└── docs/                          # Planning documents
```

### Migration Pattern

**Copy files individually as needed:**

```bash
# Example: Pull in ROM patches
cp ../macemu-dual-cpu/BasiliskII/src/rom_patches.cpp \
   phoenix-mac/basilisk/src/core/rom/

# Example: Pull in ADB device
cp ../macemu-dual-cpu/BasiliskII/src/Unix/adb.cpp \
   phoenix-mac/basilisk/src/core/devices/
```

**Each copy gets:**
1. ✅ Added to appropriate `meson.build`
2. ✅ Includes fixed (new directory structure)
3. ✅ Compiled and tested
4. ✅ Git commit with clear message

**Never copy:**
- ❌ Entire directories at once (too much cruft)
- ❌ Autotools files (configure.ac, Makefile.am)
- ❌ UAE/KPX CPU code
- ❌ Obsolete platform code

---

## Conclusion

This roadmap provides a **comprehensive, coordinated plan** for modernizing MacEmu across two major initiatives (server refactoring already complete!). The critical path runs through **Unicorn integration**, with parallel work on build system modernization.

**Key Success Factors:**
1. ✅ Focus on Unicorn first (Weeks 1-10)
2. ✅ Validate continuously with dual-CPU harness
3. ✅ Make GO/NO-GO decision at Week 10 (ROM boot)
4. ✅ Keep old code as fallback until fully validated
5. ✅ Test after every phase

**Timeline:** 10-14 weeks to production-ready modernized codebase

**Completed:** ✅ Server refactoring (saved 2-3 weeks!)

**Next Step:** Begin **Week 1 - Unicorn Setup** 🚀

---

**Document Version:** 2.1
**Status:** ✅ Planning Complete - Ready for Implementation
