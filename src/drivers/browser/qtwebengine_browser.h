/*
 *  qtwebengine_browser.h — host-side browser via in-process QWebEngineView.
 *
 *  Replaces the supervisor.cpp / xshm.cpp / bidi.cpp pipeline (Firefox on
 *  Xvfb, captured via XShm+XDamage, controlled via WebDriver-BiDi) with
 *  an in-process Chromium running under Qt6 WebEngine.
 *
 *  Threading: QApplication runs on a dedicated worker thread spawned
 *  here (qtwebengine_module_start). CPU stays on the IPC subprocess's
 *  main thread — QtWebEngine + Chromium tolerate a non-main GUI thread
 *  as long as it's a single dedicated thread for *all* QtWebEngine
 *  state. Cross-thread calls (input dispatch, navigation, etc.) marshal
 *  via QMetaObject::invokeMethod(Qt::QueuedConnection) onto the GUI
 *  thread before touching view_/page_.
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

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

class QWebEngineView;
class QWebEngineDownloadRequest;
class QTimer;

namespace browser {

class CdpClient;

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

    void dispatch_nav(const std::string& url);
    void dispatch_reload();
    void dispatch_back();
    void dispatch_forward();
    void dispatch_stop();

    void dispatch_get_selection();
    void dispatch_select_all();
    void dispatch_paste(const std::string& text);

    void dispatch_zoom_in();
    void dispatch_zoom_out();
    void dispatch_zoom_reset();
    void dispatch_resize(uint16_t w, uint16_t h);

private:
    void capture_tick();
    void metrics_tick();
    void handle_download_request(::QWebEngineDownloadRequest* req);
    void g2h_drain_loop();

    // Re-emit BR_EV_HISTORY only when canGoBack/canGoForward change.
    // Call from any urlChanged/loadFinished/loadStarted hook.
    void publish_history();
    // Re-emit BR_EV_ZOOM only when the percent changes. Call after
    // every setZoomFactor() so the guest URL bar / menu state stays
    // synced with whatever zoom step we ended up on.
    void publish_zoom();

    std::unique_ptr<QWebEngineView> view_;
    std::unique_ptr<QTimer>         capture_timer_;
    std::unique_ptr<QTimer>         metrics_timer_;
    std::unique_ptr<CdpClient>      cdp_;
    std::thread                     g2h_thread_;
    std::atomic<bool>               g2h_running_{false};

    // Last-published page metrics — only re-emit on change.
    uint32_t last_pw_ = 0, last_ph_ = 0;
    uint32_t last_sx_ = 0, last_sy_ = 0;
    uint32_t last_vw_ = 0, last_vh_ = 0;

    // Last-published title / history / zoom — only re-emit on change.
    // Empty string = "no title yet"; a guest that just attached gets
    // an immediate emit on the first titleChanged callback.
    std::string last_title_;
    int8_t      last_can_back_     = -1;   // -1 = unsent
    int8_t      last_can_forward_  = -1;
    int16_t     last_zoom_pct_     = -1;

    // Zoom step index into the standard Firefox zoom-level table.
    // Matches the [0.5, 0.67, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0]
    // step list; index 4 = 100%.
    int zoom_step_ = 4;

    // Adaptive capture poll: idle_ticks_ counts consecutive captures
    // that found nothing dirty. Past kIdleThreshold the timer slows
    // from 60Hz to 4Hz; snaps back the moment something paints again.
    int idle_ticks_ = 0;
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

// ── Navigation (Phase 8e) ────────────────────────────────────────────
// Same gating + threading semantics as the input dispatchers.
bool qt_dispatch_nav(const std::string& url);
bool qt_dispatch_reload();
bool qt_dispatch_back();
bool qt_dispatch_forward();
bool qt_dispatch_stop();

// ── Selection / paste (Phase 8f) ─────────────────────────────────────
// dispatch_get_selection emits BR_EV_SELECTION asynchronously when the
// runJavaScript callback fires. Page metrics (BR_EV_PAGE_METRICS) are
// pushed by an internal QTimer at 4Hz; no command needed to trigger.
bool qt_dispatch_get_selection();
bool qt_dispatch_select_all();
bool qt_dispatch_paste(const std::string& text);

// ── Zoom + resize (Phase 8g) ─────────────────────────────────────────
bool qt_dispatch_zoom_in();
bool qt_dispatch_zoom_out();
bool qt_dispatch_zoom_reset();
bool qt_dispatch_resize(uint16_t w, uint16_t h);

}  // namespace browser
