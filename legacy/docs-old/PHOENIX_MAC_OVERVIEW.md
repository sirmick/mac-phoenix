# Phoenix-Mac Project Overview

**Tagline:** Classic Macs, Modern Hardware 🔥🖥️

**Status:** Planning Complete, Ready for Implementation
**Start Date:** TBD
**Timeline:** 10-14 weeks (2.5-3.5 months)

---

## What is Phoenix-Mac?

Phoenix-Mac is a **clean-room reimplementation** of the BasiliskII/SheepShaver emulators, rebuilt from the ground up with:

1. ✅ **Unicorn Engine** - Modern, cross-platform CPU emulation (M68K + PPC)
2. ✅ **Meson Build** - Fast, modern build system from day 1
3. ✅ **Clean Architecture** - No legacy cruft, only what's needed
4. ✅ **WebRTC Streaming** - Web-based UI (already refactored!)
5. ✅ **ARM64 Support** - Runs on Apple Silicon, Raspberry Pi, etc.

The name "Phoenix" represents rebirth - bringing vintage Mac OS back to life on modern hardware, rising from the ashes of obsolete 68K/PPC machines.

---

## Why Phoenix-Mac?

### Problems with Current BasiliskII/SheepShaver

❌ **Autotools spaghetti** - 2,000+ line configure.ac
❌ **UAE CPU limitations** - No ARM64 JIT, platform-specific
❌ **Legacy platforms** - Still has AmigaOS, BeOS, Irix code
❌ **Scattered architecture** - Platform code mixed everywhere
❌ **Build complexity** - Hours to set up on new platforms

### Phoenix-Mac Advantages

✅ **Unicorn CPU** - Cross-platform (x86, ARM, RISC-V), actively maintained
✅ **Meson build** - 5 minute clean builds, native cross-compilation
✅ **Clean slate** - Pull in only what's needed from BasiliskII
✅ **Modern platforms** - Linux, macOS, Windows only
✅ **Incremental validation** - Dual-CPU testing from day 1

---

## Project Structure

### Two Projects Side-by-Side

```
macemu-dual-cpu/           # EXISTING - source material
├── BasiliskII/            # Copy files from here as needed
├── SheepShaver/           # Copy files from here as needed
└── web-streaming/         # Already refactored (symlink)

phoenix-mac/               # NEW - clean implementation
├── unicorn/              # Git submodule
├── basilisk/
│   └── src/
│       ├── core/
│       │   ├── cpu/
│       │   │   ├── unicorn/    # NEW: Production CPU
│       │   │   └── uae/        # COPIED: For dual-CPU validation
│       │   ├── main.cpp        # COPIED: From BasiliskII
│       │   └── rom/            # COPIED: As needed
│       ├── platform/           # Organized by OS
│       └── utils/
├── dualcpu/              # Validation harness
└── meson.build           # Meson from day 1
```

### Directory Philosophy

**OLD (BasiliskII):**
```
BasiliskII/src/
├── Unix/
│   ├── Linux/
│   ├── Darwin/
│   ├── Irix/          ← Delete
│   └── ...
├── AmigaOS/           ← Delete
├── BeOS/              ← Delete
└── uae_cpu/           ← Copy to phoenix-mac/basilisk/src/core/cpu/uae/
```

**NEW (Phoenix-Mac):**
```
phoenix-mac/basilisk/src/
├── core/              # Platform-independent (pulled as needed)
├── platform/          # Clean separation
│   ├── linux/
│   ├── macos/
│   └── windows/
└── utils/             # Truly cross-platform
```

---

## Implementation Strategy

### Clean Slate Approach

1. **Create directory structure first**
2. **Add Unicorn submodule**
3. **Copy minimal files from BasiliskII**
4. **Build incrementally** (fail → copy → fix → build → repeat)
5. **Add Unicorn adapter from scratch**
6. **Pull in UAE for dual-CPU validation**
7. **Test every step**

### What Gets Copied from BasiliskII

**Immediately:**
- `main_unix.cpp` → `main.cpp`
- `sysdeps.h`, `cpu_emulation.h`
- UAE CPU directory (for dual-CPU)

**After minimal build:**
- ROM patching system
- Memory management (vm_alloc, sigsegv)
- EmulOp system

**After ROM boots:**
- Device emulation (ADB, SCSI, serial)
- Mac OS support files
- Preferences system

**Never:**
- Autotools files
- Obsolete platform code
- Native/PowerROM CPU variants

### Build Options

```bash
# Option 1: UAE only (verify baseline)
meson setup build -Dcpu-backend=uae
meson compile -C build
./build/phoenix-basilisk  # Uses UAE CPU

# Option 2: Unicorn only (production)
meson setup build -Dcpu-backend=unicorn
meson compile -C build
./build/phoenix-basilisk  # Uses Unicorn CPU

# Option 3: Dual-CPU validation mode
meson setup build -Dcpu-backend=dual
meson compile -C build
./build/dualcpu --rom mac.rom  # Compares UAE vs Unicorn every instruction
```

---

## Timeline

### 10-14 Week Roadmap

**Weeks 1-2:** Clean slate setup
- Create phoenix-mac/ structure
- Add Unicorn submodule
- Copy UAE CPU and minimal main.cpp
- First Unicorn instruction execution

**Weeks 3-4:** Unicorn adapter
- Full CPU wrapper API
- EmulOp illegal instruction hooks
- Pull in ROM patching system

**Weeks 5-6:** Dual-CPU validation ← **CRITICAL**
- Side-by-side UAE/Unicorn testing
- State comparison after every instruction
- Find and fix divergences

**Weeks 7-8:** Instruction validation ← **CRITICAL**
- Test all M68K instruction categories
- Zero divergences required

**Weeks 9-10:** ROM boot testing ← **GO/NO-GO**
- Mac ROM boots to desktop
- Pull in devices as needed
- Performance validation

**Weeks 11-12:** Full integration
- Complete basilisk/ build
- Clean up unused code
- Performance optimization

**Weeks 13-14:** Production release
- SheepShaver (PPC) support
- Documentation
- CI/CD pipeline

---

## Success Criteria

### Code Quality
- ✅ 40-50% fewer source files (no legacy platforms)
- ✅ Average file size < 500 lines
- ✅ Clean build in < 5 minutes

### Functionality
- ✅ Mac OS 7/8/9 boot to desktop
- ✅ Classic applications run
- ✅ WebRTC streaming works
- ✅ Zero instruction divergences from UAE

### Performance
- ✅ Unicorn within 2x of UAE JIT speed
- ✅ 60fps frame encoding
- ✅ < 100ms audio latency

### Platform Support
- ✅ Linux x86-64
- ✅ Linux ARM64 (NEW! - Raspberry Pi, etc.)
- ✅ macOS x86-64
- ✅ macOS ARM64 (Apple Silicon)
- ✅ Windows x86-64

---

## Key Technologies

### Unicorn Engine
- **What:** CPU emulator framework based on QEMU
- **Why:** Cross-platform, actively maintained, clean API
- **Architectures:** M68K, PPC, x86, ARM, MIPS, SPARC, etc.
- **Version:** Unicorn2 (based on QEMU 5.0)

### Meson Build System
- **What:** Modern, fast build system
- **Why:** Cross-platform, native cross-compilation, easy dependency management
- **Speed:** ~5 minute clean builds vs ~30 minutes with autotools

### Dual-CPU Validation
- **What:** Run UAE and Unicorn side-by-side, compare every instruction
- **Why:** Catch divergences immediately, not after ROM boot fails
- **Method:** Snapshot CPU state after each instruction, binary diff

### WebRTC Streaming
- **What:** Browser-based Mac desktop via WebRTC
- **Why:** Remote access, mobile support, modern UI
- **Status:** ✅ Already refactored and working!

---

## Development Workflow

### Day 1: Scaffold
```bash
mkdir phoenix-mac
cd phoenix-mac
git init
git submodule add https://github.com/unicorn-engine/unicorn.git

# Create directory structure
mkdir -p basilisk/src/core/{cpu/{unicorn,uae},rom,devices,macos}
mkdir -p basilisk/src/platform/{linux,macos,windows}
mkdir -p basilisk/src/utils
mkdir -p dualcpu docs

# Create root meson.build
cat > meson.build << 'EOF'
project('phoenix-mac', ['c', 'cpp'], version: '1.0.0')
cpu_backend = get_option('cpu-backend')
subdir('basilisk')
EOF

git add .
git commit -m "Initial Phoenix-Mac scaffold"
```

### Day 2-3: Copy UAE & Build
```bash
# Copy UAE CPU
cp -r ../macemu-dual-cpu/BasiliskII/src/uae_cpu/* \
   basilisk/src/core/cpu/uae/

# Copy minimal main
cp ../macemu-dual-cpu/BasiliskII/src/Unix/main_unix.cpp \
   basilisk/src/core/main.cpp

# Try to build (will fail, shows what's missing)
meson setup build -Dcpu-backend=uae
meson compile -C build

# Copy missing headers iteratively
# ... repeat until build succeeds
```

### Day 4-5: Unicorn Adapter
```bash
# Create Unicorn wrapper from scratch
cat > basilisk/src/core/cpu/unicorn/unicorn_cpu.h << 'EOF'
#pragma once
#include <unicorn/unicorn.h>

struct UnicornCPU {
    uc_engine *uc;
    uint32_t pc;
    uint32_t dregs[8];
    uint32_t aregs[8];
    uint16_t sr;
};

UnicornCPU* unicorn_cpu_create(void);
void unicorn_execute_one(UnicornCPU* cpu);
// ...
EOF

# Implement adapter
nano basilisk/src/core/cpu/unicorn/unicorn_cpu.cpp

# Test
meson setup build -Dcpu-backend=unicorn
meson compile -C build
./build/phoenix-basilisk --test-cpu
```

### Week 2+: Incremental Progress
```bash
# Build fails? Copy the missing file
cp ../macemu-dual-cpu/BasiliskII/src/rom_patches.cpp \
   basilisk/src/core/rom/

# Update meson.build
# Rebuild
# Commit

# Repeat until ROM boots!
```

---

## Documentation

### For Developers
- **[IMPLEMENTATION_ROADMAP.md](IMPLEMENTATION_ROADMAP.md)** - Full 14-week plan
- **[unicorn/00_UNICORN_INTEGRATION_MASTER_PLAN.md](unicorn/00_UNICORN_INTEGRATION_MASTER_PLAN.md)** - Detailed Unicorn guide
- **[MASSIVE_REFACTOR_PLAN.md](../MASSIVE_REFACTOR_PLAN.md)** - Build system modernization

### For Users
- **README.md** - Project overview
- **BUILD.md** - Build instructions
- **MIGRATION.md** - Upgrading from old BasiliskII

---

## FAQ

**Q: Why not just patch BasiliskII?**
A: Too much legacy cruft. Clean slate is faster and cleaner.

**Q: Will old BasiliskII disk images work?**
A: Yes! Same disk/ROM formats, same emulation core.

**Q: Why keep UAE CPU?**
A: For dual-CPU validation. We compare UAE and Unicorn instruction-by-instruction to ensure perfect accuracy.

**Q: What about SheepShaver (PPC)?**
A: Week 13-14. Same pattern: copy code, add Unicorn PPC, validate.

**Q: Will this run on Raspberry Pi?**
A: Yes! ARM64 support is a primary goal (via Unicorn).

**Q: Why "Phoenix"?**
A: Rebirth - bringing dead Macs back to life. Rising from the ashes.

---

## Status

✅ **Planning:** Complete
✅ **Server Refactoring:** Complete (already done!)
⏳ **Implementation:** Ready to begin

**Next Step:** Week 1 - Create phoenix-mac/ scaffold

---

## Contributing

(TBD after Week 1 scaffold)

---

**Project:** Phoenix-Mac
**Tagline:** Classic Macs, Modern Hardware 🔥🖥️
**Version:** 1.0.0-alpha
**License:** GPL-2.0 (same as BasiliskII)
**Started:** TBD

*From the ashes of obsolete hardware, a new emulator rises.*
