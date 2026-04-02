# Project Goals and Vision

What we're building and why.

---

## Mission Statement

**Build a fast, maintainable Mac emulator supporting both M68K and PowerPC, with multiple CPU backends and web-based streaming.**

---

## Current Status (April 2026)

- **M68K**: Mac OS 7.5.5 boots to Finder with UAE (~5s) and Unicorn (~48s) backends
- **PPC**: Mac OS 9 boots to Finder with KPX interpreter (~45s)
- **Web UI**: WebRTC streaming with mouse/keyboard input, all codecs working
- **JIT**: UAE M68K JIT enabled (`--jit`/`--no-jit`); PPC dyngen JIT available but blocked by GCC codegen issue
- **Command Bridge**: App launch/quit, window list, boot phase polling all working via HTTP API

---

## CPU Backends

### UAE: The Default (M68K) ⭐

**Purpose**: Primary M68K backend for end users

**Why**: Proven, fast interpreter (decades of development). Now also has JIT compilation.

**Status**: Fully functional — fast boot, JIT support, stable

### Unicorn: The Experiment (M68K)

**Purpose**: QEMU-based JIT for M68K — validation and future performance work

**Status**: Boots to Finder (~48s, ~10x slower than UAE). Hook overhead reduced to 5.3% — the gap is structural (QEMU TCG M68K).

**Role**: Validation via DualCPU mode, research platform

### DualCPU: The Validator (M68K)

**Purpose**: Run UAE + Unicorn in lockstep, catch divergences immediately

**Status**: 514,000+ instructions validated with zero divergence

### KPX: The PPC Backend ⭐

**Purpose**: PowerPC emulation via Kheperix interpreter (from SheepShaver)

**Status**: Boots Mac OS 9 to Finder. All 40+ EmulOps, 38 NativeOps verified identical to legacy SheepShaver. Dyngen JIT compiled but blocked by GCC codegen difference.

---

## Architecture

### Key Design Principles

1. **Platform API Abstraction** — All backends implement the same `g_platform` function pointer table
2. **Meson Build System** — Fast, cross-platform builds
3. **Modular Drivers** — Adapter pattern with null defaults, runtime selection
4. **Web-First UI** — WebRTC streaming, HTTP API, browser client
5. **Continuous Validation** — DualCPU mode for M68K correctness

### Not Goals

- Cycle-accurate emulation (pragmatic over perfect)
- Support every Mac model (Quadra 650 for M68K, Gossamer G3 for PPC)
- Replace BasiliskII for all users (research + preservation project)

---

## Roadmap

### Phase 1: Core CPU Emulation ✅ COMPLETE

- UAE + Unicorn + DualCPU M68K backends
- EmulOps, A-line/F-line traps, interrupt support
- 514k+ instruction dual-CPU validation

### Phase 2: WebRTC Integration ✅ COMPLETE

- 4-thread architecture, all encoders (H.264, VP9, WebP, PNG, Opus)
- Mouse/keyboard input via data channel
- Browser client with settings UI

### Phase 3: Performance & Polish ✅ COMPLETE

- Unicorn hook overhead: 5.3%
- UAE M68K JIT compiler enabled
- Boot tests, API tests, Playwright e2e tests
- Command bridge (app launch/quit, window list, boot polling)
- ExtFS shared folders

### Phase 4: PowerPC Support ✅ COMPLETE

- KPX interpreter from SheepShaver fully integrated
- Mac OS 9 boots to Finder on Gossamer ROM
- All subsystems verified identical to legacy
- Dyngen JIT compiled (blocked by GCC codegen, works in interpreter)

### Phase 5: Application Support ⏳ NEXT

- Run Mac applications (HyperCard, classic games, productivity software)
- Stability improvements (long-running sessions)
- Sound emulation (currently stub only)

### Phase 6: Future ⏳

- Network support (lwIP driver exists, needs testing with apps)
- Performance parity between Unicorn and UAE
- Fix PPC JIT GCC codegen issue
- Mac OS 8 verification on PPC

---

## Success Metrics

### Achieved (Q1-Q2 2026)
- ✅ Both M68K backends boot to Finder
- ✅ PPC boots Mac OS 9 to Finder
- ✅ WebRTC streaming with input
- ✅ M68K JIT compiler
- ✅ Command bridge API
- ✅ Comprehensive test suite

### Next
- ⏳ Run HyperCard successfully
- ⏳ Play one classic game
- ⏳ Stable 30+ minute sessions
- ⏳ Sound output

---

## Contributing

### What We Need Help With
1. **Application Testing** — Run Mac apps, report issues
2. **Sound Emulation** — Currently stub only
3. **PPC JIT** — Debug GCC codegen difference in block dispatch loop
4. **Performance** — Unicorn M68K optimization

### What to Expect
- Platform API for all new code — no direct backend calls
- Tests required for new features
- Documentation for quirks and design decisions
