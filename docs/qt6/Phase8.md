# Phase 8 handoff — MacBrowser host pipeline → QtWebEngine

> **STATUS: COMPLETE** (2026-05-04). 8a..8j shipped on `qt-port`.
> See [`docs/MacBrowser.md`](../MacBrowser.md) for the current
> architecture and [`PLAN.md`](PLAN.md) for the Phase 8 commit list.
> One follow-up tracked: **8c-2** (event-driven capture via
> `QQuickWebEngineView` + `afterRendering()`).
>
> The rest of this doc is the original handoff plan, kept for
> historical context. Read `docs/MacBrowser.md` for what landed.

## What Phase 8 buys

- **Drops 5 apt deps**: `libxcb1-dev`, `libxcb-shm0-dev`,
  `libxcb-damage0-dev`, `libxcb-composite0-dev`, `libxcb-randr0-dev`.
- **Drops 1 subproject**: `nlohmann_json` (only `bidi.cpp` still uses
  it; everything else migrated in Phase 17).
- **Drops 2 runtime deps**: Xvfb, Firefox (Mozilla tarball).
- **Drops the `BUILD_BROWSER` cmake flag** as a Linux-only opt-in —
  becomes a default cross-platform feature.
- **Replaces ~3950 LOC of host-side browser code** (`src/drivers/browser/`)
  with a substantially smaller QtWebEngine-based implementation
  (estimate: 800–1200 LOC).

## Adds: 1 new apt dep

| Linux | Windows | macOS |
|---|---|---|
| `qt6-webengine-dev` (Ubuntu noble: `6.4.2-final+dfsg-12ubuntu9`) | Qt installer "Qt WebEngine" component | Qt installer "Qt WebEngine" component |

The package pulls in QtWebEngineCore + QtWebEngineWidgets + Chromium
runtime libraries (~250 MB on disk). Install before starting:

```bash
sudo apt install qt6-webengine-dev
```

## Architecture: before vs. after

**Before** (Phase 7 state): `src/drivers/browser/` orchestrates an
out-of-process Firefox on Xvfb, captures pixels via xcb-shm + xdamage,
drives input via WebDriver-BiDi over a WebSocket. Files:

```
bidi.{cpp,h}             589  WebDriver BiDi WebSocket client
browser_spike.{cpp,h}    157  early prototype (likely deletable)
cmd.{cpp,h}              479  BR_CMD_* dispatch → BiDi calls
module.{cpp,h}           291  lifecycle when --browser is set
mouse_poll.{cpp,h}       278  guest LMGetMouse / WindowList peek + BiDi forward
pipeline.{cpp,h}         232  damage → BGRX→RGB555 → dirty rect
ring.{cpp,h}             166  SPSC ring helpers (KEEP — no Qt equivalent)
shm.{cpp,h}              263  BrowserShm region management (KEEP — protocol layer)
supervisor.{cpp,h}       517  Xvfb + Firefox process spawn/supervise
window_resize.{cpp,h}    109  guest-side window-bounds tracking
xshm.{cpp,h}             272  XShm + XDamage on Xvfb root
```

**After**: in-process `QWebEnginePage` rendering offscreen via
`QWebEnginePage::view()->grab()` or paint into a `QImage`. Input goes
through synthesized `QMouseEvent`/`QKeyEvent`/`QWheelEvent`. Page
state via `runJavaScript()` callbacks.

```
qtwebengine_browser.{cpp,h}  ~500  in-process QWebEnginePage adapter
ring.{cpp,h}                  166  unchanged
shm.{cpp,h}                   263  unchanged
pipeline.{cpp,h}              ~120  reduced — no BGRX→RGB555 swizzle
                                    if QImage::Format_RGB555 used
mouse_poll.{cpp,h}            ~150  reduced — runJavaScript instead of BiDi
module.{cpp,h}                ~200  adapts to QWebEnginePage lifecycle
cmd.{cpp,h}                   ~250  reduced — direct method calls
window_resize.{cpp,h}         109  unchanged
```

Removed entirely:
- `bidi.{cpp,h}` — no more WebSocket BiDi client
- `supervisor.{cpp,h}` — no out-of-process Xvfb/Firefox to supervise
- `xshm.{cpp,h}` — QtWebEngine paints directly to a QImage
- `browser_spike.{cpp,h}` — likely (verify it's not still referenced)

## The wire protocol stays untouched

`src/common/include/MacBrowser.h` (BrowserShm struct, BR_CMD_* enum,
BR_EV_* enum, ring discipline) is **unchanged**. The guest-side
`MacBrowser/MacBrowser.bin` is **unchanged**. Phase 8 is purely a
host-side rewrite.

## Command/event mapping (BiDi → QtWebEngine)

| Old (BiDi via bidi.cpp) | New (QtWebEngine) |
|---|---|
| `bidi.navigate(url)` | `page->load(QUrl(url))` |
| `bidi.click(x, y, button, count)` | Synthesize `QMouseEvent::Type::MouseButtonPress/Release` posted to view |
| `bidi.mouse_move(x, y)` | Synthesize `QMouseEvent::MouseMove` |
| `bidi.scroll(x, y, dx, dy)` | Synthesize `QWheelEvent` |
| `bidi.send_key_with_mods(key, mods)` | Synthesize `QKeyEvent` |
| `bidi.type(text)` | per-char `QKeyEvent::KeyPress` + KeyRelease |
| `bidi.go_back / go_forward / reload` | `page->triggerAction(QWebEnginePage::Back / Forward / Reload)` |
| `bidi.set_viewport(w, h)` | `page->view()->resize(QSize(w, h))` |
| `bidi.evaluate(js)` | `page->runJavaScript(js, callback)` |
| `bidi.subscribe(events)` | Connect to `loadStarted`/`loadFinished`/`urlChanged` signals |
| BR_CMD_GET_SELECTION | `page->runJavaScript("window.getSelection().toString()", cb)` |
| BR_CMD_PASTE | `page->triggerAction(QWebEnginePage::Paste)` |
| BR_CMD_ZOOM_IN/OUT/RESET | `page->setZoomFactor(f)` |
| BR_CMD_RESIZE | `page->view()->resize(...)` |

| Old (h2g events) | New (QtWebEngine) |
|---|---|
| BR_EV_STATUS | `loadStarted` → STATUS_LOADING; `loadFinished(ok)` → STATUS_READY/ERROR; `urlChanged(url)` updates URL |
| BR_EV_FRAME | Connect to `paintRequested` (or use `QTimer` poll of `view->grab()`) → BGRX/RGB555 conversion in pipeline.cpp → publish to BrowserShm |
| BR_EV_SELECTION | runJavaScript callback writes to ring |
| BR_EV_PAGE_METRICS | `runJavaScript("[innerWidth, innerHeight, scrollX, scrollY, document.body.scrollWidth, document.body.scrollHeight]", cb)` — same JS as today's mouse_poll.cpp |
| BR_EV_DOWNLOAD | Connect to `QWebEngineProfile::downloadRequested` |

## Threading

QWebEngineProfile + QWebEnginePage **must run on the GUI thread** (the
QApplication thread). They're not safe to use from arbitrary threads.

This is a constraint:
- The current model has multiple worker threads for different concerns
  (BiDi I/O thread, xshm capture thread, etc.).
- With QtWebEngine, all browser-side work happens on the GUI thread.
- We need a `QApplication` (not `QCoreApplication`) when `--browser`
  is enabled, since QtWebEngine requires the widget infrastructure.

This conflicts with the current `QCoreApplication`-only headless build
model from Phase 0. Two options:

1. **Always use `QApplication`** when built with `BUILD_BROWSER` (or
   make it the default). Cost: pulls in `Qt6::Widgets` + display
   detection + a hidden window for the QWebEngineView even in headless
   mode.
2. **Use `QApplication` only in `--browser` mode**, with a runtime
   switch. Awkward — `QCoreApplication` and `QApplication` are
   sibling classes; you pick one at startup, can't swap.

Recommended: pick (1) and live with the larger dep footprint when
`BUILD_BROWSER=ON`. Provide `BUILD_BROWSER=OFF` (and corresponding
`QCoreApplication`-only headless build) as the lean variant. This
matches the spirit of Phase 9's `BUILD_NATIVE_HEAD=ON` flag — both
turn on `QApplication` and `Qt6::Widgets`.

## Sequence of commits to land Phase 8

1. **Phase 8a**: install `qt6-webengine-dev`; add Qt6 WebEngine to the
   top-level `find_package` + link `Qt6::Widgets` / `Qt6::WebEngineCore`
   / `Qt6::WebEngineWidgets` into the `drivers` library under
   `if(BUILD_BROWSER)`; update `debian/control`, `rpm/mac-phoenix.spec`,
   `packaging/Dockerfile.{dev,rpm}`, `CLAUDE.md` (Phase 1.5 protocol).
   Side-fixes (latent UB exposed by Chromium libs shifting address-space
   layout — pre-Qt the wild deref hit zero memory; with Qt loaded it
   SIGSEGVs at `init_mac_subsystems:303`):
     - `main.cpp:435` (IPC video init): pass `VDEPTH_1BIT` as the
       `default_depth` to `ipc_monitor_desc` when `mono_framebuffer`
       is true. The mono SE branch publishes a single 1-bit mode but
       was passing `VDEPTH_32BIT` as the default — `find_mode()` then
       returns null and `current_mode` stays uninitialized.
     - `kModes`: add `APPLE_512x342 = 0x9F` + a 512x342 m68k entry so
       any non-IPC video init paths that filter the table at
       `--screen 512x342` don't yield an empty modes vector.
   No browser code yet — verify CMake configure clean + ctest 19/20.
2. **Phase 8b**: write `qtwebengine_browser.{cpp,h}` skeleton —
   `QWebEnginePage` instantiation, signal connections, basic
   navigation. Validate by booting `--browser`, opening a URL,
   getting a `loadFinished` signal.
3. **Phase 8c**: pipeline migration — pixel capture from
   `paintRequested` (or QTimer poll of `view->grab()`), BGRX→RGB555
   in pipeline.cpp, publish to BrowserShm.
4. **Phase 8d**: input synthesis — BR_CMD_CLICK / MOUSE_MOVE / KEY_*
   / SCROLL via QMouseEvent / QKeyEvent / QWheelEvent.
5. **Phase 8e**: navigation + status — BR_CMD_NAV/BACK/FORWARD/RELOAD/
   STOP via QWebEnginePage methods; BR_EV_STATUS via signals.
6. **Phase 8f**: page metrics + selection + paste — runJavaScript
   callbacks.
7. **Phase 8g**: zoom + resize.
8. **Phase 8h**: downloads — `QWebEngineProfile::downloadRequested`
   wired into BrowserShm download events.
9. **Phase 8i**: delete old code — `bidi.{cpp,h}`, `supervisor.{cpp,h}`,
   `xshm.{cpp,h}`, `browser_spike.{cpp,h}`. Drop the
   `nlohmann_json` subproject + drivers' link to it. Drop `libxcb-*`
   apt deps from packaging files. Drop xvfb / firefox runtime-dep
   notes from CLAUDE.md.
10. **Phase 8j**: doc rewrite — `docs/MacBrowser.md` reflects new
    architecture; `docs/qt6/PLAN.md` Phase 8 status → done.

Each commit should keep `--browser` functional (after 8b) so we can
manually smoke-test mid-stream.

## Validation — there are no automated tests for `--browser`

`tests/test_browser_shm.cpp` is a unit test for the SPSC ring + shm
layer; it doesn't exercise BiDi or pixel capture. End-to-end
validation is manual:

```bash
./build/mac-phoenix --browser \
    --disk MacBrowser/MacBrowser.dsk \
    --bridge \
    /home/mick/roms/quadra.rom
```

…then in the guest, double-click `MacBrowser` on the floppy, type a
URL, watch it render. Test scroll, back/forward, paste, select-all,
zoom, resize, downloads.

Worth adding a Playwright-style scripted smoke test in this phase
(host-side: spawn mac-phoenix, send /api/launch for MacBrowser, check
expected guest state via /api/wait + /api/screenshot). Defer to a
follow-up if it'd block this phase.

## Risks / unknowns

- **Headless capture quality**: `QWebEnginePage::view()->grab()` works
  offscreen but QtWebEngine internally uses Chromium's compositor;
  some layers (video, WebGL) may require a real GPU. The current
  Xvfb-based capture has the same limitation.
- **Pixel format**: QtWebEngine paints in ARGB32 by default. The
  pipeline already does BGRX→RGB555 conversion; need to verify ARGB32
  is a clean swap-source (likely yes).
- **First-paint latency**: QtWebEngine startup is slower than spawning
  Firefox-on-Xvfb (Chromium init). Probably +1-2s on first launch.
  Subsequent navigations should be comparable.
- **Memory footprint**: QtWebEngine+Chromium is heavy (~300 MB RSS for
  one tab). Same order as Firefox.
- **DRM**: Widevine L1 is out (per PLAN non-goals). Widevine L3 may
  work via ChromeMediaSource — investigate if any user actually
  needs Netflix in classic Mac, otherwise skip.
- **macOS / Windows codesigning**: QtWebEngine ships with
  `QtWebEngineProcess` (a child binary). Each platform has its own
  signing/sandboxing requirements. Phase 11 (.app bundle) and
  Phase 10 (Windows installer) must include it. `macdeployqt` and
  `windeployqt` handle this automatically.

## Files to read before starting

| Read | Why |
|---|---|
| `docs/MacBrowser.md` (full) | Architecture overview, wire protocol, mouse model, VBL sync |
| `src/common/include/MacBrowser.h` | BrowserShm struct + ring layout + BR_CMD_*/BR_EV_* enums |
| `src/drivers/browser/cmd.cpp` | Inventory of BR_CMD_* dispatch — exact behavior to replicate |
| `src/drivers/browser/mouse_poll.cpp` | Guest-side mouse polling logic — keep semantics; swap BiDi for runJavaScript |
| `src/drivers/browser/module.cpp` | Lifecycle hookup — what changes when supervisor goes away |
| Qt 6.4 docs: `QWebEnginePage`, `QWebEngineProfile`, `QWebEngineView` | API surface |

## Out of scope for Phase 8

- **Implementing the new MacBrowser features** (e.g. tabs, bookmarks).
  Scope is "drop xcb/Xvfb/Firefox, keep behavior identical."
- **Changing the guest-side `MacBrowser.bin` app**. The wire protocol
  is the boundary; only the host implementation changes.
- **Replacing libdatachannel for WebRTC**. Phase 8 is about MacBrowser
  pipeline only.
