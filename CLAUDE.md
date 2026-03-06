# MacPhoenix

Classic Mac emulator with web-based UI. Boots Mac OS 7.5.5 to Finder on a Quadra 650 ROM.

## Quick Reference

```bash
# Build
ninja -C build

# Run (with web UI)
./build/mac-phoenix /home/mick/quadra.rom

# Run (headless, timed)
EMULATOR_TIMEOUT=10 ./build/mac-phoenix --no-webserver /home/mick/quadra.rom

# Test (fast — API + UAE boot + mouse, ~12s)
meson test -C build api_endpoints boot_uae mouse_position

# Test (all — includes slow Unicorn boot, ~60s)
meson test -C build

# Test (verbose)
meson test -C build -v

# Rebuild Unicorn subproject (after modifying QEMU sources)
cd subprojects/unicorn && cmake --build build -j$(nproc) && cd ../..

# Reconfigure meson (after changing meson.build or meson_options.txt)
meson setup build --reconfigure

# Playwright E2E tests (requires running emulator)
npx playwright test
```

## CPU Backends

Selected via `--backend` flag or `CPU_BACKEND` env var (default: `uae`):

| Backend | What | Speed | Use for |
|---------|------|-------|---------|
| `uae` | Hand-tuned interpreter | Fast (~5s boot) | Default, end users |
| `unicorn` | QEMU TCG JIT | Slow (~48s boot) | Validation, future perf work |
| `dualcpu` | UAE + Unicorn lockstep | Very slow | Debugging divergences |

## Project Structure

```
src/
  main.cpp                          — Entry point, thread orchestration
  config/
    emulator_config.cpp             — CLI args, JSON config, env vars
    config_manager.cpp              — Legacy MacemuConfig (being replaced)
  core/
    boot_progress.cpp               — Boot milestone tracking (phases, CHECKLOAD counting)
    rom_patches.cpp                 — ROM patching, EmulOp insertion
    emul_op.cpp                     — EmulOp handlers (RESET, IRQ, CHECKLOAD, etc.)
    adb.cpp                         — ADB mouse/keyboard emulation
  cpu/
    cpu_context.cpp                 — Memory allocation, backend init
    cpu_uae.cpp                     — UAE backend (Platform API bridge)
    cpu_unicorn.cpp                 — Unicorn backend (MMIO hooks, memory mapping)
    unicorn_wrapper.c               — Unicorn engine wrapper (hooks, perf counters)
    uae_cpu/                        — UAE interpreter source (newcpu.cpp, cpuemu.cpp)
  drivers/
    video/video_output.h            — Lock-free triple buffer for frames
    video/video_webrtc.cpp          — WebRTC video driver
    platform/timer_interrupt.cpp    — 60Hz timer via clock_gettime
  webserver/
    api_handlers.cpp                — All /api/ endpoints
    webserver_main.cpp              — HTTP server thread
  webrtc/
    webrtc_server.cpp               — WebRTC signaling + input handling
  common/
    sigsegv.cpp                     — SIGSEGV handler (skips bad accesses)
    include/platform.h              — Platform API (g_platform function pointers)

subprojects/
  unicorn/                          — Unicorn engine (forked, with m68k patches)
    qemu/target/m68k/translate.c    — M68K → TCG IR decoder (added RTR instruction)
    qemu/accel/tcg/cpu-exec.c       — TB find/compile loop (perf counters added)

tests/
  test_api_endpoints.sh             — API smoke tests (10 checks)
  test_boot_to_finder.sh            — Boot-to-Finder test (parameterized by backend)
  test_mouse_position.sh            — Mouse position API test
  e2e/                              — Playwright browser tests

client/                             — Browser UI (vanilla JS)

legacy/                             — Original BasiliskII/SheepShaver source (read-only reference)
```

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | `{emulator_running, boot_phase, checkload_count, boot_elapsed}` |
| `/api/mouse` | GET | `{x, y}` — Mac cursor position (503 if not running) |
| `/api/screenshot` | GET | PNG image of current framebuffer (503 if no frames) |
| `/api/config` | GET/POST | Unified JSON config |
| `/api/emulator/start` | POST | Start CPU execution |
| `/api/emulator/stop` | POST | Stop CPU execution |
| `/api/storage` | GET | Available ROMs and disk images |
| `/api/codec` | POST | Change video codec (h264/vp9/av1/png/webp) |
| `/api/keypress` | POST | Send key event: `{"key": "return"}` or `{"key": 36}` |

## Boot Phases

Tracked in `boot_progress.cpp`, exposed via `/api/status`:

`pre-reset` → `ROM init` → `boot globs` → `drivers` → `warm start` → `boot blocks` → `extensions` → `Finder` → `desktop`

## CLI Flags

```
./build/mac-phoenix [options] [rom-path]
  --port N              HTTP server port (default: 8080)
  --signaling-port N    WebRTC signaling port (default: 8090)
  --backend uae|unicorn Backend override (or use CPU_BACKEND env)
  --timeout N           Auto-exit after N seconds
  --no-webserver        Headless mode (no HTTP/WebRTC)
  --screen WxH          Display resolution (default: 640x480)
  --config path         JSON config file
  --screenshots         Dump PPM screenshots to /tmp
```

## Environment Variables

| Var | Description |
|-----|-------------|
| `CPU_BACKEND` | `uae`, `unicorn`, or `dualcpu` |
| `EMULATOR_TIMEOUT` | Auto-exit after N seconds |
| `MACEMU_LOG_LEVEL` | 0=milestones, 1=important ops, 2=all ops, 3=+registers |
| `MACEMU_SCREENSHOTS` | Dump PPM screenshots to /tmp |
| `MACEMU_ROM` | Default ROM path for tests |

## Key Architectural Decisions

- **Platform API**: All backends implement the same `g_platform` function pointer table. Core code never calls backend-specific functions directly.
- **Memory layout**: RAM(32MB @ 0x0) + ROM(1MB @ 0x02000000) + ScratchMem(64KB @ 0x02100000) + FrameBuffer(4MB @ 0x02110000). Framebuffer is outside RAM to avoid corrupting Mac data structures.
- **EmulOps**: ROM patches insert trap opcodes (0xAExx for Unicorn, 0x71xx for UAE) that trigger host-side handlers for I/O, drivers, and system functions.
- **Two config systems**: `EmulatorConfig` (new, CLI-aware) and `MacemuConfig` (legacy, JSON-only). Being unified — CLI flags propagate via main.cpp.
- **Triple buffer video**: CPU writes frames, encoder reads them, screenshot API reads them — all lock-free via atomic indices.

## ROM

Tests expect a Quadra 650 ROM at `/home/mick/quadra.rom`. Override with `MACEMU_ROM` env var or `meson configure -Dtest_rom=/path/to/rom build`.
