# macemu-next Project Context

**Auto-loaded context for all Claude sessions working on macemu-next**

---

## What Is This Project?

**macemu-next** is a modern Mac emulator with **Unicorn M68K CPU** backend, dual-CPU validation, and WebRTC streaming.

### Core Focus
- **Primary Goal**: Unicorn-based Mac emulator (JIT execution via QEMU backend)
- **Validation**: Dual-CPU mode (UAE + Unicorn in lockstep)
- **Web UI**: WebRTC streaming with mouse/keyboard input
- **Architecture**: Clean Platform API abstraction

### Not This
- Not a BasiliskII fork (clean-room rewrite)
- Not cycle-accurate (pragmatic emulation)
- Not focused on UAE (legacy support only)

---

## Project Status (March 2026)

### Both Backends Boot to Mac OS 7.5.5 Finder Desktop

- **UAE backend**: Boots to Finder, 2200+ CHECKLOADs
- **Unicorn backend**: Boots to Finder, 2513+ CHECKLOADs (slower, ~2x UAE speed)

### Phase 1: Core CPU Emulation - COMPLETE
- Unicorn M68K backend (68040 with JIT)
- EmulOps (0xAExx for Unicorn, 0x71xx for UAE)
- A-line/F-line traps, interrupt support, native trap execution
- 514,000+ instructions validated in dual-CPU mode

### Phase 1.5: Boot Parity - COMPLETE
- Both backends boot to Finder desktop
- Framebuffer fix (placed at 0x02110000, outside RAM, avoids WDCB overlap)
- RTR instruction added to Unicorn's QEMU m68k translator
- FPU emulation, SIGSEGV handler, serial null check

### Phase 2: WebRTC Integration - COMPLETE
- 4-thread in-process architecture (CPU, video encoder, audio encoder, web server)
- All encoders: H.264, VP9, WebP, PNG, Opus audio
- JSON configuration system (config.json + CLI overrides + env vars)
- Mouse/keyboard input via WebRTC data channel -> ADB
- Playwright e2e test framework

### Phase 3: Performance & Polish - CURRENT
- Unicorn performance optimizations (auto-ack interrupts, goto_tb, lean hook_block)
- Unicorn is ~2x slower than UAE (down from ~10x after optimizations)
- Remaining gap is structural (QEMU M68K condition codes, memory-indirect registers)

---

## Architecture Overview

### Platform API (The Heart of the System)

**Everything goes through `g_platform` struct** ([src/common/include/platform.h](../src/common/include/platform.h)):

```c
typedef struct Platform {
    CPUExecResult (*cpu_execute_one)(void);
    uint32_t (*cpu_get_pc)(void);
    void (*cpu_set_pc)(uint32_t pc);
    bool (*emulop_handler)(uint16_t opcode, bool probe);
    void (*cpu_trigger_interrupt)(int level);
    // ... 20+ more function pointers
} Platform;
extern Platform g_platform;
```

### Three CPU Backends

| Backend | Purpose | Status |
|---------|---------|--------|
| **Unicorn** | Primary (JIT) | Boots to Finder, active development |
| **UAE** | Legacy (interpreter) | Boots to Finder, maintained |
| **DualCPU** | Validation tool | Development tool only |

**Backend Selection**: `CPU_BACKEND=unicorn|uae|dualcpu`

### Memory Layout

```
RAM (32MB)        @ 0x00000000 - 0x01FFFFFF
ROM (1MB)         @ 0x02000000 - 0x020FFFFF
ScratchMem (64KB) @ 0x02100000 - 0x0210FFFF
FrameBuffer (4MB) @ 0x02110000 - 0x0250FFFF
```

### Interrupt System

**Unicorn backend** uses QEMU's native interrupt delivery:
- `g_pending_interrupt_level` set by timer/device code
- `hook_block()` checks every 4096 blocks, calls `uc_m68k_trigger_interrupt()`
- QEMU's `m68k_cpu_exec_interrupt()` delivers interrupt with auto-acknowledge
- `goto_tb` enabled for backward branches (loops chain without breaking for hooks)

**UAE backend**: Sets `SPCFLAG_INT`, processed by `do_specialties()`

### Web UI & Input

- HTTP server serves client HTML/JS/CSS
- WebRTC for video (H.264/VP9), audio (Opus), and metadata
- Data channel carries mouse/keyboard input (binary protocol)
- Input path: Browser -> DataChannel -> `process_input_message()` -> ADB functions

---

## Key Files

### CPU Backends
- `src/cpu/cpu_unicorn.cpp` — Unicorn backend (Platform API, MMIO, memory mapping)
- `src/cpu/unicorn_wrapper.c` — Unicorn hooks (hook_block, hook_interrupt, deferred updates)
- `src/cpu/unicorn_exec_loop.c` — Main uc_emu_start loop
- `src/cpu/cpu_uae.cpp` — UAE backend
- `src/cpu/cpu_dualcpu.cpp` — DualCPU validation

### Core
- `src/core/emul_op.cpp` — EmulOp handlers
- `src/core/adb.cpp` — ADB mouse/keyboard emulation
- `src/core/rom_patches.cpp` — ROM patching, EmulOp insertion
- `src/drivers/platform/timer_interrupt.cpp` — 60Hz timer polling

### Web/Streaming
- `src/webrtc/webrtc_server.cpp` — WebRTC signaling + input handling
- `src/webserver/webserver_main.cpp` — HTTP server
- `client/client.js` — Browser client (WebRTC, input, UI)

### Unicorn Internals (subproject)
- `subprojects/unicorn/qemu/target/m68k/translate.c` — M68K instruction decoder
- `subprojects/unicorn/qemu/target/m68k/op_helper.c` — Interrupt delivery (auto-ack)

---

## Essential Commands

### Build & Run
```bash
cd macemu-next
ninja -C build                                    # Build
CPU_BACKEND=uae ./build/macemu-next ~/quadra.rom  # Run with UAE
CPU_BACKEND=unicorn ./build/macemu-next ~/quadra.rom  # Run with Unicorn
./build/macemu-next ~/quadra.rom                  # Run with web server (default)
```

### Testing
```bash
MACEMU_HTTP_PORT=8000 npx playwright test          # E2E tests
EMULATOR_TIMEOUT=30 CPU_BACKEND=dualcpu ./build/macemu-next ~/quadra.rom  # DualCPU
```

### Environment Variables
- `CPU_BACKEND=unicorn|uae|dualcpu` — Select backend
- `EMULATOR_TIMEOUT=N` — Auto-exit after N seconds
- `MACEMU_SCREENSHOTS=1` — Dump /tmp/macemu_screen_*.ppm
- `MACEMU_LOG_LEVEL=2` — Enable debug logging (D() macro)
- `--no-webserver` — CLI-only mode (no HTTP/WebRTC)

---

**Last Updated**: March 5, 2026
**Project Phase**: Phase 3 - Performance & Polish
**Current Focus**: Unicorn performance optimization, web UI refinement
