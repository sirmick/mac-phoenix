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

# Optional subprojects
cmake -B build -DBUILD_NET_BRIDGE=OFF      # skip Rust net-bridge
cmake -B build -DBUILD_BROWSER=ON          # MacBrowser host pipeline (experimental; off by default)
cmake -B build -DBUILD_BRIDGE_AGENT=OFF    # skip Retro68 build of BridgeAgent.bin
cmake -B build -DBUILD_MAC_BROWSER=OFF     # skip Retro68 build of MacBrowser.bin
cmake -B build -DBUILD_EXECUTOR=OFF        # skip the Executor backend
cmake -B build -DBUILD_TESTS=OFF           # skip the test suite
```

Big parallel builds can run out of memory on a small box; `-j4` is safe.

## Run

```bash
# Quadra 650 (backend uae, UAE core), web UI on :11000
./build/mac-phoenix ~/roms/quadra.rom

# Headless, time-bounded
./build/mac-phoenix --timeout 10 --no-webserver ~/roms/quadra.rom

# 68k cores: uae (default, optional --jit) or mame-68k
./build/mac-phoenix --backend uae --jit ~/roms/quadra.rom
./build/mac-phoenix --backend uae --core mame-68k ~/roms/quadra.rom

# Power Mac G3 (backend kpx): Kheperix (default) or mame-ppc
./build/mac-phoenix --backend kpx --rom ~/storage/roms/g3.rom \
                    --disk ~/storage/images/7.6.1.img --ram 128
./build/mac-phoenix --backend kpx --core mame-ppc --jit \
                    --rom ~/storage/roms/g3.rom --disk 9.0.4.img

# Executor (no ROM), on the System Folder of a disk image in <storage>/images
./build/mac-phoenix --backend executor --executor-system 7.5.5.img
./build/mac-phoenix --backend executor --executor-system 7.5.5.img

# Custom port + screen
./build/mac-phoenix --port 9000 --screen 1024x768 ~/roms/quadra.rom

# Custom config file (or /dev/null to ignore the user file)
./build/mac-phoenix --config myconfig.json ~/roms/quadra.rom
./build/mac-phoenix --config /dev/null     ~/roms/quadra.rom

# PPM screenshot dump for non-WebRTC capture
./build/mac-phoenix --screenshots --no-webserver ~/roms/quadra.rom
```

Executor runs on `mame-68k` and needs no `vm.mmap_min_addr`. `--executor-system` takes a disk image in `<storage>/images`
(its System Folder is extracted into `<storage>/executor-systems` on first
use and the image is mounted read-only beside it) or the name of a prepared
System there. Without it Executor runs on its own data folder
(`<storage>/executor`) and starts in its Browser.

Relative `--disk`/`--cdrom` paths resolve against `<storage>/images`, a
relative ROM path against `<storage>/roms`.

## CLI flags

`--help` prints the live list (`./build/mac-phoenix --help`):

```
Machine:
  --rom PATH                 ROM file (or positional arg)
  --ram MB                   RAM size in megabytes (default: 64)
  --screen WxH               Display resolution (default: 640x480)
  --disk PATH                Disk image (repeatable)
  --cdrom PATH               CD-ROM image (repeatable)
  --extfs PATH               Shared folder (only first is mounted; ExtFS is single-volume)
  --bootdriver N             0=any, -62=CD-ROM (default: 0)
  --storage-dir PATH         Default storage root (default: ~/storage)
  --executor-data PATH       Executor data dir (default: <storage>/executor)
  --executor-app PATH        App Executor starts instead of Browser (host path)
  --executor-system NAME     System under <storage>/executor-systems to run on
  --executor-start WHAT      finder | browser | path inside the System (default: finder)
  --[no-]executor-fresh      Start each run from the System's clean copy (default: on)
  --executor-logtraps        Executor logs every trap call to stderr
  --executor-debug OPTS      Executor's own debug options, comma-separated: memcheck (heap
                             consistency checks at every Memory Manager call, slow), allocs
                             (allocations >= 4K and the code that asked), fslog,
                             trace, trapfailure, unimplemented, unexpected, errno, sound, all
  --watch ADDR[:LEN]         Report every write to guest memory ADDR (hex; LEN 1-16, default 4):
                             old/new value, 68k PC and registers, last traps, host stack (Executor)
  --executor-logtraps-nesting N  ... and calls nested N deep in Toolbox callbacks (default 1)
  --executor-writable-images Executor mounts disk images read-write (default: read-only)
  --[no-]executor-ot         Executor answers Open Transport (default: on)
  --executor-ppc             Executor runs fat applications' PowerPC code (PowerPC-only
                             applications always run it; default: off)

CPU:
  --backend NAME             uae | kpx | executor
                             (default: uae)
  --core NAME                CPU core for the backend's architecture:
                             68k uae | mame-68k (default: uae; musashi = mame-68k)
                             ppc kpx | mame-ppc (default: kpx)
  --jit / --no-jit           Enable backend's primary JIT (uae, kpx)
  --m68k-host N              mame-ppc only: 0 off; 1 host-initiated 68k routines on a
                             mame-68k core; 2 also A-line traps (experiment, breaks the bridge);
                             3 all 68k code on mame-68k (C3b, experimental, does not boot yet)
  --idlewait / --no-idlewait Pause CPU when guest idle (default: on)

Media:
  --audio                    Send sound to the browser (default: off; the Mac has sound hardware either way)
  --audio-dump PATH          Append the audio sent out (raw S16LE 48 kHz stereo) to PATH
  --zap-pram                 Clear PRAM on startup
  --model-id N               Machine type reported to the Mac (default: the ROM
                             machine's; 5 = IIci for Systems before 7.5)
  --dismiss-shutdown-dialog  Auto-dismiss improper-shutdown dialog

Networking:
  --network MODE             none | socket[:PATH] (default: none)

Serial:
  --serial-a PATH            Port A: 'pty' (auto-allocate),
                             /dev/pts/N or /dev/ttyUSB0 (open path),
                             'none'/'off' or omit (disabled)
  --serial-b PATH            Port B (same syntax as --serial-a)

Automation:
  --bridge                   Enable automation bridge
  --browser                  Run MacBrowser (experimental; needs a -DBUILD_BROWSER=ON build)
  --headless-http            HTTP API only (no video/audio)

Server:
  --port N                   HTTP+WS port (default: 11000)
  --no-webserver             Headless, no HTTP/WebRTC
  --timeout N                Auto-exit after N seconds
  --config PATH              JSON config file
  --screenshots              Dump PPM frames to /tmp

Logging:
  --log-level N              0–3
  --debug-connection
  --debug-mode-switch
  --debug-perf
  --debug-network
  --debug-mame-ppc           mame-ppc: log host ops, exceptions, a per-second CPU sample
  --trace-atraps             Record every (A-trap, caller PC); snapshots write atraps.tsv
  --no-text-feed             Don't patch _StdText (the strings on screen for /api/text)
  --deterministic            Virtual clock (instruction-counted ticks, fixed date); implies --no-idlewait
  --snapshot-at NAME@SECS    With --deterministic: snapshot SECS virtual seconds after Finder starts
  -h, --help                 Show this help message
```

Not in `--help`: `--no-dismiss-shutdown-dialog`; `--max-resolution WxH`
(cap on the screen modes offered to the guest; JSON `max_resolution`);
`--ipc` (set by the parent on the CPU subprocess it spawns); and the old
spellings `--executor-cpu` (= `--core`) and `--jit68k`/`--no-jit68k`
(retired, ignored with a warning).

`--headless-http` also turns on `--bridge`. `--bridge` without `--extfs`
makes a scratch shared folder under `<storage>/tmp`, removed at exit.

There is no `--arch`, `--signaling-port` or `--appliance` flag. Flags take
their value as a separate argument: `--backend=NAME` is reported as an
unknown argument and ignored, leaving the default.

## Test

```bash
# Whole suite (parallel is fine: per-test disk copies and net-bridge sockets)
ctest --test-dir build -j4

# By label
ctest --test-dir build -L unit       # mac_roman, browser_shm, cpu_cores
ctest --test-dir build -L api        # api_endpoints, config_api, extfs
ctest --test-dir build -L boot       # boot_*, mouse_position*, command_bridge*
ctest --test-dir build -L bridge     # bridge_trap{,_ppc,_executor}
ctest --test-dir build -L guest      # apps in the guest: suites, notify, keyboard, OT, ...
ctest --test-dir build -L mame       # everything on mame-68k / mame-ppc
ctest --test-dir build -L executor   # Executor guest cells + executor.* gtests

# Specific
ctest --test-dir build -R api_endpoints
ctest --test-dir build -R "boot_uae|boot_ppc"

# List without running
ctest --test-dir build -N
ctest --test-dir build -N -L guest

# Verbose
ctest --test-dir build -V

# Override defaults from CMake
cmake -B build -DTEST_ROM=/path/to/quadra.rom \
               -DTEST_PPC_ROM=/path/to/g3.rom \
               -DTEST_SE_ROM=/path/to/mac-se.rom

# Playwright E2E (spawns its own emulator on :18094)
npx playwright test
npx playwright test --headed
```

The test inventory, labels, the disk images the tests boot
(`tests-7.5.5`, `tests-7.6.1`, `tests-9.0.4`, `6.0.8`, copied per run by
`tests/lib/refresh_test_disk.sh`) and their other prerequisites are in
[Testing.md](Testing.md).

## Boot capacity matrix

```bash
# 7 backend/core/JIT configs x 2 OSes = 14 cells, serial, with screenshots
# and per-cell logs (default --out: ${TMPDIR:-/tmp}/boot_matrix); disks are
# test-7.5.5.img / test-7.6.1.img from tests/lib/refresh_test_disk.sh
tests/run_boot_matrix.sh --out test-results/boot-matrix
tests/run_boot_matrix.sh --only uae-jit-755,mameppc-jit-761   # some cells

# Single cell
tests/test_boot_matrix.sh --label uae-755 --backend uae \
    --rom ~/roms/quadra.rom --disk ~/storage/images/7.5.5.img \
    --timeout 60 --port 19300 --screenshot-dir /tmp/one-cell
```

## Environment variables

The emulator binary does not read configuration from the environment; use
CLI flags or the JSON config. The variables below are read by test scripts
or by trace/debug code paths.

| Variable | Scope | Purpose |
|----------|-------|---------|
| `MACEMU_ROM`, `MACEMU_DISK`, `MACEMU_SE_ROM`, `MACEMU_SE_DISK`, `MACEMU_IICI_ROM`, `MACEMU_IICI_DISK`, `MACEMU_BINARY`, `MACEMU_STORAGE` | tests | Override the test ROM, disk, binary and storage paths |
| `MACEMU_ROM_M68K`, `MACEMU_ROM_PPC`, `MACEMU_DISK_755`, `MACEMU_DISK_761` | `run_boot_matrix.sh` | ROMs and disks for the matrix |

### Tracing (m68k, UAE core)

| Variable | Effect |
|----------|--------|
| `CPU_TRACE=N` or `N-M` | Trace the first N instructions, or the range N-M |
| `CPU_TRACE_MEMORY=1` | Include memory reads in the trace |
| `CPU_TRACE_QUIET=1` | No banner, trace only |
| `M68K_LOG_FILE=<path>` | Where UAE's instruction history log goes (default `log.68k`) |

For performance statistics use `--debug-perf`. `--trace-atraps` records
(A-trap, caller PC) pairs on the ROM machines; `--executor-logtraps` and
`--executor-debug` are Executor's equivalents.

### Debug output

| Variable | Effect |
|----------|--------|
| `MACEMU_DEBUG_FRAMES=1` | Log video frames through the encoder and WebRTC |
| `MACEMU_DEBUG_AUDIO=1` | Log the Opus encoder |
| `MACEMU_TRACE_DISKPRIME=1` | Log each disk driver Prime call |

### Tracing (PPC)

| Variable | Effect |
|----------|--------|
| `MACEMU_PPC_TRACE=<path>` | Per-EmulOp boundary state (EMULOP + POST lines) to a file (kpx and mame-ppc) |
| `MACEMU_PPC_CR2_TRACE=<lo>[:<hi>]` | Per-instruction `[CR]` lines for EmulOp seq in `[lo, hi)`; Kheperix only, disables its JIT |
| `MACEMU_PPC_TRACE_TRAP=1` | Log each `execute_68k` / EXEC_NATIVE dispatch (Kheperix) |
| `MACEMU_PPC_TRACE_68K_ENTRY=<hex>[,<hex>...]` | Dump register context when a listed 68k PC (`r24`) executes; Kheperix only, disables its JIT |
| `MACEMU_PPC_TRACE_68K_MAX=N` | Max hits per target for `TRACE_68K_ENTRY` before suppression (default 5) |
| `MACEMU_PPC_NO_IRQ=1` | Suppress async IRQ injection so EmulOp traces are deterministic (kpx and mame-ppc) |

`--debug-mame-ppc` prints mame-ppc's host ops, exceptions and a per-second
CPU sample.

## Debug workflows

### Quick smoke

```bash
cmake --build build -j$(nproc) && \
    ./build/mac-phoenix --timeout 5 --no-webserver ~/roms/quadra.rom
```

### Instruction trace (m68k)

```bash
CPU_TRACE=0-250000 ./build/mac-phoenix --backend uae --timeout 2 \
    --no-webserver ~/roms/quadra.rom > uae.log 2>&1
```

### GDB

```bash
gdb --args ./build/mac-phoenix --no-webserver ~/roms/quadra.rom
```

### perf

```bash
sudo sysctl kernel.perf_event_paranoid=-1
perf record -g -F 997 ./build/mac-phoenix --backend uae \
    --no-webserver ~/roms/quadra.rom
perf report
```

## Config file

```
~/.config/mac-phoenix/config.json   (default)
--config /path/to/config.json       (override)
```

Schema: [JsonConfig.md](JsonConfig.md). The web UI's settings dialog round-trips
through `GET /api/config` / `POST /api/config` and only persists fields the UI
actually changes; CLI args are runtime-only and never saved.

## Troubleshooting

- **ROM not found**: use absolute paths, `~` expansion fails in some shells.
- **Port in use**: pick a different `--port`. `/ws` rides the same port; there
  is no separate signaling port.
