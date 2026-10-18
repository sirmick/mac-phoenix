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

# Executor backend: no ROM, C++ Toolbox (docs/executor/PLAN.md). On the UAE
# core (default) it needs vm.mmap_min_addr=0; --core mame-68k does not
# (Executor's four address windows). Pick "Executor" in the web UI, or:
./build/mac-phoenix --backend executor
./build/mac-phoenix --backend executor --core mame-68k
# ... on Apple's 7.5.5 System with real Finder (Settings > System disk):
./build/mac-phoenix --backend executor --executor-system macos-7.5.5-noext.img
# ... with sound (Executor mixes in C++ and feeds the same Opus/WebRTC path):
./build/mac-phoenix --backend executor --audio
# ... with Open Transport, native C++ (docs/executor/OpenTransport.md; in progress):
./build/mac-phoenix --backend executor --executor-system macos-7.5.5.img --executor-ot
ctest --test-dir build -L executor

# MPW (68k SC/Link/Rez) inside a headless Executor session (docs/MPW.md)
tools/mpw/mpw setup                    # once: MPW disk + System, built from snapshots
tools/mpw/mpw build tests/guest/ot     # build.mpw; products -> DIR/build/*.bin
tools/mpw/mpw run 'Version'            # exit code = {Status}; mpw stop when done
tools/mpw/mpw --toolchain cw build DIR  # CodeWarrior Pro 4's MPW (MWC68K, MWCPPC; public media)

# Guest software (docs/Provisioning.md, docs/provisioning/PLAN.md): pinned
# media (provisioning/media.json) -> bakes run in the guest -> snapshots
# (~/storage/baked) -> images (provisioning/images/*.json). Dev disks:
# Development.img + dev-7.5.5.img / dev-7.6.1.img, for both 68k and PPC;
# Development-9.img + dev-8.6.img / dev-9.0.4.img, PPC only (G3 ROM).
python3 provisioning/bake.py --all --jobs 3     # snapshots not up to date
python3 provisioning/build.py --all             # images not up to date
python3 provisioning/test_image.py dev-7.6.1 Development   # launch every app, 68k + PPC
python3 provisioning/bake.py NAME --explore --record       # work out a bake; --do/--finish

# arm64 / riscv64 builds on this x86-64 box: Debian containers under qemu
# (binfmt_misc + docker). Build dirs build-arm64/, build-riscv64/.
tools/xarch/xarch.sh arm64                     # configure + build
tools/xarch/xarch.sh arm64 ctest -R cpu_cores  # run tests inside
tools/xarch/xarch.sh riscv64 shell             # interactive

# Boot capacity matrix (all backend × JIT config × OS cells)
# Writes CSV + PNG screenshots + per-cell logs to --out dir.
tests/run_boot_matrix.sh --out /home/mick/mac-phoenix/test-results/boot-matrix
# Single cell:
tests/test_boot_matrix.sh --label uae-755 --backend uae \
    --rom ~/roms/quadra.rom --disk ~/storage/images/macos-7.5.5.img \
    --timeout 60 --port 19300 --screenshot-dir /tmp/one-cell
```

## CPU Backends

Selected via `--backend` flag (default: `uae`). The backend token uniquely
determines the CPU architecture — there is no separate `--arch` flag. The
`Backend` enum and Platform API are kept as the extension point for adding
further backends.

| Backend | Arch | What | Speed | Use for |
|---------|------|------|-------|---------|
| `uae` | m68k | ROM-based Mac; CPU core `--core uae` (interpreter, +optional `--jit`) or `--core mame-68k` (MAME's Musashi: PMMU, softfloat FPU, several instances per process; `src/cpu/mame/`; `musashi` names it too, the vendored v3.32 is gone) | Fast (~5s boot) | Default, end users |
| `kpx` | ppc | Power Mac G3 machine; CPU core `--core kpx` (Kheperix, +optional `--jit`) or `--core mame-ppc` (MAME's PowerPC recompiler: `--jit` native x86-64 back-end, `--no-jit` UML interpreter; `src/cpu/mame/`, installer `src/cpu/kpx/cpu_ppc_mame.cpp`) | Medium | Default for PPC |
| `executor` | m68k | Executor 2000 Toolbox in C++ on UAE; no ROM | Fast start | Toolbox reimplementation work (`src/executor/`) |

MAME cores (docs/cpu/PLAN.md): MAME's Musashi with PMMU and FPU as the 68k
core `mame-68k` (C1, done), MAME's PowerPC recompiler as the PPC core
`mame-ppc` (C2: core, Mac side and the x86-64 back-end done, `--jit`; arm64
back-end boots 7.5.5 on arm64 Linux under qemu, `tools/xarch`), and the host 68k core replacing the nanokernel's 68k
emulator on the PPC machine (C3a: `--m68k-host 1` runs host-initiated
68k routines on a `mame-68k` core with handoff to the emulator at the first
trap; `2` is an experiment that breaks the bridge; the main 68k thread is
still the nanokernel's emulator). `--core mame-ppc --jit` boots 7.5.5 to the
desktop in 0.9 s and Mac OS 9.0.4 in 9.7 s. Reference checkout:
`/home/mick/src/mame-src`. Tests: `ctest -R 'cpu_cores|boot_core_mame68k|boot_ppc_mame|guest_suite_ppc_mame|executor\.mame68k\.'` (`boot_ppc_mame_jit` is the native back-end;
`guest_suite_ppc_mame` is the Mac OS 9.0.4 guest suite on it;
`command_bridge_ppc_mame_m68k` is C3a). `--debug-mame-ppc` prints host ops,
exceptions and a per-second PC and framebuffer sample from the mame-ppc
installer. Open items: the Stragglers
section of docs/cpu/PLAN.md.

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
    command_bridge.cpp              — Read commands (peek Mac mem from IRQ); init
    command_bridge.h                — CmdType enum, CommandResult, init signatures
    emulator_subprocess.h           — Subprocess management for m68k and PPC (IPC via SHM+socket)
    adb.cpp                         — ADB mouse/keyboard emulation
    cpu_context.cpp                 — Memory allocation, backend init
  cpu/
    uae_cpu/                        — UAE interpreter source (newcpu.cpp, cpuemu.cpp)
    kpx/                            — KPX PPC backend (SheepShaver Kheperix interpreter)
    cpu_m68k.cpp                    — The ROM-based 68k Mac on a cpu::Core (EmulOps, InterruptFlags, reset)
    core/                           — cpu::Core: one interface for every core (GuestMemory, host ops, run/step, IRQ, contexts); uae + mame-68k
    core/platform_cpu.cpp           — g_platform's memory/CPU/Execute68k/interrupt entries on the active core (every machine)
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
  executor/                         — Executor 2000 Toolbox core (MIT), imported; see docs/executor/PLAN.md
    cpu/                            — syn68k API facade (syn68k_common) on a cpu::Core + PowerCore stub
    romlib/                         — the Toolbox (lightly patched upstream source)
    multiversal/                    — API definitions → generated headers/trap glue (Ruby)
    tests/                          — Executor gtest suite (ctest label: executor)
  common/
    sigsegv.cpp                     — SIGSEGV handler (skips bad accesses)
    include/platform.h              — Platform API (g_platform function pointers)

BridgeAgent/
  BridgeAgent.c                     — BridgeAgent source (Retro68 m68k); takes requests over the bridge trap $ABE5
  BridgeAgent.bin                   — Pre-built MacBinary (committed; rebuilt by CMake when Retro68 present)
  BridgeAgent.r                     — Resource fork: SIZE, BNDL, FREF, creator sig, icons.r include

tests/
  test_api_endpoints.sh             — API smoke tests (10 checks)
  test_boot_to_finder.sh            — Boot-to-Finder test (parameterized by backend)
  test_mouse_position.sh            — Mouse position API test
  test_command_bridge.sh            — Command bridge integration tests (7 checks)
  test_guest_suite.sh               — Boot → launch MacPhoenixSuite via /api/launch → read results
  guest/suite/                      — MacPhoenixSuite: the guest test suite (files, audio, AppleScript, network, keyboard, Finder), a fat C app built by tools/mpw/mpw (Apple or CodeWarrior)
  test_extfs.sh                     — ExtFS config, CLI, backward compat tests (8 checks)
  test_boot_matrix.sh               — Single-cell capacity check ({backend,arch,disk,jit} → Finder)
  run_boot_matrix.sh                — Orchestrator: 14 cells (uae, mame-68k, kpx, mame-ppc), serial, CSV + PNG + per-cell log
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
| `/api/ui` | GET | Read command: front process's windows (screen rects), controls, dialog items, menus (open popups included) as JSON — click by name, fall back to pixels |
| `/api/text` | GET | Text on screen now, as QuickDraw drew it (`src/core/text_feed.cpp`: `_StdText`/`_CopyBits`/`_ScrollRect` patched in the trap table; ROM machines): strings with screen rects, font, window/app, `inverted` (looks selected). `?since=N`: the draw log (offscreen too). For what `/api/ui` can't name: List Manager lists, self-drawn controls, open popups |
| `/api/wait` | POST | Poll condition: `boot=Finder`, `app=Name`, `text=Substring` (drawn on screen) |
| `/api/annotate` | POST | `{"text": "..."}` — a line from whatever drives the guest (provisioning/bake.py posts each step); the last 6 are in `/api/status` `annotations` and shown over the screen by the web UI. `{"text": ""}` clears. Open the UI with `?view` to watch without sending input |
| `/api/launch` | POST | Launch app via BridgeAgent: `{"path":"HD:App"}`. With `"open":true`, opens the document via generic `'aevt'/'odoc'` (any registered creator: Frontier `LAND`, MacPerl `McPL`, MPW `MPS `, …) |
| `/api/script` | POST | Generic do-script via `'misc'/'dosc'`: `{"creator":"McPL","script":"..."}` (or `"script_b64"` for exact MacRoman bytes). Works for MacPerl, Frontier UserLand, MPW Shell, AppleScript editor — any app registered for `dosc`. |
| `/api/copy` | POST | In-guest File Manager copy via BridgeAgent, both forks + Finder info: `{"from":"Vol:Item","to":"Vol:Folder:"}` |
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
  --executor-data PATH       Executor data dir (default: <storage>/executor)
  --executor-system IMAGE    Run on the System Folder of <storage>/images/IMAGE
                             (copied out on first use, image mounted too)
  --executor-start WHAT      finder | browser | path in the System (default: finder)
  --[no-]executor-fresh      Restore the System from its clean copy each run (default: on)
  --executor-ot              Executor answers Open Transport (in progress; default: off)

CPU:
  --backend NAME             uae | kpx | executor
                             (default: uae; backend implies architecture;
                             executor needs no ROM)
  --core NAME                CPU core for the backend's architecture:
                             68k uae | mame-68k (default: uae; ROM machine and Executor;
                             musashi = mame-68k since the vendored v3.32 was retired)
                             ppc kpx | mame-ppc (default: kpx)
  --jit / --no-jit           Enable backend's primary JIT (uae, kpx)
  --m68k-host N              mame-ppc: 1 runs host-initiated 68k routines on a
                             mame-68k core (C3a); 2 the parked experiment;
                             3 all 68k code on mame-68k (C3b, experimental:
                             does not boot yet)
  --idlewait / --no-idlewait Pause CPU when guest idle (default: on)

Media:
  --audio                    Enable audio emulation (default: off)
  --zap-pram                 Clear PRAM on startup
  --dismiss-shutdown-dialog  Auto-dismiss improper-shutdown dialog

Networking:
  --network MODE             none | socket[:PATH] (default: none)

Automation:
  --bridge                   Enable automation bridge (BridgeAgent + auto ExtFS mount)
  --browser                  Run MacBrowser (in-process Chromium via Qt6 WebEngine)
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
  --debug-network            Debug net-bridge NAT/DNS/ICMP/TCP/UDP
  --debug-mame-ppc           mame-ppc: host ops, exceptions, per-second CPU sample
  --trace-atraps             Record (A-trap, caller PC) pairs; snapshots write atraps.tsv
  --no-text-feed             Don't patch _StdText & co. (no /api/text)
  --executor-logtraps        Executor logs every trap call to stderr
  --executor-logtraps-nesting N  ... and calls nested N deep in Toolbox callbacks
  --executor-debug OPTS      Executor's own debug options: memcheck (heap checks at every
                             Memory Manager call), allocs (log allocations >= 4K with the
                             calling code), drag (the Drag Manager's dispatch and Finder's
                             hook calls), fslog, trace, trapfailure, unimplemented, ...
  --watch ADDR[:LEN]         Report every write to guest memory ADDR (Executor, Linux x86-64):
                             old/new value, 68k PC/registers/code, last traps, host stack
```

## Environment Variables

Configuration is CLI flags only; the binary reads no environment variables
for it. The exceptions are Kheperix-era diagnostic switches on the PPC
machine.

| Var | Scope | Description |
|-----|-------|-------------|
| `MACEMU_ROM` | Test scripts only | Default ROM path (not read by the binary) |
| `MACEMU_PPC_NO_IRQ`, `MACEMU_PPC_TRACE_TRAP` | `--backend kpx` | Kheperix-era debug switches |

## Key Architectural Decisions

- **Platform API**: All backends implement the same `g_platform` function pointer table. Core code never calls backend-specific functions directly.
- **PPC guest memory**: Kheperix (`--core kpx`) uses identity addressing (Mac address = host address; needs `vm.mmap_min_addr=0`). `--core mame-ppc` puts guest memory in a 4GB-aligned 4GB reservation, host = `VMBaseDiff` + Mac address, so it needs no sysctl.
- **Memory layout**: RAM(32MB @ 0x0) + ROM(1MB @ 0x02000000) + ScratchMem(64KB @ 0x02100000) + FrameBuffer(4MB @ 0x02110000). Framebuffer is outside RAM to avoid corrupting Mac data structures.
- **EmulOps**: ROM patches insert trap opcodes (0x71xx) that trigger host-side handlers for I/O, drivers, and system functions.
- **Single config system**: `EmulatorConfig` — handles CLI args and JSON file. `cpu_core` (`--core`) is in the JSON and the web UI (Settings > Advanced > CPU core) and is reset to the backend's default when it doesn't fit (`normalize_cpu_core()`). The web UI has built-in profiles `68k`, `se`, `ppc` and `exec` (client.js `ROM_PROFILES`), shown when their ROM and disk are in storage. CLI args override at runtime but are never saved. UI changes go through `merge_ui_json()` which updates both runtime config and `file_config_` (what gets persisted). Flat JSON format; backend token (`uae`/`kpx`) determines architecture; legacy `architecture`+`cpu_backend`+`m68k.*`+`ppc.*` keys are coerced on load for one release of backward compat.
- **Triple buffer video**: CPU writes frames, encoder reads them, screenshot API reads them — all lock-free via atomic indices.
- **Single-port HTTP + WebSocket**: HTTP/1.1 server in `src/webserver/http_server.cpp`: a QThread accept loop (POSIX `accept` on the listening fd) hands each connection to a pool of 16 QThread workers, which read and answer on the fd with `poll()`-bounded `recv`/`send`. API handlers run one at a time under a lock, except routes registered with `allow_concurrent` (the bridge's, `/api/wait`), so a long `/api/exec` or `/api/copy` doesn't hold up the rest. On `Upgrade: websocket` the handshake is written and the fd is `dup()`'d off to a thread that owns the WebSocket. Long-poll stream routes (`/api/stream`) use the same fd-handoff pattern. `src/webserver/websocket.cpp` implements RFC 6455 in-process on the dup'd fd. One TCP listener serves static UI, REST API, `/api/frame` long-poll, and `/ws` (signaling + input + PNG/WebP frames). libdatachannel's `rtc::WebSocketServer` is not used. WebRTC RTP (H.264/VP9 media + Opus audio) still rides direct UDP ports negotiated via ICE.
- **Three transport modes**: PNG/WebP → WebSocket binary on `/ws`; H.264/VP9 → WebRTC RTP track; `httpstream` → `/api/frame` long-poll. Signaling JSON + input events always ride the `/ws` WebSocket regardless of codec.
- **Command bridge**: Two layers. **Read commands** (`/api/app`, `/api/windows`) peek Mac memory directly from the IRQ — no guest cooperation. **Action commands** (`/api/launch`, `/api/exec`, `/api/shutdown`, `/api/restart`, `/api/quit`, …) go to a guest-side `BridgeAgent` app (the emulator launches it from the bridge dir on the Host volume at desktop time; the boot disks aren't written) through the bridge trap `$ABE5` (`guest_bridge.h`; the request slot is in the IPC buffer), which wakes it with a Time Manager task; it executes them via Process Manager / Shutdown Manager / AppleEvents and answers through the trap. `/api/status` `bridge_agent_connected` says it's there. Enable with `--bridge` or `bridge_enabled: true`. See `docs/CommandBridge.md`.

## ROM

Tests expect a Quadra 650 ROM at `~/roms/quadra.rom`, a G3 ROM at `~/storage/roms/g3.rom`, and boot copies (`tests/lib/refresh_test_disk.sh`) of the images `provisioning/build.py` makes: `tests-7.5.5`, `tests-7.6.1` (built on demand from the snapshots in `~/storage/baked`); 9.0.4 and 6.0.8 are still the hand-made `.orig` baselines. Override with `MACEMU_ROM` / `MACEMU_DISK` env vars or `cmake -B build -DTEST_ROM=/path/to/rom`. ROMs and disk images are not distributed.

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
| OpenSSL | `libssl-dev` | `brew install openssl` | DTLS/SRTP for libdatachannel (WebRTC). Our code no longer uses it directly — `QCryptographicHash` handles SHA1/MD5. |
| pkg-config | `pkg-config` | `brew install pkg-config` | Dependency detection |
| Qt6 (≥6.4) | `qt6-base-dev` | `brew install qt@6` | Core, Network (QLocalSocket / QTcpServer), Widgets, plus QJsonDocument for all JSON parsing. 6.4 minimum is set by MacBrowser's use of `QWebEngineDownloadRequest` + its `receivedBytesChanged`/`isFinishedChanged` signals. See [docs/qt6/PLAN.md](docs/qt6/PLAN.md) for the port history. |
| Boost, Ruby, Bison, Perl | `libboost-filesystem-dev libboost-program-options-dev ruby bison perl` | `brew install boost ruby bison` | Executor backend only. If missing, the build skips it with a warning; `-DBUILD_EXECUTOR=OFF` disables it. |
| Qt6 WebEngine (≥6.4) | `qt6-webengine-dev` | included in `brew install qt@6` | Only with `-DBUILD_BROWSER=ON` (experimental MacBrowser; off by default). In-process Chromium for MacBrowser |

## MacBrowser

**Experimental, off by default.** Configure with `-DBUILD_BROWSER=ON` (needs
Qt6 WebEngine); that defines `MACPHOENIX_BROWSER=1` and compiles
`src/drivers/browser/`. Without it `--browser` and `browser_enabled` are
ignored with a warning and `/api/config` reports `browser_available: false`.

`--browser` runs an in-process Chromium via Qt6 WebEngine and pipes
its rendered pixels into a guest Mac app called **MacBrowser**. The
guest app provides a native Mac chrome (toolbar, URL bar, V/H
scrollbars) over a 1:1 pixel viewport; clicks/keys/scroll are
forwarded as synthesized `QMouseEvent` / `QKeyEvent` / `QWheelEvent`
posted into the hidden `QWebEngineView`. See
[`docs/MacBrowser.md`](docs/MacBrowser.md) for the architecture
deep-dive (memory layout, ring protocol, capture pipeline).

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
| `BR_CMD_NAV` | Navigate to URL — `QWebEnginePage::load` |
| `BR_CMD_CLICK` / `_MOUSE_MOVE` / `_MOUSE_OUT` | Pointer input (host-polled, mostly) |
| `BR_CMD_KEY_DOWN` / `_KEY_UP` | Key events; mods passed through, special keys remapped to W3C codepoints |
| `BR_CMD_SCROLL` | Wheel scroll, dx/dy in CSS px |
| `BR_CMD_BACK` / `_FORWARD` / `_RELOAD` / `_STOP` | Toolbar nav |
| `BR_CMD_RESIZE` | Window grew/shrunk → resize the QWebEngineView |
| `BR_CMD_GET_SELECTION` / `_PASTE` / `_SELECT_ALL` | Clipboard + select-all bridge |
| `BR_CMD_ZOOM_IN` / `_ZOOM_OUT` / `_ZOOM_RESET` | CSS-zoom step |

| Event (h2g) | Description |
|---|---|
| `BR_EV_STATUS` | Loading / Ready / Error + URL — drives URL bar |
| `BR_EV_FRAME` | Framebuffer updated — guest re-blits |
| `BR_EV_SELECTION` | Reply to GET_SELECTION → write to TEScrap |
| `BR_EV_PAGE_METRICS` | page_w/h, scroll_x/y, viewport_w/h — drives V/H scrollbar thumb + active state |
| `BR_EV_DOWNLOAD` | (M7, planned) download start/progress/done |
