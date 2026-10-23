# Installing and building

MacPhoenix builds with CMake on Linux (x86-64, and arm64/riscv64 through
`tools/xarch`) and macOS. CMake probes for every optional part and prints
what it found; anything missing is skipped with a message, not an error.

## Ubuntu 24.04 / Debian 12 (backports) / Fedora 40

Required:

```bash
sudo apt install build-essential cmake pkg-config git \
                 libssl-dev libyuv-dev qt6-base-dev
```

Qt 6.4 or newer is required (Ubuntu 24.04, Debian 12 backports and Fedora
40 ship it; Ubuntu 22.04's 6.2 is too old). OpenSSL is only for
libdatachannel's DTLS/SRTP (WebRTC); libyuv does the encoders' colour
conversion.

Optional, each found by `cmake -B build`:

| Part | Packages (apt) | Without it | Turn off |
|---|---|---|---|
| H.264 (WebRTC) | `libopenh264-dev` | codec hidden in the UI | — |
| VP9 (WebRTC) | `libvpx-dev` | codec hidden | — |
| WebP (WebSocket) | `libwebp-dev` | codec hidden; PNG always works | — |
| Opus (sound) | `libopus-dev` | no sound in the browser | — |
| Executor backend | nothing more (its generated sources are committed; `ruby bison perl` only to regenerate them, `-DEXECUTOR_REGENERATE=ON`) | — | `-DBUILD_EXECUTOR=OFF` |
| net-bridge (guest networking) | `cargo rustc` (rustc ≥ 1.75) | `--network socket` unavailable | `-DBUILD_NET_BRIDGE=OFF` |
| MacBrowser host side (experimental) | `qt6-webengine-dev` | `--browser` ignored | off by default; `-DBUILD_BROWSER=ON` |
| Tests, provisioning | `hfsutils python3`, plus `unar` for `populate_disk.py` | disk-image tests skip | `-DBUILD_TESTS=OFF` |

Rust older than 1.75 (Ubuntu 22.04, Debian 12 without backports): install
a current one with rustup, or build without net-bridge.

Then:

```bash
git clone https://github.com/sirmick/mac-phoenix.git
cd mac-phoenix
cmake -B build
cmake --build build -j4
```

Re-run `cmake -B build` after installing a library; its summary lists the
codecs found. Large `-j` values can run a small machine out of memory (the
MAME cores and Executor are big translation units); `-j4` is safe.

## macOS

```bash
brew install cmake pkg-config qt@6 openssl libyuv openh264 libvpx webp opus \
             rust hfsutils
```

A Homebrew formula draft is in `packaging/homebrew/`
([docs/Packaging.md](Packaging.md)).

## Host settings

* **`vm.mmap_min_addr=0`** for the cores that map guest memory at host
  address 0: `uae` (the 68k default) and `kpx` (the PowerPC default).
  Executor and the MAME cores (`--core mame-68k`, `--core mame-ppc`)
  don't need it.

  ```bash
  sudo sysctl vm.mmap_min_addr=0
  echo 'vm.mmap_min_addr = 0' | sudo tee /etc/sysctl.d/99-mac-phoenix.conf
  ```

  Without it the emulator says `Failed to map RAM at address 0` and how to
  fix it.
* **Guest `ping`** (net-bridge uses unprivileged ICMP sockets): see
  [Networking.md](Networking.md#ping).

## The guest-side applications

Two classic Mac applications ship as committed binaries, so a normal build
never needs a Mac toolchain:

* `BridgeAgent/BridgeAgent.bin`: the automation agent the emulator starts
  inside the guest ([CommandBridge.md](CommandBridge.md)).
* `MacBrowser/MacBrowser.bin` and `MacBrowser.dsk`: the guest side of
  MacBrowser ([MacBrowser.md](MacBrowser.md)).

To rebuild them from source you need the Retro68 cross-toolchain and
Apple's Universal Interfaces 3.4:

```bash
sudo apt install bison flex texinfo libgmp-dev libmpfr-dev libmpc-dev libboost-all-dev
provisioning/build_retro68.sh        # 20–40 min; installs to toolchain/retro68/
```

Put Universal Interfaces 3.4 at `private/Universal Interfaces/` (not
redistributable; `private/` is gitignored): Retro68's open Multiversal
headers lack some of what BridgeAgent uses (`Script.h`, `Aliases.h`,
`ShutDown.h`, `Scrap.h`). With both present, `cmake -B build` rebuilds the
`.bin` files whenever their sources change (`BUILD_BRIDGE_AGENT`,
`BUILD_MAC_BROWSER`); commit the new binaries.

The guest test applications (`tests/guest/*`) are built differently: with
MPW inside a headless Executor session, `tools/mpw/mpw build DIR`
([MPW.md](MPW.md)).

## Other architectures

`tools/xarch/xarch.sh arm64` (or `riscv64`) builds and tests in a Debian
container under qemu on an x86-64 host: build directories `build-arm64/`,
`build-riscv64/`. Kheperix is x86-only (elsewhere `--core kpx` is a stub):
use `--core mame-ppc` there.
