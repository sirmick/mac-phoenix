# Commands Reference

Build, test, run, and debug commands for mac-phoenix.

See also: [CLAUDE.md](../CLAUDE.md) for project overview and architecture.

---

## Build

```bash
# Standard build
ninja -C build

# Clean build
rm -rf build && meson setup build && ninja -C build

# Debug build
meson setup build --buildtype=debug && ninja -C build

# Release build
meson setup build --buildtype=release && ninja -C build

# Reconfigure (after changing meson.build or meson_options.txt)
meson setup build --reconfigure

# Rebuild Unicorn subproject (after modifying QEMU sources)
cd subprojects/unicorn && cmake --build build -j$(nproc) && cd ../..
```

---

## Run

```bash
# Default (UAE backend, web UI on port 8000)
./build/mac-phoenix /home/mick/quadra.rom

# Headless with timeout
EMULATOR_TIMEOUT=10 ./build/mac-phoenix --no-webserver /home/mick/quadra.rom

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
meson test -C build api_endpoints boot_uae mouse_position

# All tests (includes slow Unicorn boot, ~60s)
meson test -C build

# Verbose output
meson test -C build -v

# Playwright E2E tests (requires running emulator on port 18094)
npx playwright test
npx playwright test --headed    # watch in browser
npx playwright test --ui        # interactive UI
```

---

## CLI Flags

```
./build/mac-phoenix [options] [rom-path]
  --rom path            ROM file path (alternative to positional arg)
  --disk path           Disk image path (repeatable)
  --cdrom path          CDROM image path (repeatable)
  --ram MB              RAM size in megabytes
  --port N              HTTP server port (default: 8000)
  --signaling-port N    WebRTC signaling port (default: 8090)
  --backend uae|unicorn Backend override (or use CPU_BACKEND env)
  --arch m68k|ppc       CPU architecture
  --timeout N           Auto-exit after N seconds
  --no-webserver        Headless mode (no HTTP/WebRTC)
  --screen WxH          Display resolution (default: 640x480)
  --config path         JSON config file
  --screenshots         Dump PPM screenshots to /tmp
  --log-level N         Log level 0-3 (or use MACEMU_LOG_LEVEL env)
  --debug-connection    Debug WebRTC connections
  --debug-mode-switch   Debug video mode switches
  --debug-perf          Debug performance
```

---

## Environment Variables

### Core

| Variable | Values | Purpose |
|----------|--------|---------|
| `CPU_BACKEND` | `uae`, `unicorn`, `dualcpu` | Select CPU backend (default: uae) |
| `EMULATOR_TIMEOUT` | seconds | Auto-exit after N seconds |
| `MACEMU_LOG_LEVEL` | 0-3 | 0=milestones, 1=important, 2=all ops, 3=+registers |
| `MACEMU_SCREENSHOTS` | any | Enable PPM screenshot dumps to /tmp |
| `MACEMU_ROM` | path | Default ROM path for tests |

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
ninja -C build && EMULATOR_TIMEOUT=5 ./build/mac-phoenix --no-webserver /home/mick/quadra.rom
```

### Trace Comparison (UAE vs Unicorn)

```bash
# Generate traces
EMULATOR_TIMEOUT=2 CPU_TRACE=0-250000 ./build/mac-phoenix --backend uae \
    --no-webserver /home/mick/quadra.rom > uae.log 2>&1

EMULATOR_TIMEOUT=2 CPU_TRACE=0-250000 ./build/mac-phoenix --backend unicorn \
    --no-webserver /home/mick/quadra.rom > unicorn.log 2>&1

# Compare
diff uae.log unicorn.log | head -50
```

### DualCPU Validation

```bash
EMULATOR_TIMEOUT=30 DUALCPU_TRACE_DEPTH=20 \
    ./build/mac-phoenix --backend dualcpu --no-webserver /home/mick/quadra.rom
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
ninja -C build
```
