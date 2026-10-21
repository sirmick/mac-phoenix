# Windows build environment

> **Status (checked 2026-10-21):** a plan that was never carried out.
> There is no Windows build: no commit since the Qt port (merged
> 2026-05-06) has worked on Windows, there is no Windows CI job, and
> git history shows no MSVC build of the tree. The only Windows-specific code is
> Phase 7's `HAVE_WIN32_EXCEPTIONS` define (`src/core/CMakeLists.txt`,
> `e80f314f`), never exercised. Since this was written the tree has
> gained a lot that was never looked at with Windows in mind: the MAME
> CPU cores, the Executor backend (Boost, Ruby, Bison, Perl; a
> low-address link layout), PPC identity addressing for `--core kpx`,
> `net-bridge` listening on a Unix socket, and about 36 non-vendored
> sources outside `src/executor` that include `<sys/mman.h>`,
> `<sys/socket.h>`, `<poll.h>` or `<unistd.h>`. Treat the steps below
> as an untested starting point.

Setup notes for a Windows VM/host that can build MacPhoenix as part of
the Qt6 port (PLAN Phase 10). **Phase 10 is not done yet** — this doc
exists so a Windows VM can be provisioned in parallel with the
remaining porting work and used to flush out platform-specific bugs as
they surface.

What's already in place: `sigsegv.cpp`'s Windows SEH path was activated
in Phase 7 (commit `e80f314f`) but has never been exercised on real
Windows. The remaining POSIX surface (see "Known gaps" at the end)
needs porting before the build will link, let alone boot.

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
| Qt 6 (≥ 6.4, prefer 6.7+) | 6.7.x LTS | https://www.qt.io/download-qt-installer-oss | Open-source online installer. Select MSVC 2022 64-bit; modules: **Qt Base** (Core + Network; Phase 5 went to `QTcpServer`, so Qt HTTP Server is not needed). Qt WebEngine only for `-DBUILD_BROWSER=ON` |
| Rust | stable | https://www.rust-lang.org/tools/install | `rustup-init.exe`, default profile, x86_64-pc-windows-msvc toolchain |
| Git | latest | bundled with VS 2022, or https://git-scm.com/download/win | Configure long-paths: `git config --global core.longpaths true` |
| Python 3 | 3.11+ | Microsoft Store or https://www.python.org/downloads/ | Used by some build scripts (boot tests, ROM patching) |

## Build dependencies

These are the Linux deps from `debian/control`. Windows mapping:

| Linux dep | Windows source | Notes |
|---|---|---|
| `libssl-dev` (OpenSSL) | vcpkg `openssl`, or via Qt's bundled OpenSSL | Needed by libdatachannel (DTLS/SRTP). Our own SHA1/MD5 use moved to `QCryptographicHash` in Phase 12. |
| `libopenh264-dev` | vcpkg `openh264` | Optional codec — disabled at configure if missing |
| `libvpx-dev` | vcpkg `libvpx` | Optional codec |
| `libwebp-dev` | vcpkg `libwebp` | Optional codec |
| `libopus-dev` | vcpkg `opus` | Optional codec (audio) |
| `libyuv-dev` | vcpkg `libyuv` | Optional codec helper |
| `qt6-base-dev` (≥ 6.4) | Qt installer (above) | Required |
| `cmake` | bundled with VS 2022 | ≥ 3.16 |
| `cargo` / `rustc` | rustup (above) | net-bridge subproject (uses a Unix socket: `std::os::unix`) |
| Boost, Ruby, Bison, Perl | not worked out | Executor backend only; `-DBUILD_EXECUTOR=OFF` skips it |

(The `libxcb*` deps this table used to list went away with Phase 8.)

Easiest path for the optional codecs: install [vcpkg](https://github.com/microsoft/vcpkg)
in `C:\vcpkg` and run:

```powershell
C:\vcpkg\vcpkg install openssl openh264 libvpx libwebp opus libyuv --triplet x64-windows
```

…then pass `-DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake`
to CMake. Or skip them entirely on first build — only PNG (zlib,
which Qt needs anyway) is required.

## Clone + build (first time)

Open **"x64 Native Tools Command Prompt for VS 2022"** so MSVC + the
Windows SDK are in PATH. Then:

```powershell
git clone https://github.com/sirmick/mac-phoenix.git
cd mac-phoenix
REM qt-port was merged into main in 2026-05; build main.

REM Tell CMake where Qt is. Adjust path to your Qt install.
set CMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2022_64

REM Configure. -DBUILD_NET_BRIDGE=OFF skips Rust (net-bridge is Unix-only
REM today); -DBUILD_EXECUTOR=OFF skips the Executor backend.
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DBUILD_NET_BRIDGE=OFF -DBUILD_EXECUTOR=OFF

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

The table as written in 2026-05 listed six gaps. Five were closed on
Linux by later Qt-port phases: serial `pthread`/`sem_t` (Phase 15,
`ed8f47fe`), `nanosleep`/`clock_gettime` (Phase 13, `440bb808`),
leftover `fork`/`execvp` (Phase 14, `3f1079dc`), direct OpenSSL SHA1/MD5
(Phase 12, `4d273997`; OpenSSL is still needed by libdatachannel) and
the `xcb-*` MacBrowser pipeline (Phase 8). The one left:

| Symbol | File | Phase | Replacement |
|---|---|---|---|
| `mmap`/`mprotect` JIT pages | UAE JIT cache (search `MAP_ANON\|MAP_JIT\|mprotect`) | 10 | `VirtualAlloc(PAGE_EXECUTE_READWRITE)` + `FlushInstructionCache` |

This is not a complete list for today's tree; see the status note at
the top. No one has audited it for Windows.

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
