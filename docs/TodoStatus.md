# TODO Status

Track what's done and what's next.

---

## MILESTONES

| Milestone | Date | Details |
|-----------|------|---------|
| M68K boots to Finder | March 2026 | Both UAE (~5s) and Unicorn (~48s) boot Mac OS 7.5.5 |
| WebRTC integration | January 2026 | 4-thread architecture, all codecs, browser client |
| PPC boots to Finder | April 2026 | KPX interpreter, Mac OS 9, Gossamer ROM |
| M68K JIT compiler | April 2026 | UAE JIT enabled via `--jit` flag |
| Command bridge | April 2026 | App launch/quit, window list, boot polling |

---

## Phase 1: Core CPU Emulation ✅ COMPLETE

### Build System ✅
- ✅ CMake build, UAE compilation, Unicorn submodule, backend selection

### Memory System ✅
- ✅ Direct addressing, ROM loading, RAM allocation, endianness handling

### UAE Backend ✅
- ✅ Full 68020 interpreter, EmulOps (0x71xx), A-line/F-line traps, interrupts
- ✅ JIT compiler enabled (`--jit`/`--no-jit` flags, default: on)

### Unicorn Backend ✅
- ✅ Unicorn engine with hooks, EmulOps (0xAExx), A-line/F-line via deferred updates
- ✅ QEMU native interrupt delivery, MMIO, JIT TB invalidation
- ✅ Boot parity with UAE

### DualCPU Backend ✅
- ✅ Lockstep execution, register comparison, 514k+ instruction validation

### Platform API ✅
- ✅ Function pointer table, runtime backend selection, trap/interrupt abstraction

---

## Phase 2: WebRTC Integration ✅ COMPLETE

- ✅ 4-thread architecture (CPU, video encoder, audio encoder, web server)
- ✅ All encoders (H.264, VP9, WebP, PNG, Opus)
- ✅ JSON configuration system
- ✅ Mouse/keyboard input via WebRTC data channel
- ✅ Browser client (HTML/JS/CSS)
- ✅ Playwright e2e tests

---

## Phase 3: Performance & Polish ✅ COMPLETE

- ✅ Unicorn performance: auto-ack interrupts, goto_tb, lean hook_block (hooks 5.3%)
- ✅ Web UI mouse/keyboard input
- ✅ Playwright e2e tests (6 tests)
- ✅ Framebuffer fix (0x02110000, outside RAM)
- ✅ RTR instruction added to Unicorn's QEMU m68k translator
- ✅ FPU emulation, SIGSEGV handler, serial null check
- ✅ Command bridge: app launch/quit, window list, boot polling, keypress
- ✅ ExtFS shared folders (`--extfs` flag, config, tests)

---

## Phase 4: PowerPC Support ✅ COMPLETE

- ✅ KPX interpreter integrated (`--arch ppc`)
- ✅ Mac OS 9 boots to Finder on Gossamer ROM
- ✅ ROM patches (PatchROM_PPC, all 4 phases) — verified identical to legacy
- ✅ All 40+ EmulOps, 38 NativeOps — functionally identical to SheepShaver
- ✅ HandleInterrupt (all 3 modes), KernelData init, XLM setup
- ✅ NQD acceleration, ExtFS, video driver — all working
- ✅ Virtual clock, atomic interrupts, forced PatchAfterStartup
- ✅ Dyngen JIT compiled (`--ppc-jit` flag) — blocked by GCC codegen, interpreter default

---

## Phase 5: Application Support ⏳ NEXT

- ⏳ HyperCard stacks run
- ⏳ Classic game playable
- ⏳ Productivity software (MacWrite, PageMaker)
- ⏳ 30+ minute sessions without crash
- ⏳ Sound emulation (currently stub: `audio_null.cpp`)

---

## Phase 6: Future ⏳

- ⏳ PPC JIT fix (GCC codegen difference in block dispatch loop)
- ⏳ Network testing with applications (lwIP driver exists)
- ⏳ Unicorn performance optimization
- ⏳ Mac OS 8 verification on PPC

---

## Test Suite

| Test | Backend | What | Timeout |
|------|---------|------|---------|
| `api_endpoints` | UAE | 10 API smoke checks | 15s |
| `boot_uae_interp` | UAE | Boot to Finder (no JIT) | 30s |
| `boot_uae_jit` | UAE | Boot to Finder (JIT) | 30s |
| `boot_unicorn` | Unicorn | Boot to Finder | 120s |
| `boot_ppc_interp` | KPX | Boot to Finder (interpreter) | 120s |
| `boot_ppc_jit` | KPX | Boot to Finder (JIT) | 120s |
| `boot_ppc_api` | KPX | Boot + API phase tracking | 120s |
| `mouse_position` | UAE | Mouse position API | 15s |
| `command_bridge` | UAE | App/windows/launch/wait | 30s |
| `extfs` | UAE | ExtFS config + CLI tests | 15s |
| Playwright e2e | UAE | Browser UI, codecs, input | 60s |

---

**Last Updated**: April 2, 2026
**Current Phase**: Phase 5 — Application Support
**Status**: M68K and PPC both boot to Finder desktop
