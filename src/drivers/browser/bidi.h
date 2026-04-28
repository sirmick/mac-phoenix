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

    /* Send a method call with arbitrary JSON params, block for
     * response. Returns the response object's `result` field
     * serialized as JSON, or empty string + sets `error` on failure
     * (transport, timeout, BiDi error response). */
    std::string call(const std::string& method,
                     const std::string& params_json,
                     std::string* error,
                     std::chrono::milliseconds timeout =
                         std::chrono::seconds(10));

    /* Convenience wrapper for browsingContext.navigate. wait="complete"
     * blocks until DOMContentLoaded — feels like a page load to the
     * caller. */
    bool navigate(const std::string& url, std::string* error = nullptr);

    /* Async event hook: receives the raw JSON for any non-response
     * frame (BiDi events have type=="event" and a method field). */
    void on_event(std::function<void(const std::string& json)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace browser

#endif  // DRIVERS_BROWSER_BIDI_H
