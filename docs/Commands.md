# Commands Reference

Build, test, run, and debug commands for mac-phoenix.

See also: [CLAUDE.md](../CLAUDE.md) for project overview and architecture.

---

## Build

```bash
# Standard build
cmake --build build -j$(nproc)

# Clean build
rm -rf build && cmake -B build && cmake --build build -j$(nproc)

# Debug build
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)

# Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)

# Reconfigure (after changing CMakeLists.txt options)
cmake -B build

# Rebuild Unicorn subproject (after modifying QEMU sources)
cd subprojects/unicorn && cmake --build build -j$(nproc) && cd ../..
```

---

## Run

```bash
# Default (UAE backend, web UI on port 11000)
./build/mac-phoenix /home/mick/quadra.rom

# Headless with timeout
./build/mac-phoenix --timeout 10 --no-webserver /home/mick/quadra.rom

# Specific backend
./build/mac-phoenix --backend unicorn /home/mick/quadra.rom

# Custom ports
./build/mac-phoenix --port 9000 --signaling-port 9001 /home/mick/quadra.rom

# Custom screen resolution
./build/mac-phoenix --screen 1024x768 /home/mick/quadra.rom

# Custom config file
./build/mac-phoenix --config myconfig.json /home/mick/quadra.rom

# Screenshot mode (dumps PPM to /tmp)
./build/mac-phoenix --screenshots --no-webserver /home/mick/quadra.rom
```

---

## Test

```bash
# Fast tests (API + UAE boot + mouse, ~12s)
ctest --test-dir build -R "api_endpoints|boot_uae|mouse_position"

# All tests (includes slow Unicorn boot, ~60s)
ctest --test-dir build

# Verbose output
ctest --test-dir build -V

# Playwright E2E tests (requires running emulator on port 18094)
npx playwright test
npx playwright test --headed    # watch in browser
npx playwright test --ui        # interactive UI
```

---

## CLI Flags

```
./build/mac-phoenix [options] [rom-path]
  --rom PATH                 ROM file path (or positional arg)
  --disk PATH                Disk image path (repeatable)
  --cdrom PATH               CD-ROM image path (repeatable)
  --extfs PATH               Shared host folder (repeatable)
  --ram MB                   RAM size in megabytes (default: 64)
  --screen WxH               Display resolution (default: 640x480)
  --port N                   HTTP server port (default: 11000) — also hosts /ws signaling
  --backend NAME             uae | unicorn-m68k | unicorn-ppc | kpx | dualcpu
                             (default: uae; backend implies architecture)
  --jit / --no-jit           Enable backend's primary JIT (uae, kpx)
  --jit68k / --no-jit68k     Enable 68k-on-PPC DR JIT (kpx only)
  --idlewait / --no-idlewait Pause CPU when guest is idle (default: on)
  --network MODE             none | socket[:PATH]
  --bridge                   Enable automation bridge
  --audio                    Enable audio
  --timeout N                Auto-exit after N seconds
  --no-webserver             Headless mode (no HTTP/WebRTC)
  --config PATH              JSON config file
  --screenshots              Dump PPM screenshots to /tmp
  --log-level N              Log level 0-3
  --debug-connection         Debug WebRTC connections
  --debug-mode-switch        Debug video mode switches
  --debug-perf               Debug performance
  --debug-network            Debug net-bridge / lwIP
```

---

## Environment Variables

The emulator binary does not read environment variables for configuration — use CLI flags instead (e.g. `--backend`, `--timeout`, `--log-level`, `--screenshots`).

| Variable | Scope | Purpose |
|----------|-------|---------|
| `MACEMU_ROM` | Test scripts only | Default ROM path (not read by the binary) |

### Tracing & Debugging

| Variable | Values | Purpose |
|----------|--------|---------|
| `CPU_TRACE` | `N` or `N-M` | Trace first N instructions or range N to M |
| `CPU_TRACE_MEMORY` | `1` | Include memory accesses in trace output |
| `CPU_TRACE_QUIET` | `1` | Suppress normal output, trace only |
| `EMULOP_VERBOSE` | `1` | Log EmulOp calls |

### DualCPU Validation

| Variable | Values | Purpose |
|----------|--------|---------|
| `DUALCPU_TRACE_DEPTH` | N | History depth for divergence analysis |
| `DUALCPU_MASTER` | `uae` or `unicorn` | Which CPU is authoritative on divergence (default: uae) |

---

## Debug Workflows

### Quick Test

```bash
# Build and test (5 second boot)
cmake --build build -j$(nproc) && ./build/mac-phoenix --timeout 5 --no-webserver /home/mick/quadra.rom
```

### Trace Comparison (UAE vs Unicorn)

```bash
# Generate traces
CPU_TRACE=0-250000 ./build/mac-phoenix --backend uae --timeout 2 \
    --no-webserver /home/mick/quadra.rom > uae.log 2>&1

CPU_TRACE=0-250000 ./build/mac-phoenix --backend unicorn --timeout 2 \
    --no-webserver /home/mick/quadra.rom > unicorn.log 2>&1

# Compare
diff uae.log unicorn.log | head -50
```

### DualCPU Validation

```bash
DUALCPU_TRACE_DEPTH=20 \
    ./build/mac-phoenix --backend dualcpu --timeout 30 --no-webserver /home/mick/quadra.rom
```

### GDB

```bash
gdb --args ./build/mac-phoenix --no-webserver /home/mick/quadra.rom
```

### Perf Profiling

```bash
sudo sysctl kernel.perf_event_paranoid=-1
perf record -g -F 997 ./build/mac-phoenix --backend unicorn --no-webserver /home/mick/quadra.rom
perf report
```

---

## Configuration

```bash
# Edit config
nano ~/.config/mac-phoenix/config.json

# Or copy example
cp config.example.json ~/.config/mac-phoenix/config.json
```

See [JsonConfig.md](JsonConfig.md) for config file format.

---

## Troubleshooting

**ROM not found**: Use absolute path — `~` expansion can fail in some contexts.

**Port already in use**: Use `--port` and `--signaling-port` to pick different ports.

**Unicorn build issues**: Rebuild the subproject:
```bash
cd subprojects/unicorn && cmake --build build -j$(nproc) && cd ../..
cmake --build build -j$(nproc)
```
