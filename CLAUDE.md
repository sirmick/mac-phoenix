# MacPhoenix

Classic Mac emulator with a web UI. Two kinds of machine:

- The ROM machines (`src/`): a 68k Mac (Quadra 650 ROM; also SE, IIci) and a
  Power Mac G3, booting Apple's Systems 6.0.8 to 9.0.4.
- Executor (`executor/`, its own product: `executor/README.md`): the Toolbox
  in C++, no ROM. It runs in mac-phoenix (`--backend executor`), as a desktop
  application (`executor-desktop`), and as WebAssembly in a browser tab served
  by a Go server (`executor-standalone`).

## Quick Reference

```bash
# Configure, build (emulator, executor-desktop, net-bridge's Rust NAT); -j4 at most here
cmake -B build                       # -DTEST_ROM=PATH  -DBUILD_NET_BRIDGE=OFF (no cargo)
cmake --build build -j4

# Run (web UI on http://localhost:11000), or headless and timed
./build/mac-phoenix ~/roms/quadra.rom
./build/mac-phoenix --timeout 10 --no-webserver ~/roms/quadra.rom
./build/mac-phoenix --backend kpx --core mame-ppc --jit --rom ~/storage/roms/g3.rom --disk IMG

# Tests (labels: api boot bridge executor guest mame provisioning tools unit; ctest --print-labels)
ctest --test-dir build -j4
ctest --test-dir build -L api
ctest --test-dir build -R api_endpoints -V
npx playwright test                  # tests/e2e, against a running emulator
python3 tests/lib/ports.py held      # test ports claimed now: each test claims free ones (docs/Testing.md "Ports")

# Executor in mac-phoenix (docs/executor/PLAN.md): mame-68k only, no ROM.
# "Executor" in the web UI, or:
./build/mac-phoenix --backend executor                                  # its own System
./build/mac-phoenix --backend executor --executor-system 7.5.5.img      # Apple's System and Finder
./build/mac-phoenix --backend executor --executor-system 8.6.img --executor-ppc   # fat apps' PowerPC code
ctest --test-dir build -L executor
# Open Transport is Executor's own C++, on by default (docs/executor/OpenTransport.md;
# ctest -R 'ot_'); --no-executor-ot leaves applications to MacTCP. -DEXECUTOR_NET=none
# builds MacTCP/OT with no network (the Windows build's default; wasm: the relay).
# CFM-68K on Executor's Code Fragment Manager (docs/executor/CFM68K.md; ctest -R guest_suite_cfm68k).
# FAT disk images through PC Exchange (docs/executor/FSM.md; ctest -R pcexchange_executor;
# tests/lib/fat_image.py). StdCLib/MathLib for PowerPC in C++ (docs/executor/PPCLibs.md;
# ctest -R 'clib_|guest_suite_ppc_executor'). Heaps that fill: docs/executor/FullHeap.md.
# Calls Executor lacks: "[unimplemented]" in the log, and ~/storage/executor/unimplemented.log.

# HiDPI (docs/executor/HiDPI.md): prebuilt Skia, off by default
tools/skia/fetch.sh && cmake -B build -DEXECUTOR_SKIA=ON
./build/mac-phoenix --backend executor --executor-system 8.6.img --executor-ppc --executor-scale 2

# Executor tools
tools/executor/grid.py 8.6.img --jobs 3        # every app/control panel, one session each
                                               # -> ~/storage/executor-grid/8.6/grid.md (docs/executor/MacOS8-9.md)
tools/executor/grid_compare.py OLD-GRID NEW-GRID
tools/difftest/run.py appr [--dump]            # Apple's 8.6 libraries vs Executor's (docs/executor/DiffTest.md;
                                               # probes in tests/guest/difftest; ctest -R difftest_)
tools/macdecode/libraries.py --census --rom ~/storage/roms/g3.rom --system FOLDER --logs GRID-DIR
./build/mac-phoenix --backend executor --executor-system 8.6.img --executor-ppc \
    --executor-ppc-break '!AESend'             # docs/executor/PPCTrace.md; ctest -R ppc_break_executor
# Executor's API definitions (executor/core/multiversal/defs) are also Retro68's
# Multiversal Interfaces (docs/executor/LIBRARIES.md): ctest -R multiversal_retro68;
# RetroTest (tests/guest/retro68) on Executor and Apple's Systems: ctest -R retro68_
tools/retro68/automated_tests.py               # Retro68's AutomatedTests (ctest -R retro68_automated_executor)

# Executor desktop application (executor/frontends/desktop/README.md): Qt 6 Widgets
./build/executor-desktop [--system ~/storage/web-library/boot-8.6.img --ppc]
QT_QPA_PLATFORM=offscreen ./build/executor-desktop --data DIR --screenshot s.png --quit
ctest --test-dir build -R executor.desktop
cmake --build build --target executor-desktop-appimage   # tools/desktop/appimage.sh; --test APPIMAGE: debian:stable

# Windows, cross-built here (cmake/ExecutorWindows.cmake, tools/windows; toolchain and
# Qt in ~/storage/deps/windows): exe in build-windows/, zip in ~/storage/tmp/windows-try
tools/windows/build.sh && tools/windows/package.sh
tools/windows/smoke.sh ZIP ~/storage/tmp/win-smoke --system ~/storage/web-library/boot-8.6.img --ppc   # Wine, docker
tools/windows/symbolize.sh crash-STAMP.txt [unstripped executor-desktop.exe]

# WebAssembly (docs/executor/DIRECTION.md): emsdk in ~/emsdk
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm && cmake --build build-wasm -j4
ctest --test-dir build-wasm -L executor        # gtests under node, executor.web.* cells
# executor.web.* need go and -DEXECUTOR_WEB_NODE_PATH=<node_modules with playwright>
# (-DEXECUTOR_WEB_CHROMIUM=<browser> if not playwright's own).
# The Go server with the tab embedded, the tab's only server (docs/executor/Standalone.md):
make -C executor/server WASM_BUILD=$PWD/build-wasm   # -> executor/server/executor-standalone
make -C executor/server test                         # go test
executor/server/executor-standalone --data DIR [--images DIR] [--port 8090]   # -h: all flags

# MPW (SC/Link/Rez) in a headless Executor session (docs/MPW.md)
tools/mpw/mpw setup                    # once: MPW disk + System, from snapshots
tools/mpw/mpw build tests/guest/ot     # build.mpw; products -> DIR/build/*.bin
tools/mpw/mpw run 'Version'            # exit code = {Status}; tools/mpw/mpw stop when done
tools/mpw/mpw --toolchain cw build DIR # CodeWarrior Pro 4's MPW (MWC68K, MWCPPC)

# Guest software (docs/Provisioning.md): pinned media (provisioning/media.json) ->
# bakes in the guest -> snapshots (~/storage/baked) -> images (provisioning/images/*.json)
python3 provisioning/bake.py --all --jobs 3          # snapshots not up to date
python3 provisioning/build.py --all                  # images not up to date
python3 provisioning/test_image.py dev-7.6.1 dev-tools-system-7   # launch every app
python3 provisioning/bake.py NAME --explore --record # work out a bake; --do / --finish
tools/web/make_library.py                            # images -> ~/storage/web-library (the tab's)

# arm64 / riscv64 in Debian containers under qemu (build-arm64/, build-riscv64/)
tools/xarch/xarch.sh arm64                     # configure + build
tools/xarch/xarch.sh arm64 ctest -R cpu_cores
tools/xarch/xarch.sh riscv64 shell

# Boot matrix: every backend x core x JIT x OS cell -> CSV, screenshots, logs
tests/run_boot_matrix.sh --out ~/storage/tmp/boot-matrix [--only LABEL,...]
```

## CPU Backends

`--backend` picks the machine and so the architecture; `--core` the CPU core.

| Backend | Arch | Cores | What |
|---------|------|-------|------|
| `uae` (default) | m68k | `uae` (interpreter, `--jit`), `mame-68k` (MAME's Musashi: PMMU, softfloat FPU; `musashi` names it too) | ROM-based 68k Mac, ~5 s to the desktop |
| `kpx` | ppc | `kpx` (Kheperix, `--jit`), `mame-ppc` (MAME's PowerPC recompiler: `--jit` native x86-64/arm64 back-end, `--no-jit` UML interpreter) | Power Mac G3; `--core mame-ppc --jit` boots 7.5.5 in 0.9 s, 9.0.4 in 9.7 s |
| `executor` | m68k | `mame-68k` only; PowerPC code on `mame-ppc` | Executor's Toolbox, no ROM |

MAME cores are in `src/cpu/mame/` (mame-ppc's installer: `src/cpu/kpx/cpu_ppc_mame.cpp`;
reference checkout `/home/mick/src/mame-src`). The goal is MAME cores only
(docs/cpu/PLAN.md, open items in its Stragglers section). `--m68k-host 1` runs
host-initiated 68k routines on a mame-68k core on the PPC machine (C3a); the
main 68k thread is still the nanokernel's emulator. Tests:
`ctest -R 'cpu_cores|boot_core_mame68k|boot_ppc_mame|guest_suite_ppc_mame|command_bridge_ppc_mame_m68k'`.

## Project Structure

```
src/                                 — the ROM machines and mac-phoenix's server
  main.cpp                           — entry point, threads; the emulator runs in a child process
  config/emulator_config.cpp         — config: CLI flags, JSON, save/load, --help
  core/                              — boot_progress, rom_patches, emul_op (EmulOps), command_bridge,
                                       emulator_subprocess.h (child IPC: SHM + socket), adb, cpu_context,
                                       text_feed (/api/text)
  cpu/                               — uae_cpu/, kpx/, mame/; cpu_m68k.cpp (the ROM 68k Mac on a cpu::Core);
                                       core/: cpu::Core, one interface for every core; core/platform_cpu.cpp
  drivers/                           — video (triple buffer, encoders, WebRTC), audio, platform (disk I/O,
                                       ExtFS, timer), browser/ (MacBrowser, -DBUILD_BROWSER=ON)
  webserver/                         — http_server, websocket, api_handlers (/api/*), file_scanner (storage)
  webrtc/webrtc_server.cpp           — /ws signaling, H.264/VP9 + Opus RTP
  common/include/platform.h          — g_platform; guest_bridge.h (trap $ABE5)
executor/                            — Executor (MIT); its directory table: executor/README.md
  core/                              — cpu/ (syn68k API on cpu::Core, PowerCore on mame-ppc), romlib/ (the
                                       Toolbox), multiversal/ (API definitions -> generated headers), res/, third_party/
  frontends/                         — phoenix/ (executor_host API; executor_child.cpp: --backend executor),
                                       wasm/ (the tab's module), desktop/ (executor-desktop), headless/
  host/                              — shared by the tab and the desktop app: session, disk images, overlays, requests
  tests/                             — gtest suite (label executor); web/: the tab's node/Playwright cells
  web/                               — the tab (JS/CSS/HTML)
  server/                            — executor-standalone (Go)
  SHOWCASE.md                        — Executor at 2x on Apple's free 7.5.3 and freeware
BridgeAgent/                         — the guest agent for action commands (Retro68; .bin committed)
MacBrowser/                          — MacBrowser guest app (experimental)
client/                              — mac-phoenix's browser UI (vanilla JS)
provisioning/                        — media, bakes, image definitions (docs/Provisioning.md)
tests/                               — test_*.sh cells, lib/, guest/ (MacPhoenixSuite, OT, difftest probes,
                                       retro68 ...), e2e/ (Playwright)
tools/                               — executor/, difftest/, macdecode/, mpw/, retro68/, skia/, web/, desktop/,
                                       windows/, xarch/, macfetch/, ...
cmake/                               — ExecutorWasm.cmake, ExecutorWindows.cmake, ...
docs/                                — README.md indexes them; API.md, Architecture.md, executor/, cpu/
```

## API Endpoints

mac-phoenix's main endpoints; all of them, with parameters: `docs/API.md`.
executor-standalone's API: `docs/executor/Standalone.md`.

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | `emulator_running`, `boot_phase`, `boot_elapsed`, `bridge_agent_connected`, `annotations`, ... |
| `/api/screenshot` | GET | the screen as PNG (`?scale=1`: Executor's 1x screen) |
| `/api/mouse` | GET/POST | cursor `{x, y}`; POST `{"x","y"}` or `{"dx","dy"}`, `"button"`/`"down"` |
| `/api/keypress` | POST | `{"key":"return"}` or a key code `{"key":36}` |
| `/api/config` | GET/POST | the JSON config (docs/JsonConfig.md) |
| `/api/emulator/start`, `/stop`, `/restart` | POST | the machine |
| `/api/storage` | GET | ROMs, disk images, Executor Systems |
| `/api/app`, `/api/ui` | GET | read commands: front application; its windows, controls, dialog items, menus |
| `/api/text` | GET | text on screen as QuickDraw drew it (ROM machines; `?since=N`: the draw log) |
| `/api/wait` | POST | until `boot=Finder`, `app=Name` or `text=Substring` holds |
| `/api/launch`, `/api/quit`, `/api/script`, `/api/exec`, `/api/copy`, `/api/shutdown`, `/api/restart` | POST | action commands through BridgeAgent |
| `/api/annotate` | POST | a line shown over the screen (provisioning posts each step) |
| `/api/snapshot` | POST | guest RAM and ROM to `<storage>/snapshots/NAME/` (tools/macdecode) |
| `/api/executor/ppc-break` | GET/POST | Executor's PowerPC breakpoints and hits |

## Boot Phases

`boot_progress.cpp`, in `/api/status`:
`pre-reset` → `ROM init` → `boot globs` → `drivers` → `warm start` → `boot blocks` → `extensions` → `Finder` → `desktop`

## CLI Flags

`./build/mac-phoenix --help` (copy it here again when flags change). `executor-desktop --help` and
`executor-standalone -h` have their own (executor/frontends/desktop/README.md,
docs/executor/Standalone.md).

```
Usage: ./build/mac-phoenix [options] [rom-path]

Machine:
  --rom PATH                 ROM file (or positional arg)
  --ram MB                   RAM size in megabytes (default: 64)
  --screen WxH               Display resolution (default: 640x480)
  --depth N                  68k and Executor: colour depth the screen starts in, bits
                             (1 2 4 8 16 32; default 32, Executor 32 on 8.x and 8 on 7.x;
                             8 for 256-colour games)
  --disk PATH                Disk image (repeatable)
  --cdrom PATH               CD-ROM image (repeatable)
  --extfs PATH               Shared folder (repeatable; the ROM machines mount only the first)
  --bootdriver N             0=any, -62=CD-ROM (default: 0)
  --storage-dir PATH         Default storage root (default: ~/storage)
  --executor-data PATH       Executor data dir (default: <storage>/executor)
  --executor-app PATH        App Executor starts instead of Browser (host path)
  --executor-system IMAGE    Run on the System Folder of <storage>/images/IMAGE (copied
                             to <storage>/executor-systems on first use; image mounted too)
  --executor-start WHAT      finder | browser | path inside the System (default: finder)
  --[no-]executor-fresh      Start each run from the System's clean copy (default: on)
  --executor-logtraps        Executor logs every trap call to stderr
  --executor-debug OPTS      Executor's own debug options, comma-separated: memcheck (heap
                             consistency checks at every Memory Manager call, slow), allocs
                             (allocations >= 4K and the code that asked), pcsample (where
                             PowerPC code spends its time, every 5 s), hidpi (the text
                             --executor-scale redraws, and what became of it), ppcinterp
                             (mame-ppc's UML interpreter, not its native back-end), mmu68k
                             (mame-68k with its PMMU, no tables set up), mmuppc (mame-ppc
                             translating every access, identity), fslog,
                             trace, trapfailure, unimplemented, unexpected, errno, sound, all
  --watch ADDR[:LEN]         Report every write to guest memory ADDR (hex; LEN 1-16, default 4):
                             old/new value, 68k PC and registers, last traps, host stack (Executor)
  --executor-ppc-break SPEC  PowerPC breakpoint (Executor; repeatable; docs/executor/PPCTrace.md):
                             WHERE[@N][*HITS][#MEM], WHERE $ADDR, FRAGMENT[:SECT]+OFF (hex) or
                             !Routine (Executor's); on a hit registers, back chain, stack, MEM
                             (rN+OFF:LEN, $ADDR:LEN, ^ reads through), then N instructions traced;
                             HITS hits reported (default 1, 0 all). "on": none yet, for the API
  --executor-logtraps-nesting N  ... and calls nested N deep in Toolbox callbacks (default 1)
  --executor-logtraps-filter LIST  ... only calls whose names match these comma-separated
                             wildcards ("Gestalt,HideWindow,PB*"): less slowdown;
                             -NAME leaves calls out ("-WaitNextEvent,-Get*")
  --executor-writable-images Executor mounts disk images read-write (default: read-only;
                             not the --executor-system image unless --disk names it too)
  --[no-]executor-ot         Executor answers Open Transport (default: on)
  --executor-ppc             Executor runs fat applications' PowerPC code (PowerPC-only
                             applications always run it; default: off)
  --executor-scale X         Executor's display scale (2: text drawn from outlines at
                             twice the size; 1x for programs; needs -DEXECUTOR_SKIA=ON)

CPU:
  --backend NAME             uae | kpx | executor
                             (default: uae)
  --core NAME                CPU core for the backend's architecture:
                             68k uae | mame-68k (default: uae; musashi = mame-68k)
                             ppc kpx | mame-ppc (default: kpx)
  --jit / --no-jit           Enable backend's primary JIT (uae, kpx)
  --m68k-host N              mame-ppc only: 0 off; 1 host-initiated 68k routines on a
                             mame-68k core; 2 also A-line traps (experiment, breaks the bridge);
                             3 all 68k code on mame-68k (C3b, experimental, does not boot yet)
  --idlewait / --no-idlewait Pause CPU when guest idle (default: on)

Media:
  --audio                    Send sound to the browser (default: off; the Mac has sound hardware either way)
  --audio-dump PATH          Append the audio sent out (raw S16LE 48 kHz stereo) to PATH
  --zap-pram                 Clear PRAM on startup
  --model-id N               Machine type reported to the Mac (default: the ROM
                             machine's; 5 = IIci for Systems before 7.5)
  --dismiss-shutdown-dialog  Auto-dismiss improper-shutdown dialog

Networking:
  --network MODE             none | socket[:PATH] (default: none)

Serial:
  --serial-a PATH            Port A: 'pty' (auto-allocate),
                             /dev/pts/N or /dev/ttyUSB0 (open path),
                             'none'/'off' or omit (disabled)
  --serial-b PATH            Port B (same syntax as --serial-a)

Automation:
  --bridge                   Enable automation bridge
  --browser                  Run MacBrowser (experimental; needs a -DBUILD_BROWSER=ON build)
  --headless-http            HTTP API only (no video/audio)

Server:
  --port N                   HTTP+WS port (default: 11000)
  --no-webserver             Headless, no HTTP/WebRTC
  --timeout N                Auto-exit after N seconds
  --config PATH              JSON config file
  --screenshots              Dump PPM frames to /tmp

Logging:
  --log-level N              0–3
  --debug-connection
  --debug-mode-switch
  --debug-perf
  --debug-network
  --debug-mame-ppc           mame-ppc: log host ops, exceptions, a per-second CPU sample
  --cpu-mmu                  The MMU's cost (docs/executor/Speed.md): mame-68k with its PMMU
                             (the ROM's tables), mame-ppc translating every access (identity)
  --trace-atraps             Record every (A-trap, caller PC); snapshots write atraps.tsv
  --no-text-feed             Don't patch _StdText (the strings on screen for /api/text)
  --deterministic            Virtual clock (instruction-counted ticks, fixed date); implies --no-idlewait
  --snapshot-at NAME@SECS    With --deterministic: snapshot SECS virtual seconds after Finder starts
  -h, --help                 Show this help message
```

`--executor-debug` also takes `drag` (the Drag Manager's dispatch and the
Finder's hook calls).

## Environment Variables

Configuration is CLI flags and JSON only. The binary reads environment
variables only as debug switches:

| Var | Scope | What |
|-----|-------|------|
| `MACEMU_ROM`, `MACEMU_DISK` | test scripts | ROM and boot disk (not read by the binary) |
| `MACEMU_PPC_NO_IRQ`, `MACEMU_PPC_TRACE_TRAP`, `MACEMU_PPC_TRACE` | PPC machine | Kheperix-era debug switches |
| `CPU_TRACE`, `CPU_TRACE_QUIET`, `CPU_TRACE_MEMORY` | 68k | instruction trace (`src/cpu/cpu_trace.c`) |
| `MACEMU_DEBUG_FRAMES`, `MACEMU_DEBUG_AUDIO`, `MACEMU_TRACE_DISKPRIME` | all | frame, Opus and disk-read logging |

## ROMs and Test Images

Tests expect the Quadra 650 ROM at `~/roms/quadra.rom`, the G3 ROM at
`~/storage/roms/g3.rom` and the IIci's at `~/storage/roms/iici.rom`
(`~/storage/roms` and `~/storage/images` are the private assets repo:
never commit built images). Boot disks are copies made per test run by
`tests/lib/refresh_test_disk.sh NAME $$` (`test-NAME-PID.img`, removed at
exit, so `ctest -j` is safe) of images `provisioning/build.py` makes:
`tests-7.5.5`, `tests-7.6.1`, `tests-9.0.4`, `6.0.8` (SE and IIci), built on
demand from `~/storage/baked`. Override with `MACEMU_ROM` / `MACEMU_DISK` or
`-DTEST_ROM=`. ROMs and disk images are not distributed.

## Licensing

The whole is GPL-2.0-or-later (UAE, Basilisk II, SheepShaver code); code
written for MacPhoenix is also MIT (`LICENSE-MIT`), Executor and our
changes to it are MIT (`executor/LICENSE-CONTRIBUTIONS`), MAME cores
BSD-3. Path-by-path map: `LICENSING.md`. New code is written from
documentation and behaviour, never adapted from GPL source; code adapted
from a GPL file keeps its GPL header and goes in LICENSING.md's GPL table.
Executor's origin and what we changed: `docs/executor/README.md`
(`tools/executor-upstream-diff.sh` compares with upstream).

## Key Architectural Decisions

- **Platform API**: every machine implements the `g_platform` function table; shared code reaches CPU and memory only through it.
- **Child process**: the emulator runs in a child (`--ipc`); the server talks to it over shared memory and a socket (`emulator_subprocess.h`). Stopping kills the child.
- **68k memory**: one block, `[RAM][ROM 1 MB][ScratchMem 64 KB][frame buffer 8 MB]` (`cpu_context.cpp`): the frame buffer is outside RAM.
- **PPC memory**: Kheperix uses identity addressing (Mac address = host address; needs `vm.mmap_min_addr=0`). mame-ppc puts guest memory in a 4 GB-aligned 4 GB reservation at `VMBaseDiff`, no sysctl.
- **EmulOps**: ROM patches insert `0x71xx` opcodes handled on the host (I/O, drivers, system calls).
- **Config**: `EmulatorConfig`, flat JSON. CLI flags override at runtime and are never saved; UI changes go through `merge_ui_json()` (runtime and persisted config). `cpu_core` is reset to the backend's default when it doesn't fit (`normalize_cpu_core()`). The web UI's built-in profiles (client.js `ROM_PROFILES`, OS-machine, oldest first: `6.0.8-se` ... `9.0.4-dev-ppc`, and `7.5.5-executor`) show when their ROM and disks are in storage. Disks show the title, tags and note of their record (`NAME.img.json`, from `provisioning/build.py`). `model_id` (client.js `MACHINE_TYPES`) only for 1 MB 68k ROMs.
- **Video**: a lock-free triple buffer; the encoder and the screenshot API read it.
- **One port**: `src/webserver/http_server.cpp`, a POSIX accept loop and a pool of worker threads. Handlers run one at a time under a lock, except routes registered `allow_concurrent` (bridge commands, `/api/wait`). WebSocket upgrades and long-poll streams (`/api/stream`) are handed off on a `dup()`'d fd; `websocket.cpp` is RFC 6455 in-process. The port serves the UI, the API, `/api/frame` and `/ws`.
- **Transports**: PNG/WebP frames as `/ws` binary messages (sound as `"OPUS"`+packet there, client.js `WsAudioPlayer`); H.264/VP9 and Opus as WebRTC RTP over ICE-negotiated UDP; `/api/frame` long-poll. Signaling and input always on `/ws`.
- **Command bridge** (docs/CommandBridge.md): read commands (`/api/app`, `/api/ui`) peek guest memory; action commands go to BridgeAgent, launched from the bridge folder on the Host volume at desktop time (boot disks not written), over trap `$ABE5` (`guest_bridge.h`), woken by a Time Manager task. `--bridge` or `bridge_enabled`.
- **Executor front ends**: all run Executor through `executor/frontends/phoenix` (`executor_host.h`); the tab and the desktop app share `executor/host` (session, disks, requests).
- **Disk images never written by the tab or desktop app**: the Mac's changes go to an overlay of changed 4 KB blocks (`executor/host/overlay.h` over `core/romlib/hfs/image_io.h`), the same format as `executor/server/overlay.go`, so each reads the other's.
- **executor-standalone**: one Go binary per OS with the tab embedded, the tab's only server: images only, no accounts; the first browser to open it claims it (cookie); it keeps the overlays and settings.
- **Windows**: the desktop app and executor-standalone, cross-built on Linux with llvm-mingw (cmake/ExecutorWindows.cmake): no network, no Skia, none of mac-phoenix itself.

## Dependencies

Required: CMake, pkg-config, OpenSSL (`libssl-dev`: libdatachannel's DTLS/SRTP),
Qt 6.4+ (`qt6-base-dev`: Core, Network, Widgets; executor-desktop needs Widgets,
`-DEXECUTOR_DESKTOP=OFF` leaves it out). Optional: cargo (net-bridge),
Ruby + Bison + Perl (only for `-DEXECUTOR_REGENERATE=ON`, after editing
multiversal YAML, `.map` files, `prefs/parse.ypp` or blitter templates;
the output is committed), Retro68 (rebuilds BridgeAgent and MacBrowser;
the committed `.bin`s work without), Qt 6 WebEngine (only `-DBUILD_BROWSER=ON`).
`-DBUILD_EXECUTOR=OFF` leaves Executor out.

Codecs, detected at configure time (missing: not offered in the UI):
OpenH264 (`libopenh264-dev`, H.264), libvpx (`libvpx-dev`, VP9), libwebp
(`libwebp-dev`), Opus (`libopus-dev`, sound), libyuv (`libyuv-dev`). PNG is
always there.

## MacBrowser

Experimental, off by default: `-DBUILD_BROWSER=ON` (Qt 6 WebEngine) builds
`src/drivers/browser/`; otherwise `--browser` is ignored with a warning and
`/api/config` says `browser_available: false`. An in-process Chromium renders
into the guest app MacBrowser (native chrome over a 1:1 viewport; input
through the DevTools protocol, `cdp_client.cpp`). Run with
`--browser --bridge --disk MacBrowser/MacBrowser.dsk ROM` and open it on the
floppy. Architecture and the host/guest ring protocol: `docs/MacBrowser.md`.
