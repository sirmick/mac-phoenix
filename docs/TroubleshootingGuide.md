# Troubleshooting

Quick triage for the common failure modes. Backend-specific perf details
live in [`UnicornPerformanceAnalysis.md`](UnicornPerformanceAnalysis.md);
PPC-specific debug knobs live in
[`ppc/UnicornPpcStatus.md`](ppc/UnicornPpcStatus.md).

## First-line checks

```bash
# Headless smoke — should print "Boot phase: …" lines and exit clean
./build/mac-phoenix --no-webserver --timeout 5 ~/storage/roms/quadra.rom

# Compare backends side-by-side
./build/mac-phoenix --backend uae           --no-webserver --timeout 5 ~/storage/roms/quadra.rom 2>&1 | tail -20
./build/mac-phoenix --backend unicorn-m68k  --no-webserver --timeout 15 ~/storage/roms/quadra.rom 2>&1 | tail -20

# Trace first N instructions (m68k)
CPU_TRACE=0-1000 ./build/mac-phoenix --no-webserver ~/storage/roms/quadra.rom 2>&1 | head -100

# DualCPU lockstep — fail-fast on register divergence
./build/mac-phoenix --backend dualcpu --no-webserver ~/storage/roms/quadra.rom
```

## Common issues

### Boot hangs early

m68k: enable `CPU_TRACE` over the suspect range, look for an EmulOp loop.
The IRQ EmulOp encoding is `0x7129` in `src/core/rom_patches.cpp` — if
you see it dispatching as `0xAE29`, the patcher regressed.

PPC: check `boot_phase` in `/api/status` (or stderr `[Boot +N.Ns]`
markers). A stall at "Installing drivers" with healthy DiskPrime
throughput is the SCALE=1 throughput collapse — see
`ppc/UnicornPpcStatus.md`.

### "ROM not found"

Use absolute paths. `~` expansion fails in some shells / IDEs. Tests use
`MACEMU_ROM` (m68k) or `TEST_PPC_ROM` (PPC) cmake cache variables.

### Port already in use

Pick a different `--port`. There is no separate signaling port — `/ws`
rides the same TCP listener.

### Unicorn-m68k boot is slow

That's the QEMU TCG cost, not a bug. ~12 s to Finder is current; the
detailed perf breakdown is in [`UnicornPerformanceAnalysis.md`](UnicornPerformanceAnalysis.md).
Persistent TB caching across runs would close most of the remaining
gap (not done).

### Unicorn-PPC reaches `[Boot +N] Desktop ready` but the framebuffer is
hourglass-only or all black

Read [`ppc/UnicornPpcStatus.md`](ppc/UnicornPpcStatus.md) before
debugging — the headless `Desktop ready` flag flips on a `CurApName` peek
and idle poll, not on actual paint. Verify with `/api/screenshot`.

### Build issues in `subprojects/unicorn`

Rebuild that subproject directly first, then the top level:

```bash
cmake --build subprojects/unicorn/build -j$(nproc)
cmake --build build -j$(nproc)
```

If patches in `subprojects/unicorn-patches/` won't apply, the vendored
tree drifted from pristine 2.1.4 — `git -C subprojects/unicorn status`
to find the divergence.

## Debug environment variables

These are read by the binary or test scripts, not by the JSON config.

### m68k tracing

| Variable | Purpose |
|----------|---------|
| `CPU_TRACE=N` or `N-M` | Trace first N instructions or range |
| `CPU_TRACE_MEMORY=1` | Include memory reads |
| `CPU_TRACE_QUIET=1` | Suppress banner |
| `EMULOP_VERBOSE=1` | Log each EmulOp dispatch |
| `MACEMU_DEBUG_PERF=1` | Enable per-block timing in Unicorn-m68k hooks |

### DualCPU validation

| Variable | Purpose |
|----------|---------|
| `DUALCPU_TRACE_DEPTH=N` | History depth on divergence |
| `DUALCPU_MASTER=uae|unicorn` | Authoritative side (default `uae`) |

### PPC

See [`ppc/UnicornPpcStatus.md`](ppc/UnicornPpcStatus.md) — the
`MACEMU_PPC_*` family of knobs is documented there.

## GDB

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)
gdb --args ./build/mac-phoenix --no-webserver ~/storage/roms/quadra.rom
```

Useful breakpoints:

| Where | Why |
|-------|-----|
| `unicorn_execute_with_interrupts` (`unicorn_exec_loop.c`) | Top of the Unicorn-m68k execute loop |
| `apply_deferred_updates_and_flush` (`unicorn_wrapper.c`) | Where deferred register writes land |
| `command_bridge_*` (`src/core/command_bridge.cpp`) | Bridge read commands |
| `bridge_command` (`src/webserver/api_handlers.cpp`) | Action commands (LAUNCH/QUIT/SHUTDOWN/RESTART) |
| `ppc_emul_op` / `execute_native_op_pure` (`src/cpu/kpx/`) | PPC EmulOp / NativeOp dispatch |

## Memory debugging

```bash
valgrind --leak-check=full --suppressions=tools/valgrind.supp \
    ./build/mac-phoenix --no-webserver --timeout 5 ~/storage/roms/quadra.rom
```

Expect noise from Unicorn's TCG code generator and libdatachannel — live
with it or build with `-DBUILD_BROWSER=OFF -DBUILD_NET_BRIDGE=OFF` to
narrow the surface.

## Log analysis

```bash
# EmulOp histogram
grep "EmulOp" /tmp/run.log | awk '{print $NF}' | sort | uniq -c | sort -rn

# Boot phases
grep -E "\[Boot \+[0-9]+\.[0-9]+s\]" /tmp/run.log

# Errors / aborts
grep -iE "error|fail|crash|abort|assert" /tmp/run.log
```

## Where to look first

| Failure | Files |
|---------|-------|
| ROM patching regression | `src/core/rom_patches.cpp` (m68k), `src/cpu/kpx/rom_patches_ppc.cpp` (PPC) |
| Boot phase wrong / stuck | `src/core/boot_progress.cpp` |
| EmulOp dispatch | `src/core/emul_op.cpp` (m68k), `src/cpu/kpx/emul_op_ppc.cpp` (PPC) |
| Bridge timeouts | `src/core/command_bridge.cpp`, `src/webserver/api_handlers.cpp:bridge_command`, `BridgeAgent/BridgeAgent.c` |
| Mouse / keyboard | `src/core/adb.cpp`, `src/webserver/api_handlers.cpp:1105+` |
| Video frame issues | `src/drivers/video/video_output.cpp`, `video_encoder_thread.cpp` |
| WebRTC / signaling | `src/webrtc/`, `src/webserver/websocket.cpp` |
| MacBrowser | `src/drivers/browser/`, `MacBrowser/MacBrowser.c`, `docs/MacBrowser.md` |
