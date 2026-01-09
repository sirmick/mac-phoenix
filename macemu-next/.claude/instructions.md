# Claude Instructions for macemu-next Project

**Custom instructions for all Claude sessions working on macemu-next**

---

## Working with Michael

**User Preferences**:
- Michael (he/him) prefers to go by "Michael"
- Michael likes to review work between major topic changes
- Always commit code before switching topics
- Keep commits clean and well-documented
- Michael appreciates seeing the "why" behind decisions

**Workflow Pattern**:
1. Complete a logical unit of work
2. Commit with detailed message
3. Show Michael what was accomplished
4. Get approval before moving to next topic
5. Update TodoStatus.md to track progress

---

## Project-Specific Guidelines

### 1. Unicorn-First Development

When implementing features or fixing bugs:

✅ **DO**:
- Focus on Unicorn backend (`src/cpu/cpu_unicorn.cpp`, `src/cpu/unicorn_wrapper.c`)
- Ensure Unicorn is self-contained (no UAE dependencies)
- Use Platform API (`g_platform`) for all core code
- Optimize for Unicorn's JIT (minimize cache invalidation, efficient hooks)

❌ **DON'T**:
- Prioritize UAE backend (it's legacy, not the focus)
- Add UAE dependencies to Unicorn code
- Call UAE functions directly from core code
- Assume UAE behavior is always correct (validate via DualCPU)

### 2. Platform API Abstraction

When touching core emulation code:

✅ **DO**:
- Use `g_platform.cpu_execute_one()`, not direct backend calls
- Add new Platform API functions if needed
- Keep core code backend-agnostic
- Document Platform API changes

❌ **DON'T**:
- Call `uae_execute()` or `unicorn_execute()` directly from core
- Mix backend-specific code in `src/core/`
- Assume a specific backend is active

### 3. Validation is Critical

After any CPU-related changes:

✅ **DO**:
- Run DualCPU mode: `EMULATOR_TIMEOUT=30 CPU_BACKEND=dualcpu ./build/macemu-next ~/quadra.rom`
- Generate trace comparisons if divergence occurs
- Document new divergences in deepdive/ docs
- Expect ~514k instruction validation or better

❌ **DON'T**:
- Skip validation ("it looks right")
- Ignore DualCPU divergences
- Assume UAE and Unicorn are identical

### 4. Documentation Standards

When documenting:

✅ **DO**:
- Document quirks immediately (UAE and Unicorn have surprising behavior)
- Use CamelCase for deepdive/ files
- Update TodoStatus.md when completing features
- Include commit hashes for significant changes
- Explain "why" not just "what"

❌ **DON'T**:
- Write code without documenting quirks
- Use inconsistent naming (stick to CamelCase in deepdive/)
- Leave TODO status outdated
- Document without explaining rationale

---

## Code Reading Priority

When asked to understand or modify code, read in this order:

### 1. Always Start With
- [docs/Architecture.md](../docs/Architecture.md) - Platform API overview
- [docs/ProjectGoals.md](../docs/ProjectGoals.md) - Understand vision
- [docs/TodoStatus.md](../docs/TodoStatus.md) - Current state

### 2. For CPU Work
- [src/common/include/platform.h](../src/common/include/platform.h) - Platform API definition
- [src/cpu/cpu_unicorn.cpp](../src/cpu/cpu_unicorn.cpp) - Unicorn backend implementation
- [src/cpu/unicorn_wrapper.c](../src/cpu/unicorn_wrapper.c) - Unicorn API wrapper
- [docs/deepdive/UnicornQuirks.md](../docs/deepdive/UnicornQuirks.md) - Quirks

### 3. For Memory Work
- [docs/deepdive/MemoryArchitecture.md](../docs/deepdive/MemoryArchitecture.md) - Direct addressing
- [docs/deepdive/UaeQuirks.md](../docs/deepdive/UaeQuirks.md) - UAE memory model
- [src/cpu/uae_cpu/memory.cpp](../src/cpu/uae_cpu/memory.cpp) - UAE memory implementation

### 4. For Trap Work
- [docs/deepdive/ALineAndFLineTrapHandling.md](../docs/deepdive/ALineAndFLineTrapHandling.md) - Trap design
- [src/core/emul_op.cpp](../src/core/emul_op.cpp) - EmulOp handlers
- [src/cpu/unicorn_wrapper.c](../src/cpu/unicorn_wrapper.c) - hook_insn_invalid()

### 5. For Current Issues
- [docs/deepdive/InterruptTimingAnalysis.md](../docs/deepdive/InterruptTimingAnalysis.md) - Timer timing
- [docs/TodoStatus.md](../docs/TodoStatus.md) - Known issues

---

## IMPORTANT: How to Run Commands Correctly

### Always use the correct working directory
**ALWAYS run from the macemu-next directory:**
```bash
cd /home/mick/macemu-dual-cpu/macemu-next
```

### Running the emulator

**CRITICAL: Always use --no-webserver flag for testing/debugging!**

```bash
# Test UAE backend (uses config from ~/.config/macemu-next/config.json)
EMULATOR_TIMEOUT=5 CPU_BACKEND=uae ./build/macemu-next --no-webserver

# Test Unicorn backend
EMULATOR_TIMEOUT=5 CPU_BACKEND=unicorn ./build/macemu-next --no-webserver

# Test DualCPU validation
EMULATOR_TIMEOUT=10 CPU_BACKEND=dualcpu ./build/macemu-next --no-webserver

# With specific ROM file
EMULATOR_TIMEOUT=5 CPU_BACKEND=uae ./build/macemu-next --no-webserver ~/quadra.rom

# Generate traces (auto-loads config)
EMULATOR_TIMEOUT=5 CPU_BACKEND=uae CPU_TRACE=0-1000 ./build/macemu-next --no-webserver > uae.trace
EMULATOR_TIMEOUT=5 CPU_BACKEND=unicorn CPU_TRACE=0-1000 ./build/macemu-next --no-webserver > unicorn.trace

# Running test scripts
./scripts/run_traces.sh       # Compares UAE, Unicorn, DualCPU traces
./scripts/compare_boot.sh     # Compares BasiliskII vs macemu-next

# Building (with debug symbols for proper crash backtraces)
meson configure build -Dbuildtype=debug  # Only needed once
meson compile -C build
```

### Environment Variables (ALWAYS use these, not external commands)
- `CPU_BACKEND=uae|unicorn|dualcpu` - Select backend
- `EMULATOR_TIMEOUT=N` - Auto-exit after N seconds (DO NOT use timeout command!)
- `CPU_TRACE=N-M` - Trace instruction range
- `CPU_TRACE_MEMORY=1` - Include memory accesses
- `EMULOP_VERBOSE=1` - Log EmulOp calls
- `DUALCPU_TRACE_DEPTH=N` - DualCPU history depth

### Common mistakes to AVOID:
- ❌ Forgetting `--no-webserver` (launches web UI instead of test mode)
- ❌ Passing both config.json AND ROM file as arguments (use one or the other)
- ❌ Using external `timeout` command instead of `EMULATOR_TIMEOUT`
- ❌ `cd macemu-next` when already in macemu-next
- ❌ Wrong relative paths when not in macemu-next directory

### ALWAYS verify current directory first:
```bash
pwd  # Should show: /home/mick/macemu-dual-cpu/macemu-next
```

## Common Patterns to Follow

### Pattern 1: Adding Platform API Function

```c
// 1. Add to platform.h
typedef struct Platform {
    // ... existing ...
    void (*new_function)(uint32_t arg);
} Platform;

// 2. Implement in cpu_unicorn.cpp
static void unicorn_backend_new_function(uint32_t arg) {
    // Unicorn-specific implementation
}

// 3. Register in unicorn_backend_init()
g_platform.new_function = unicorn_backend_new_function;

// 4. Implement in cpu_uae.cpp (for validation)
static void uae_backend_new_function(uint32_t arg) {
    // UAE-specific implementation
}

// 5. Use in core code
g_platform.new_function(arg);
```

### Pattern 2: Debugging Divergence

```bash
# 1. Run DualCPU to find divergence point
EMULATOR_TIMEOUT=10 DUALCPU_TRACE_DEPTH=20 CPU_BACKEND=dualcpu \
    ./build/macemu-next ~/quadra.rom 2>&1 | tee divergence.log

# 2. Extract divergence instruction (e.g., 29518)
grep "DIVERGENCE" divergence.log

# 3. Generate detailed traces
CPU_TRACE=29500-29600 CPU_TRACE_MEMORY=1 CPU_BACKEND=uae \
    ./build/macemu-next ~/quadra.rom > uae.log

CPU_TRACE=29500-29600 CPU_TRACE_MEMORY=1 CPU_BACKEND=unicorn \
    ./build/macemu-next ~/quadra.rom > uni.log

# 4. Compare
diff uae.log uni.log

# 5. Read relevant quirks doc (UaeQuirks.md or UnicornQuirks.md)
```

### Pattern 3: Implementing Unicorn Hook

```c
// GOOD: Use UC_HOOK_BLOCK for periodic checks (interrupts)
static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    if (PendingInterrupt) {
        // Handle interrupt
        uc_emu_stop(uc);
    }
}

// Register:
uc_hook_add(cpu->uc, &cpu->block_hook, UC_HOOK_BLOCK, hook_block, cpu, 1, 0);

// GOOD: Use UC_HOOK_INSN_INVALID for illegal instructions (EmulOps)
static bool hook_insn_invalid(uc_engine *uc, void *user_data) {
    if (is_emulop(opcode)) {
        g_platform.emulop_handler(opcode, false);
        return true;  // Continue execution
    }
    return false;  // Stop
}

// BAD: Don't use UC_HOOK_CODE (10x slower)
// ❌ uc_hook_add(cpu->uc, &cpu->code_hook, UC_HOOK_CODE, ...);
```

---

## Terminology

### Backends
- **Unicorn** = Primary backend, JIT-compiled, the future
- **UAE** = Legacy backend, interpreter, validation baseline
- **DualCPU** = Validation backend, runs both in lockstep

### Traps
- **EmulOp** = 0x71xx illegal instructions (emulator-specific)
- **A-line trap** = 0xAxxx Mac OS Toolbox calls
- **F-line trap** = 0xFxxx FPU emulation

### Memory
- **Direct addressing** = Mac addresses map directly to host memory (fast)
- **MEMBaseDiff** = Offset to convert Mac address to host pointer
- **RAMBaseHost** / **ROMBaseHost** = Host memory buffers

### Hooks (Unicorn)
- **UC_HOOK_BLOCK** = Called at basic block boundaries (~100k/sec)
- **UC_HOOK_INSN_INVALID** = Called on illegal instructions (~1k/sec)
- **UC_HOOK_CODE** = ❌ Deprecated (called every instruction, 10x slower)

---

## Response Style for This Project

### When Explaining Code
1. **Start with Platform API** - Show how core code uses `g_platform`
2. **Show Unicorn implementation** - Primary focus
3. **Mention UAE for context** - "UAE does this differently..."
4. **Reference docs** - "See docs/deepdive/UnicornQuirks.md for details"

### When Debugging
1. **Check TodoStatus.md** - Is this a known issue?
2. **Suggest DualCPU validation** - "Let's run DualCPU to find divergence"
3. **Generate traces** - Show exact commands
4. **Read quirks docs** - "This might be related to VBR handling, see deepdive/..."

### When Implementing Features
1. **Platform API first** - "We need to add this to g_platform"
2. **Unicorn focus** - "Let's implement for Unicorn backend"
3. **Validation plan** - "After implementation, run DualCPU to validate"
4. **Documentation** - "Document this in deepdive/NewFeature.md"

---

## Quick Reference Checklist

Before suggesting code changes:
- [ ] Does it go through Platform API? (if core code)
- [ ] Is Unicorn backend prioritized? (not UAE)
- [ ] Have I considered hook performance? (BLOCK vs CODE)
- [ ] Will this require DualCPU validation?
- [ ] Should quirks be documented?

Before answering architecture questions:
- [ ] Have I read docs/Architecture.md?
- [ ] Do I understand Platform API role?
- [ ] Am I clear on Unicorn=primary, UAE=legacy?
- [ ] Have I checked TodoStatus.md for current state?

Before debugging:
- [ ] Suggested DualCPU mode to find divergence?
- [ ] Recommended trace comparison?
- [ ] Referenced relevant deepdive/ docs?
- [ ] Checked if it's a known issue?

---

## Files to Always Keep in Mind

**Core Architecture**:
- `src/common/include/platform.h` - Platform API definition
- `src/cpu/cpu_unicorn.cpp` - Unicorn backend (PRIMARY FOCUS)
- `src/cpu/unicorn_wrapper.c` - Unicorn hooks and wrappers

**Documentation**:
- `docs/Architecture.md` - System overview
- `docs/ProjectGoals.md` - Vision and roles
- `docs/TodoStatus.md` - Current state
- `docs/deepdive/InterruptTimingAnalysis.md` - Current blocker

**Validation**:
- `src/cpu/cpu_dualcpu.cpp` - Dual-CPU validation
- `src/cpu/unicorn_validation.cpp` - Validation logic

---

## Special Considerations

### Timer Interrupts
- Non-deterministic (wall-clock, not instruction-count)
- UAE and Unicorn diverge at instruction #29,518
- **This is expected** - not a bug to fix
- See docs/deepdive/InterruptTimingAnalysis.md

### Endianness
- UAE: RAM little-endian, ROM big-endian
- Unicorn: All big-endian
- **Must byte-swap RAM when copying to Unicorn**
- See docs/deepdive/MemoryArchitecture.md

### VBR Register
- Added by us to Unicorn (missing in upstream)
- Commit 006cc0f8
- See docs/completed/VBR_FIX_SUMMARY.md

### CPU Type
- Unicorn enum values ≠ array indices
- Fixed in commit 74fbd578
- See docs/completed/CPU_TYPE_FIX_SUMMARY.md

---

**Remember**: Unicorn is the future, UAE is the baseline, DualCPU is the validator. Always validate changes with DualCPU mode!
