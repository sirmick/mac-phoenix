# mac-phoenix

Classic Mac emulator with multiple CPU backends and a web-based streaming UI.
Derived from BasiliskII / SheepShaver but cleanly separated from their legacy
plumbing.

The canonical project overview lives in [`../CLAUDE.md`](../CLAUDE.md) — flags,
endpoints, project structure, and a one-glance summary of every subsystem.
This folder contains the deeper-dive docs.

## What works

- **Mac SE** (68000) boots System 6 to Finder — 512×342 monochrome.
- **Quadra 650** (68040) boots Mac OS 7.5.5 / 7.6.1 to Finder.
  - UAE backend: ~5 s (~3 s with `--jit`).
  - Unicorn-m68k backend: ~12 s.
- **Power Mac G3** (PPC 750) boots Mac OS 7.5.5 / 7.6.1 to Finder under KPX
  (~45 s, default for PPC). The dyngen JIT (`--jit`) is compiled but
  blocked by a GCC codegen difference.
- **Unicorn-PPC** reaches Finder under 7.6.1 but is unstable — see
  [`ppc/UnicornPpcStatus.md`](ppc/UnicornPpcStatus.md). Not the default.
- HTTP API + WebRTC streaming, file-based automation bridge (BridgeAgent),
  MacBrowser (in-process Chromium via Qt6 WebEngine, piped into a guest
  Mac app), guest-side networking via the Rust net-bridge.

Audio output is still a stub (`src/drivers/audio/audio_null.cpp` —
encoder thread infrastructure exists but isn't wired to a real Sound
Manager hook).

## Why

- **Modern host integration.** WebRTC streaming, HTTP API, browser
  client. Drive the emulator from any modern stack; no native UI per OS.
- **Multiple CPU backends behind one Platform API.** UAE (m68k default),
  Unicorn-m68k (QEMU TCG, validation), Unicorn-PPC (experimental), KPX
  (PPC default), DualCPU (lockstep). New backends don't touch core code.
- **Programmable.** BridgeAgent for automation, MacBrowser for the modern
  web inside System 7, ExtFS for host filesystem access without restarting.
- **Validation built in.** DualCPU runs UAE and Unicorn-m68k in lockstep
  and fails fast on register divergence.

We are explicitly **not** chasing cycle accuracy, every Mac model
(focus is SE, Quadra 650, Beige G3), or replacing BasiliskII /
SheepShaver for end users — this is a research + preservation project.

## Quick start

```bash
cmake -B build && cmake --build build -j$(nproc)

# Quadra 650, UAE backend (default), web UI on :11000
./build/mac-phoenix ~/storage/roms/quadra.rom

# Power Mac G3, KPX backend
./build/mac-phoenix --backend kpx --rom ~/storage/roms/g3.rom \
                    --disk ~/storage/images/macos-7.6.1.img --ram 128

# Mac SE
./build/mac-phoenix ~/storage/roms/mac-se.rom --disk ~/storage/images/system6.img
```

There is no `--arch` flag — the `--backend` token determines the CPU
architecture (`uae` / `unicorn-m68k` / `dualcpu` → m68k,
`kpx` / `unicorn-ppc` → ppc).

## Documentation

### Reference
- [Architecture.md](Architecture.md) — Platform API, backends, memory, traps, IRQs.
- [Commands.md](Commands.md) — Build, run, test, debug commands.
- [JsonConfig.md](JsonConfig.md) — Config file schema.
- [DeveloperGuide.md](DeveloperGuide.md) — Backend internals, common dev tasks.
- [Testing.md](Testing.md) — CTest + Playwright suites.
- [TroubleshootingGuide.md](TroubleshootingGuide.md) — Quick diagnostics.

### Subsystems
- [CommandBridge.md](CommandBridge.md) — Read commands (peek Mac mem) +
  action commands (BridgeAgent file-based dispatch).
- [ThreadingArchitecture.md](ThreadingArchitecture.md) — Process + thread model.
- [LatencyShortcomings.md](LatencyShortcomings.md) — Known latency cliffs in
  the input/video paths.
- [UnicornPerformanceAnalysis.md](UnicornPerformanceAnalysis.md) — Unicorn-m68k
  vs UAE perf breakdown.

### PowerPC
- [ppc/README.md](ppc/README.md) — Backends, memory layout, execution model,
  ROM patching, networking, file map.
- [ppc/UnicornPpcStatus.md](ppc/UnicornPpcStatus.md) — Unicorn-PPC live status,
  debug knobs, known crashes.

- [MacBrowser.md](MacBrowser.md) — MacBrowser host pipeline + guest
  app architecture.

### Deep dives
- [deepdive/README.md](deepdive/README.md) — Index.

## Project layout

```
mac-phoenix/
├── src/
│   ├── common/include/    # Shared headers (platform.h, MacBrowser.h, ...)
│   ├── core/              # Mac managers, command_bridge, rom_patches, etc.
│   ├── cpu/               # uae/, kpx/, cpu_unicorn{,_ppc}.cpp, dualcpu, traces
│   ├── drivers/           # video, audio, browser, ether, serial, scsi, platform
│   ├── webrtc/            # WebRTC server (signaling + RTP)
│   ├── webserver/         # HTTP server, /ws WebSocket, API handlers
│   └── config/            # EmulatorConfig, machine profiles
├── client/                # Browser UI (vanilla JS)
├── BridgeAgent/           # Guest m68k automation agent (Retro68 source + .bin)
├── MacBrowser/            # Guest m68k browser app (Retro68 source + .bin)
├── tests/                 # Shell + Playwright + unit tests
├── provisioning/          # Disk image creation/population scripts
├── subprojects/           # Unicorn (vendored) + patches, libdatachannel, json
└── docs/
```

## License

GPL v2 (based on BasiliskII / SheepShaver).
