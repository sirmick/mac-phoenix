# MacBrowser

`--browser` runs an in-process Chromium (via Qt6 WebEngine) and pipes
its rendered pixels into a guest Mac app called **MacBrowser**. The
guest sees a normal classic-Mac window with a URL bar, back / forward /
stop / reload toolbar, status strip, and a scrollable PixMap viewport.
Mouse, keys, scroll, paste, and select-all are forwarded as synthesized
`QMouseEvent` / `QKeyEvent` / `QWheelEvent` posted into a hidden
`QWebEngineView`. Downloads land in the guest filesystem via the
existing ExtFS share. Cookies / logins / prefs survive launches because
QtWebEngine runs with a persistent profile.

This avoids three intractable problems with running a 1996 browser
against the modern web — TLS handshake compatibility, custom-CA-import
UI, and modern HTML/CSS/JS rendering on a 25 MHz 68040.

## Architecture

```
┌─ mac-phoenix process ────────────────────────────────────┐
│                                                          │
│  ┌─ src/drivers/browser/ ─────────────────────────────┐  │
│  │  qtwebengine_browser  — owns QWebEngineView,       │  │
│  │                         capture timer, metrics     │  │
│  │                         timer, g2h drain thread    │  │
│  │  cmd                  — BR_CMD_* → qt_dispatch_*   │  │
│  │  mouse_poll           — host-side cursor peek;     │  │
│  │                         forwards via qt_dispatch_  │  │
│  │  shm                  — owns BrowserShm region,    │  │
│  │                         send_event / read_command  │  │
│  │  ring                 — SPSC ring helpers          │  │
│  │  module               — thin lifecycle shim        │  │
│  └────────────────────────────────────────────────────┘  │
│         │                                                │
│         │ BrowserShm (~1.7 MiB) — guest-allocated;       │
│         │ host gets a writable pointer via Mac2HostAddr  │
│         ▼                                                │
│  ┌─ guest VM ─────────────────────────────────────────┐  │
│  │  RAM       @ 0x00000000 (32 MiB)                   │  │
│  │  ROM       @ 0x02000000 (1 MiB)                    │  │
│  │  Scratch   @ 0x02100000 (64 KiB)                   │  │
│  │  FrameBuf  @ 0x02110000 (8 MiB)                    │  │
│  │  BrowserShm — MacBrowser.app's app heap, location  │  │
│  │               published via ExtFS handshake file   │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  QApplication owns the main thread (set up in main.cpp   │
│  when --browser is on; CPU runs on a worker thread).     │
│  app.exec() drives Chromium's compositor signals,        │
│  QTimer ticks (capture, metrics), runJavaScript          │
│  callbacks, downloadRequested, etc.                      │
└──────────────────────────────────────────────────────────┘
                         ▲
                         │ QWebEngineProfile (persistent)
                         │ QWebEnginePage (in-process Chromium)
                         │ QSG software RHI → QImage backing store
                         │
                         └── QtWebEngineProcess (Chromium child;
                             managed by Qt, --no-sandbox set)

Inside the guest:

┌─ MacBrowser.app (Retro68 m68k) ──────────────────────────┐
│  Window: URL bar | Back | Fwd | Stop | Reload | viewport │
│  V/H scrollbars; status strip with loading spinner;      │
│  Cmd-L / Cmd-R / Cmd-[ / Cmd-] / Cmd-W / Cmd-Q / Cmd-+/− │
│                                                          │
│  VBL task (interrupt-level, A5 stashed in VBLTask):      │
│    drain h2g ring → set pending_main flag                │
│    drain queued user-events → g2h ring                   │
│                                                          │
│  WaitNextEvent main loop:                                │
│    translate user events → enqueue commands              │
│    if pending_main: handle events; on BR_EV_FRAME,       │
│      CopyBits each fb.dirty[] rect from BrowserShm into  │
│      the window                                          │
└──────────────────────────────────────────────────────────┘
```

Net-bridge is **not** involved — host ↔ guest goes entirely through
`BrowserShm` plus the small ExtFS handshake file.

## Smoothness strategy

QtWebEngine's renderer paints through Qt Scene Graph (QSG) over the
RHI (Rendering Hardware Interface) abstraction. Three constraints
shape our setup:

1. **Software rasterization, forced.** Chromium's hardware overlays
   for `<video>` and WebGL bypass any captured surface — frames render
   black. The IPC subprocess's `main.cpp` sets `QTWEBENGINE_CHROMIUM_FLAGS=
   --no-sandbox --disable-gpu-compositing --in-process-gpu --use-gl=
   swiftshader` and `QSG_RHI_BACKEND=software` **before** the
   `QApplication` constructor. SwiftShader gives Chromium a software
   GL implementation; QSG software-rhi rasterizes to a `QImage` that
   QtWebEngine composites into its backing store.

2. **`QApplication` on the main thread.** QtWebEngine refuses to
   deliver `loadFinished` / `urlChanged` / `paintEvent` correctly when
   the application instance lives on a worker thread. The IPC
   subprocess pre-scans `argv` for `--browser` + `--ipc`, switches
   from `QCoreApplication` to `QApplication` if both are present, and
   spawns `cpu_execute_fast` on a detached worker so `app.exec()` can
   drive the main thread.

3. **Polling, not paintEvent.** `QWebEngineView` doesn't override
   `paintEvent` — Chromium renders through the internal
   `RenderWidgetHostViewQtDelegateWidget`, bypassing the QWidget paint
   chain. A 60 Hz `QTimer` calls `view->grab()` (synchronous render
   into a `QPixmap`), the result is converted ARGB32 → RGB555-BE and
   pushed into `BrowserShm.fb.pixels` with the whole image as one
   dirty rect. Measured cost at 1024×768: grab=1.0ms, blit=2ms, total
   well under the 16ms budget. An event-driven rewrite using
   `QQuickWebEngineView` + `QQuickWindow::afterRendering()` is tracked
   as Phase 8c-2 in `docs/qt6/PLAN.md` — same architectural shape,
   zero idle CPU when the page isn't repainting.

## The shared memory contract

A single `BrowserShm` struct (~1.7 MiB, mostly framebuffer) lives in
guest memory. MacBrowser.app `NewPtrClear`s it from its own application
heap (SIZE resource ≥ 4 MiB) and writes the buffer's Mac address as
ASCII hex into `Host:MacPhoenix:browser_shm.txt` on the ExtFS share.
The host's shm watcher reads that file, validates `magic == 'BRWS'`
and `version == BR_VERSION`, and translates Mac → host via
`Mac2HostAddr()`. That dodges per-backend banking work — every backend
(UAE, Unicorn-m68k, Unicorn-PPC, KPX) sees ordinary guest RAM, no
fixed `BR_BASE_ADDR`, no `uc_mem_map_ptr` for the region, no UAE
`ram_bank` extension.

| Field | Direction | Description |
|---|---|---|
| `magic` | guest writes | `'BRWS'` |
| `version` | guest writes | `BR_VERSION = 2` |
| `flags` | guest writes | capability bits |
| `h2g` | host → guest ring | events (frame-ready, status, downloads, selection, page metrics) |
| `g2h` | guest → host ring | commands (nav, click, key, paste, scroll, zoom, select-all, resize) |
| `log` | guest writes | single-slot lossy debug log; host polls + prints to stderr with `[BrowserGuest <level>]` |
| `viewport_screen_left/top` | guest writes | viewport top-left in Mac screen coords for host-side mouse-poll math |
| `fb.seq` | host writes | bumped per frame; guest detects new frames |
| `fb.dirty[]` | host writes | damage rectangles for the current `seq` |
| `fb.pixels[]` | host writes | RGB555 pixel data, top-down |

Layout, message types, and accessors are in
`src/common/include/MacBrowser.h`. Host defines `BR_HOST` to enable
byte-swap on multi-byte field access; the guest is native big-endian
and the swap is a no-op. `BR_FENCE_RELEASE` / `BR_FENCE_ACQUIRE` macros
emit `__sync_synchronize` on the host, `eieio` / `lwsync` on PPC
guests, and compiler fences on m68k.

### SPSC ring discipline

Each ring is single-producer / single-consumer with separate
`write_idx` / `read_idx` and a 4-byte gap so empty (`write == read`)
is distinguishable from full. Messages are `[u16 type][u16 len]
[payload]`, payload padded to 4-byte alignment. A `BR_MSG_WRAP`
sentinel (`type=0, len=0`) jumps the read pointer back to ring offset
0 when a message wouldn't fit before the buffer end. The producer
accounts for both the WRAP header **and** the unused tail bytes
between `write_idx` and the ring boundary when checking free space —
without that accounting, `write_idx` can land exactly on `read_idx`
after a successful push, which then *looks* empty and silently loses
the just-pushed payload.

`tests/test_browser_shm.cpp` (10 cases / 47 assertions, ctest `unit`
label) round-trips messages under wraparound, fill-to-full,
interleaved push/pop, bidirectional independence, oversized
rejection, and truncation reporting. Compiles `ring.cpp` directly so
we exercise the exact code `shm.cpp` links against.

## Wire protocol

`BR_VERSION = 2` — bumped from 1 on the qt-port branch to add
`BR_EV_TITLE`, `BR_EV_HISTORY`, `BR_EV_ZOOM` and to enforce strict
per-opcode payload lengths in the host dispatcher (`cmd.cpp`). Each
opcode has a single canonical length contract documented in
`MacBrowser.h`; mismatched payloads are dropped with a one-line
warning rather than partial-dispatched.

| Command (g2h) | Routed to |
|---|---|
| `BR_CMD_NAV` | `QWebEngineView::setUrl(QUrl)` |
| `BR_CMD_CLICK` / `_MOUSE_MOVE` / `_MOUSE_OUT` | Synthesized `QMouseEvent` posted to view |
| `BR_CMD_KEY_DOWN` / `_KEY_UP` | Synthesized `QKeyEvent` |
| `BR_CMD_SCROLL` | Synthesized `QWheelEvent` |
| `BR_CMD_BACK` / `_FORWARD` / `_RELOAD` / `_STOP` | `QWebEnginePage::triggerAction(...)` |
| `BR_CMD_RESIZE` | `view->resize(QSize)` + update `fb.width/height` |
| `BR_CMD_GET_SELECTION` | `runJavaScript("window.getSelection().toString()")` callback emits `BR_EV_SELECTION` |
| `BR_CMD_SELECT_ALL` | `triggerAction(SelectAll)` |
| `BR_CMD_PASTE` | `runJavaScript` insertText (input/textarea/contentEditable paths) |
| `BR_CMD_ZOOM_IN` / `_OUT` / `_RESET` | `setZoomFactor(steps[idx])` |

| Event (h2g) | Generated by |
|---|---|
| `BR_EV_STATUS` | `loadStarted` / `loadFinished` / `urlChanged` signals |
| `BR_EV_TITLE` | `QWebEnginePage::titleChanged`; emitted only on change |
| `BR_EV_HISTORY` | `QWebEngineHistory::canGoBack/canGoForward` after each url/load transition; emitted only on change |
| `BR_EV_ZOOM` | After every `setZoomFactor()` from `BR_CMD_ZOOM_*`; emitted only on change |
| `BR_EV_FRAME` | Capture timer (60 Hz) bumps `fb.seq` |
| `BR_EV_SELECTION` | Reply from `runJavaScript` callback |
| `BR_EV_PAGE_METRICS` | Metrics timer (4 Hz) `runJavaScript` of [scrollWidth, scrollHeight, scrollX, scrollY, innerWidth, innerHeight] |
| `BR_EV_DOWNLOAD` | `QWebEngineProfile::downloadRequested` + per-request `receivedBytesChanged` / `isFinishedChanged` |

### State ownership

The host owns every piece of page state (URL, title, loading, history,
zoom, scroll/metrics, selection, downloads, framebuffer). Each state
piece has exactly one canonical storage location on the host and one
event that publishes changes to the guest. The guest mirrors what it
needs for UI; it never invents page state on its own. The only state
the guest authoritatively owns is the chrome surface itself: window
position, the URL bar TextEdit while the user is typing, and viewport
pixel size (committed via `BR_CMD_RESIZE`). See the comment block at
the top of `src/common/include/MacBrowser.h` for the full table.

## Threading

| Thread | Owner | Responsibility |
|---|---|---|
| **GUI** (main) | `QApplication::exec()` | QWebEngineView, paint, JS callbacks, download signals, capture timer (60 Hz), metrics timer (4 Hz). All QtWebEngine objects live here. |
| CPU | `cpu_execute_fast()` worker | m68k/PPC instruction dispatch. Spawned as a detached thread when `--browser` is set. |
| g2h drain | `QtWebEngineBrowser` worker | Polls `g2h` ring + `poll_log` at 16 ms. Calls `cmd_dispatch` → `qt_dispatch_*`. The `qt_dispatch_*` layer marshals each event back to the GUI thread via `QMetaObject::invokeMethod(Qt::QueuedConnection)`. |
| Mouse poll | `mouse_poll` worker | Reads Mac low-memory globals at 60 Hz, forwards via `qt_dispatch_mouse_move/click`. |
| (other) | (existing) | HTTP, WebRTC, encoders, BridgeAgent supervisor — unchanged from the rest of the emulator. |

Cross-thread access to `g_browser` (the singleton `QtWebEngineBrowser`)
is always via `QMetaObject::invokeMethod` on the `QApplication`
instance; the lambda captures any payload, runs on the GUI thread,
and null-checks the singleton (it may have been reset between marshal
and invoke).

## Mouse model — host-side polling, zero guest events

The host does **not** receive mouse-move events through the g2h ring.
Both pieces of state it needs — cursor position and front-app gate —
are already in guest memory at fixed locations the host can read
directly.

| Source | What | Notes |
|---|---|---|
| `Mouse.v` $082C, `Mouse.h` $082E | Screen-space cursor | Always current; ADB driver updates per VBL |
| `MBState` $0172 | Mouse button state | 0xFF = up, 0x00 = down |
| `CurApName` $0910 (Pascal string) | Frontmost app name | Gate: only forward when MacBrowser is front |
| `BrowserShm.viewport_screen_left/top` | Viewport top-left in Mac screen coords | Guest publishes whenever its window moves |
| `BrowserShm.fb.{width,height}` | Viewport extent | Bounds the inside-page check |

`page_xy = mouse_screen_xy − viewport_screen_topleft`, then forwarded
via `qt_dispatch_mouse_move(page_x, page_y)` (or `qt_dispatch_click`
on a button-down transition). Cost: one read per Mac low-memory global
per 16 ms tick. Multi-click detection (500 ms, 5 px radius, capped at
3) and rate-limiting (8 clicks/s) keep stuck-button scenarios sane.

The guest still sends events with intrinsic semantics the host can't
infer: `BR_CMD_CLICK` (must align with the frame the user saw),
`BR_CMD_KEY_*`, `BR_CMD_NAV` / `_BACK` / `_FORWARD` / `_STOP` /
`_RELOAD`, `BR_CMD_SCROLL` for keyboard scroll (Page Down / arrows),
and `BR_CMD_PASTE` / `_GET_SELECTION` for the clipboard bridge.

## Files

### Host: `src/drivers/browser/`

| File | Role |
|---|---|
| `qtwebengine_browser.{h,cpp}` | `QtWebEngineBrowser` owns the hidden `QWebEngineView`, capture timer, metrics timer, and g2h drain worker. `qt_dispatch_*` thread-safe wrappers around member methods (marshal to GUI thread). Download lifecycle hooked via `QWebEngineProfile::downloadRequested`. |
| `cmd.{h,cpp}` | Stateless `cmd_dispatch(type, payload, len)` switch that maps each `BR_CMD_*` to the corresponding `qt_dispatch_*`. |
| `mouse_poll.{h,cpp}` | Host-side cursor poll loop — reads Mac low-memory globals, forwards via `qt_dispatch_*`. Independent worker thread (needs `RAMBaseHost`/`ReadMacInt8`, not Qt). |
| `shm.{h,cpp}` | ExtFS handshake watcher; `send_event`, `read_command`, `poll_log`. |
| `ring.{h,cpp}` | SPSC ring push/pop, shared with the unit test. |
| `module.{h,cpp}` | Thin shim — `browser_module_start` calls `qtwebengine_module_start` + `mouse_poll_start`. Kept so main.cpp's call sites don't fan out. |

### Guest: `MacBrowser/` (top-level, peer to `src/`)

| File | Role |
|---|---|
| `MacBrowser.c` | App: window, URL bar, toolbar, viewport, event loop, BlockMove + CopyBits from shm |
| `browser_shm.{h,c}` | Guest-side ring helpers, magic/version check |
| `MacBrowser.r` / `icons.r` | Resources: SIZE, WIND, MENU, ALRT, icon family |
| `MacBrowser.bin` | Committed binary (CMake regenerates from source when Retro68 is detected) |
| `MacBrowser.dsk` | Committed floppy image used by tests + the docs example boot |

CMake builds `MacBrowser.bin` from source when Retro68 is detected;
opt out with `-DBUILD_MAC_BROWSER=OFF` (the committed binary is the
fallback).

### Wiring

| File | Hook |
|---|---|
| `src/main.cpp` | Pre-scans argv for `--browser` + `--ipc`. Sets `Qt::AA_ShareOpenGLContexts` + Chromium env flags before `QApplication` ctor. In the IPC child, spawns `cpu_execute_fast` on a detached thread and runs `app.exec()` on main when `--browser` is active. Calls `browser_module_start()` after `init_m68k`/`init_ppc`. |
| `src/config/emulator_config.{h,cpp}` | `browser_enabled` boolean, JSON serialise, CLI parse |
| `src/core/emulator_subprocess.cpp` | Propagates `--browser` to IPC child argv |

## Design rationale (the bits that aren't obvious from the code)

- **`QApplication` on main thread, CPU on a worker.** Earlier
  iterations kept the existing `QCoreApplication` on main + spawned
  `QApplication` on a worker thread for QtWebEngine — Chromium would
  init, `urlChanged` would fire, but `loadFinished` never delivered.
  Putting `QApplication` on the literal main thread fixed every
  signal delivery problem and produced a clean process model: GUI
  thread = Qt + browser; CPU thread = emulator.
- **`Qt::AA_ShareOpenGLContexts` set before any `QCoreApplication`.**
  Without it QtWebEngine emits a warning and certain compositor paths
  silently degrade. Set unconditionally when `--browser` + `--ipc` are
  in argv, before any Qt activity.
- **`--no-sandbox` for Chromium.** Chromium's user-namespace sandbox
  is fragile under many container setups and we run inside the
  emulator's own host-trust boundary; the additional sandbox doesn't
  add a meaningful boundary here.
- **`--use-gl=swiftshader` over `--disable-gpu`.** With pure
  `--disable-gpu`, QSG can't find a graphics backend ("No suitable
  graphics backend found") and pages don't load. SwiftShader provides
  a software GL that satisfies QSG without engaging real hardware.
- **Whole-frame dirty rect.** The 8c skeleton publishes the entire
  captured frame as `fb.dirty[0]` rather than per-region diffing.
  Conversion is fast enough at 1024×768 (~2 ms) that it doesn't
  matter for smoothness; per-region diff is a follow-up if guest
  CopyBits cost becomes the bottleneck.
- **No raw multi-byte access in BrowserShm** — every load/store goes
  through `br_u16/u32_load/store` accessors gated on `BR_HOST`. The
  one wraparound bug that surfaced was tail-byte accounting in the
  ring producer, not endianness.
- **Toolbar = text labels**, not icons. Period-correct (Netscape
  Navigator 3, iCab 1, Cyberdog all used text), avoids "icons that
  don't quite match Susan Kare voice." Loading spinner is QuickDraw,
  six lines at 60° rotated each tick.

## Runtime requirements

`apt install qt6-webengine-dev` (Ubuntu 24.04) or `qt6-qtwebengine-devel`
(Fedora). The package pulls in `QtWebEngineCore` + `QtWebEngineWidgets`
+ `QtWebEngineProcess` (Chromium child binary, ~250 MB on disk).

`--browser` itself is feature-gated: `BUILD_BROWSER=OFF` at CMake time
strips the host pipeline; `--browser` at runtime is the on-switch.
mac-phoenix without `--browser` doesn't pull in any QtWebEngine cost
(libraries aren't linked unless `BUILD_BROWSER=ON`).

Headless setups need either a display (`DISPLAY=:0`) or
`QT_QPA_PLATFORM=offscreen` set externally. `main.cpp` auto-applies
the offscreen QPA platform when neither `DISPLAY` nor `WAYLAND_DISPLAY`
is set, so headless CI just works.

## Known limits

- **Single window.** Multiple windows would need multiple browsing
  contexts and multiple `BrowserShm` regions — out of scope.
- **No tile-diff yet.** The pipeline converts the full image each
  frame instead of doing a hash-keyed tile diff. Matters under
  full-page repaints; fine for most browsing.
- **Audio is silent.** YouTube embeds, podcast players, web-radio,
  and the half of the modern web inside `<audio>` / `<video>` produce
  no sound. The plan is to terminate Chromium audio at a virtual
  PulseAudio null sink and feed the resulting PCM into the existing
  Sound Manager → Opus → WebRTC pipeline; not yet wired up.
- **Polling capture, not event-driven.** 8c-2 (tracked in
  `docs/qt6/PLAN.md`) replaces the QTimer poll with a
  `QQuickWebEngineView` + `QQuickWindow::afterRendering()` event-
  driven path. Current poll path is fine on modern host hardware
  but burns 6% of one CPU core during browsing even when nothing's
  changing.
- **DRM video.** Widevine L1 (Netflix HBO etc.) is screen-grab-
  protected by Chromium's compositor and can't be captured. Out of
  scope per `docs/qt6/PLAN.md` non-goals.
