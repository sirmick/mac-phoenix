# macemu Project Overview

## What is macemu?

**macemu** is an open-source Macintosh emulator collection containing:

| Component | Purpose | Target Systems |
|-----------|---------|----------------|
| **BasiliskII** | 68k Macintosh emulator | System 0.x through MacOS 8.1 |
| **SheepShaver** | PowerPC Macintosh emulator | MacOS 9.x and X 10.0 |
| **cxmon** | Command-line debugger/monitor | Multi-architecture |
| **web-streaming** | Headless browser access | WebRTC-based remote display |

**License:** GNU General Public License (GPL)

## Repository Structure

```
macemu/
├── BasiliskII/           # 68k Mac emulator (primary codebase)
│   ├── src/              # Core source code
│   │   ├── Unix/         # Linux/macOS/FreeBSD platform code
│   │   ├── Windows/      # Windows platform code
│   │   ├── SDL/          # SDL2/SDL3 graphics/audio backends
│   │   ├── uae_cpu/      # 68k CPU emulation (UAE core)
│   │   ├── uae_cpu_2021/ # Updated 68k CPU emulation
│   │   ├── slirp/        # User-mode TCP/IP networking
│   │   ├── CrossPlatform/# Shared cross-platform utilities
│   │   └── include/      # Header files
│   └── MacOSX/           # Xcode project for macOS
│
├── SheepShaver/          # PowerPC Mac emulator
│   └── src/              # Extends BasiliskII (many symlinked files)
│       ├── kpx_cpu/      # PowerPC CPU support
│       └── Unix/         # Platform-specific code
│
├── cxmon/                # Standalone debugger tool
│   └── src/              # Disassemblers for multiple architectures
│
├── web-streaming/        # WebRTC streaming for headless mode
│   ├── libdatachannel/   # WebRTC library (git submodule)
│   ├── server/           # C++ WebRTC server + VP8 encoder
│   └── client/           # JavaScript/HTML web client (embedded)
│
└── docs/                 # Documentation (this folder)
```

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                      MacOS Guest                            │
├─────────────────────────────────────────────────────────────┤
│  ROM Patches (rom_patches.cpp) │ Resource Patches           │
├────────────────────────────────┴────────────────────────────┤
│                    CPU Emulation Layer                      │
│  ┌─────────────────┐  ┌──────────────────────────────────┐  │
│  │  UAE Interpreter │  │  JIT Compiler (x86/x86-64 only) │  │
│  │  (uae_cpu/)      │  │  (uae_cpu/compiler/)            │  │
│  └─────────────────┘  └──────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                   Hardware Abstraction                      │
│  ┌────────┐ ┌───────┐ ┌──────┐ ┌────────┐ ┌─────────────┐  │
│  │ Video  │ │ Audio │ │ ADB  │ │ Storage│ │  Networking │  │
│  │ (SDL)  │ │ (SDL) │ │      │ │ (disk) │ │  (slirp)    │  │
│  └────────┘ └───────┘ └──────┘ └────────┘ └─────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                   Platform Layer                            │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌──────────────────┐  │
│  │  Unix   │ │ Windows │ │  macOS  │ │  Web Streaming   │  │
│  └─────────┘ └─────────┘ └─────────┘ └──────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                     Host Operating System                   │
└─────────────────────────────────────────────────────────────┘
```

## SheepShaver Relationship

SheepShaver shares most code with BasiliskII via symlinks:
- Same video, audio, storage, networking subsystems
- Different CPU emulation (PowerPC via kpx_cpu/)
- Additional files: `gfxaccel.cpp`, `name_registry.cpp`, `thunks.cpp`

## Build System

### Unix/Linux (Autotools)
```bash
cd BasiliskII/src/Unix
./autogen.sh          # Generate configure script
./configure [options] # Configure build
make                  # Build
```

### Key Configure Options
- `--enable-jit-compiler` - Enable x86 JIT for faster emulation
- `--enable-sdl-video` / `--enable-sdl-audio` - Use SDL backends
- `--enable-webstreaming` - Enable headless web streaming mode
- `--with-gtk` - Build GTK preferences editor

### Windows
Visual Studio solution at `BasiliskII/src/Windows/BasiliskII.sln`

### macOS
Xcode project at `BasiliskII/MacOSX/BasiliskII.xcodeproj`
