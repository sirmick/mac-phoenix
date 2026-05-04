# Qt6 Port — Next Session Handoff

You're picking up the `qt-port` branch mid-port. Read this, then
`docs/qt6/PLAN.md` for the full plan. **Don't re-derive what's
already done.**

## Where we are

Branch: `qt-port` (8 commits ahead of `main`). Linux build works,
20/20 ctest passes (modulo two known flakes — see below).

The **foundation** is in place:
- Qt6 linked via CMake (`Qt6::Core`, `Qt6::Network` where used)
- `QCoreApplication` instantiated in `main()` (no event loop yet —
  CPU emulator owns main thread)
- `QDir`/`QFileInfo` for filesystem ops
- `QProcess` for child emulator subprocess (the parent/child split
  is **structural** — see `feedback_keep_emulator_subprocess.md` in
  memory; never collapse into one process)
- `QSharedMemory` for the IPC video SHM
- Windows SEH path armed in `sigsegv.cpp` (no Windows build yet)
- Build/packaging/CI updated for Qt6 deps

The **HTTP/WebSocket/WebRTC** layer is **untouched**. So is the IPC
socket layer (still uses raw `AF_UNIX` + `eventfd` + `SCM_RIGHTS`).
QtWebEngine browser swap also untouched.

## Locked-in design decisions

Don't relitigate these unless the user asks:

| Decision | Choice |
|---|---|
| Migration style | Hard switch per subsystem (no `#ifdef USE_QT`) |
| Qt version | Qt 6.4 minimum |
| String type | `std::string` internal, `QString` only at Qt API boundaries |
| JSON | Keep `nlohmann::json`. `QJsonDocument` only when a Qt API hands you one |
| WebRTC | Keep `libdatachannel` (Qt has no RTP stack) |
| Subprocess | **MUST stay** — parent/child split is structural |
| Refactor tooling | grep + sed + tests. No `comby`. |

## Test posture

```
ctest --test-dir build --output-on-failure   # ~230s total, 20 tests
```

**Known flakes** (do not block on these):
- `boot_uae_jit` — pre-existing UAE JIT instability, "fault at 0x0"
- `command_bridge` — ordering / load-sensitive; passes in isolation in ~2s

After any meaningful change, run the full ctest. The cost is one
context window of waiting; the benefit is catching regressions before
they pile up.

## What's next

**Start of next session: Phase 5 (HTTP server → QHttpServer)**.

Recommended sub-decomposition (also in PLAN.md):

- **5a**: GET routes that don't need long-poll or WebSocket — should map
  cleanly to `QHttpServer::route()`. ~70% of the user-facing API.
- **5b**: Long-poll `/api/frame` + `/api/stream`. Qt 6.4's QHttpServer
  deferred-response support is limited; may need to extract raw fd via
  `QTcpSocket::socketDescriptor()` and keep the current
  StreamHandler model (hybrid pattern).
- **5c**: WebSocket `/ws` upgrade — defers to Phase 6.

**Build infra**: add `qt6-httpserver-dev` to `debian/control`,
`rpm/mac-phoenix.spec`, `packaging/Dockerfile.dev`,
`packaging/Dockerfile.rpm` (Phase 1.5 protocol — same set of files
each time a new Qt component is introduced). Install via:

```
sudo apt install qt6-httpserver-dev
```

(Will need user's sudo prompt — Bash tool can't enter it.)

## Files you'll touch in Phase 5

- `src/webserver/http_server.cpp` — full rewrite (425 lines)
- `src/webserver/http_server.h` — API surface change
- `src/webserver/api_handlers.cpp` — 1978 lines of handlers; **bodies
  stay**, only the dispatch wrapper changes
- `src/webserver/http_stream.cpp` — long-poll machinery (5b)
- `src/webserver/static_files.cpp` — re-route via `QHttpServer::route`
- `src/webserver/webserver_main.cpp` — server init/shutdown
- `src/webrtc/webrtc_server.cpp` — `register_routes(server)` adapts
  to QHttpServer's API
- `src/webserver/CMakeLists.txt` — add `Qt6::HttpServer`

## Phase 3b (deferred — not for next session)

The QLocalSocket + notify-byte attempt was reverted (see commit
`76f9a086` and PLAN.md "Phase 3b lessons" section). **Don't retry it
in next session** — it should land *together with* Phase 4 (encoder
becomes a `QObject` in a `QThread`), which gives the notify socket a
natural home and signal-driven semantics. That's its own dedicated
session after Phase 5/6 are done.

## Things to avoid

- **Don't** collapse the emulator into the parent process even if it
  looks tempting under Qt (memory: `feedback_keep_emulator_subprocess.md`)
- **Don't** start Phase 8 (QtWebEngine MacBrowser swap) without the
  user's explicit go-ahead — pulls in ~250MB Chromium and is
  user-visible
- **Don't** wholesale-migrate to `QString` or `QJsonDocument` — see
  decisions table above
- **Don't** skip the test suite "to save time" — flakes are flakes,
  but real regressions hide there

## First thing to do in next session

```
git status                  # confirm clean
git log --oneline -10       # see what's landed
ctest --test-dir build      # baseline (should be 20/20 modulo flakes)
```

Then start Phase 5a. Read `src/webserver/http_server.cpp` and
`src/webserver/http_server.h` first to internalize the existing API
before drafting the QHttpServer-based replacement.
