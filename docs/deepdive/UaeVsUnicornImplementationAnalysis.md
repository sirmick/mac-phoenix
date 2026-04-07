# UAE vs Unicorn: Implementation Analysis

**Purpose**: Document the architectural differences between the two M68K CPU backends.

---

## Executive Summary

**mac-phoenix** implements two M68K CPU backends:
- **UAE** (WinUAE M68K interpreter) — default backend, ~5s boot, JIT available
- **Unicorn** (QEMU-based JIT) — validation backend, ~48s boot, standalone Finder boot

Both backends achieve full boot parity. 514,000+ instructions validated in lockstep via DualCPU mode.

---

## Architecture Comparison

### UAE Backend Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ UAE M68K Interpreter (from WinUAE)                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐ │
│  │  Instruction │ →  │   Execute    │ →  │   Update     │ │
│  │    Fetch     │    │  Interpreter │    │   Registers  │ │
│  └──────────────┘    └──────────────┘    └──────────────┘ │
│         │                    │                    │        │
│         ▼                    ▼                    ▼        │
│  ┌──────────────────────────────────────────────────────┐ │
│  │         UAE Internal State Machine                   │ │
│  │  • Direct register access (regs.regs[])            │ │
│  │  • Exception handling (Exception() function)        │ │
│  │  • SPCFLAGS for interrupts/exceptions              │ │
│  │  • Prefetch queue emulation                        │ │
│  │  • Cycle-accurate timing (optional)                │ │
│  └──────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### Unicorn Backend Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Unicorn M68K JIT (from QEMU)                                │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐ │
│  │  Basic Block │ →  │  QEMU TCG    │ →  │  Host Code   │ │
│  │  Translation │    │  JIT Compile │    │  Execution   │ │
│  └──────────────┘    └──────────────┘    └──────────────┘ │
│         │                    │                    │        │
│         ▼                    ▼                    ▼        │
│  ┌──────────────────────────────────────────────────────┐ │
│  │           Unicorn Hook System                        │ │
│  │  • UC_HOOK_BLOCK — basic block boundaries           │ │
│  │  • UC_HOOK_INTR — interrupt/exception trigger       │ │
│  │  • UC_HOOK_INSN_INVALID — illegal instructions      │ │
│  │  • API-based register access (uc_reg_read/write)    │ │
│  │  • Deferred register updates at block boundaries    │ │
│  └──────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## Feature Comparison Matrix

| Feature | UAE | Unicorn | Notes |
|---------|-----|---------|-------|
| **Normal Instructions** | ✅ | ✅ | Both execute M68K correctly |
| **Performance** | ~5s boot | ~48s boot | UAE faster as interpreter; Unicorn's QEMU TCG overhead |
| **EmulOps (0x71xx)** | ✅ Via `op_illg()` | ✅ Via UC_HOOK_INSN_INVALID | |
| **A-line EmulOps (0xAE00-0xAE3F)** | ✅ Via `op_illg()` | ✅ Via UC_HOOK_INTR | |
| **Mac OS A-line Traps (0xA000+)** | ✅ Via Exception() | ✅ Via deferred register updates | |
| **Mac OS F-line Traps (0xF000+)** | ✅ Via Exception() | ✅ Via deferred register updates | |
| **Interrupts** | ✅ SPCFLAGS | ✅ UC_HOOK_BLOCK polling | |
| **Exception Simulation** | ✅ Direct | ✅ Via deferred register updates | |
| **RTE Instruction** | ✅ | ✅ Patched in cpu-exec.c | |
| **VBR Register** | ✅ Native | ✅ Custom API (UC_M68K_REG_CR_VBR) | |
| **SR Lazy Flags** | ✅ | ⚠️ Minor upstream bugs | Known Unicorn issue, not critical |
| **Prefetch Queue** | Optional | Not modeled | Not critical for macemu |
| **Cycle Counting** | Accurate | Approximate | Not critical for macemu |
| **ROM Patching** | ✅ Direct | ✅ Via memory copy | |

---

## Deferred Register Updates

Unicorn's QEMU backend overwrites PC after `UC_HOOK_INTR` returns (via `exception_next_eip`). To handle A-line/F-line traps and exceptions, Unicorn defers all register writes and applies them at the next `hook_block()` boundary:

1. `hook_interrupt()` queues register changes (including PC) in deferred arrays
2. At the next basic block boundary, `hook_block()` fires
3. `apply_deferred_updates_and_flush()` applies all queued register writes
4. Execution continues from the correct address

SR updates are also deferred for the same reason (`unicorn_defer_sr_update()`).

---

## EmulOp Handling

### 0x71xx EmulOps (Illegal Instructions)

**UAE**: Native `op_illg()` handler dispatches to `EmulOp_C()`.
**Unicorn**: `UC_HOOK_INSN_INVALID` catches the opcode, extracts/restores registers, advances PC by 2.

### A-line EmulOps (0xAE00-0xAE3F)

BasiliskII-specific A-line instructions for emulation services. These don't require PC changes — the handler runs and PC auto-advances.

### Mac OS A-line Traps (0xA000-0xAFFF)

Both backends build an M68K exception frame (push SR, push PC, read vector) and jump to the trap handler. UAE does this natively via `Exception()`. Unicorn does this via deferred register updates.

Both backends populate 87 identical OS trap table entries.

---

## Interrupt Delivery

**UAE**: Polls timer via `SPCFLAG_INT` on every instruction. On interrupt, builds exception frame via `Exception()`.

**Unicorn**: Polls timer in `UC_HOOK_BLOCK` (~every 100 instructions). On interrupt, manually builds the M68K exception frame in memory and defers PC/SR updates.

Timer interrupts fire at different instruction counts between backends because the timer is wall-clock based — UAE's slower execution reaches the 16.67ms boundary at a different instruction count than Unicorn. This is expected and not a bug.

---

## Known Limitations

| Feature | UAE | Unicorn | Impact |
|---------|-----|---------|--------|
| SR lazy flags | ✅ | ⚠️ Minor bugs | Upstream issue, doesn't affect boot |
| Cycle timing | ✅ Accurate | Approximate | Not needed for macemu |
| Prefetch queue | ✅ Optional | Not modeled | Not needed for macemu |
| Interrupt timing | Wall-clock | Wall-clock | Non-deterministic between backends (expected) |

The ~10x performance gap is structural — QEMU's TCG M68K code generation has inherent overhead from translation block compilation and the JIT pipeline.

---

## Remaining Work

1. **Unicorn performance** — structural QEMU TCG overhead, not easily fixable
2. **SR lazy flag bugs** — upstream Unicorn issue, minor impact
3. **Machine profile testing** — validate Unicorn with Mac SE (68000) and other profiles

---

**See Also**:
- [cpu/UnicornQuirks.md](cpu/UnicornQuirks.md) — Unicorn-specific implementation details
- [cpu/ALineAndFLineStatus.md](cpu/ALineAndFLineStatus.md) — Trap handling details
- [cpu/UaeQuirks.md](cpu/UaeQuirks.md) — UAE-specific implementation details
- [InterruptTimingAnalysis.md](InterruptTimingAnalysis.md) — Timing divergence analysis
