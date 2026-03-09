# TODO Status

Track what's done and what's next.

---

## MILESTONE: Both Backends Boot to Mac OS 7.5.5 Finder Desktop (March 2026)

**Both the Unicorn JIT backend and the UAE interpreter backend boot Mac OS 7.5.5 to the Finder desktop.**

| Metric | UAE | Unicorn |
|--------|-----|---------|
| Boot to Finder | ~5s | ~48s |
| CHECKLOADs | 2200+ | 2513+ |
| Performance | Baseline | ~10x slower |

---

## Phase 1: Core CPU Emulation ✅ COMPLETE

### Build System
- ✅ Meson build configuration
- ✅ UAE CPU compilation
- ✅ Unicorn integration (git submodule)
- ✅ Backend selection via Meson options

### Memory System
- ✅ Direct addressing mode
- ✅ ROM loading (1MB Quadra 650 ROM)
- ✅ RAM allocation (configurable size)
- ✅ Endianness handling (UAE LE RAM, BE ROM)
- ✅ Byte-swapping when copying to Unicorn

### UAE Backend
- ✅ Full 68020 interpreter integrated
- ✅ Memory system (mem_banks, get_long/put_long)
- ✅ Exception handling (A-line, F-line traps)
- ✅ EmulOp support (0x71xx traps)
- ✅ Interrupt processing (SPCFLAG_INT)

### Unicorn Backend
- ✅ Unicorn engine initialization
- ✅ Memory mapping (RAM, ROM, dummy regions)
- ✅ Register access (D0-D7, A0-A7, PC, SR)
- ✅ **VBR register support** (added missing API, commit 006cc0f8)
- ✅ **CPU type selection fix** (68020 not 68030, commit 74fbd578)
- ✅ **Hook architecture optimization** (UC_HOOK_BLOCK + UC_HOOK_INSN_INVALID)
- ✅ EmulOp handling (0x71xx traps)
- ✅ A-line EmulOps (0xAE00-0xAE3F) - BasiliskII-specific traps
- ✅ **A-line/F-line trap handling (0xAxxx, 0xFxxx)** - **WORKING** via deferred register updates
  - Previous Unicorn PC limitation (issue #1027) solved by deferring register writes
  - All A-line traps work, 87 OS trap table entries populated (matching UAE)
  - See: [deepdive/cpu/ALineAndFLineStatus.md](deepdive/cpu/ALineAndFLineStatus.md)
- ✅ **JIT TB invalidation** - 60Hz `uc_ctl_flush_tb()` workaround
- ✅ **MMIO infrastructure** - `uc_mmio_map()` for hardware registers (VIA/SCC/SCSI/ASC/DAFB stubs)
- ✅ **Boot parity with UAE** - identical state at all checkpoints (March 2026)
- ✅ **Interrupt detection** (UC_HOOK_BLOCK for efficiency, commit 1305d3b2)
- ✅ **Legacy API removal** (~236 lines, commit ebd3d1b2)
- ✅ **RTE (Return from Exception) fix** - Patched Unicorn cpu-exec.c (Jan 5, 2026)
  - Fixed UC_ERR_EXCEPTION crash when returning from interrupts
  - Fixed infinite loop bug preventing batch execution
  - Unicorn now runs for extended periods
  - **Batch execution enabled**: Using count=10000 for 1.93x performance boost
  - See: [deepdive/cpu/UnicornBatchExecutionRTEBug.md](deepdive/cpu/UnicornBatchExecutionRTEBug.md)

### DualCPU Backend
- ✅ Lockstep execution (UAE + Unicorn)
- ✅ Register comparison after each instruction
- ✅ Divergence detection and logging
- ✅ Trace history (circular buffer)
- ✅ **514,000+ instruction validation** (commit 155497f0)

### Platform API
- ✅ Platform struct with function pointers
- ✅ Backend-independent core code
- ✅ Runtime backend selection (`--backend` CLI flag)
- ✅ Trap handlers (emulop_handler, trap_handler)
- ✅ **68k trap execution API** (cpu_execute_68k_trap)
- ✅ **Interrupt abstraction** (cpu_trigger_interrupt, commit c388b229)

---

## Phase 1.5: CPU Divergence Investigation ✅ RESOLVED

### Issue Discovery
- ✅ UAE executes 250k+ instructions successfully
- ✅ Unicorn crashes at ~145k instructions
- ✅ DualCPU shows divergence starting at instruction #3832
- ✅ Root cause identified: **Unicorn skips instructions when interrupt triggered**

### Diagnostic Tools Created
- ✅ Enhanced trace logging with interrupt/EmulOp markers
- ✅ Side-by-side trace analyzer with disassembly (trace_analyzer_v2.py)
- ✅ Event extraction (interrupts triggered/taken, EmulOps)
- ✅ Automated trace runner (run_traces.sh)

### Root Cause Analysis ✅ FIXED
**Bug**: Unicorn's interrupt handling in `hook_block()` called `uc_emu_stop()` which caused instruction skipping.

**Evidence**:
- At instruction #3831: Unicorn receives interrupt trigger
- At instruction #3832 (BEFORE FIX):
  - UAE executes PC=0x0208113A (correct)
  - Unicorn executes PC=0x02081138 (PREVIOUS instruction - skipped ahead!)
- This caused D0 register divergence (0x05 vs 0x00)

**Fix Applied**: Removed `uc_emu_stop()` call, let execution continue naturally after updating PC to interrupt handler.

**Result**: ✅ Perfect synchronization between UAE and Unicorn - all registers match exactly at instruction #3832 and beyond.

### Timer Implementation ✅ COMPLETE
**Original approach**: SIGALRM signal-based timer (126 lines)
- ❌ Failed: Blocked by signal masking (~0.2 Hz instead of 60 Hz)

**Final approach**: Polling-based timer using `clock_gettime(CLOCK_MONOTONIC)` (~60 lines)
- ✅ Simple, fast (~20-50ns overhead)
- ✅ Reliable (can't be blocked)
- ✅ Portable (POSIX)
- ✅ Works for both UAE and Unicorn
- ✅ Verified at 60.0 Hz

**Documentation**: See [TIMER_IMPLEMENTATION_FINAL.md](TIMER_IMPLEMENTATION_FINAL.md), [TIMER_IMPLEMENTATION_COMPARISON.md](TIMER_IMPLEMENTATION_COMPARISON.md)

---

## Phase 1.6: Trace Analysis Tools ✅ COMPLETE (Jan 4, 2026)

### Trace Infrastructure
- ✅ **Enhanced run_traces.sh** - Support for instruction ranges (start-end)
- ✅ **Fixed trace_analyzer.py** - Proper handling of interrupt event markers
  - Separates instructions from events before comparison
  - Prevents false divergence reports from interrupt timing
  - Shows events in context without using them for alignment
- ✅ **CPU type initialization fix** - DualCPU now respects config settings
  - Fixed unicorn_validation.cpp hardcoded 68040
  - Both backends now use CPUType/FPUType from prefs system

### Validation Results
- ✅ UAE vs Unicorn: 98.5% match (29,536 instructions identical)
- ✅ First divergence at instruction #29,537 (timing-related, expected)
- ✅ Both backends execute same instructions in same order
- ✅ Interrupt timing differences confirmed as architectural (not bugs)

---

## Phase 2: WebRTC Integration ✅ COMPLETE (Jan 5, 2026)

### Planning Phase ✅ COMPLETE
- ✅ Merge master branch (WebRTC streaming code)
- ✅ Architecture design (4-thread model - simplified from 7)
- ✅ File migration mapping (180 files mapped)
- ✅ Threading model documented
- ✅ Create comprehensive integration plan

### Configuration System ✅ COMPLETE (Jan 4, 2026)
- ✅ Copy JSON config system (nlohmann/json) - commit 19d871c8
- ✅ Implement JSON config loading/saving
- ✅ Add XDG directory support (~/.config/mac-phoenix/)
- ✅ Add --config and --save-config CLI options
- ✅ Human-readable CPU names ("68040" vs "4")
- ✅ Complete documentation (JSON_CONFIG.md)
- ✅ Test with all three backends (UAE, Unicorn, DualCPU)
- ✅ WebRTC-specific config options integrated

### Platform API (In-Process Buffers) ✅ COMPLETE (Jan 5, 2026)
- ✅ Design video_output.h / audio_output.h APIs
- ✅ Implement triple buffer (lock-free, atomic operations)
- ✅ Implement audio ring buffer (mutex-protected)
- ✅ Full implementation in src/platform/
- ✅ Integration with emulator core (commit eb850af5)

### WebRTC Server Integration ✅ COMPLETE (Jan 5, 2026)
- ✅ Copy encoders (H.264, VP9, WebP, PNG, Opus) - 13 files
- ✅ Adapt HTTP server for in-process model
- ✅ Create webrtc_server.cpp (coordinator)
- ✅ Create video_encoder_thread.cpp
- ✅ Create audio_encoder_thread.cpp
- ✅ Wire encoders to in-process buffers

### Main Entry Point ✅ COMPLETE (Jan 5, 2026)
- ✅ Create unified main.cpp (573 lines)
- ✅ Launch 4 threads (CPU/Main, Video Encoder, Audio Encoder, WebRTC/HTTP)
- ✅ Implement clean shutdown
- ✅ Signal handling (SIGINT/SIGTERM)

### Build System ✅ COMPLETE (Jan 5, 2026)
- ✅ Update meson.build (add WebRTC dependencies)
- ✅ Add all new source files (encoders, http, storage, utils)
- ✅ Fixed libyuv detection (Ubuntu lacks .pc file)
- ✅ Conditional compilation via -Dwebrtc=true/false
- ✅ **Verified**: Build successful with WebRTC enabled

### Client & Testing ✅ COMPLETE (March 2026)
- ✅ Browser client (HTML/JS/CSS) with WebRTC connection
- ✅ Mouse/keyboard input via data channel binary protocol
- ✅ End-to-end: Browser connects, views emulator, sends input
- ✅ Playwright e2e tests (mouse, keyboard, data channel)

---

## Phase 3: Performance & Polish 🎯 CURRENT FOCUS

### Completed
- ✅ **Unicorn performance optimizations** (hooks reduced to 5.3% overhead; JIT itself ~10x slower)
  - Auto-ack interrupts in QEMU's `m68k_cpu_exec_interrupt()`
  - `goto_tb` enabled for backward branches
  - Lean `hook_block()` (stripped perf timing, block stats, stale TB detector)
- ✅ **Web UI mouse/keyboard input** via WebRTC data channel
- ✅ **Playwright e2e tests** for input pipeline (6 tests)
- ✅ **Framebuffer fix** (placed at 0x02110000, outside RAM)
- ✅ **RTR instruction** added to Unicorn's QEMU m68k translator
- ✅ **FPU emulation**, SIGSEGV handler, serial null check

### In Progress
- ⏳ Application support (HyperCard, classic games)
- ⏳ Stability improvements (long-running sessions)

---

## Phase 4: Application Support ⏳ NEXT

### Application Testing
- ⏳ HyperCard stacks run
- ⏳ Classic game playable (e.g., Dark Castle, Marathon)
- ⏳ Productivity software (MacWrite, PageMaker)

### Stability
- ⏳ 30+ minute sessions without crash
- ⏳ Save/restore state
- ⏳ Error recovery

---

## Phase 5: Performance & Polish ⏳ FUTURE

### Performance Optimization
- ⏳ Profile Unicorn backend
- ⏳ Optimize hot paths
- ⏳ JIT tuning
- ⏳ Reduce hook overhead further (if possible)

### User Interface
- ⏳ SDL-based window/input
- ⏳ Preferences UI
- ⏳ Debugger integration (step, breakpoints)

### Testing & CI
- ⏳ Automated testing suite
- ⏳ Regression tests
- ⏳ Continuous integration (GitHub Actions)

---

## Phase 6: PowerPC Support ⏳ FAR FUTURE

### SheepShaver Integration
- ⏳ PowerPC CPU backend
- ⏳ Mac OS 8.5-9.0.4 support
- ⏳ Mixed-mode (68K + PPC) execution

**Note**: Very far out, 68K focus first

---

## Bug Fixes & Investigations

### Completed ✅
- ✅ **VBR corruption** (missing Unicorn register API, commit 006cc0f8)
  - Symptom: VBR reads returned garbage (0xCEDF1400, etc.)
  - Fix: Added UC_M68K_REG_CR_VBR to reg_read/reg_write
  - Impact: +330% execution (23k → 100k instructions)

- ✅ **CPU type mismatch** (enum/array confusion, commit 74fbd578)
  - Symptom: Unicorn created 68030 instead of 68020
  - Fix: Use array indices not UC_CPU_M68K_* enum values
  - Impact: Both backends now correctly create 68020

- ✅ **Interrupt support** (Unicorn ignored interrupts, commit 1305d3b2)
  - Symptom: Divergence at ~29k instructions, crash at ~175k
  - Fix: UC_HOOK_BLOCK for interrupts, shared PendingInterrupt flag
  - Impact: Both backends process timer/ADB interrupts

- ✅ **Platform API interrupt abstraction** (Global state elimination, commit c388b229)
  - Replaced: PendingInterrupt global flag with platform API
  - UAE: Uses native SPCFLAG_INT mechanism
  - Unicorn: Manual M68K exception stack frame building
  - Impact: Backend-agnostic interrupt triggering, cleaner architecture
  - See: docs/deepdive/PlatformAPIInterrupts.md

- ✅ **Hybrid execution crash** (UAE dependency, commit d90208dc)
  - Symptom: Unicorn crashed at 175k when EmulOps called Execute68kTrap
  - Fix: Unicorn-native 68k trap execution
  - Impact: +24,696 instructions (175k → 200k), no UAE dependency

- ✅ **Performance overhead** (UC_HOOK_CODE, commit ebd3d1b2)
  - Symptom: 10x slowdown from per-instruction hook
  - Fix: UC_HOOK_INSN_INVALID for EmulOps, UC_HOOK_BLOCK for interrupts
  - Impact: Expected 5-10x performance improvement

### Resolved (February-March 2026) ✅

- ✅ **A-line/F-line traps** (deferred register updates, Feb 2026)
  - Symptom: Unicorn ignored PC changes in UC_HOOK_INTR callbacks
  - Fix: Defer all register writes, apply at next hook_block() boundary
  - Impact: All A-line traps work, 87 OS trap table entries populated

- ✅ **JIT TB invalidation** (60Hz flush workaround, Feb 2026)
  - Symptom: Mac OS heap overwrites RAM patches; JIT executes stale code → crash at PC=0x2
  - Fix: `uc_ctl_flush_tb()` on every 60Hz timer tick
  - Impact: Boot progresses through all phases without JIT-related crashes
  - Proper fix needed: QEMU `TLB_NOTDIRTY` mechanism

- ✅ **IRQ storm** (4-phase fix, Jan-Feb 2026)
  - Symptom: 781,000+ IRQ polls/10s instead of ~600
  - Fix: Correct EmulOp encoding, QEMU-style loop, deferred updates, proper interrupt delivery
  - Impact: 99.997% overhead reduction, proper 60Hz timer

### Previously Resolved ✅
- ✅ **Timer interrupt timing** (wall-clock vs instruction-count) - RESOLVED
  - Not a bug, design characteristic. Accept non-determinism for performance.

- ✅ **Unicorn execution length** (200k limit) - RESOLVED
  - No longer an issue after native trap execution and deferred updates.

---

## Documentation

### Completed ✅
- ✅ README.md - Quick start guide
- ✅ Architecture.md - Platform API, backend abstraction
- ✅ ProjectGoals.md - Vision, Unicorn-first focus
- ✅ TodoStatus.md - This file
- ✅ Commands.md - Build, test, trace commands
- ✅ completed/ folder - Archived historical docs
- ✅ deepdive/ folder - Detailed technical docs
  - ✅ PlatformAPIInterrupts.md - Interrupt abstraction design & implementation

### Completed ⏳→✅
- ✅ TroubleshootingGuide.md - Common issues and solutions
- ✅ DeveloperGuide.md - Architecture, debugging, patterns
- ✅ ThreadingArchitecture.md - 4-thread model
- ✅ WebRtcIntegrationStatus.md - WebRTC pipeline details
- ✅ UnicornPerformanceAnalysis.md - JIT vs interpreter analysis

---

## Recent Commits (Dec 2025 - Jan 2026)

```
19d871c8 - Add JSON configuration system with XDG support (Jan 4, 2026)
c388b229 - Platform API interrupt abstraction (Jan 4, 2026)
a3712b98 - WebRTC integration planning (initial documents)
309d4fab - Merge master branch with WebRTC improvements
66f5d428 - Resolve "200k execution limit" investigation
1ddf847d - Claude instructions with Michael's preferences
30d604ee - JIT block size measurement and analysis
74347217 - Add interrupt timing divergence analysis and reorganize documentation
449d34bf - Document interrupt timing divergence root cause analysis
d90208dc - Implement Unicorn-native 68k trap execution to eliminate UAE dependency
ebd3d1b2 - Remove legacy per-CPU hook API and UC_HOOK_CODE implementation
1305d3b2 - WIP: Interrupt support implementation (needs optimization)
```

---

## Next Actions

### Current Focus: Phase 3 - Performance & Polish
- ⏳ Application support (HyperCard, classic games)
- ⏳ Stability improvements (long-running sessions)
- ⏳ Further Unicorn performance optimization

### Long-Term
- ⏳ Mac OS 8 support
- ⏳ Performance parity with UAE
- ⏳ SheepShaver / PowerPC support

---

**Last Updated**: March 5, 2026
**Current Phase**: Phase 3 - Performance & Polish
**Branch**: phoenix-mac-planning
**Status**: Both backends boot to Mac OS 7.5.5 Finder desktop

**Major Milestones**:
- ✅ **Both Backends Boot to Finder** (March 2026)
  - UAE: ~5s to Finder, Unicorn: ~48s to Finder
  - Framebuffer fix, RTR instruction, FPU emulation
- ✅ **Unicorn Performance** (hooks 5.3% overhead; JIT ~10x slower than UAE)
  - Auto-ack interrupts, goto_tb, lean hook_block
- ✅ **Web UI Input** (March 2026)
  - Mouse/keyboard via WebRTC data channel
  - Playwright e2e tests
- ✅ **WebRTC Integration** (January 2026)
  - 4-thread in-process architecture, all encoders
- ✅ **JSON Configuration System**
