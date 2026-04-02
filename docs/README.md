# mac-phoenix

Modern Mac emulator with multiple CPU backends and web-based streaming UI.

---

## What Is This?

**mac-phoenix** emulates classic Macintosh computers, supporting both M68K (68020) and PowerPC architectures:

1. **M68K Emulation** — UAE interpreter (default, fast, JIT), Unicorn QEMU backend, DualCPU validation
2. **PowerPC Emulation** — KPX (Kheperix) interpreter from SheepShaver, boots Mac OS 9
3. **Web-Based UI** — WebRTC streaming with mouse/keyboard input, HTTP API
4. **Modern Architecture** — Clean platform API, modular drivers, Meson build

**Current Status** (April 2026):
- ✅ M68K: Mac OS 7.5.5 boots to Finder (UAE ~5s, Unicorn ~48s)
- ✅ PPC: Mac OS 9 boots to Finder (KPX interpreter ~45s)
- ✅ M68K JIT compiler (`--jit` flag)
- ✅ Command bridge API (app launch/quit, window list, boot polling)

---

## Quick Start

### Build
```bash
meson setup build
ninja -C build
```

### Run
```bash
# M68K (default)
./build/mac-phoenix ~/quadra.rom

# PPC
./build/mac-phoenix --arch ppc --rom ~/g3.rom --disk ~/mac9.hfv --ram 64
```

See **[Commands.md](Commands.md)** for complete build and testing guide.
See **[JsonConfig.md](JsonConfig.md)** for configuration documentation.

---

## Documentation

### Essential
- **[Architecture.md](Architecture.md)** — Platform API, backends, memory layout
- **[ProjectGoals.md](ProjectGoals.md)** — Vision, roadmap, current status
- **[Commands.md](Commands.md)** — Build, test, debug commands
- **[JsonConfig.md](JsonConfig.md)** — Configuration system
- **[TodoStatus.md](TodoStatus.md)** — What's done ✅ and what's next ⏳
- **[DeveloperGuide.md](DeveloperGuide.md)** — Backend details, debugging, contributing

### Command & Control
- **[CommandBridge.md](CommandBridge.md)** — jGNEFilter, mailbox, HTTP API for controlling Mac OS
- **[ApplianceLayer.md](ApplianceLayer.md)** — Programmable appliance design (partially implemented)

### PowerPC
- **[ppc/](ppc/)** — PPC emulation documentation
  - **[ppc/README.md](ppc/README.md)** — Status, boot commands, file map
  - **[ppc/Architecture.md](ppc/Architecture.md)** — Platform API integration
  - **[ppc/ExecutionModel.md](ppc/ExecutionModel.md)** — Boot sequence, mode switching
  - **[ppc/MemoryLayout.md](ppc/MemoryLayout.md)** — PPC memory map, kernel data

### Technical Deep Dives
- **[deepdive/](deepdive/)** — Detailed technical documentation
  - **[cpu/](deepdive/cpu/)** — CPU backend details, quirks, analysis
  - **[MemoryArchitecture.md](deepdive/MemoryArchitecture.md)** — Memory system
  - **[PlatformAPIInterrupts.md](deepdive/PlatformAPIInterrupts.md)** — Interrupt abstraction

### Other
- **[ThreadingArchitecture.md](ThreadingArchitecture.md)** — Thread model, IPC, video/audio
- **[ConfigUnification.md](ConfigUnification.md)** — Config system design
- **[Provisioning.md](Provisioning.md)** — Disk image creation, ExtFS, MPW
- **[Testing.md](Testing.md)** — Test framework documentation
- **[TroubleshootingGuide.md](TroubleshootingGuide.md)** — Debug help

---

## Directory Structure

```
mac-phoenix/
├── src/
│   ├── common/include/    # Shared headers (platform.h, emul_op.h)
│   ├── core/              # Core Mac managers (emul_op, adb, rom_patches, command_bridge)
│   ├── cpu/               # CPU backends
│   │   ├── uae_cpu/       # UAE M68K interpreter + JIT
│   │   ├── cpu_unicorn.cpp     # Unicorn M68K backend
│   │   ├── cpu_dualcpu.c       # DualCPU validation backend
│   │   └── kpx/                # KPX PPC interpreter + dyngen JIT
│   ├── drivers/           # Video, audio, platform, network drivers
│   ├── webrtc/            # WebRTC server (signaling + input)
│   ├── webserver/         # HTTP server, API handlers
│   └── config/            # JSON config system
├── client/                # Browser client (HTML, JS, CSS)
├── tests/                 # Shell + Playwright tests
├── subprojects/           # Unicorn, libdatachannel, nlohmann_json
├── docs/                  # Documentation (you are here!)
└── meson.build
```

---

## License

GPL v2 (based on BasiliskII / SheepShaver)

## References

- Original BasiliskII: https://github.com/kanjitalk755/macemu
- Unicorn Engine: https://www.unicorn-engine.org/
- M68K Reference: Motorola M68000 Family Programmer's Reference Manual
