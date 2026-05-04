# Windows build environment

Setup notes for a Windows VM/host that can build MacPhoenix as part of
the Qt6 port (PLAN Phase 10). **Phase 10 is not done yet** — this doc
exists so a Windows VM can be provisioned in parallel with the
remaining porting work and used to flush out platform-specific bugs as
they surface.

What's already in place: `sigsegv.cpp`'s Windows SEH path was activated
in Phase 7 (commit `e80f314f`) but has never been exercised on real
Windows. Phases 0–3a + 7 of the Qt port should compile on Windows once
the deps below are in place; the remaining POSIX surface (see "Known
gaps" at the end) needs porting before a full boot will work.

## Pre-canned VM options

Three paths to a working Windows build env, ranked by effort:

| Option | Effort | Closeness to GH `windows-2022` runner |
|---|---|---|
| **Microsoft "Windows 11 dev environment" via Hyper-V Quick Create** ⭐ | 20 min download + boot | Close — VS 2022 + Windows SDK preinstalled, but not byte-identical to the GH image |
| **`actions/runner-images` Packer templates** ([github.com/actions/runner-images](https://github.com/actions/runner-images)) | 1–2 hr build | Byte-identical — produces the exact image GH uses for `windows-2022` |
| Manual Windows 11 install + VS Studio 2022 setup | 1–2 hr install + manual configuration | Variable — depends on what you install |

**Recommended: the Microsoft "Windows 11 dev environment" Hyper-V image**.
Open Hyper-V Manager → Quick Create → "Windows 11 dev environment".
~20 GB download, free, 90-day eval (no Microsoft account required).
Visual Studio 2022 + Windows SDK + WSL2 preinstalled. Just install Qt
+ Rust on top per "Tooling" below. Free re-download after the eval
expires.

For "behaves exactly like CI" debugging, build the
`actions/runner-images` Packer template for `windows-2022`. That image
matches what GH Actions runs the build matrix on.

VS Dev Box is Microsoft's paid managed cloud option; overkill unless
you specifically want to pay for zero local resource use.

## VM specs (manual install path)

| | Recommended | Minimum |
|---|---|---|
| OS | Windows 11 Pro x64 (24H2 or later) | Windows 10 22H2 x64 |
| CPU | 8 cores | 4 cores |
| RAM | 16 GB | 8 GB |
| Disk | 80 GB SSD | 40 GB |
| Display | 1920×1080 | 1024×768 |
| Virtualization | nested virtualization OFF (we don't need it; UAE/KPX are software-only translators) | — |

ARM64 Windows is in scope long-term but defer until x64 works.

## Tooling

Install in this order so PATH ends up sane.

| Tool | Version | Source | Notes |
|---|---|---|---|
| Visual Studio 2022 Community | latest | https://visualstudio.microsoft.com/downloads/ | Workloads: **"Desktop development with C++"**. Components: MSVC v143, Windows 11 SDK (latest), C++ CMake tools, Git for Windows |
| Qt 6 (≥ 6.4, prefer 6.7+) | 6.7.x LTS | https://www.qt.io/download-qt-installer-oss | Open-source online installer. Select MSVC 2022 64-bit; modules: **Qt Base** + **Qt HTTP Server** (deferred — only if Phase 5 lands on QHttpServer; current Phase 5 path uses `Qt6::Network` from Qt Base only) |
| Rust | stable | https://www.rust-lang.org/tools/install | `rustup-init.exe`, default profile, x86_64-pc-windows-msvc toolchain |
| Git | latest | bundled with VS 2022, or https://git-scm.com/download/win | Configure long-paths: `git config --global core.longpaths true` |
| Python 3 | 3.11+ | Microsoft Store or https://www.python.org/downloads/ | Used by some build scripts (boot tests, ROM patching) |

## Build dependencies

These are the Linux deps from `debian/control`. Windows mapping:

| Linux dep | Windows source | Notes |
|---|---|---|
| `libssl-dev` (OpenSSL) | vcpkg `openssl`, or via Qt's bundled OpenSSL | Used by webserver (WebSocket SHA1, file scanner MD5). **Candidate for Qt simplification — see below.** |
| `libopenh264-dev` | vcpkg `openh264` | Optional codec — disabled at configure if missing |
| `libvpx-dev` | vcpkg `libvpx` | Optional codec |
| `libwebp-dev` | vcpkg `libwebp` | Optional codec |
| `libopus-dev` | vcpkg `opus` | Optional codec (audio) |
| `libyuv-dev` | vcpkg `libyuv` | Optional codec helper |
| `libxcb*-dev` | **N/A** | Linux-only (MacBrowser host pipeline) — Phase 8 will remove via QtWebEngine |
| `cmake` | bundled with VS 2022 | ≥ 3.16 |
| `cargo` / `rustc` | rustup (above) | net-bridge subproject |

Easiest path for the optional codecs: install [vcpkg](https://github.com/microsoft/vcpkg)
in `C:\vcpkg` and run:

```powershell
C:\vcpkg\vcpkg install openssl openh264 libvpx libwebp opus libyuv --triplet x64-windows
```

…then pass `-DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake`
to CMake. Or skip them entirely on first build — only PNG (built-in
fpng) is required.

## Clone + build (first time)

Open **"x64 Native Tools Command Prompt for VS 2022"** so MSVC + the
Windows SDK are in PATH. Then:

```powershell
git clone https://github.com/sirmick/mac-phoenix.git
cd mac-phoenix
git checkout qt-port

REM Tell CMake where Qt is. Adjust path to your Qt install.
set CMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2022_64

REM Configure. -DBUILD_NET_BRIDGE=OFF skips Rust if rustup isn't ready.
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DBUILD_NET_BRIDGE=OFF

REM Build (parallel over all cores).
cmake --build build --config RelWithDebInfo -j
```

The `BridgeAgent.bin` and `MacBrowser.bin` guest binaries are committed
prebuilt — Retro68 is **not** required on Windows.

## Smoke test

Once the build links successfully (which it likely won't at first
attempt — see "Known gaps"), copy a Quadra 650 ROM into the VM and:

```powershell
build\RelWithDebInfo\mac-phoenix.exe --no-webserver --timeout 10 ^
    C:\path\to\quadra.rom
```

Headless 10-second boot probe. No video means `sigsegv.cpp`'s SEH path
is exercised but no framebuffer is rendered.

For the web UI:

```powershell
build\RelWithDebInfo\mac-phoenix.exe C:\path\to\quadra.rom
```

…and open `http://localhost:11000` from any browser (host or guest).

## Packaging (Phase 10 distribution)

Once the build works, package with:

```powershell
windeployqt --release --no-translations --no-quick-import ^
    build\RelWithDebInfo\mac-phoenix.exe
```

That populates `build\RelWithDebInfo\` with all Qt DLLs + plugins. Then
either:
- **NSIS** (`makensis script.nsi`) — small, scriptable, MIT-licensed
- **WiX 4** (`wix build`) — produces .msi, signs cleanly with EV cert
- **MSIX** — Windows Store-compatible if/when we go that route

Code-signing with an EV cert (~$300/yr) avoids SmartScreen warnings —
strongly recommended before public distribution. Until then, users will
see "Windows protected your PC" on first run; they can click "More
info" → "Run anyway."

## Known gaps (Phase 10 work-in-progress)

These are the POSIX surfaces that still need Windows treatment before
the build will link/run cleanly:

| Symbol | File | Phase | Replacement |
|---|---|---|---|
| `pthread_*`, `sem_t` | `src/drivers/serial/serial_unix.cpp` | 10 | `QThread`/`QSemaphore`, or `#ifdef WIN32` to a `serial_win.cpp` |
| `nanosleep`, `clock_gettime` | `src/drivers/platform/timer_interrupt.cpp` | 10 | `QThread::usleep`, `QElapsedTimer` (cross-platform, no headers needed) |
| `fork`/`execvp` | `src/webserver/api_handlers.cpp::handle_create_image`, `src/drivers/ether/ether_socket.cpp` | 10 | `QProcess` (Phase 2 already did the big subprocess fork; these are the leftovers) |
| `mmap`/`mprotect` JIT pages | UAE JIT cache (search `MAP_ANON\|MAP_JIT\|mprotect`) | 10 | `VirtualAlloc(PAGE_EXECUTE_READWRITE)` + `FlushInstructionCache` |
| Optional: `OpenSSL` SHA1/MD5 | `src/webserver/websocket.cpp`, `src/webserver/file_scanner.cpp` | post-5 | `QCryptographicHash` — drops the OpenSSL dep entirely |
| `xcb-*` MacBrowser pipeline | `src/drivers/browser/*.cpp` | 8 | `QWebEnginePage` — Linux-only today; deletes the whole subsystem |

Track Phase 10 progress against `docs/qt6/PLAN.md` Phase 10 section.

## Future CI integration

Mirror `.github/workflows/build.yml`'s Linux matrix with a
`windows-2022` runner job. Sketch (don't add until Phase 10 builds
locally):

```yaml
windows:
  runs-on: windows-2022
  steps:
    - uses: actions/checkout@v4
    - uses: jurplel/install-qt-action@v4
      with: { version: '6.7.0', host: 'windows', target: 'desktop', arch: 'win64_msvc2022_64' }
    - uses: ilammy/msvc-dev-cmd@v1
    - run: cmake -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_NET_BRIDGE=OFF
    - run: cmake --build build --config RelWithDebInfo -j
    - run: ctest --test-dir build -L unit --output-on-failure --build-config RelWithDebInfo
```

Only `-L unit` initially — `-L boot` and `-L api` need a ROM that
won't be committed to git. Boot tests on Windows are a follow-up once
the artifact storage (S3? GitHub-hosted release?) is decided.
