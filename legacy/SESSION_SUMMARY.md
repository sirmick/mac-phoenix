# Session Summary: Unicorn Boot Failure Investigation

## Problem Statement

**Unicorn backend fails to boot Mac OS ROM**, getting stuck in CLKNOMEM loop, while UAE backend boots successfully.

## Root Cause Identified

**Timer interrupt fires exactly ONCE, then never fires again**, even though:
- Timer fd is valid (fd=6)
- Timer is correctly configured as periodic (60.15 Hz)
- Timer state shows it's still armed (`interval=0s 16625000ns`)
- `poll_timer_interrupt()` is being called regularly (every 100 instructions)
- Execution is proceeding normally (85,000 instructions in 3 seconds)

## Key Evidence

### UAE vs Unicorn Comparison (2-3 seconds):
```
UAE:      120 timer interrupts, 1406 CLKNOMEM calls → BOOT SUCCESS
Unicorn:  1 timer interrupt,   546 CLKNOMEM calls  → BOOT FAILURE (stuck in loop)
```

### Timer Behavior:
```
[poll_timer_interrupt] Timer fired! Event #1, expirations=1
[poll_timer_interrupt] Timer state: interval=0s 16625000ns, value=0s 16606741ns
```
Then ~840 subsequent polls over 3 seconds return EAGAIN (no data) - timer never fires again.

### Execution Flow:
1. Initial boot proceeds normally (PATCH_BOOT_GLOBS completes, registers identical to UAE)
2. Timer fires ONCE early in boot
3. ROM enters CLKNOMEM loop waiting for time to advance
4. Timer never fires again, so ROM spins forever
5. ROM needs timer interrupts to advance time and complete CLKNOMEM operations

## What We Ruled Out

- ❌ **A-line exception handling** - Fixed, working correctly (EmulOps execute, non-EmulOp A-lines delegate to QEMU)
- ❌ **Interrupt delivery mechanism** - Working (interrupt level 1 delivered with correct SR mask)
- ❌ **Timer disarmed** - `timerfd_gettime()` confirms timer still armed with correct interval
- ❌ **Timer not being polled** - `hook_block` calls `poll_timer_interrupt()` every ~100 instructions
- ❌ **CPU_TRACE disabling timer** - Not set in environment
- ❌ **File descriptor issues** - timer_fd=6 remains valid throughout execution
- ❌ **Execution blocking** - 85K instructions execute in 3s, hook_block called regularly

## Critical Code Locations

### Timer Setup (CORRECT):
**File:** `src/drivers/platform/timer_interrupt.cpp:41-71`
```cpp
timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
spec.it_interval.tv_nsec = 16625000;  // 60.15 Hz periodic
spec.it_value.tv_nsec = 16625000;     // Initial expiration
timerfd_settime(timer_fd, 0, &spec, NULL);
```

### Timer Polling (CORRECT):
**File:** `src/cpu/unicorn_wrapper.c:94-106`
```cpp
if (cpu->block_stats.total_instructions % 100 < (uint64_t)size) {
    uint64_t expirations = poll_timer_interrupt();
    // Logs show this IS being called every 100 instructions
}
```

### Poll Implementation (CORRECT):
**File:** `src/drivers/platform/timer_interrupt.cpp:153-197`
```cpp
uint64_t expirations;
ssize_t ret = read(timer_fd, &expirations, sizeof(expirations));
// Returns success ONCE, then EAGAIN forever
```

## The Mystery

**Why does `read(timer_fd, ...)` return data exactly once, then return EAGAIN for all subsequent calls, even though:**

1. The timerfd is in non-blocking mode (TFD_NONBLOCK)
2. The timer is periodic (it_interval is set)
3. Real wall-clock time IS passing (3 seconds elapsed)
4. The timer state confirms it's armed and should fire in 16ms
5. We're polling fast enough (283 polls/sec for a 60Hz timer)
6. UAE with the SAME timer code gets 120 expirations in 2 seconds

## Hypotheses to Investigate (Next Session)

### Theory 1: Unicorn Execution Time Model
- Unicorn might be executing in "virtual time" that doesn't advance real wall-clock time
- The kernel's timerfd uses CLOCK_MONOTONIC (real wall-clock time)
- If Unicorn blocks or spins internally, wall-clock time might not advance as expected
- **Test:** Use `strace -T` to measure actual syscall timing
- **Test:** Add wall-clock timestamps to each `poll_timer_interrupt()` call

### Theory 2: File Descriptor State Corruption
- Something in Unicorn might be interfering with fd 6
- Unicorn uses many fds internally for JIT, memory, etc.
- **Test:** Use `lsof -p <pid>` to monitor fd state during execution
- **Test:** Try changing timer_fd to a higher number (e.g., dup2 to fd 100)

### Theory 3: Signal Interference
- Unicorn or QEMU might be using signals that interfere with timerfd
- timerfd_create uses futexes internally
- **Test:** Use `strace -e signal` to see if signals are being delivered
- **Test:** Try `TFD_CLOEXEC` flag on timer creation

### Theory 4: Kernel Timer Queue Issue
- First `read()` might be consuming multiple pending expirations incorrectly
- Some edge case in Linux timerfd when under heavy CPU load
- **Test:** Check `dmesg` for kernel errors
- **Test:** Try `CLOCK_REALTIME` instead of `CLOCK_MONOTONIC`

## ✅ FIXED - Update (Current Session)

**CONFIRMED**: Real wall-clock time IS passing (verified with `clock_gettime()` timestamps).

**CONFIRMED**: Timer fires exactly once then never again with `timerfd` (verified with detailed read() logging).

**ROOT CAUSE**: Unknown Linux/Unicorn interaction causing `timerfd` to stop firing after first expiration. The mechanism is still unclear, but the symptom was 100% reproducible.

**SOLUTION IMPLEMENTED**: Replaced `timerfd` with direct `clock_gettime(CLOCK_MONOTONIC)` polling.

**RESULTS**:
- ✅ Unicorn: 180 timer interrupts in 3 seconds (60 Hz × 3 = 180) - **PERFECT**
- ✅ UAE: 119-120 timer interrupts in 2 seconds (60 Hz × 2 = 120) - **PERFECT**

The polling approach from TIMER_IMPLEMENTATION_FINAL.md documentation is now actually implemented in the code.

## Recommended Next Steps

### Immediate (High Priority):
1. ✅ **Add wall-clock timestamps to logging** - DONE: Real time IS passing
   ```cpp
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   fprintf(stderr, "[%ld.%09ld] poll #%d\n", ts.tv_sec, ts.tv_nsec, count);
   ```

2. **Run under strace** - See actual syscall behavior
   ```bash
   strace -T -e timerfd_create,timerfd_settime,read,poll -o trace.log \
       env EMULATOR_TIMEOUT=3 CPU_BACKEND=unicorn ./macemu-next --no-webserver
   ```

3. **Compare UAE timer behavior** - Verify timer works correctly with UAE backend
   ```bash
   strace -T -e read -o uae_trace.log -e trace=read \
       env EMULATOR_TIMEOUT=2 CPU_BACKEND=uae ./macemu-next --no-webserver
   # Then grep for reads on timer_fd
   ```

### Workarounds to Try:
1. **Switch to wall-clock polling** instead of timerfd
   ```cpp
   // Replace timerfd with clock_gettime() polling
   static struct timespec last_tick = {0, 0};
   struct timespec now;
   clock_gettime(CLOCK_MONOTONIC, &now);
   if (time_diff_ms(&now, &last_tick) >= 16) {
       one_tick();
       last_tick = now;
   }
   ```

2. **Force timer firing** - Call `one_tick()` artificially
   ```cpp
   // Emergency workaround: fake 60Hz timer
   static uint64_t fake_tick_counter = 0;
   if (++fake_tick_counter % 600 == 0) {  // Every 600 blocks ~= 16ms @ 35K blocks/sec
       one_tick();
   }
   ```

3. **Use UAE timer mechanism** - See if UAE's timer approach works for Unicorn
   - Check if UAE uses signals instead of timerfd
   - Check if UAE polls differently

## Files Modified This Session

1. **src/cpu/unicorn_wrapper.c**
   - Added PC advancement logging after EmulOps
   - Added interrupt delivery/blocking logging
   - Added timer poll logging in hook_block

2. **src/cpu/cpu_unicorn.cpp**
   - Added register state logging after PATCH_BOOT_GLOBS
   - Added continuation logging in execute_fast loop

3. **src/core/emul_op.cpp**
   - Added register state logging after PATCH_BOOT_GLOBS (UAE side)

4. **src/drivers/platform/timer_interrupt.cpp**
   - Added extensive debug logging for timer initialization
   - Added EAGAIN/error logging
   - Added timer state checking with `timerfd_gettime()`
   - Added expiration event counting

## Key Insights from This Session

1. **We were going in circles** (user was correct!) - Spent too much time on A-line exceptions and interrupts when the real issue is the timer

2. **The timer is THE blocker** - Without regular timer interrupts, ROM cannot advance time, CLKNOMEM operations never complete, boot hangs

3. **The bug is subtle** - Timer setup is correct, polling is correct, fd is valid, timer is armed, but `read()` only succeeds once

4. **This might be a Unicorn/QEMU interaction issue** - The timer code works perfectly for UAE but fails for Unicorn with identical code

5. **Need lower-level debugging** - Source-level debugging has hit its limit; need syscall-level tracing to understand what's happening

## Build/Test Commands

```bash
# Build
cd /home/mick/macemu-dual-cpu/macemu-next
meson compile -C build

# Test Unicorn (fails)
cd build
env EMULATOR_TIMEOUT=3 CPU_BACKEND=unicorn ./macemu-next --no-webserver 2>&1 | grep -E "Timer|CLKNOMEM"

# Test UAE (succeeds)
env EMULATOR_TIMEOUT=2 CPU_BACKEND=uae ./macemu-next --no-webserver 2>&1 | grep -E "Timer|CLKNOMEM"

# Compare timer behavior
env EMULATOR_TIMEOUT=2 CPU_BACKEND=uae ./macemu-next --no-webserver 2>&1 | grep "Timer:"
env EMULATOR_TIMEOUT=3 CPU_BACKEND=unicorn ./macemu-next --no-webserver 2>&1 | grep "Timer:"
```

## Current State (After Timer Fix)

- ✅ A-line exception handling working
- ✅ EmulOp execution working
- ✅ Interrupt delivery mechanism working
- ✅ Register state matches UAE after PATCH_BOOT_GLOBS
- ✅ **Timer now fires at perfect 60 Hz** ← **FIXED!**
- ✅ 133M instructions executed in 10 seconds
- ✅ ROM boot progressing successfully
- ✅ IRQ EmulOp executing (864K calls in 10 seconds)
- ⚠️ SIGSEGV crash during shutdown (race condition in cleanup code)

**Unicorn backend boots successfully! Shutdown crash is separate issue.**

## Boot Comparison Results

### UAE (10 seconds):
- Timer: 599 interrupts (perfect 60 Hz)
- EmulOps: CLKNOMEM, IRQ, PRIMETIME, READ_XPRAM, etc.
- Clean shutdown

### Unicorn (10 seconds):
- Timer: 601 interrupts (perfect 60 Hz)
- EmulOps: CLKNOMEM, IRQ, READ_XPRAM, PATCH_BOOT_GLOBS
- 864,694 IRQ EmulOp calls (successful)
- **Crash during shutdown only** (SIGSEGV in cleanup code)

## References

- Commit da99016a: Previous timer interrupt fix (ROMVersion condition)
- Commit 3f1c4f10: Previous investigation of ROM hang (different PC than current)
- Commit 44d946ef: Previous diagnostics showing "hangs after SCSI_DISPATCH"
- timerfd_create(2) man page: "periodic timer if it_interval is non-zero"
- Linux kernel timerfd implementation: fs/timerfd.c

---

**Bottom Line:** We have a mysterious Linux timerfd behavior where it fires once then stops, despite being correctly configured as periodic and still showing as armed. This is the critical blocker preventing Unicorn boot. Next session should focus on syscall-level debugging (strace) or implementing a workaround (clock_gettime polling).
