# Troubleshooting

Quick triage for the common failure modes. PPC-specific background lives
in [`ppc/README.md`](ppc/README.md); the trace environment variables are
listed in [`Commands.md`](Commands.md#environment-variables).

## First-line checks

```bash
# Headless smoke: should print "[Boot +  N.NNs] ..." milestones and exit clean
./build/mac-phoenix --no-webserver --timeout 5 ~/roms/quadra.rom

# Compare cores side by side: UAE interpreter, UAE JIT, MAME's Musashi
./build/mac-phoenix --no-jit --no-webserver --timeout 5 ~/roms/quadra.rom 2>&1 | tail -20
./build/mac-phoenix --jit    --no-webserver --timeout 5 ~/roms/quadra.rom 2>&1 | tail -20
./build/mac-phoenix --core mame-68k --no-webserver --timeout 5 ~/roms/quadra.rom 2>&1 | tail -20

# Ignore the saved config (~/.config/mac-phoenix/config.json) for a scratch run
./build/mac-phoenix --config /dev/null --no-webserver --timeout 5 ~/roms/quadra.rom

# Trace first N instructions (m68k, UAE core)
CPU_TRACE=0-1000 ./build/mac-phoenix --no-webserver ~/roms/quadra.rom 2>&1 | head -100
```

## Common issues

### Boot hangs early

Check `boot_phase` in `/api/status` (or the stderr `[Boot +  N.NNs]`
milestones) to see which phase stalled.

m68k: enable `CPU_TRACE` over the suspect range (UAE core), look for an
EmulOp loop. EmulOps are `0x71xx` (`src/common/include/emul_op.h`; the IRQ
EmulOp is `0x7129`); `src/core/rom_patches.cpp` emits them through
`platform_make_emulop()`. Trying the other core (`--core mame-68k` vs
`--core uae`) tells a core bug from a patching one. UAE `--jit` stalls are a
known issue.

PPC: enable `MACEMU_PPC_TRACE` to find the last EmulOp dispatched; compare
`--core kpx` with `--core mame-ppc` (`--debug-mame-ppc` prints a per-second
PC sample on mame-ppc).

### "ROM not found"

Use absolute paths; `~` is not expanded when the shell doesn't expand it
(quoted, or from an IDE). A relative ROM path is looked up in
`<storage>/roms`, relative disks in `<storage>/images`. Tests take the ROM
from `MACEMU_ROM`, which ctest sets from the `TEST_ROM` (68k) or
`TEST_PPC_ROM` (PPC) cache variable; see
[Testing.md](Testing.md#configuration).

### Port already in use

Pick a different `--port`. There is no separate signaling port: `/ws`
rides the same TCP listener. Tests use fixed ports in 18080-18442
([Testing.md](Testing.md#running-in-parallel)).

### Log says `Desktop ready` but the screen is hourglass-only or black

The `Desktop ready (Finder idle, no modal)` milestone comes from the Finder
being the current application and idle, not from what is painted. Check with
`/api/screenshot`, or `/api/text` for what has been drawn.

### Bridge commands time out

`/api/launch`, `/api/script` and the other action commands need
`--bridge` (or `--headless-http`, which implies it) and the guest's
BridgeAgent, which the emulator launches from the Host volume after the
Finder is up. Check `bridge_agent_connected` in `/api/status`. Requests go
through the bridge trap `$ABE5`; see [CommandBridge.md](CommandBridge.md).

## Debug flags and environment variables

Flags: `--log-level 0-3`, `--debug-perf` (encoder statistics),
`--debug-connection`, `--debug-mode-switch`, `--debug-network`,
`--debug-mame-ppc`, `--trace-atraps` (A-trap and caller PC pairs, written
with snapshots), and for Executor `--executor-logtraps`, `--executor-debug`
and `--watch ADDR[:LEN]` (`./build/mac-phoenix --help`).

Environment variables (m68k `CPU_TRACE*`, `MACEMU_DEBUG_FRAMES`,
`MACEMU_DEBUG_AUDIO`, the PPC `MACEMU_PPC_*` knobs) are tabulated in
[`Commands.md`](Commands.md#environment-variables).

## GDB

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j4
gdb --args ./build/mac-phoenix --no-webserver ~/roms/quadra.rom
```

Useful breakpoints:

| Where | Why |
|-------|-----|
| `m68k_do_execute` (`src/cpu/uae_cpu/newcpu.cpp`) | UAE interpreter inner loop (opcode fetch, `cpufunctbl` dispatch, `SPCFLAGS` check) |
| `trigger_interrupt` (`src/cpu/core/platform_cpu.cpp`) | Where `g_platform.cpu_trigger_interrupt` lands on the 68k machines (any 68k core) |
| `command_bridge_read` (`src/core/command_bridge.cpp`) | Bridge read commands |
| `bridge_command` (`src/webserver/api_handlers.cpp`) | Action commands (LAUNCH/QUIT/SHUTDOWN/RESTART) |
| `ppc::EmulOp` (`src/cpu/kpx/emul_op_ppc.cpp`), `ppc_native_op_pure` (`src/cpu/kpx/native_ops_ppc.cpp`) | PPC EmulOp / NativeOp dispatch |

## Memory debugging

```bash
valgrind --leak-check=full \
    ./build/mac-phoenix --no-webserver --timeout 5 ~/roms/quadra.rom
```

There is no suppressions file in the tree. Expect noise from libdatachannel
and Qt (and from Qt WebEngine in a `-DBUILD_BROWSER=ON` build).

For Executor, its own tools are quicker: `--executor-debug memcheck` (heap
checks at every Memory Manager call) and `--watch ADDR[:LEN]` (every write
to a guest address, with the 68k PC, registers and the last traps).

## Log analysis

```bash
# Boot milestones
grep -E "\[Boot \+ *[0-9]+\.[0-9]+s\]" /tmp/run.log

# Errors / aborts
grep -iE "error|fail|crash|abort|assert" /tmp/run.log
```

## Where to look first

| Failure | Files |
|---------|-------|
| ROM patching regression | `src/core/rom_patches.cpp` (m68k), `src/cpu/kpx/rom_patches_ppc.cpp` (PPC) |
| Boot phase wrong / stuck | `src/core/boot_progress.cpp` |
| EmulOp dispatch | `src/core/emul_op.cpp` (m68k), `src/cpu/kpx/emul_op_ppc.cpp` (PPC) |
| CPU core | `src/cpu/core/` (the `cpu::Core` interface), `src/cpu/uae_cpu/`, `src/cpu/mame/`, `src/cpu/kpx/` |
| Bridge timeouts | `src/core/command_bridge.cpp`, `src/common/include/guest_bridge.h`, `bridge_command` in `src/webserver/api_handlers.cpp`, `BridgeAgent/BridgeAgent.c` |
| Mouse / keyboard | `src/core/adb.cpp`; `handle_mouse_move` / `handle_keypress` in `src/webserver/api_handlers.cpp`; `process_input_message` in `src/webrtc/webrtc_server.cpp` |
| Video frame issues | `src/drivers/video/video_output.cpp`, `src/drivers/video/video_encoder_thread.cpp` |
| WebRTC / signaling | `src/webrtc/`, `src/webserver/websocket.cpp` |
| Text feed (`/api/text`) | `src/core/text_feed.cpp` |
| Executor | `src/executor/`, [executor/PLAN.md](executor/PLAN.md) |
| MacBrowser | `src/drivers/browser/`, `MacBrowser/MacBrowser.c`, [MacBrowser.md](MacBrowser.md) |
