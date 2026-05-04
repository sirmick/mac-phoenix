# MacPhoenix

Classic Mac emulator with web-based UI. Boots Mac OS 7.5.5 to Finder on a Quadra 650 ROM.

## Quick Reference

```bash
# Configure (first time, or after changing CMakeLists.txt)
cmake -B build

# Build (emulator + net-bridge Rust NAT)
cmake --build build -j$(nproc)

# Disable net-bridge build (skip cargo, e.g. in minimal CI)
cmake -B build -DBUILD_NET_BRIDGE=OFF

# Run (with web UI)
./build/mac-phoenix /home/mick/quadra.rom

# Run (headless, timed)
./build/mac-phoenix --timeout 10 --no-webserver /home/mick/quadra.rom

# Test (all)
ctest --test-dir build

# Test (verbose)
ctest --test-dir build -V

# Test (by label: api or boot)
ctest --test-dir build -L api
ctest --test-dir build -L boot

# Test (specific)
ctest --test-dir build -R api_endpoints

# Configure with test ROM path
cmake -B build -DTEST_ROM=/path/to/quadra.rom

# Playwright E2E tests (requires running emulator)
npx playwright test

# Boot capacity matrix (12 cells: backend × JIT configs × 2 OSes)
# Writes CSV + PNG screenshots + per-cell logs to --out dir.
tests/run_boot_matrix.sh --out /home/mick/mac-phoenix/test-results/boot-matrix
# Single cell:
tests/test_boot_matrix.sh --label unicorn-m68k-755 --backend unicorn-m68k \
    --rom ~/roms/quadra.rom --disk ~/storage/images/macos-7.5.5.img \
    --timeout 60 --port 19300 --screenshot-dir /tmp/one-cell
```

## CPU Backends

Selected via `--backend` flag (default: `uae`). The backend token uniquely
determines the CPU architecture — there is no separate `--arch` flag.

| Backend | Arch | What | Speed | Use for |
|---------|------|------|-------|---------|
| `uae` | m68k | Hand-tuned interpreter (+optional `--jit`) | Fast (~5s boot) | Default, end users |
| `unicorn-m68k` | m68k | QEMU TCG | Slow (~48s boot) | Validation, future perf work |
| `unicorn-ppc` | ppc | QEMU TCG | Slow | PPC validation |
| `kpx` | ppc | KPX translator (+optional `--jit`, +optional `--jit68k`) | Medium | Default for PPC |
| `dualcpu` | m68k | UAE + Unicorn lockstep | Very slow | Debugging divergences |

## Project Structure

```
src/
  main.cpp                          — Entry point, thread orchestration
  config/
    emulator_config.cpp             — Unified config: CLI args, JSON, env vars, save/load
  core/
    boot_progress.cpp               — Boot milestone tracking (phases, CHECKLOAD counting)
    rom_patches.cpp                 — ROM patching, EmulOp insertion
    emul_op.cpp                     — EmulOp handlers (RESET, IRQ, CHECKLOAD, CMD_DISPATCH)
    command_bridge.cpp              — Read commands (peek Mac mem from IRQ); init + heartbeat watchdog
    command_bridge.h                — CmdType enum, CommandResult, init/watchdog signatures
    emulator_subprocess.h           — Subprocess management for m68k and PPC (IPC via SHM+socket)
    adb.cpp                         — ADB mouse/keyboard emulation
    cpu_context.cpp                 — Memory allocation, backend init
  cpu/
    cpu_uae.c                       — UAE backend (Platform API bridge)
    cpu_unicorn.cpp                 — Unicorn backend (MMIO hooks, memory mapping)
    unicorn_wrapper.c               — Unicorn engine wrapper (hooks, perf counters)
    uae_cpu/                        — UAE interpreter source (newcpu.cpp, cpuemu.cpp)
  drivers/
    video/video_output.h            — Lock-free triple buffer for frames
    video/video_webrtc.cpp          — WebRTC video driver
    platform/platform_unix.cpp      — Unix platform: disk I/O + ExtFS filesystem driver
    platform/timer_interrupt.cpp    — 60Hz timer via clock_gettime
  webserver/
    http_server.cpp                 — HTTP/1.1 server, stream routes, RFC 6455 WS upgrade
    websocket.cpp                   — In-process WebSocket (RFC 6455 framing + SHA1/base64 handshake)
    api_handlers.cpp                — All /api/ endpoints
    webserver_main.cpp              — HTTP server thread; registers /ws route via WebRTCServer
  webrtc/
    webrtc_server.cpp               — Signaling (/ws), peer connections for H.264/VP9 RTP
  common/
    sigsegv.cpp                     — SIGSEGV handler (skips bad accesses)
    include/platform.h              — Platform API (g_platform function pointers)

subprojects/
  unicorn/                          — Unicorn engine (forked, with m68k patches)
    qemu/target/m68k/translate.c    — M68K → TCG IR decoder (added RTR instruction)
    qemu/accel/tcg/cpu-exec.c       — TB find/compile loop (perf counters added)

BridgeAgent/
  BridgeAgent.c                     — BridgeAgent source (Retro68 m68k); polls bridge files in Host: ExtFS
  BridgeAgent.bin                   — Pre-built MacBinary (committed; rebuilt by CMake when Retro68 present)
  BridgeAgent.r                     — Resource fork: SIZE, BNDL, FREF, creator sig, icons.r include

tests/
  test_api_endpoints.sh             — API smoke tests (10 checks)
  test_boot_to_finder.sh            — Boot-to-Finder test (parameterized by backend)
  test_mouse_position.sh            — Mouse position API test
  test_command_bridge.sh            — Command bridge integration tests (7 checks)
  test_guest_suite.sh               — Boot → dispatch MacTestSuite.pl via /api/launch → read results
  guest/MacTestSuite.pl             — MacPerl script run inside the guest by the agent
  test_extfs.sh                     — ExtFS config, CLI, backward compat tests (8 checks)
  test_boot_matrix.sh               — Single-cell capacity check ({backend,arch,disk,jit} → Finder)
  run_boot_matrix.sh                — Orchestrator: 12 cells, serial, CSV + PNG + per-cell log
  e2e/                              — Playwright browser tests

client/                             — Browser UI (vanilla JS)

```

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | `{emulator_running, boot_phase, checkload_count, boot_elapsed}` |
| `/api/mouse` | GET | `{x, y}` — Mac cursor position (503 if not running) |
| `/api/mouse` | POST | Move cursor: `{"x":N,"y":N}` absolute, `{"dx":N,"dy":N}` relative |
| `/api/screenshot` | GET | PNG image of current framebuffer (503 if no frames) |
| `/api/config` | GET/POST | Unified JSON config |
| `/api/emulator/start` | POST | Start CPU execution |
| `/api/emulator/stop` | POST | Stop CPU execution |
| `/api/storage` | GET | Available ROMs and disk images |
| `/api/codec` | POST | Change video codec (h264/vp9/png/webp) |
| `/api/codecs` | GET | Available codecs: `{codecs: [{id, name, available}]}` |
| `/api/keypress` | POST | Send key event: `{"key": "return"}` or `{"key": 36}` |
| `/api/invoke-debug` | POST | Invoke debugger (Programmer's Key: NMI on 68k, Cmd+Power on PPC) |
| `/api/app` | GET | Current app name (read command — peek `CurApName`) |
| `/api/windows` | GET | Window list (read command — walk `WindowList`) |
| `/api/wait` | POST | Poll condition: `boot=Finder`, `app=Name` |
| `/api/launch` | POST | Launch app via BridgeAgent: `{"path":"HD:App"}`. With `"open":true`, opens the document via generic `'aevt'/'odoc'` (any registered creator: Frontier `LAND`, MacPerl `McPL`, MPW `MPS `, …) |
| `/api/script` | POST | Generic do-script via `'misc'/'dosc'`: `{"creator":"McPL","script":"..."}` (or `"script_b64"` for exact MacRoman bytes). Works for MacPerl, Frontier UserLand, MPW Shell, AppleScript editor — any app registered for `dosc`. |
| `/api/quit` | POST | Quit front app via BridgeAgent (`kAEQuitApplication`) |
| `/api/shutdown` | POST | Graceful guest OS shutdown via BridgeAgent (`ShutDwnPower`) |
| `/api/restart` | POST | Graceful guest OS restart via BridgeAgent (`ShutDwnStart`) |

## Boot Phases

Tracked in `boot_progress.cpp`, exposed via `/api/status`:

`pre-reset` → `ROM init` → `boot globs` → `drivers` → `warm start` → `boot blocks` → `extensions` → `Finder` → `desktop`

## CLI Flags

```
./build/mac-phoenix [options] [rom-path]

Machine:
  --rom PATH                 ROM file (or positional arg)
  --ram MB                   RAM size in megabytes (default: 64)
  --screen WxH               Display resolution (default: 640x480)
  --disk PATH                Disk image (repeatable)
  --cdrom PATH               CD-ROM image (repeatable)
  --extfs PATH               Shared folder (repeatable)
  --bootdriver N             0=any, -62=CD-ROM (default: 0)
  --storage-dir PATH         Default storage root (default: ~/storage)

CPU:
  --backend NAME             uae | unicorn-m68k | unicorn-ppc | kpx | dualcpu
                             (default: uae; backend implies architecture)
  --jit / --no-jit           Enable backend's primary JIT (uae, kpx)
  --jit68k / --no-jit68k     Enable 68k-on-PPC DR JIT (kpx only, default: on)
  --idlewait / --no-idlewait Pause CPU when guest idle (default: on)

Media:
  --audio                    Enable audio emulation (default: off)
  --zap-pram                 Clear PRAM on startup
  --dismiss-shutdown-dialog  Auto-dismiss improper-shutdown dialog

Networking:
  --network MODE             none | socket[:PATH] (default: none)

Automation:
  --bridge                   Enable automation bridge (BridgeAgent + auto ExtFS mount)
  --browser                  Run MacBrowser (Firefox-on-Xvfb pipeline; needs xvfb + firefox)
  --headless-http            HTTP API only (no video/audio); implies --bridge

Server:
  --port N                   HTTP+WS port (default: 11000) — also hosts /ws signaling
  --no-webserver             Headless, no HTTP/WebRTC
  --timeout N                Auto-exit after N seconds
  --config PATH              JSON config file
  --screenshots              Dump PPM frames to /tmp

Logging:
  --log-level N              0–3
  --debug-connection         Debug WebRTC connections
  --debug-mode-switch        Debug video mode switches
  --debug-perf               Debug performance
  --debug-network            Debug net-bridge / lwIP NAT/DNS/ICMP/TCP/UDP
```

## Environment Variables

The emulator binary does not read environment variables. Use CLI flags instead.

| Var | Scope | Description |
|-----|-------|-------------|
| `MACEMU_ROM` | Test scripts only | Default ROM path (not read by the binary) |

## Key Architectural Decisions

- **Platform API**: All backends implement the same `g_platform` function pointer table. Core code never calls backend-specific functions directly.
- **Memory layout**: RAM(32MB @ 0x0) + ROM(1MB @ 0x02000000) + ScratchMem(64KB @ 0x02100000) + FrameBuffer(4MB @ 0x02110000). Framebuffer is outside RAM to avoid corrupting Mac data structures.
- **EmulOps**: ROM patches insert trap opcodes (0xAExx for Unicorn, 0x71xx for UAE) that trigger host-side handlers for I/O, drivers, and system functions.
- **Single config system**: `EmulatorConfig` — handles CLI args and JSON file. CLI args override at runtime but are never saved. UI changes go through `merge_ui_json()` which updates both runtime config and `file_config_` (what gets persisted). Flat JSON format; backend token (`uae`/`unicorn-m68k`/`unicorn-ppc`/`kpx`/`dualcpu`) determines architecture; legacy `architecture`+`cpu_backend`+`m68k.*`+`ppc.*` keys are coerced on load for one release of backward compat.
- **Triple buffer video**: CPU writes frames, encoder reads them, screenshot API reads them — all lock-free via atomic indices.
- **Single-port HTTP + WebSocket**: HTTP/1.1 server in `src/webserver/http_server.cpp` is built on `QTcpServer` + `QTcpSocket` (`Qt6::Network`); the accept loop runs on a dedicated `std::thread` using synchronous `waitForX` APIs (no Qt event loop required). On `Upgrade: websocket` the upgrade handshake is written via the QTcpSocket and the underlying fd is `dup()`'d off to a worker thread that owns the WebSocket. Long-poll stream routes (`/api/stream`) use the same fd-handoff pattern. `src/webserver/websocket.cpp` implements RFC 6455 in-process on the dup'd fd. One TCP listener serves static UI, REST API, `/api/frame` long-poll, and `/ws` (signaling + input + PNG/WebP frames). libdatachannel's `rtc::WebSocketServer` is not used. WebRTC RTP (H.264/VP9 media + Opus audio) still rides direct UDP ports negotiated via ICE.
- **Three transport modes**: PNG/WebP → WebSocket binary on `/ws`; H.264/VP9 → WebRTC RTP track; `httpstream` → `/api/frame` long-poll. Signaling JSON + input events always ride the `/ws` WebSocket regardless of codec.
- **Command bridge**: Two layers. **Read commands** (`/api/app`, `/api/windows`) peek Mac memory directly from the IRQ — no guest cooperation. **Action commands** (`/api/launch`, `/api/shutdown`, `/api/restart`, `/api/quit`) write a request file into `bridge_dir`, which a guest-side `BridgeAgent` app (installed in `:System Folder:Startup Items:`) polls and executes via Process Manager / Shutdown Manager / AppleEvents. Files in `bridge_dir` cross the parent/IPC-child process split for free since both processes see the same disk path. Enable with `--bridge` or `bridge_enabled: true`. See `docs/CommandBridge.md`.

## ROM

Tests expect a Quadra 650 ROM at `~/roms/quadra.rom` and disk image at `~/storage/images/7.6.img`. Override with `MACEMU_ROM` / `MACEMU_DISK` env vars or `cmake -B build -DTEST_ROM=/path/to/rom`. ROMs and disk images are not distributed.

## Optional Codec Dependencies

Video/audio codecs are auto-detected at configure time. If a library is missing, the codec is disabled and won't appear in the UI.

| Library | Package (Ubuntu) | Package (macOS) | Codec |
|---------|-----------------|-----------------|-------|
| OpenH264 | `libopenh264-dev` | `brew install openh264` | H.264 |
| libvpx | `libvpx-dev` | `brew install libvpx` | VP9 |
| libwebp | `libwebp-dev` | `brew install webp` | WebP |
| Opus | `libopus-dev` | `brew install opus` | Audio |
| libyuv | `libyuv-dev` | `brew install libyuv` | Color conversion |

PNG encoding (fpng) has no external dependencies and is always available.

## Required Dependencies

| Library | Package (Ubuntu) | Package (macOS) | Purpose |
|---------|-----------------|-----------------|---------|
| CMake | `cmake` | Xcode CLI Tools or `brew install cmake` | Build system |
| OpenSSL | `libssl-dev` | `brew install openssl` | MD5, WebRTC |
| pkg-config | `pkg-config` | `brew install pkg-config` | Dependency detection |
| Qt6 (≥6.4) | `qt6-base-dev qt6-tools-dev` | `brew install qt@6` | Cross-platform abstraction (see [docs/qt6/PLAN.md](docs/qt6/PLAN.md)) |
| xcb (+shm/damage/composite/randr) | `libxcb1-dev libxcb-shm0-dev libxcb-damage0-dev libxcb-composite0-dev libxcb-randr0-dev` | n/a (Linux-only feature) | MacBrowser host pipeline (removed in Phase 8) |

## MacBrowser

`--browser` runs a host-side Firefox on Xvfb and pipes its rendered
pixels into a guest Mac app called **MacBrowser**. The guest app
provides a native Mac chrome (toolbar, URL bar, V/H scrollbars) over
a 1:1 pixel viewport; clicks/keys/scroll are forwarded to Firefox
via WebDriver-BiDi. See [`docs/MacBrowser.md`](docs/MacBrowser.md)
for the architecture deep-dive (memory layout, ring protocol, BiDi
flow).

**Runtime deps**: `apt install xvfb` and Firefox (Mozilla tarball at
`/opt/firefox/firefox` — *not* the snap). The supervisor probes
`/opt/firefox/firefox` first, falls back to `/usr/bin/firefox`.

**Build**: `MacBrowser/MacBrowser.bin` is the m68k guest app, built
by CMake from `MacBrowser/MacBrowser.c` whenever the Retro68
toolchain is available. Same opt-in pattern as BridgeAgent (`-DBUILD_MAC_BROWSER=OFF`
to skip; the committed `.bin` always works as fallback).

**Run**: mount the floppy + `--browser`:

```bash
./build/mac-phoenix --browser \
  --disk MacBrowser/MacBrowser.dsk \
  --bridge \
  /path/to/quadra.rom
```

Inside the guest, double-click `MacBrowser` on the floppy.

**Wire protocol** (between host and guest, via SPSC rings in
BrowserShm):

| Command (g2h) | Description |
|---|---|
| `BR_CMD_NAV` | Navigate to URL — `bidi.navigate` |
| `BR_CMD_CLICK` / `_MOUSE_MOVE` / `_MOUSE_OUT` | Pointer input (host-polled, mostly) |
| `BR_CMD_KEY_DOWN` / `_KEY_UP` | Key events; mods passed through, special keys remapped to W3C codepoints |
| `BR_CMD_SCROLL` | Wheel scroll, dx/dy in CSS px |
| `BR_CMD_BACK` / `_FORWARD` / `_RELOAD` / `_STOP` | Toolbar nav |
| `BR_CMD_RESIZE` | Window grew/shrunk → resize Firefox window inside Xvfb |
| `BR_CMD_GET_SELECTION` / `_PASTE` / `_SELECT_ALL` | Clipboard + select-all bridge |
| `BR_CMD_ZOOM_IN` / `_ZOOM_OUT` / `_ZOOM_RESET` | CSS-zoom step |

| Event (h2g) | Description |
|---|---|
| `BR_EV_STATUS` | Loading / Ready / Error + URL — drives URL bar |
| `BR_EV_FRAME` | Framebuffer updated — guest re-blits |
| `BR_EV_SELECTION` | Reply to GET_SELECTION → write to TEScrap |
| `BR_EV_PAGE_METRICS` | page_w/h, scroll_x/y, viewport_w/h — drives V/H scrollbar thumb + active state |
| `BR_EV_DOWNLOAD` | (M7, planned) download start/progress/done |
