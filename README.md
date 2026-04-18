# MacPhoenix

A classic Macintosh emulator that runs in your browser. Boot System 6 through Mac OS 9 and interact with it over WebRTC — no native GUI needed.

![License](https://img.shields.io/badge/license-GPL--2.0-blue)

![Mac OS 7.5.5 running in MacPhoenix — browser UI](docs/images/browser-desktop.png)

## What is this?

MacPhoenix is a ground-up rewrite of the [BasiliskII/SheepShaver](https://github.com/kanjitalk755/macemu) emulator family. It replaces the SDL desktop UI with a web-based streaming interface: the emulator runs as a headless server and streams video to your browser via WebRTC, with keyboard and mouse input sent back over a data channel.

Most-tested guest OS versions: **System 6.0.8**, **System 7.5.5**, **Mac OS 7.6.1**, and **Mac OS 9.0.4**. Other 7.x / 8.x / 9.x releases mostly work; file a bug if they don't.

### Key features

- **Multiple Macs** — Mac SE, Quadra 650, and Power Mac G3, with more on the way
- **Browser UI** — connect from any device, no plugins or installs
- **WebRTC streaming** — low-latency video with H.264, VP9, PNG, or WebP encoding
- **REST API** — boot status, screenshots, config, app launching, and control via HTTP endpoints
- **Automation bridge** — launch classic apps, graceful guest shutdown and restart, with clipboard integration on the roadmap
- **NAT networking** — optional Unix-socket bridge (smoltcp + NAT) so the guest can reach the Internet without root
- **Headless mode** — run without any UI for testing and automation

## Quick start

### 1. Install dependencies (Ubuntu/Debian)

```bash
# Required — build toolchain + JSON + libyuv for color conversion
sudo apt install build-essential cmake pkg-config git \
                 libssl-dev nlohmann-json3-dev libyuv-dev

# Video/audio encoders (all optional — each codec appears in the UI only
# if its library is present at configure time; PNG is always available).
sudo apt install libopenh264-dev   # H.264
sudo apt install libvpx-dev        # VP9
sudo apt install libwebp-dev       # WebP
sudo apt install libopus-dev       # Opus audio

# Provisioning tools (for creating/populating HFS disk images)
sudo apt install hfsutils unar
```

macOS equivalents are in [CLAUDE.md](CLAUDE.md#optional-codec-dependencies).

### 2. Build

```bash
git clone --recursive https://github.com/sirmick/mac-phoenix.git
cd mac-phoenix
cmake -B build
cmake --build build -j$(nproc)
```

`cmake -B build` probes for OpenH264 / VP9 / WebP / Opus / libyuv and prints which codecs were found — re-run it after installing additional encoder libraries.

### 3. Run

```bash
./build/mac-phoenix /path/to/quadra.rom
```

Open **http://localhost:8000** in your browser. That's it — you'll see the Mac desktop in a few seconds.

### What you need

- Linux (x86_64)
- A compatible ROM file (not included)

You choose a machine profile, then supply a matching ROM:

| Machine | CPU | Mac OS | Display | ROM needed |
|---------|-----|--------|---------|------------|
| Mac SE | 68000 | System 6 | 512×342 mono | Mac SE (256 KB) |
| Quadra 650 | 68040 | 7.1–7.6 | 640×480 color | Quadra 650 (1 MB) |
| Power Mac G3 | PPC 603e | 8.1–9.2 | Up to 1600×1200 | Power Mac G3 (4 MB) |

The machine profile (CPU type, FPU, screen size, 24- vs 32-bit mode, disk refnum, …) is **auto-detected from the ROM version word at startup** — you never need to tell the emulator which Mac to be, just hand it the right ROM. A disk image is optional; without one the emulator boots to the ROM's built-in system.

![Emulator settings — select machine type, ROM, disk, and resolution](docs/images/browser-config.png)

### Storage layout

MacPhoenix resolves relative ROM and disk paths against `storage_dir` (default `~/storage`):

```
~/storage/
  roms/         ROM files (*.rom) — scanned recursively, shown in Settings → ROM File
  images/       Hard-disk and CD-ROM images (*.img, *.dsk, *.iso, *.toast, …)
  installers/   Free-form dump of .sit / .hqx / .bin / .img / .dsk installers
                — the source tree for `populate_disk.py` (see below)
```

Absolute paths work too. Override the root with `--storage-dir /some/where` or `"storage_dir": "/some/where"` in the config file.

### Disk images

Python scripts in [`provisioning/`](provisioning/) create and populate HFS / HFS+ disk images from Linux. They shell out to `hfsutils` and `unar` (installed above).

```bash
# Create a blank 120 MB HFS image at ~/storage/images/fresh.img
# (parent directory is auto-created if missing)
python3 provisioning/create_hfs.py -o ~/storage/images/fresh.img \
                                   -s 120M -n "Macintosh HD"

# Same for HFS+ (Mac OS 8.1+)
python3 provisioning/create_hfs_plus.py -o ~/storage/images/os9.img \
                                        -s 500M -n "Macintosh HD"

# Walk ~/storage/installers/ and copy every supported archive / image onto
# disk.img, preserving resource forks via MacBinary. Subdirectory layout
# is mirrored into the HFS volume.
python3 provisioning/populate_disk.py -i ~/storage/images/fresh.img
#   -s /other/src        use a different source (default: ~/storage/installers)
#   --dry-run            print planned operations without writing

# Bless a System Folder so the image is bootable
python3 provisioning/hfs_bless.py ~/storage/images/fresh.img
```

`populate_disk.py` dispatches each file by extension:

| Extension | Handling |
|-----------|----------|
| `.sit` / `.sea` / `.hqx` / `.bin` | Extract with `unar` and rebuild MacBinary so both forks survive |
| `.img_.bin` | MacBinary-wrapped disk image (raw extract + MacBinary copy) |
| `.img` / `.iso` / `.toast` / `.dsk` | Raw data-fork copy |
| `.dsk` | If it looks like an HFS volume, recursively copied out |

Drop whatever 68K or PPC software you have into `~/storage/installers/` — subfolders become folders on the target volume — and re-run `populate_disk.py` whenever you add more. The directory is *yours*; no schema is enforced. If the installers directory doesn't exist the script creates an empty one and exits so you know where to put things.

See [docs/Provisioning.md](docs/Provisioning.md) for the full guide (including Retro68, MPW, and `hfsutils` internals).

## Architectures

MacPhoenix supports two CPU architectures, selected with `--arch`:

```bash
# 68K (default) — machine type auto-detected from ROM
./build/mac-phoenix /path/to/quadra.rom
./build/mac-phoenix /path/to/mac-se.rom --disk /path/to/system6.img

# PowerPC — Power Mac G3 emulation
./build/mac-phoenix --arch ppc /path/to/g3.rom --disk /path/to/macos9.img
```

### 68K CPU backends

The 68K architecture has two CPU backends, selected with `--backend`:

| Backend | Engine | Boot time | Use case |
|---------|--------|-----------|----------|
| `uae` (default) | Hand-tuned interpreter | ~5s | General use |
| `unicorn` | QEMU TCG JIT | ~48s | Validation / perf work |

The PPC architecture uses the Kheperix (KPX) interpreter.

## Web UI

Everything you need for day-to-day use lives in the browser. The top toolbar has:

- **Codec** selector — switch H.264 / VP9 / PNG / WebP live (only codecs compiled in appear here).
- **Mouse mode** — absolute (normal) or relative (captured pointer, needed for some games).
- **Settings** — machine/ROM/RAM/disks/CDs/screen + an advanced section for CPU backend, FPU, JIT, network, and the automation bridge.
- **Start / Power Off ▾ / Reset ▾** — the split buttons distinguish a hard kill (immediate `SIGTERM`) from a graceful **Shut Down…** / **Restart…** (routed through the bridge — see below).
- **Debug** — invokes the Programmer's Key (NMI on 68K, Cmd+Power on PPC).
- **Logs** — toggles a live debug / stats pane.
- **Fullscreen**.

The Settings dialog supports named presets and saves changes into `~/.config/mac-phoenix/config.json`. Most changes take effect on the next emulator start; codec and mouse mode apply immediately.

**Create Disk Image** (Settings → Disk Images → New…) wraps `create_hfs.py` / `create_hfs_plus.py` — new images land in `storage_dir/images/`.

## Configuration

Settings are managed through the browser UI and stored in `~/.config/mac-phoenix/config.json`. A minimal config just needs a ROM and disk:

```json
{
  "rom": "quadra650.rom",
  "disks": ["system.img"],
  "storage_dir": "~/storage"
}
```

Relative paths resolve against `storage_dir` (`roms/` for ROMs, `images/` for disks and CD-ROMs). CLI flags override config file values and are never saved back. Commonly set fields:

| Field | Default | Notes |
|-------|---------|-------|
| `architecture` | `"m68k"` | `"m68k"` or `"ppc"` |
| `cpu_backend` | `"uae"` | `"uae"` or `"unicorn"` (68K only) |
| `ram_mb` | `32` | |
| `rom`, `disks`, `cdroms`, `floppies` | — | Absolute or relative to `storage_dir` |
| `extfs` | `""` | Shared host folder (repeatable in the UI) |
| `screen` | `"640x480"` | `"WxH"` |
| `codec` | `"png"` | `"png"`, `"h264"`, `"vp9"`, `"webp"` |
| `mousemode` | `"absolute"` | `"absolute"` or `"relative"` |
| `http_port` | `8000` | |
| `signaling_port` | `8090` | WebRTC signaling |
| `bridge_enabled` | `false` | Enable the automation bridge (see below) |
| `dismiss_shutdown_dialog` | `false` | Auto-dismiss the improper-shutdown dialog on boot |
| `log_level` | `0` | 0 = milestones, 3 = + registers |

Arch-specific fields live under `m68k` / `ppc` sub-objects (`cpu_type`, `fpu`, `modelid`, `jit`, `idlewait`, `ignoresegv`, `swap_opt_cmd`, `keyboardtype`, …). See [docs/JsonConfig.md](docs/JsonConfig.md) for the full schema.

## CLI reference

```
./build/mac-phoenix [options] [rom-path]
  --rom PATH                 ROM file (alternative to positional arg)
  --disk PATH                Disk image (repeatable)
  --cdrom PATH               CD-ROM image (repeatable)
  --extfs PATH               Shared host folder (repeatable)
  --storage-dir PATH         Root for relative rom/disk paths (default: ~/storage)
  --arch m68k|ppc            CPU architecture (default: m68k)
  --backend uae|unicorn|kpx  CPU backend (default: uae; kpx auto-selected for ppc)
  --ram MB                   RAM size in megabytes
  --screen WxH               Display resolution (default: 640x480)
  --port N                   HTTP server port (default: 8000)
  --signaling-port N         WebRTC signaling port (default: 8090)
  --timeout N                Auto-exit after N seconds (useful for tests)
  --no-webserver             Headless mode (no HTTP / WebRTC)
  --network MODE             Network: none | socket[:<unix-socket-path>]
  --bridge                   Enable the automation bridge (BridgeAgent + ExtFS)
  --config PATH              JSON config file (use /dev/null to ignore user config)
  --zap-pram                 Clear PRAM on startup
  --dismiss-shutdown-dialog  Auto-dismiss improper-shutdown dialog on boot
  --screenshots              Dump PPM framebuffer snapshots to /tmp
  --log-level N              Log verbosity 0-3
  --debug-connection         Log WebRTC connection details
  --debug-mode-switch        Log video mode switches
  --debug-perf               Log performance counters
  --debug-network            Log lwIP NAT/DNS/ICMP/TCP/UDP
  --jitexperimental / --no-jitexperimental     M68K JIT (experimental)
  --ppc-jit / --no-ppc-jit                     PPC JIT (default: off)
  -h, --help                 This help
```

CLI flags always win over the config file. The emulator binary does not read environment variables — test scripts use `MACEMU_ROM` / `MACEMU_DISK`, but the binary itself does not.

## Automation bridge

Launch apps, quit apps, and trigger a graceful guest-OS shutdown or restart — all over HTTP. Enable with `--bridge` (or `bridge_enabled: true`, or the Settings → "Enable automation bridge" checkbox). `--no-webserver` mode turns it on automatically for test scripts.

How it works:

- **Read commands** (`/api/app`, `/api/windows`, `/api/wait`) peek Mac low memory (`CurApName`, `WindowList`, `Ticks`) directly from the 60 Hz IRQ — no guest cooperation required.
- **Action commands** (`/api/launch`, `/api/quit`, `/api/shutdown`, `/api/restart`) write a request file into the bridge directory. A tiny guest-side app (`BridgeAgent`, a Retro68-built m68k binary) installed in `:System Folder:Startup Items:` picks it up in its `WaitNextEvent` loop and drives the Process Manager / Shutdown Manager / Apple Events.

When `--bridge` is on, a fresh temp directory is created under `/tmp/macemu-bridge-XXXXXX` and auto-mounted as ExtFS volume `Host:` inside the guest. `provisioning/install_bridge_agent.sh` is invoked against every `--disk` to (re)install `BridgeAgent.bin` into the System Folder. A watchdog looks for a `bridge_heartbeat` file once the Finder is up and logs a warning if the agent never reports in.

```bash
# Launch an app and wait for it to come up
curl -s -X POST http://localhost:8000/api/launch \
     -d '{"path":"Macintosh HD:Applications:TeachText"}'
curl -s -X POST http://localhost:8000/api/wait \
     -d '{"app":"TeachText","timeout":10}'

# Graceful power-off (quits every app, then ShutDwnPower)
curl -s -X POST http://localhost:8000/api/shutdown
```

See [docs/CommandBridge.md](docs/CommandBridge.md) for the full protocol and the list of files exchanged in the bridge directory.

## Networking

Guest networking is an opt-in `net-bridge` sidecar written in Rust (`net-bridge/`). It pairs a userspace TCP/IP stack (`smoltcp`) with a NAT that proxies the guest's TCP / UDP / ICMP through ordinary host sockets — so the Mac can reach the Internet without raw sockets, `CAP_NET_ADMIN`, or a TUN/TAP device. The bridge also runs a tiny DHCP server, so classic MacTCP / Open Transport just works with "Configure via DHCP".

Default addressing:

| Role | Value |
|------|-------|
| Gateway (bridge) | `10.0.2.1` |
| Netmask | `255.255.255.0` |
| Guest (via DHCP) | `10.0.2.x` |

### Building the bridge

```bash
cargo build --release --manifest-path net-bridge/Cargo.toml
# Produces net-bridge/target/release/net-bridge
```

### Enabling it

Pick **NAT (net-bridge)** in Settings → Network, or launch with `--network socket`:

```bash
./build/mac-phoenix --network socket /path/to/quadra.rom
```

The emulator auto-spawns `net-bridge` over a Unix socket (default `/tmp/mac-ether.sock`) — it looks in `./net-bridge/target/release/`, `./net-bridge/`, and `/usr/local/bin/` in that order. Override the socket path with `--network socket:/some/where.sock`.

If you'd rather manage the bridge yourself (e.g. attach `strace`, run under `valgrind`):

```bash
# Terminal 1
./net-bridge/target/release/net-bridge --socket /tmp/mac-ether.sock

# Terminal 2 — emulator connects to the existing socket
./build/mac-phoenix --network socket:/tmp/mac-ether.sock /path/to/quadra.rom
```

Once the guest has an IP, MacTCP / Open Transport / Internet Config just works — try Netscape, Fetch, NCSA Telnet, iCab, or `curl` inside MPW. `--debug-network` turns on verbose packet logging on the emulator side; `RUST_LOG=debug` does the same for `net-bridge`.

The bridge also exposes a tiny echo service on the gateway (`10.0.2.1:7`, both TCP and UDP) — handy for round-trip smoke tests from inside the guest without needing the host to run an `inetd`-style daemon.

### Ping (ICMP echo)

`net-bridge` proxies ICMP through Linux's *unprivileged* ICMP datagram sockets — no `CAP_NET_RAW` needed — but the kernel still gates them on the `net.ipv4.ping_group_range` sysctl. If guest `ping` returns "host unreachable" while TCP / UDP / DNS / traceroute all work, your gid is outside the range. Allow everyone:

```bash
sudo sysctl -w net.ipv4.ping_group_range="0 2147483647"
# Persist:
echo 'net.ipv4.ping_group_range = 0 2147483647' | \
    sudo tee /etc/sysctl.d/99-mac-phoenix-ping.conf
```

Traceroute keeps working without this — classic Mac traceroute is UDP-to-33434 with low TTL, and the resulting ICMP Time-Exceeded replies arrive via the ordinary UDP NAT path.

## API

Read-only:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | `{emulator_running, boot_phase, checkload_count, boot_elapsed}` |
| `/api/screenshot` | GET | PNG of the current framebuffer |
| `/api/mouse` | GET | Current Mac cursor position |
| `/api/config` | GET | Current configuration |
| `/api/storage` | GET | Available ROMs and disk images under `storage_dir` |
| `/api/codecs` | GET | Available codecs + which ones were compiled in |
| `/api/app` | GET | Current foreground application (bridge read command) |
| `/api/windows` | GET | Window list as JSON (bridge read command) |

Control / input:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/config` | POST | Partial config merge, persists to disk |
| `/api/mouse` | POST | Absolute `{"x":N,"y":N}` or relative `{"dx":N,"dy":N}` |
| `/api/keypress` | POST | `{"key":"return"}` or raw Mac keycode `{"key":36}` |
| `/api/codec` | POST | Switch video codec |
| `/api/emulator/start` | POST | Start CPU execution |
| `/api/emulator/stop` | POST | Stop CPU execution |
| `/api/invoke-debug` | POST | Programmer's Key (NMI / Cmd+Power) |

Automation bridge (requires `--bridge`):

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/wait` | POST | Poll for `boot=Finder` or `app=Name` |
| `/api/launch` | POST | `{"path":"HD:App","open":bool?}` — launch or open-document |
| `/api/quit` | POST | `kAEQuitApplication` to the front app |
| `/api/shutdown` | POST | Graceful guest power-off (`ShutDwnPower`) |
| `/api/restart` | POST | Graceful guest restart (`ShutDwnStart`) |

## Building guest components

Two pieces of classic-Mac code ship with the repo: the **BridgeAgent** (drives the automation bridge from inside the guest OS) and the **MacPerl test script** (the guest-side suite). The compiled `BridgeAgent.bin` is committed, so you only need the Retro68 toolchain if you're *changing* the agent. The test script is plain Perl — no build step — but it needs resource-fork metadata before it can sit on an HFS volume.

### Retro68 toolchain (once)

```bash
# Clones autc04/Retro68 into toolchain/retro68-src/ and builds into
# toolchain/retro68/. ~20-40 min, incremental on re-run. Needs a working
# C/C++ compiler, cmake, bison, flex, texinfo, and ruby on the host.
provisioning/build_retro68.sh

export PATH="$PWD/toolchain/retro68/bin:$PATH"
```

### BridgeAgent

```bash
# Produces BridgeAgent.bin (MacBinary, committed to the tree)
make -C tests/guest/bridge
```

The `.bin` is installed into `:System Folder:Startup Items:` of every `--disk` automatically when the bridge is enabled (see [Automation bridge](#automation-bridge)); `provisioning/install_bridge_agent.sh <image>` does the same thing manually.

### MacPerl test script

```bash
# Writes MacTestSuite.pl (+ AppleDouble ._MacTestSuite.pl with the
# TEXT/McPL type+creator) into the ExtFS mount, so MacPerl can open it.
python3 tests/guest/install_perl_test.py /path/to/extfs_dir
```

Guest-side tests then call `/api/launch` with `open: true` — BridgeAgent sends the `dosc` AppleEvent to MacPerl, which runs the script and writes results back into the shared folder.

## Testing

All native tests are CMake-registered; use `ctest` labels to scope.

```bash
# Smoke tests only — runs the binary briefly, no boot (~5 s)
ctest --test-dir build -L unit

# API + config + ExtFS — boots to Finder with the default backend (~15 s)
ctest --test-dir build -L api

# Boot tests across backends (UAE interpreter, UAE JIT, Unicorn, PPC) (~5 min)
ctest --test-dir build -L boot

# Guest-side suite (requires MacPerl + BridgeAgent on the disk image)
ctest --test-dir build -L guest

# Everything
ctest --test-dir build

# One-off, verbose
ctest --test-dir build -V -R api_endpoints
```

Tests pick up the ROM and a disk image from `MACEMU_ROM` / `MACEMU_DISK` (or `-DTEST_ROM=...` at `cmake -B build` time). Defaults assume `~/roms/quadra.rom` and `~/storage/images/7.6.img`.

### Guest test suite (standalone)

You can run `test_guest_suite.sh` directly for faster iteration:

```bash
# Boot the default 7.6 image, dispatch MacTestSuite.pl, collect results
tests/test_guest_suite.sh --timeout 120

# Against a specific OS version / disk
tests/test_guest_suite.sh --os-version 7.5.5
tests/test_guest_suite.sh --disk ~/storage/images/quadra.img

# PPC (uses the PPC ROM + Mac OS 9 image; 68k BridgeAgent runs under PPC's
# built-in 68k emulator)
tests/test_guest_suite.sh --arch ppc
```

The script starts a headless emulator on port 18094, waits for `bridge_heartbeat`, calls `/api/launch` with the script path, polls for a result file in the bridge directory, and tears everything down.

### Playwright E2E tests

Browser-level tests (WebRTC handshake, Settings dialog, codec switching, mouse input) live under `tests/e2e/` and drive a headful Chromium.

```bash
# One-time
npm install
npx playwright install chromium

# Run the full suite (~2 min; starts its own emulator per spec)
npx playwright test

# Debug / UI modes
npm run test:headed   # visible browser
npm run test:ui       # interactive runner
npx playwright test tests/e2e/codec.spec.ts   # one file
```

See [docs/Testing.md](docs/Testing.md) for the full matrix and tips on tracking down boot regressions.

## Architecture

MacPhoenix uses a **Platform API** abstraction: all CPU backends implement the same function pointer table, so core code (ROM patching, interrupts, ADB, video) is backend-agnostic. Video uses a **lock-free triple buffer** — the CPU writes frames, the encoder reads them, and the screenshot API reads them, all without locks.

See [CLAUDE.md](CLAUDE.md) for the full developer reference.

## Heritage

MacPhoenix descends from the BasiliskII/SheepShaver emulator family originally created by Christian Bauer. The original source is preserved in [`legacy/`](legacy/) for reference.

## License

GPL-2.0 — see [LICENSE](LICENSE).
