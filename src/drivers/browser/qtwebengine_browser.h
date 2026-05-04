/*
 *  qtwebengine_browser.h — host-side browser via in-process QWebEngineView.
 *
 *  Replaces the supervisor.cpp / xshm.cpp / bidi.cpp pipeline (Firefox on
 *  Xvfb, captured via XShm+XDamage, controlled via WebDriver-BiDi) with
 *  an in-process Chromium running under Qt6 WebEngine.
 *
 *  Threading: QApplication owns the IPC subprocess's main thread (set up
 *  in main.cpp when --browser is detected); CPU runs on a worker thread.
 *  QtWebEngine objects live on the main thread; cross-thread calls marshal
 *  via QMetaObject::invokeMethod(Qt::QueuedConnection).
 *
 *  Smoothness: software rasterization is forced via QTWEBENGINE_CHROMIUM_FLAGS
 *  before QApplication construction so <video>/WebGL frames land in the
 *  widget's backing store (Chromium hardware overlays would otherwise bypass
 *  any read-back path).
 *
 *  Phase 8c: pixel capture wired. A 60Hz QTimer polls QWidget::grab() on
 *  the hidden view, converts ARGB32 → RGB555, and pushes to BrowserShm.fb
 *  (whole frame as one dirty rect for now; per-region diff is a follow-up).
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

class QWebEngineView;
class QTimer;

namespace browser {

class QtWebEngineBrowser {
public:
    QtWebEngineBrowser();
    ~QtWebEngineBrowser();

    QtWebEngineBrowser(const QtWebEngineBrowser&) = delete;
    QtWebEngineBrowser& operator=(const QtWebEngineBrowser&) = delete;

    void load(const std::string& url);

    // Input dispatch — must be called on the GUI thread. The free
    // qt_dispatch_* functions below marshal across thread boundaries.
    void dispatch_click(int x, int y, int button, int count);
    void dispatch_mouse_move(int x, int y);
    void dispatch_mouse_out();
    void dispatch_key_down(uint16_t vk, uint16_t mods,
                           const std::string& text);
    void dispatch_key_up(uint16_t vk);
    void dispatch_scroll(int dx, int dy);

private:
    void capture_tick();

    std::unique_ptr<QWebEngineView> view_;
    std::unique_ptr<QTimer>         capture_timer_;
};

// Create QtWebEngineBrowser on the main (GUI) thread, load `initial_url`.
// Safe to call from any thread; if not on the GUI thread the call is
// posted to it via QMetaObject::invokeMethod.
//
// Requires a QApplication on the main thread (set up in main.cpp when
// --browser was passed). No-op + warns if absent.
void qtwebengine_module_start(const std::string& initial_url = {});

// Tear down QtWebEngineBrowser on the main thread. Safe to call from any
// thread. Idempotent.
void qtwebengine_module_stop();

// ── Input dispatch (Phase 8d) ────────────────────────────────────────
// Each function returns true if QtWebEngine is active and the event was
// posted to the GUI thread; false otherwise so the caller can fall
// through to the legacy BiDi path. Coordinates are page pixels (same as
// the BiDi path).
//
// All functions are thread-safe. Internally they marshal to the GUI
// thread via QMetaObject::invokeMethod(Qt::QueuedConnection); the
// underlying QMouseEvent/QKeyEvent/QWheelEvent is posted to the hidden
// QWebEngineView. The actual delivery and Chromium handling happens
// asynchronously when the GUI event loop next runs.
bool qt_dispatch_click(int x, int y, int button, int count);
bool qt_dispatch_mouse_move(int x, int y);
bool qt_dispatch_mouse_out();
bool qt_dispatch_key_down(uint16_t vk, uint16_t mods,
                          const uint8_t* text, uint8_t text_len);
bool qt_dispatch_key_up(uint16_t vk);
bool qt_dispatch_scroll(int dx, int dy);

}  // namespace browser
