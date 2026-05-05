/*
 *  cdp_client.h — Chrome DevTools Protocol client for in-process QtWebEngine.
 *
 *  Why: QApplication::postEvent + focusProxy + setFocus is a fragile
 *  way to drive a hidden offscreen QWebEngineView. Synthetic QKeyEvents
 *  posted to the focus proxy work *most* of the time but the focus
 *  chain is shaky (no real window manager, no real activation), and
 *  the synthetic events are still "untrusted" enough that some pages
 *  don't react. CDP sidesteps all of this — it's the same protocol
 *  Playwright/Puppeteer use, talks directly to the Chromium renderer,
 *  and produces trusted input events identical to a physical keystroke.
 *
 *  Wire: ws://127.0.0.1:<port>/devtools/page/<id>, JSON-RPC.
 *  Enable with QTWEBENGINE_REMOTE_DEBUGGING=<port> before QApplication
 *  ctor. Discovery: GET http://127.0.0.1:<port>/json/list, find the
 *  type=page target, take its webSocketDebuggerUrl.
 *
 *  Threading: connect() spawns a worker that does HTTP discovery +
 *  rtc::WebSocket open synchronously. Dispatch methods are thread-safe;
 *  they drop if the WS isn't open yet. There's no queue — early input
 *  during the ~200ms boot window after page load may be lost. Fine for
 *  v1; the user has to click into a control before typing anyway.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace rtc { class WebSocket; }

class QJsonObject;

namespace browser {

class CdpClient {
public:
    // debug_port: the port we set QTWEBENGINE_REMOTE_DEBUGGING to.
    explicit CdpClient(uint16_t debug_port);
    ~CdpClient();

    CdpClient(const CdpClient&) = delete;
    CdpClient& operator=(const CdpClient&) = delete;

    // Kick off async discovery + connect. Safe to call once. The
    // connection is fully async; is_connected() flips when ready.
    void start();

    // True once the per-page WS is open and CDP commands will land.
    bool is_connected() const { return open_.load(std::memory_order_acquire); }

    // ── Keyboard ────────────────────────────────────────────────
    //
    // type      = "keyDown" | "keyUp" | "char"
    // windows_vk = Windows virtual key code (e.g. 13 for Enter, 65
    //              for 'A'). Pass 0 for pure-text "char" events.
    // key       = W3C UI Events `key` value ("a", "Enter", "ArrowLeft", "")
    // code      = W3C UI Events `code` value ("KeyA", "Enter", "ArrowLeft", "")
    // text      = Unicode the key produces (empty for special keys)
    // modifiers = CDP bitfield: 1=Alt, 2=Ctrl, 4=Meta, 8=Shift
    void key_event(const std::string& type,
                   int windows_vk,
                   const std::string& key,
                   const std::string& code,
                   const std::string& text,
                   int modifiers);

    // Type a single Unicode character (or grapheme cluster). This is
    // the right entrypoint for printable input — it bypasses the
    // key/code mapping mess and lets Chromium do its IME magic.
    void type_text(const std::string& text);

    // ── Mouse ───────────────────────────────────────────────────
    //
    // type   = "mousePressed" | "mouseReleased" | "mouseMoved"
    // button = "none" | "left" | "middle" | "right"
    void mouse_event(const std::string& type,
                     int x, int y,
                     const std::string& button,
                     int click_count,
                     int modifiers);

    // mouseWheel — same dispatchMouseEvent endpoint. Note: Chromium
    // routes the wheel event to the deepest *hovered* element, so this
    // is right for "user spun the mouse wheel at (x,y)" but wrong for
    // "scroll the page by N pixels regardless of cursor". Use
    // scroll_by() for the latter.
    void mouse_wheel(int x, int y, int delta_x, int delta_y);

    // window.scrollBy(dx, dy) via Runtime.evaluate. Drives the document
    // scrolling element directly, ignoring cursor position. Right for
    // BR_CMD_SCROLL coming from the Mac chrome scrollbars.
    void scroll_by(int dx, int dy);

    // Emulation.setScrollbarsHidden — hides Chromium's native scrollbars
    // for the page. We render our own (Mac chrome scrollbars) on the
    // guest side, so the embedded webview shouldn't paint a second pair.
    void set_scrollbars_hidden(bool hidden);

    // ── Navigation ──────────────────────────────────────────────
    void navigate(const std::string& url);
    void reload();
    void stop();

    // History navigation. CDP doesn't have direct back/forward;
    // it requires Page.getNavigationHistory then
    // Page.navigateToHistoryEntry. We pipeline both as one logical
    // op — the response handler walks the history and fires the nav.
    void back();
    void forward();

    // ── Selection / clipboard ───────────────────────────────────
    // Read window.getSelection().toString(). The result arrives via
    // the on_selection callback registered through set_selection_cb.
    void get_selection();
    void set_selection_cb(std::function<void(std::string)> cb);

private:
    void worker_main();
    void on_message_locked(const QJsonObject& msg);

    // Send fire-and-forget (no reply tracking).
    void send(const std::string& method, const QJsonObject& params);
    // Send and invoke `cb` with the result QJsonObject when the
    // response arrives. cb runs on the WebSocket I/O thread.
    void send_with_reply(const std::string& method,
                         const QJsonObject& params,
                         std::function<void(const QJsonObject&)> cb);

    uint16_t debug_port_;
    std::shared_ptr<rtc::WebSocket> ws_;
    std::atomic<bool> open_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<int>  next_id_{1};
    std::thread       worker_;

    // Pending replies keyed by message id. Inserted on send, called +
    // erased on response. Mutex protects the map only; callbacks fire
    // outside the lock to avoid re-entry deadlocks.
    std::mutex pending_mutex_;
    std::unordered_map<int, std::function<void(const QJsonObject&)>> pending_;

    // Selection callback, set once by the owning QtWebEngineBrowser.
    std::function<void(std::string)> selection_cb_;
};

}  // namespace browser
