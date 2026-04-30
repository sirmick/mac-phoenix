/*
 *  bidi.h — minimal WebDriver BiDi client for Firefox.
 *
 *  Firefox is launched with `--remote-debugging-port=9222` and BiDi
 *  enabled in the profile (`remote.active-protocols=2`). This client:
 *
 *    1. Opens a WebSocket to ws://127.0.0.1:9222/session.
 *    2. Sends `session.new` with capabilities.
 *    3. Reads back the assigned `sessionId` + the top-level browsing
 *       context (created on first navigate).
 *    4. Exposes a synchronous request/response API: send a JSON
 *       command, block until matching id arrives.
 *    5. Async events from Firefox (page-load, network, etc.) hit
 *       `on_event` if set — caller can fan them into BrowserShm h2g.
 *
 *  The transport is rtc::WebSocket (libdatachannel) — already linked.
 *
 *  Threading: one I/O thread inside rtc::WebSocket. start()/stop()
 *  block; navigate()/click()/etc. block waiting for the response,
 *  bounded by a per-call timeout.
 */
#ifndef DRIVERS_BROWSER_BIDI_H
#define DRIVERS_BROWSER_BIDI_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace browser {

class BidiClient {
public:
    BidiClient();
    ~BidiClient();

    /* Connect to ws://host:port/session, run session.new. Returns
     * true if the session is open and ready to take commands.
     * Blocks up to `timeout` for the connection + handshake. */
    bool start(const std::string& host, int port,
               std::chrono::milliseconds timeout =
                   std::chrono::seconds(10));

    /* Close the WebSocket and join the I/O thread. Idempotent. */
    void stop();

    bool is_open() const;

    /* Top-level browsing context id, queried via
     * `browsingContext.getTree` after session.new. Empty until
     * start() succeeds. */
    std::string top_context() const;

    /* Current input target — the most recently created top-level
     * browsing context (popup, target=_blank, window.open), or the
     * original top_context if no popup is live. Pointer/wheel/key
     * methods route here automatically. */
    std::string active_context() const;

    /* Send a method call with arbitrary JSON params, block for
     * response. Returns the response object's `result` field
     * serialized as JSON, or empty string + sets `error` on failure
     * (transport, timeout, BiDi error response). */
    std::string call(const std::string& method,
                     const std::string& params_json,
                     std::string* error,
                     std::chrono::milliseconds timeout =
                         std::chrono::seconds(10));

    /* Convenience wrappers — all return true on success. Errors are
     * the BiDi error envelope (JSON) on protocol failures, or short
     * strings ("timeout", "not connected") on transport problems.
     * Call call() directly if you need the raw response body. */

    bool navigate(const std::string& url, std::string* error = nullptr);
    bool reload  (std::string* error = nullptr);
    bool go_back (std::string* error = nullptr);  /* delta=-1 */
    bool go_forward(std::string* error = nullptr); /* delta=+1 */

    /* Pointer at viewport coords (x,y), single click of `button`
     * (0=left, 1=middle, 2=right) with `count` clicks. */
    bool click(int x, int y, int button = 0, int count = 1,
               std::string* error = nullptr);

    /* Hover/move-only (no buttons). Useful for hover-driven UI. */
    bool mouse_move(int x, int y, std::string* error = nullptr);

    /* Wheel scroll at coords. Positive dy = scroll down. */
    bool scroll(int x, int y, int dx, int dy,
                std::string* error = nullptr);

    /* Type a string of text into the focused element. Each character
     * fires a keyDown + keyUp pair. UTF-8 in, BiDi handles unicode. */
    bool type(const std::string& utf8, std::string* error = nullptr);

    /* Press one key with one or more modifiers held (Shift, Ctrl,
     * Alt, Meta). Builds an input.performActions sequence that
     * keyDown's each modifier, keyDown+keyUp's the key, then keyUp's
     * the modifiers in reverse. Used for Mac→W3C key combos like
     * Shift+ArrowRight or Ctrl+A. `key` is one Unicode codepoint
     * (W3C private-use \uE0xx for special keys, or a printable). */
    enum KeyMod {
        kModShift = 1u << 0,
        kModCtrl  = 1u << 1,
        kModAlt   = 1u << 2,
        kModMeta  = 1u << 3,
    };
    bool send_key_with_mods(const std::string& key, unsigned mods,
                            std::string* error = nullptr);

    /* Resize the browser viewport. The pixel pipeline picks up the
     * new dimensions on the next paint. */
    bool set_viewport(int width, int height,
                      std::string* error = nullptr);

    /* Evaluate JS in the top-level frame. Returns the serialized
     * result (BiDi remote-value JSON) on success. */
    std::string evaluate(const std::string& expr,
                         std::string* error = nullptr);

    /* session.subscribe to one or more BiDi event names — required
     * before on_event will receive them. Common: "browsingContext.load",
     * "browsingContext.navigationCommitted", "log.entryAdded". */
    bool subscribe(const std::vector<std::string>& events,
                   std::string* error = nullptr);

    /* Register a script that runs in every page before its own
     * scripts (BiDi script.addPreloadScript). Useful for injecting
     * style overrides like scrollbar-hide CSS once per session. */
    bool add_preload_script(const std::string& js,
                            std::string* error = nullptr);

    /* Async event hook: receives the raw JSON for any non-response
     * frame (BiDi events have type=="event" and a method field). */
    void on_event(std::function<void(const std::string& json)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace browser

#endif  // DRIVERS_BROWSER_BIDI_H
