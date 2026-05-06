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

# Rebuild Unicorn after touching qemu sources
cd subprojects/unicorn && cmake --build build -j$(nproc) && cd ../..
```

## Run

```bash
# UAE (default), web UI on :11000
./build/mac-phoenix ~/storage/roms/quadra.rom

# Headless, time-bounded
./build/mac-phoenix --timeout 10 --no-webserver ~/storage/roms/quadra.rom

# Specific backend (uae | unicorn-m68k | unicorn-ppc | kpx | dualcpu)
./build/mac-phoenix --backend unicorn-m68k ~/storage/roms/quadra.rom
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
  --backend NAME             uae | unicorn-m68k | unicorn-ppc | kpx | dualcpu
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
  --browser                  Run MacBrowser (Firefox-on-Xvfb host pipeline)
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
that mentioned them are wrong.

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
`boot_uae_interp`, `boot_uae_jit`, `boot_unicorn`, `boot_ppc_interp`,
`boot_ppc_jit`, `boot_ppc_api`, `mouse_position`, `mouse_position_ppc`,
`command_bridge`, `command_bridge_ppc`, `wne_patch_sanity`, `guest_suite`,
`guest_suite_761`, `guest_suite_ppc`.

## Boot capacity matrix

```bash
# 12 cells: backend × JIT × OS — serial, with screenshots + per-cell logs
tests/run_boot_matrix.sh --out test-results/boot-matrix

# Single cell
tests/test_boot_matrix.sh --label unicorn-m68k-755 --backend unicorn-m68k \
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
| `MACEMU_DEBUG_PERF=1` | Enable per-block timing in Unicorn-m68k hooks |

### DualCPU validation

| Variable | Effect |
|----------|--------|
| `DUALCPU_TRACE_DEPTH=N` | History depth for divergence reports |
| `DUALCPU_MASTER=uae\|unicorn` | Authoritative side on divergence (default uae) |

### Unicorn PPC (debug knobs — see `docs/ppc/UnicornPpcStatus.md`)

`MACEMU_PPC_TICK_PERIOD_SCALE`, `MACEMU_PPC_NO_IRQ`,
`MACEMU_PPC_BLOCK_TRACE`, `MACEMU_PPC_TRACE`, `MACEMU_PPC_TRACE_TRAP`,
`MACEMU_PPC_TRACE_IRQ`, `MACEMU_PPC_TRACE_68K_ENTRY`,
`MACEMU_PPC_CR2_TRACE`, `MACEMU_PPC_NO_IRQ_HOOK`,
`MACEMU_PPC_TB_FLUSH_EVERY`, `MACEMU_PPC_MIN_EMULOPS_PER_IRQ`,
`MACEMU_PPC_DEFER_FIRST_IRQ`.

## Debug workflows

### Quick smoke

```bash
cmake --build build -j$(nproc) && \
    ./build/mac-phoenix --timeout 5 --no-webserver ~/storage/roms/quadra.rom
```

### UAE vs Unicorn-m68k trace diff

```bash
CPU_TRACE=0-250000 ./build/mac-phoenix --backend uae --timeout 2 \
    --no-webserver ~/storage/roms/quadra.rom > uae.log 2>&1
CPU_TRACE=0-250000 ./build/mac-phoenix --backend unicorn-m68k --timeout 2 \
    --no-webserver ~/storage/roms/quadra.rom > unicorn.log 2>&1
diff uae.log unicorn.log | head -50
```

### DualCPU lockstep

```bash
DUALCPU_TRACE_DEPTH=20 \
    ./build/mac-phoenix --backend dualcpu --timeout 30 \
    --no-webserver ~/storage/roms/quadra.rom
```

### GDB

```bash
gdb --args ./build/mac-phoenix --no-webserver ~/storage/roms/quadra.rom
```

### perf

```bash
sudo sysctl kernel.perf_event_paranoid=-1
perf record -g -F 997 ./build/mac-phoenix --backend unicorn-m68k \
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
- **Unicorn build issues**: rebuild `subprojects/unicorn`'s build dir directly,
  then run the top-level build again.
