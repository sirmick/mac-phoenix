# Commands Reference

Build, run, test, debug.

See [`../CLAUDE.md`](../CLAUDE.md) for a tighter cheat-sheet covering the same
ground; this doc adds debug workflows.

## Build

```bash
# Standard
cmake -B build
cmake --build build -j$(nproc)

# Clean
rm -rf build && cmake -B build && cmake --build build -j$(nproc)

# Debug / Release
cmake -B build -DCMAKE_BUILD_TYPE=Debug   && cmake --build build -j$(nproc)
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)

# Skip optional subprojects
cmake -B build -DBUILD_NET_BRIDGE=OFF      # skip Rust net-bridge
cmake -B build -DBUILD_BROWSER=OFF         # skip MacBrowser host pipeline
cmake -B build -DBUILD_BRIDGE_AGENT=OFF    # skip Retro68 build of BridgeAgent.bin
cmake -B build -DBUILD_MAC_BROWSER=OFF     # skip Retro68 build of MacBrowser.bin
```

## Run

```bash
# UAE (default), web UI on :11000
./build/mac-phoenix ~/storage/roms/quadra.rom

# Headless, time-bounded
./build/mac-phoenix --timeout 10 --no-webserver ~/storage/roms/quadra.rom

# Specific backend (uae | kpx)
./build/mac-phoenix --backend uae --jit ~/storage/roms/quadra.rom
./build/mac-phoenix --backend kpx --rom ~/storage/roms/g3.rom \
                    --disk ~/storage/images/macos-7.6.1.img --ram 128

# Custom port + screen
./build/mac-phoenix --port 9000 --screen 1024x768 ~/storage/roms/quadra.rom

# Custom config file (or /dev/null to ignore the user file)
./build/mac-phoenix --config myconfig.json ~/storage/roms/quadra.rom
./build/mac-phoenix --config /dev/null     ~/storage/roms/quadra.rom

# PPM screenshot dump for non-WebRTC capture
./build/mac-phoenix --screenshots --no-webserver ~/storage/roms/quadra.rom
```

## CLI flags

`--help` prints the live list:

```
Machine:
  --rom PATH                 ROM file (or positional arg)
  --ram MB                   RAM in MB (default 64)
  --screen WxH               Display resolution (default 640x480)
  --disk PATH                Disk image (repeatable)
  --cdrom PATH               CD-ROM image (repeatable)
  --extfs PATH               Shared host folder (repeatable)
  --bootdriver N             0=any, -62=CD-ROM
  --storage-dir PATH         Default storage root (default ~/storage)

CPU:
  --backend NAME             uae | kpx
                             (default: uae; backend implies architecture)
  --jit / --no-jit           Enable backend's primary JIT (uae, kpx)
  --jit68k / --no-jit68k     Enable 68k-on-PPC DR JIT (kpx; default on)
  --idlewait / --no-idlewait Pause CPU when guest is idle (default on)

Media / I/O:
  --audio                    Enable audio (Opus over WebRTC)
  --zap-pram                 Clear PRAM on startup
  --dismiss-shutdown-dialog  Auto-dismiss "improper shutdown" dialog
  --no-dismiss-shutdown-dialog
  --serial-a PATH            Serial port A backend (PTY/tty)
  --serial-b PATH            Serial port B backend

Networking:
  --network MODE             none | socket[:PATH]

Automation:
  --bridge                   Enable BridgeAgent automation + auto ExtFS mount
  --browser                  Run MacBrowser (in-process Chromium via Qt6 WebEngine)
  --headless-http            HTTP API only, no video/audio (implies --bridge)

Server:
  --port N                   HTTP+WS port (default 11000) — also hosts /ws
  --no-webserver             Headless, no HTTP/WebRTC
  --timeout N                Auto-exit after N seconds
  --config PATH              JSON config file
  --screenshots              Dump PPM frames to /tmp

Logging:
  --log-level N              0–3
  --debug-connection         WebRTC connection details
  --debug-mode-switch        Video mode switches
  --debug-perf               Performance stats
  --debug-network            net-bridge NAT/DNS/ICMP/TCP/UDP

Internal:
  --ipc                      Run as the CPU IPC subprocess (set by parent)
```

There is no `--arch`, `--signaling-port`, or `--appliance` flag — earlier docs
that mentioned them are wrong. `--backend` takes its value as a separate
argument; the `--backend=NAME` form is not recognised and silently falls
through to the default.

## Test

```bash
# Whole suite
ctest --test-dir build

# By label
ctest --test-dir build -L unit       # mac_roman, browser_shm
ctest --test-dir build -L api        # api_endpoints, config_api, extfs
ctest --test-dir build -L boot       # boot_*, mouse_position, command_bridge
ctest --test-dir build -L bridge     # wne_patch_sanity
ctest --test-dir build -L guest      # guest_suite{,_761,_ppc}

# Specific
ctest --test-dir build -R api_endpoints
ctest --test-dir build -R "boot_uae|boot_ppc"

# Verbose
ctest --test-dir build -V

# Override defaults from CMake
cmake -B build -DTEST_ROM=/path/to/quadra.rom \
               -DTEST_PPC_ROM=/path/to/g3.rom \
               -DTEST_SE_ROM=/path/to/mac-se.rom

# Playwright E2E (auto-spawns emulator on :18094)
npx playwright test
npx playwright test --headed
```

The test names registered in `tests/CMakeLists.txt` are
`mac_roman`, `browser_shm`, `api_endpoints`, `config_api`, `extfs`, `boot_se`,
`boot_uae_interp`, `boot_uae_jit`, `boot_ppc_interp`,
`boot_ppc_jit`, `boot_ppc_api`, `mouse_position`, `mouse_position_ppc`,
`command_bridge`, `command_bridge_ppc`, `wne_patch_sanity`, `guest_suite`,
`guest_suite_761`, `guest_suite_ppc`.

## Boot capacity matrix

```bash
# All backend × JIT × OS cells — serial, with screenshots + per-cell logs
tests/run_boot_matrix.sh --out test-results/boot-matrix

# Single cell
tests/test_boot_matrix.sh --label uae-755 --backend uae \
    --rom ~/storage/roms/quadra.rom --disk ~/storage/images/macos-7.5.5.img \
    --timeout 60 --port 19300 --screenshot-dir /tmp/one-cell
```

## Environment variables

The emulator binary itself **does not** read configuration from the
environment — use CLI flags or the JSON config. The vars below are read by
test scripts or by trace/debug code paths.

| Variable | Scope | Purpose |
|----------|-------|---------|
| `MACEMU_ROM`, `MACEMU_DISK`, `MACEMU_ROM_M68K`, `MACEMU_ROM_PPC`, `MACEMU_DISK_755`, `MACEMU_DISK_761` | tests | Override default test ROM/disk paths |

### Tracing (m68k)

| Variable | Effect |
|----------|--------|
| `CPU_TRACE=N` or `N-M` | Trace first N instructions or range |
| `CPU_TRACE_MEMORY=1` | Include memory reads in trace |
| `CPU_TRACE_QUIET=1` | Suppress banner, trace only |
| `EMULOP_VERBOSE=1` | Log EmulOp dispatch |
| `MACEMU_DEBUG_PERF=1` | Video encoder thread performance statistics |

### Tracing (PPC / KPX)

| Variable | Effect |
|----------|--------|
| `MACEMU_PPC_TRACE=<path>` | Per-EmulOp boundary state (EMULOP + POST lines) to a file |
| `MACEMU_PPC_CR2_TRACE=<lo>[:<hi>]` | Per-instruction `[CR]` lines for EmulOp seq in `[lo, hi)`; disables the KPX JIT |
| `MACEMU_PPC_TRACE_TRAP=1` | Log each `execute_68k` / EXEC_NATIVE dispatch |
| `MACEMU_PPC_TRACE_68K_ENTRY=<hex>[,<hex>...]` | Dump register context when a listed 68k PC (`r24`) executes; disables the KPX JIT |
| `MACEMU_PPC_TRACE_68K_MAX=N` | Max hits per target for `TRACE_68K_ENTRY` before suppression (default 5) |
| `MACEMU_PPC_NO_IRQ=1` | Suppress async IRQ injection so EmulOp traces are deterministic |

## Debug workflows

### Quick smoke

```bash
cmake --build build -j$(nproc) && \
    ./build/mac-phoenix --timeout 5 --no-webserver ~/storage/roms/quadra.rom
```

### Instruction trace (m68k)

```bash
CPU_TRACE=0-250000 ./build/mac-phoenix --backend uae --timeout 2 \
    --no-webserver ~/storage/roms/quadra.rom > uae.log 2>&1
```

### GDB

```bash
gdb --args ./build/mac-phoenix --no-webserver ~/storage/roms/quadra.rom
```

### perf

```bash
sudo sysctl kernel.perf_event_paranoid=-1
perf record -g -F 997 ./build/mac-phoenix --backend uae \
    --no-webserver ~/storage/roms/quadra.rom
perf report
```

## Config file

```
~/.config/mac-phoenix/config.json   (default)
--config /path/to/config.json       (override)
```

Schema: [JsonConfig.md](JsonConfig.md). The web UI's settings dialog round-trips
through `GET /api/config` / `POST /api/config` and only persists fields the UI
actually changes — CLI args are runtime-only and never saved.

## Troubleshooting

- **ROM not found**: use absolute paths, `~` expansion fails in some shells.
- **Port in use**: pick a different `--port`. `/ws` rides the same port; there
  is no separate signaling port.
