/*
 *  cdp.h — minimal Chrome DevTools Protocol client.
 *
 *  Connects to ws://127.0.0.1:<port>/devtools/browser/<id> via
 *  rtc::WebSocket. After open, calls `Target.getTargets`, picks the
 *  first page-type target, and does `Target.attachToTarget` with
 *  `flatten:true` so all per-page messages multiplex over the
 *  browser-level socket. From then on, the caller drives Chromium
 *  through a single call() method that JSON-encodes a CDP request
 *  and waits for the matching response by id.
 *
 *  CDP framing:
 *    request : { "id": N, "method": "Domain.method", "params": {...},
 *                "sessionId": "<page>" }
 *    response: { "id": N, "result": {...} }       — success
 *              { "id": N, "error":  {"code":, "message":} } — error
 *    event   : { "method": "Domain.event", "params": {...},
 *                "sessionId": "<page>" }
 *
 *  All threading is internal. call() blocks the caller's thread until
 *  the response arrives or a timeout fires. on_event() registers a
 *  callback for a specific method, invoked from the libdatachannel
 *  WS receiver thread.
 *
 *  No TLS. CDP is plain ws://, since Chromium binds 127.0.0.1.
 */
#ifndef DRIVERS_BROWSER_CDP_H
#define DRIVERS_BROWSER_CDP_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace rtc { class WebSocket; }

namespace browser {

class CdpClient {
public:
    using Json = nlohmann::json;
    using EventHandler = std::function<void(const Json& params)>;

    CdpClient();
    ~CdpClient();

    /* Open the WS connection, fetch + attach to the first page target.
     * Returns true on success. After this, call()/on_event() are
     * usable. Idempotent — second call after success is a no-op; after
     * close() must call again. */
    bool open(const std::string& browser_ws_url,
              std::chrono::milliseconds timeout =
                  std::chrono::milliseconds(8000));

    /* Tear down the WS connection. Always called by destructor. */
    void close();

    /* Issue a CDP request and wait for the response. Returns the
     * "result" object on success or an empty Json (with `ok` set
     * false) on error/timeout. */
    struct Response {
        bool ok = false;
        Json result;
        Json error;
    };
    Response call(const std::string& method,
                  const Json& params = Json::object(),
                  std::chrono::milliseconds timeout =
                      std::chrono::milliseconds(5000));

    /* Register a CDP event handler. Method is e.g. "Page.frameNavigated"
     * or "Page.loadEventFired". Replaces any previous handler for that
     * method. Pass nullptr to clear. Called from the WS receiver
     * thread — handler must be quick or hand off to another thread. */
    void on_event(const std::string& method, EventHandler handler);

    bool is_open() const { return open_.load(std::memory_order_acquire); }
    const std::string& session_id() const { return session_id_; }

private:
    void handle_message(const std::string& text);

    std::unique_ptr<rtc::WebSocket> ws_;
    std::atomic<bool> open_{false};

    std::mutex mtx_;
    std::condition_variable cv_;
    int next_id_ = 1;

    /* Pending in-flight calls keyed by id. */
    struct Pending {
        bool done = false;
        Response resp;
    };
    std::map<int, std::shared_ptr<Pending>> pending_;

    /* Event handlers keyed by CDP method ("Page.frameNavigated" etc). */
    std::map<std::string, EventHandler> handlers_;

    /* Set by the open() flow once Target.attachToTarget succeeds. */
    std::string session_id_;
};

}  // namespace browser

#endif  // DRIVERS_BROWSER_CDP_H
