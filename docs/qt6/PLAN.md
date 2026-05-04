# Qt6 Port Plan

Branch: `qt-port`. Hard-switching POSIX/X11 subsystems to Qt6 to enable a
proper Windows port and easier macOS distribution. Each phase is one
PR-sized change; Linux must remain fully working at every step, validated
by the existing test suite.

## Goals

- **Cross-platform abstraction**: get off raw POSIX (`fork`/`shm_open`/
  `eventfd`/`AF_UNIX`) and X11 (`xcb-shm`/`xcb-damage`/Xvfb).
- **Drop dependencies wherever Qt has a credible replacement.** Each
  dropped dep removes one `find_package` call, one `apt install` line,
  one set of platform-specific quirks. Active targets:
  - `OpenSSL` → `QCryptographicHash` (drops `libssl-dev` apt dep)
  - `nlohmann::json` → `QJsonDocument`/`QJsonObject` (drops the
    `nlohmann_json` subproject)
  - `libxcb-*` → `QtWebEngine` (drops 5 apt deps; Phase 8)
- **Idiomatic Qt where possible.** `QThread` over `std::thread`,
  `QSemaphore` over `sem_t`, `QElapsedTimer` over `clock_gettime`,
  `QProcess` over `fork`/`execvp` — even when the existing primitive
  works on Linux. Reduces the Windows-port surface and makes the
  codebase consistent.
- **Windows is a first-class target.** Native Windows build, no Cygwin/
  WSL, packaged as an installer.
- **macOS distribution becomes easy.** `macdeployqt`-driven `.app` bundle
  with `create-dmg` for delivery. Code-signed and notarized for direct
  download.
- **Single binary, optional native head.** Default build is headless
  (`QCoreApplication`) and serves the existing web UI. Optional
  `-DBUILD_NATIVE_HEAD=ON` build adds a Qt Quick window for direct video/
  audio display, with the same web-based settings UI.

## Non-goals

- **DRM video** (Widevine L1: HBO, Netflix, etc.). The screen-grab
  pipeline can't capture DRM-protected content regardless of
  implementation. Out of scope.
- **App Store distribution.** Mac App Store sandbox + JIT restrictions
  make this impractical. Direct download via Developer ID is the target.
- **Wholesale `QString` migration.** Keep `std::string` internally;
  convert at Qt API boundaries with `QString::fromStdString` /
  `.toStdString`. UTF-16 doubling for ASCII is not worth the diff.
- **Replacing `libdatachannel`.** Qt has no RTP stack; keep it.
- **Replacing the codec libs** (OpenH264, libvpx, libwebp, Opus, libyuv).
  Qt Multimedia uses ffmpeg by default — much larger blast radius than
  the current per-codec libs. Keep current per-codec deps.

## Architectural decisions (locked in)

| Decision | Choice | Reason |
|---|---|---|
| Migration style | Hard switch per subsystem (no `#ifdef USE_QT`) | Cleaner reviews, no dual maintenance |
| Qt version | Qt 6.4 minimum | Ubuntu 24.04 ships it; sufficient for Phase 5–9 + dep-drops |
| String type | `std::string` internal, `QString` at Qt boundaries | Avoid massive textual diff, no UTF-16 doubling for ASCII |
| JSON | **Migrate to `QJsonDocument`/`QJsonObject`** (was: keep `nlohmann::json`) | Drops the `nlohmann_json` subproject; aligns with "drop deps" goal. Migration is mechanical (`j["k"]=v` → `obj["k"]=v`); ~2k LOC of api_handlers.cpp to touch but no behavior change. |
| HTTP server | `QTcpServer` + thin custom router (was: `QHttpServer`) | Qt 6.4 `QHttpServer` is Tech Preview + lacks WebSocket-on-same-port. Pivot decided 2026-05-03 in Phase 5. |
| WebRTC | Keep `libdatachannel` | Qt has no RTP stack |
| Codec libs | Keep per-codec libs | Qt Multimedia → ffmpeg is bigger blast radius |
| Threading | `QThread` over `std::thread`; `QSemaphore` over `sem_t` | Idiomatic Qt; uniform Windows behavior |
| Tooling | grep + sed + tests, no `comby` | Patterns are simple; tests are the safety net |
| Plan tracking | `TaskCreate`/`TaskUpdate` per phase | One task per phase, marked in-progress when started |

## Phases

Each phase: scope → refactor approach → validation tests → doc updates.

### Phase 0 — Bootstrap

**Scope**: Qt6 in CMake, `QCoreApplication` instance in `main.cpp`
constructed but event loop not yet running. No behavior change.

**Refactor**: `find_package(Qt6 6.4 COMPONENTS Core REQUIRED)`,
`target_link_libraries(... Qt6::Core)`. Construct
`QCoreApplication app(argc, argv)` early in `main()`, delete it last.

**Validation**:
- `cmake -B build && cmake --build build -j$(nproc)`
- `ctest --test-dir build` (all labels)

**Docs**: This file. Update CLAUDE.md "Required Dependencies" table to
add `qt6-base-dev`.

---

### Phase 1 — File I/O

**Scope**: POSIX directory/stat APIs → Qt equivalents.

**Files**:
- `src/drivers/platform/extfs.cpp` — `opendir`/`readdir`/`closedir`/
  `stat`/`access` → `QDir`/`QDirIterator`/`QFileInfo`
- `src/config/emulator_config.cpp` — storage dir scan

**Refactor approach**: `git grep -n 'opendir\|readdir\|stat(\|access('`
to enumerate sites, replace per-file. Patterns are repetitive (3-4
distinct shapes); review by diff.

**Validation**:
- `tests/test_extfs.sh`
- `/api/storage` payload byte-identical (eyeball or diff against
  pre-port snapshot)
- Boot-to-Finder finds disk images

**Docs**: Note `QDir`/`QDirIterator` choice in this PLAN; no CLAUDE.md
change needed (filesystem semantics unchanged).

---

### Phase 2 — Subprocess management → QProcess

**Scope**: `fork`/`execv`/`waitpid` → `QProcess`. Introduces a real Qt
event loop on the parent.

**Files**:
- `src/core/emulator_subprocess.cpp` (~lines 208–350)

**Refactor approach**: Spawn a dedicated `QThread` running
`QCoreApplication::exec()` so existing thread topology is undisturbed.
Drop the subprocess monitor thread; use `QProcess::finished` signal +
`QProcess::errorOccurred`.

**Validation**:
- PPC backend boot (spawns subprocess)
- Kill subprocess externally → verify supervisor restart cycle
- `tests/test_command_bridge.sh` (subprocess crosses parent/child split)

**Docs**: Update CLAUDE.md "Key Architectural Decisions" to note
`QProcess`-based subprocess management. Update this PLAN with notes on
event loop placement.

---

### Phase 3 — IPC SHM → QSharedMemory + QLocalSocket

**Scope**: The trickiest layer. POSIX SHM, Unix domain sockets,
`eventfd`, `SCM_RIGHTS` fd-passing → Qt equivalents.

**Files**:
- `src/ipc/ipc_client.cpp` (~lines 34–126)
- `src/ipc/ipc_protocol.h` (key naming, atomic types)
- `src/core/emulator_subprocess.cpp` (IPC setup at spawn time)

**Refactor approach**:
- `shm_open`+`mmap` → `QSharedMemory` (key: `macemu-video-{PID}` becomes
  the `QSharedMemory::setKey()` argument; semantics map cleanly)
- `AF_UNIX/SOCK_STREAM` → `QLocalSocket` / `QLocalServer` (uses Unix
  sockets on Linux, named pipes on Windows — same API)
- `eventfd` + `SCM_RIGHTS` → **dedicated notification `QLocalSocket`,
  child writes 1 byte per frame**. Originally planned `QSystemSemaphore`,
  but its `acquire()` is indefinite-block only (no timeout); the encoder
  uses `poll(eventfd, 16ms)` for periodic wakeups. `QLocalSocket::
  waitForReadyRead(16)` is a direct 1:1 replacement. Two `QLocalSocket`s
  total: one for control (parent→child commands), one for notification
  (child→parent frame-ready). Latency ~20μs vs eventfd ~3μs — invisible
  at 60fps.

**Commit split**: 3a = `QSharedMemory` swap only (no protocol change),
3b = sockets + notification together (entangled via SCM_RIGHTS).

**Status (2026-05-03)**: 3a landed (commit `702ccba8`). 3b was attempted
but introduced a child-side regression in `init_mac_subsystems` (boot_se
crashed in `VideoMonitors[0].get_current_mode()` after IPC connect — root
cause not isolated). Reverted; will revisit once a more isolated
prototype confirms the QLocalSocket + raw-fd hybrid pattern works against
the existing init flow. Possibly folds into Phase 4 (threading cleanup)
where the encoder thread needs restructuring anyway.

**Validation**:
- Subprocess + IPC mode boots
- Frame rate unchanged (compare PPM dump cadence)
- Audio IPC ring still works (audio in headless mode)

**Docs**: Major CLAUDE.md update — the "IPC via SHM+socket" line
becomes "IPC via QSharedMemory + QLocalSocket". Update
`docs/Architecture.md` if it covers IPC details.

---

### Phase 4 — Threading cleanup at Qt seams

**Scope**: Threads that now talk to QObjects need `QThread` for safe
signal/slot. Other `std::thread` usage stays untouched.

**Files**: subprocess monitor (Phase 2), IPC reader (Phase 3), any new
QObject-touching thread.

**Refactor approach**: `QObject::moveToThread()` pattern; signals
auto-marshal across threads via queued connections. Leave the 60Hz
timer thread on `std::chrono::steady_clock` — `QTimer` jitter at 16ms
is worse than a tight sleep loop.

**Validation**:
- Boot timing within noise of pre-Phase-4 numbers
- No Qt warnings about cross-thread QObject access

**Docs**: Note threading model in this PLAN; CLAUDE.md if it gains a
threading section.

---

### Phase 5 — HTTP server → QTcpServer/QTcpSocket (QHttpServer rejected)

**Scope**: Raw `socket`+`poll` HTTP/1.1 → `QTcpServer`/`QTcpSocket`
(via `Qt6::Network`, no new package). Pivoted from `QHttpServer`
after surveying Qt 6.4's tech-preview status and its lack of native
WebSocket-on-same-port support — see "Phase 5 — actual scope (revised
2026-05-03)" below.

**Files**:
- `src/webserver/http_server.h` — forward-decl `QTcpSocket`, private
  signatures take `QTcpSocket*`
- `src/webserver/http_server.cpp` — full rewrite of the server class
  internals; public `Request`/`Response`/`Server` API unchanged
- `src/webserver/CMakeLists.txt` — add `Qt6::Network` link

**Refactor approach**: `QTcpServer::listen()` + `waitForNewConnection`
on a dedicated `std::thread`. Per-request reads via
`QTcpSocket::waitForReadyRead` + `readAll`; writes via `write` +
`waitForBytesWritten`. For long-poll stream and WebSocket upgrade, dup
the underlying fd via `socketDescriptor()` and hand it to the worker
thread — Qt closes its own fd when the `QTcpSocket` is destroyed but
the dup'd reference keeps the TCP connection alive on the worker.

**Validation**:
- `tests/test_api_endpoints.sh` (all 10 checks)
- `tests/test_mouse_position.sh`
- `npx playwright test` (browser-side E2E)
- Full `ctest --test-dir build`

**Docs**: CLAUDE.md "Single-port HTTP + WebSocket" architectural decision
updated to reflect QTcpServer.

---

### Phase 6 — WebSocket → QWebSocketServer

**Scope**: In-process RFC 6455 → `QWebSocketServer`.

**Files**:
- `src/webserver/websocket.cpp` (full rewrite)
- `src/webrtc/webrtc_server.cpp` (signaling glue — change WS handle
  type)

**Refactor approach**: `QWebSocketServer` listens on the HTTP server's
TCP port via `handleConnection()` — verify Qt supports sharing the port
with `QHttpServer`. Fallback: separate WS port if not. Frame send/
receive via `QWebSocket::sendTextMessage` / `sendBinaryMessage`.

**Validation**:
- Browser UI loads, mouse/keyboard work
- Codec switch via `/api/codec` succeeds and frames continue
- WebRTC signaling completes (ICE + SDP exchange)
- PNG/WebP binary frame path works

**Docs**: Update CLAUDE.md WebSocket architecture decision.

---

### Phase 7 — Windows SEH path activation (Linux-only commit)

**Scope**: Make `sigsegv.cpp`'s pre-existing Windows SEH path
visible/reviewable.

**Files**:
- `CMakeLists.txt` — add `HAVE_WIN32_EXCEPTIONS` define under
  `if(WIN32)`
- `src/common/sigsegv.cpp` — audit lines 596–644, 3093–3166 for any
  bit-rot (file is from Bruno Haible's library, has SEH support since
  forever)

**Validation**: Linux build + ctest unchanged. No Windows build yet.

**Docs**: Note in this PLAN that the SEH path was always there; Phase 10
will exercise it.

---

### Phase 8 — MacBrowser → QtWebEngine

**Scope**: Drop xcb+Xvfb+Firefox+BiDi entirely. Replace with in-process
`QWebEnginePage`.

**Files**:
- Delete `src/drivers/browser/supervisor.cpp`,
  `src/drivers/browser/xshm.cpp`, `src/drivers/browser/bidi.cpp`
- New: `src/drivers/browser/qtwebengine_browser.cpp`
- Keep `BrowserShm` wire protocol (guest-side `MacBrowser.bin`
  unchanged)
- CMake: drop `libxcb-*` deps; add `Qt6::WebEngineCore`,
  `Qt6::WebEngineWidgets`

**Refactor approach** — wire protocol mapping:

| Old (g2h cmd) | New (QtWebEngine) |
|---|---|
| `BR_CMD_NAV` | `QWebEnginePage::load(QUrl)` |
| `BR_CMD_CLICK` / `_MOUSE_MOVE` | Synthesized `QMouseEvent` posted to page view |
| `BR_CMD_KEY_DOWN` / `_KEY_UP` | Synthesized `QKeyEvent` |
| `BR_CMD_SCROLL` | Synthesized `QWheelEvent` |
| `BR_CMD_BACK`/`_FORWARD`/`_RELOAD`/`_STOP` | `QWebEnginePage::triggerAction(WebAction::Back/...)` |
| `BR_CMD_RESIZE` | `QWebEnginePage::view()->resize(QSize)` |
| `BR_CMD_GET_SELECTION` | `QWebEnginePage::runJavaScript("window.getSelection().toString()")` |
| `BR_CMD_PASTE` | `triggerAction(WebAction::Paste)` |
| `BR_CMD_ZOOM_IN`/`_OUT`/`_RESET` | `QWebEnginePage::setZoomFactor()` |

| Old (h2g event) | New |
|---|---|
| `BR_EV_FRAME` | Connect `QWebEnginePage::loadFinished`/`paintRequested`; grab via offscreen QPainter into `BrowserShm` |
| `BR_EV_STATUS` | `loadStarted`/`loadFinished` + `urlChanged` |
| `BR_EV_SELECTION` | Reply from `runJavaScript` callback |
| `BR_EV_PAGE_METRICS` | `runJavaScript("[innerWidth, innerHeight, scrollX, scrollY, document.body.scrollWidth, document.body.scrollHeight]")` |

**Validation**:
- `--browser` mode loads a page (e.g. `https://example.com`)
- Scroll, click, type, back/forward all work
- URL bar updates on navigation
- Resize the Mac window → Firefox window resizes inside Qt

**Docs**: Major rewrite of `docs/MacBrowser.md` — new architecture
section. Drop xcb dependencies from CLAUDE.md "Required Dependencies".

---

### Phase 9 — Native head (`-DBUILD_NATIVE_HEAD=ON`)

**Scope**: Add a `QApplication`-mode build that opens a local Qt
window for video/audio. Settings UI continues to be browser-served.

**Files**:
- New: `src/native_head/main_window.cpp` (Qt Quick or QWidget)
- New: `src/drivers/audio/audio_qaudio.cpp` (`QAudioSink`)
- `CMakeLists.txt` — `-DBUILD_NATIVE_HEAD=ON` adds `Qt6::Quick`,
  `Qt6::Multimedia`
- `main.cpp` — switch on build flag between `QCoreApplication` and
  `QApplication`

**Refactor approach**: Native window is another reader on the existing
triple-buffer (alongside the WebRTC encoder). `QImage`-backed widget
or `QQuickFramebufferObject` for paint. `QAudioSink` consumes the audio
ring directly.

**Validation**:
- Native head boot: window opens, video shows
- Audio plays through host speakers
- Web UI on port 11000 still works simultaneously
- Headless build (default) unchanged

**Docs**: New `docs/qt6/NativeHead.md`. Update CLAUDE.md to describe
the build flag.

---

### Phase 10 — Windows port

**Scope**: Actually build for Windows. Most code already portable.

**Remaining work**:
- `QSystemSemaphore` polish (Windows-specific naming/permissions)
- JIT page-protection — find UAE JIT cache mmap, replace with
  `VirtualAlloc(PAGE_EXECUTE_READWRITE)` + `FlushInstructionCache()`
- Verify `HAVE_WIN32_EXCEPTIONS` SEH path on real Windows
- Packaging: `windeployqt` for runtime DLLs + Qt plugins, NSIS or
  WiX installer
- Windows audio: `QAudioSink` already gives us WASAPI

**Validation**: Windows build succeeds; boot-to-Finder works on a
Windows VM/host.

**Docs**: New `docs/qt6/Windows.md` with build + install instructions.

---

### Phase 11 — macOS port + `.app` bundle distribution

**Scope**: Build for macOS, package as signed/notarized `.app`.

**Build**: Mostly free since POSIX layer largely works on macOS. The
`sigsegv.cpp` Mach exception path is already in the file (alongside
the SEH path). Audio: `QAudioSink` gives us CoreAudio.

**Distribution**:
- `macdeployqt MacPhoenix.app` — copies Qt frameworks, plugins,
  `QtWebEngineProcess.app` into the bundle
- Code-sign with Apple Developer ID (~$99/yr Apple Developer account
  required)
- Hardened runtime + entitlements:
  - `com.apple.security.cs.allow-jit` = true (UAE JIT)
  - `com.apple.security.cs.allow-unsigned-executable-memory` = true
    (if JIT writes RWX pages)
  - `com.apple.security.cs.disable-library-validation` = true (Qt
    plugins)
- Notarize via `xcrun notarytool submit`
- Universal binary: build twice (arm64 + x86_64), `lipo` together, or
  use Qt's universal config
- DMG: `create-dmg` for the installer

**Validation**: `.app` runs on a fresh macOS install, no security
warnings, video/audio work.

**Docs**: New `docs/qt6/macOS.md` with build + distribution
instructions.

---

## macOS distribution: how easy?

**Much easier than today** (which has zero macOS story), but not
zero-effort. The Qt port specifically gives us:

- Standard `.app` bundle structure for free
- `macdeployqt` automates framework copying + relinking
- Cross-platform Qt code that just works
- `QAudioSink` → CoreAudio
- `QtWebEngine` → WKWebView-equivalent without raw Cocoa code

What still requires effort (none of this is Qt-specific):

- Apple Developer account ($99/year)
- Code-signing dance (CSR, certificates, profiles)
- Notarization (one-time setup, then automated)
- JIT entitlements need Apple's blessing — usually granted but requires
  declaring them and surviving review
- Universal binary (arm64 + x86_64) doubles build time
- Testing on actual macOS hardware (or a VM with dev tools)

**Realistic timeline**: 1-2 weeks from "Qt port works on Linux" to
"signed/notarized DMG that runs on a stranger's Mac." Most of that is
the first-time signing/notarization setup, not coding.

## Windows distribution: how easy?

Similar story:

- `windeployqt` automates DLL/plugin gathering
- NSIS or WiX or MSIX for the installer
- Optional: code-signing with an EV cert (~$300/yr) to avoid
  SmartScreen warnings — strongly recommended for distribution

**Realistic timeline**: a few days once the Windows build itself works.

## Risk & rollback

- Each phase is one or more commits on `qt-port`. Rollback = `git
  revert` of those commits.
- The `qt-port` branch is never force-pushed to. `main` stays untouched
  until the port is complete and merged.
- If a phase introduces a regression that tests catch, revert + redo;
  if tests *don't* catch it, add a test before reverting.
- Phase 3 (IPC) is the highest-risk single phase. Prototype the
  semaphore wakeup latency *before* committing the full swap.

## Doc update protocol

Per phase:
1. Update this PLAN with what was actually done (vs. what was
   estimated)
2. Update `CLAUDE.md` for any architectural decisions or build deps
   that changed
3. Update or create the relevant `docs/*.md` for user-facing changes
   (e.g. `MacBrowser.md` after Phase 8)
4. Commit doc changes with the code change, not separately

## Status

| # | Phase | Status | Commit |
|---|---|---|---|
| 0 | Bootstrap Qt6 in CMake | ✅ done | `faed9de9` |
| 1 | File I/O → QDir/QFileInfo | ✅ done | `731602f3` |
| 1.5 | Qt6 in build/packaging/CI infra | ✅ done | `a0cb5df8` |
| 2 | Subprocess → QProcess | ✅ done | `bbf10324` |
| 3a | IPC SHM → QSharedMemory | ✅ done | `702ccba8` |
| 3b | IPC sockets + notify → QLocalSocket | ⚠️ deferred (attempted, reverted — see `76f9a086`) |
| 4 | Threading cleanup at Qt seams | ⚠️ deferred (no work in current state; lands with 3b retry) |
| 5 | HTTP server → QTcpServer/QTcpSocket | ✅ done — pivoted from QHttpServer; see notes |
| 6 | WebSocket → QWebSocketServer | pending |
| 7 | Windows SEH path activation | ✅ done | `e80f314f` |
| 8 | MacBrowser → QtWebEngine | pending |
| 9 | Native head (QApplication mode) | pending |
| 10 | Windows port + installer | pending |
| 11 | macOS port + signed `.app` + DMG | pending |
| 12 | OpenSSL → QCryptographicHash | ✅ done — drops direct OpenSSL use (libdatachannel still needs it transitively for DTLS/SRTP) |
| 13 | POSIX timing → `QElapsedTimer` + `QThread::usleep` | ✅ done — `timer_interrupt.cpp`; ctest 20/20 |
| 14 | Leftover `fork`/`execvp` → `QProcess` | ✅ done — `api_handlers::run_fork_exec` + `ether_socket::spawn_bridge`/`stop_bridge` |
| 15 | `pthread`/`sem_t` in `serial_unix.cpp` → `QThread`/`QSemaphore` | pending — Phase 10 unblocker |
| 16 | `std::thread` in webserver → `QThread` | ✅ done — accept loop wrapped in `HttpServerThread : QThread` |
| 17 | `nlohmann::json` → `QJsonDocument`/`QJsonObject` | partial — all kept files migrated; subproject drop blocks on Phase 8 (bidi.cpp still uses it) |

**Recommended execution order for the dep-drop phases**: 12 (easy
warmup, deletes a `find_package`) → 13/14 (small, mechanical) → 17
(big diff but biggest dep payoff) → 15/16 (cosmetic at this point).
Phase 8 (xcb → QtWebEngine) drops 5 more apt deps but is
independently large and orthogonal — schedule when there's appetite
for a multi-day chunk.

## Phase 1.5 — Qt6 in build/packaging/CI

Added retroactively after Phase 1 because the deb/rpm build path isn't
covered by `cmake -B build` validation. Single source-of-truth files:

- `debian/control` Build-Depends — also covers
  `.github/workflows/build.yml` (uses `apt-get build-dep .`) and
  `packaging/Dockerfile.deb` (same)
- `rpm/mac-phoenix.spec` BuildRequires
- `packaging/Dockerfile.dev` (apt install)
- `packaging/Dockerfile.rpm` (dnf install)

`packaging/build-ctx/` is gitignored (regenerated from source-of-truth
files by `tools/make-source-tarball.sh` / `packaging/run_matrix.sh`).
`packaging/Dockerfile.sandbox` runs the installed .deb so its Qt6
runtime libs come in via `${shlibs:Depends}`.

**Protocol**: each subsequent phase that introduces a new Qt component
(httpserver in 5, websockets in 6, webengine in 8, multimedia in 9)
updates this same set of files in its commit.

## Phase 3b lessons (for retry)

The attempt swapped AF_UNIX socket for `QLocalServer`/`QLocalSocket` and
replaced `eventfd` + `SCM_RIGHTS` with a notify-byte over a second
`QLocalSocket`. Build was clean; tests caught a child-side regression in
`init_mac_subsystems` at `VideoMonitors[0].get_current_mode()` after IPC
setup completes (`boot_se` deterministic crash). Root cause not isolated
in the deferred-from-this-session timebox.

**For retry**:
- Run **Phase 4 first** — encoder thread becomes a `QObject` in a `QThread`
  with `exec()`. Then the notify socket has a natural home (lives on the
  encoder thread, signal/slot driven via `QLocalSocket::readyRead`).
- The hybrid raw-`::send` pattern from CPU thread is still correct; what
  was wrong was either init ordering or some interaction between
  `QLocalServer` setup and the child's video init that we didn't isolate.
- Keep `IPCBuffer` struct layout stable (rename `frame_ready_eventfd` →
  `_reserved_eventfd`, don't change size — both processes rebuild from
  same source, but stable layout makes for easier diffing).
- Single biggest bisection question: does the regression appear if we
  swap *only* the control socket (not notification) with everything else
  unchanged? That isolates the QLocalServer-vs-init-ordering interaction.
- Strongly consider doing the swap in a tiny one-file prototype first
  (a synthetic child + parent that just exchange the IPC handshake)
  before re-touching the real code.

## Phase 5 — actual scope (revised 2026-05-03)

Originally planned as `QHttpServer`. **Pivoted to `QTcpServer` /
`QTcpSocket` directly** after this round of investigation surfaced two
blockers that the original PLAN didn't fully internalize:

1. Qt 6.4's `QHttpServer` is shipped as a Tech Preview module; the API
   wasn't fully stabilized until ~6.7.
2. `QHttpServer` in 6.4 has no native WebSocket-on-same-port support
   (`QHttpServerWebSocketUpgradeResponse` only landed in 6.5+). To keep
   `/ws` and `/api/stream` sharing port 11000 with the new HTTP routes
   we'd have needed a custom `QTcpServer` subclass with `peek()`-based
   dispatch — i.e., we'd be working around `QHttpServer` for ~half the
   surface anyway.

Since the goal of Phase 5 is "get off raw POSIX so Windows port is
unblocked," `QTcpServer` directly achieves it with strictly less risk.
What landed:

- `http_server.h` — forward-decl `QTcpSocket`, private signatures
  switch from `int client_fd` to `QTcpSocket*`.
- `http_server.cpp` — `Server::run()` owns a stack-allocated
  `QTcpServer` for its lifetime; `start()` waits on a
  `std::promise<bool>` so it can still return `false` on bind failure.
  Per-request reads via `waitForReadyRead` + `readAll`; writes via
  `write` + `waitForBytesWritten`. Long-poll stream and WebSocket
  upgrade `dup()` the underlying `socketDescriptor()` and hand the
  copy to a worker thread; the original Qt-owned fd is closed at
  `QTcpSocket` destruction but the dup'd reference keeps the TCP
  connection alive on the worker.
- Public `Request`/`Response`/`Server` API and `StreamHandler`/
  `WebSocketHandler` callback signatures unchanged → `api_handlers.cpp`,
  `webrtc_server.cpp`, `webserver_main.cpp`, `static_files.cpp`,
  `http_stream.cpp`, and `websocket.cpp` need no changes.
- `src/webserver/CMakeLists.txt` — link `Qt6::Network`. No new package
  dep (Network is part of `qt6-base-dev`).

WebSocket move to `QWebSocketServer` defers to Phase 6 as planned.
`std::thread` → `QThread` sweep tracked separately as Phase 16.

## Phases 12–16 — POSIX cleanup & Qt completeness

Identified during the Phase 5 investigation. Sequenced before Phase 10
(Windows port) because each one is a Windows-build blocker.

### Phase 12 — OpenSSL → QCryptographicHash

**Scope**: Drop the OpenSSL build dep entirely.

**Files**:
- `src/webserver/websocket.cpp` — `SHA1()` for RFC 6455 accept hash
- `src/webserver/file_scanner.cpp` — `EVP_DigestInit_ex(md5)` for ROM
  fingerprinting

**Refactor**: `QCryptographicHash::hash(data, QCryptographicHash::Sha1)`
returns a `QByteArray`. Identical for MD5. Drop `find_package(OpenSSL)`
and `OpenSSL::Crypto` link from `src/webserver/CMakeLists.txt` and
top-level `CMakeLists.txt`. Remove `libssl-dev` from `debian/control`
and the rpm/Dockerfile equivalents.

**Validation**: `tests/test_api_endpoints.sh` (websocket handshake),
ROM scan in `/api/storage`.

### Phase 13 — POSIX timing → Qt timing

**Scope**: Replace `nanosleep` and `clock_gettime` so the timer thread
is portable.

**Files**:
- `src/drivers/platform/timer_interrupt.cpp` — `clock_gettime` →
  `QElapsedTimer`, `nanosleep` → `QThread::usleep` (Qt's portable
  sleep, no header dance on Windows).

**Validation**: 60.15 Hz tick rate unchanged (boot timing within
noise of pre-13 numbers).

### Phase 14 — Leftover `fork`/`execvp` → `QProcess`

**Scope**: Phase 2 ported the big subprocess fork. Two leftover sites
remain.

**Files**:
- `src/webserver/api_handlers.cpp::handle_create_image` — wraps
  `hfsutils` invocation in a fork/execvp.
- `src/drivers/ether/ether_socket.cpp` — net-bridge spawn.

**Refactor**: `QProcess::execute(program, args)` for synchronous
invocations; `QProcess::start()` + signals for the long-lived
net-bridge spawn (or keep synchronous if the lifecycle is simple).

**Validation**: `POST /api/storage/create-image` round-trip;
`--network socket:` smoke test.

### Phase 15 — `pthread`/`sem_t` → `QThread`/`QSemaphore` in serial driver

**Scope**: `serial_unix.cpp` is the last big POSIX-threaded
subsystem. Rename to `serial.cpp` (cross-platform).

**Files**:
- `src/drivers/serial/serial_unix.cpp` — `pthread_create`/`_cancel`/
  `_join` → `QThread`; `sem_init`/`sem_wait`/`sem_post` →
  `QSemaphore::acquire`/`release`.

**Validation**: Serial port loopback if any test exists; otherwise
manual smoke (rare-use feature).

### Phase 16 — `std::thread` in webserver → `QThread`

**Scope**: Completeness sweep — once Phase 5 is in, the HTTP accept
loop and detached stream/WS workers should be `QThread` for
consistency with the rest of the Qt port. Functionally `std::thread`
works (Qt 6.4 doesn't enforce QObject thread-affinity for synchronous
`waitForX` calls), but `QThread` integrates with Qt's debug/warning
machinery and signals.

**Files**:
- `src/webserver/http_server.cpp` — wrap `Server::run()` in a
  `QThread` subclass.
- Detached stream/WS worker threads — optional swap; they don't use
  Qt APIs after the fd handoff so `std::thread::detach()` is fine.

**Validation**: full ctest, no Qt cross-thread warnings in debug
build.

### Phase 17 — `nlohmann::json` → `QJsonDocument` / `QJsonObject`

**Scope**: Drop the `nlohmann_json` subproject entirely. Big mechanical
diff but biggest single dep removal in the port.

**Files** (every site that includes `<nlohmann/json.hpp>`):
- `src/webserver/api_handlers.cpp` (~1978 LOC; majority of touch
  surface — every request body parse + response build)
- `src/webserver/static_files.cpp` (`inject_config_template`)
- `src/webrtc/webrtc_server.cpp` (signaling JSON)
- `src/config/json_utils.{h,cpp}` (the local convenience wrapper —
  becomes a `QJsonObject` wrapper instead, or gets inlined out)
- `src/config/emulator_config.cpp` (config save/load)
- `src/core/command_bridge.cpp`, `src/core/emulator_subprocess.cpp`,
  `src/core/boot_progress.cpp` — any site that builds JSON for
  inter-process or status reporting

**Refactor approach**: `nlohmann::json` and `QJsonObject` differ in
two ways that matter:

1. `nlohmann` uses initializer lists: `j["k"] = v;`,
   `j.push_back({{"k", "v"}})`. `QJsonObject` requires explicit
   construction: `QJsonObject obj; obj["k"] = QJsonValue(v);`. Each
   leaf assignment translates 1:1.
2. `nlohmann` parses with `json::parse(str)` returning `json`;
   `QJsonDocument` parses with `QJsonDocument::fromJson(QByteArray)`
   returning `QJsonDocument`, then `.object()` for the root.

The `json_utils::get_string`/`get_int` helpers become
`QJsonObject::value(k).toString()`/`.toInt()` — same pattern, fewer
characters.

Recommended sub-decomposition:
- **17a**: introduce a `QJsonObject`-backed `json_utils` and migrate
  all the small sites (config, command_bridge, boot_progress, webrtc,
  static_files). Validates the helper is right.
- **17b**: migrate `api_handlers.cpp` in topical chunks (config
  endpoints → storage → emulator → command bridge → media). Each
  chunk testable in isolation against `tests/test_api_endpoints.sh`.
- **17c**: drop the `nlohmann_json` subproject, remove `find_package`
  from `CMakeLists.txt`, remove the linkage from
  `src/webserver/CMakeLists.txt`.

**Validation**: every API endpoint response byte-identical to
pre-migration (compare via `diff` against captured baseline JSON).
ctest 20/20.

5a alone is ~70% of the user-facing API surface.
| 11 | macOS port + signed `.app` + DMG | pending |
