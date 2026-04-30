/*
 *  bidi.cpp — WebDriver BiDi client. See bidi.h.
 */
#include "bidi.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

namespace browser {

using json = nlohmann::json;

struct BidiClient::Impl {
    std::shared_ptr<rtc::WebSocket> ws;

    std::mutex                       mtx;
    std::condition_variable          cv_open;
    std::condition_variable          cv_resp;
    std::atomic<bool>                opened{false};
    std::atomic<bool>                closed{false};
    std::atomic<int>                 next_id{1};

    std::map<int, std::string>       responses;   /* id → result/error JSON */
    std::map<int, bool>              ok;          /* id → success vs error */

    std::string                      top_context;
    /* Most recently created top-level browsing context (popup, target=_blank,
     * window.open). Pointer/wheel/keyboard actions route here so popups
     * actually receive input. Falls back to top_context when it gets
     * destroyed. Updated from the WS thread, read from caller threads,
     * so always read/write under `mtx`. */
    std::string                      active_context;
    std::function<void(const std::string&)> on_event_cb;
};

BidiClient::BidiClient() : impl_(std::make_unique<Impl>()) {}
BidiClient::~BidiClient() { stop(); }

namespace {

/* Quick safe extraction so we don't need full json paths in the
 * hot path. Returns empty string if missing. */
std::string get_string(const json& j, const char* key)
{
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : "";
}

}  // namespace

bool BidiClient::start(const std::string& host, int port,
                       std::chrono::milliseconds timeout)
{
    if (impl_->ws && impl_->opened.load() && !impl_->closed.load()) {
        return true;  /* already up */
    }
    /* Reset state so retries on a fresh ws don't see stale flags. */
    if (impl_->ws) impl_->ws.reset();
    impl_->opened.store(false);
    impl_->closed.store(false);

    impl_->ws = std::make_shared<rtc::WebSocket>();

    impl_->ws->onOpen([this]() {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->opened.store(true);
        impl_->cv_open.notify_all();
    });
    impl_->ws->onClosed([this]() {
        impl_->closed.store(true);
        impl_->cv_open.notify_all();
        impl_->cv_resp.notify_all();
    });
    impl_->ws->onError([this](std::string err) {
        /* Connect retries are normal during Firefox boot — only surface
         * the error once the channel was actually open. The caller logs
         * a single summary on permanent failure. */
        if (impl_->opened.load()) {
            fprintf(stderr, "[BiDi] ws error: %s\n", err.c_str());
        }
        impl_->closed.store(true);
        impl_->cv_open.notify_all();
        impl_->cv_resp.notify_all();
    });
    impl_->ws->onMessage(
        /* binary */ [](rtc::binary){},
        /* text   */ [this](std::string msg) {
            json j;
            try { j = json::parse(msg); }
            catch (...) {
                fprintf(stderr, "[BiDi] non-JSON frame, %zu bytes\n",
                        msg.size());
                return;
            }
            std::string type = get_string(j, "type");
            if (type == "event") {
                /* Track top-level browsing contexts internally so input
                 * events follow popups and new windows. The user-supplied
                 * on_event_cb still fires unmodified. */
                std::string method = get_string(j, "method");
                if (method == "browsingContext.contextCreated") {
                    auto pit = j.find("params");
                    if (pit != j.end() && pit->is_object()) {
                        std::string ctx = get_string(*pit, "context");
                        auto par = pit->find("parent");
                        bool is_top = (par == pit->end()) || par->is_null() ||
                                      (par->is_string() &&
                                       par->get<std::string>().empty());
                        if (is_top && !ctx.empty()) {
                            std::lock_guard<std::mutex> lk(impl_->mtx);
                            impl_->active_context = ctx;
                        }
                    }
                } else if (method == "browsingContext.contextDestroyed") {
                    auto pit = j.find("params");
                    if (pit != j.end() && pit->is_object()) {
                        std::string ctx = get_string(*pit, "context");
                        std::lock_guard<std::mutex> lk(impl_->mtx);
                        if (!ctx.empty() && ctx == impl_->active_context) {
                            impl_->active_context = impl_->top_context;
                        }
                    }
                }
                if (impl_->on_event_cb) impl_->on_event_cb(msg);
                return;
            }
            /* "success" or "error" — both are responses to a call. */
            auto id_it = j.find("id");
            if (id_it == j.end() || !id_it->is_number_integer()) {
                fprintf(stderr, "[BiDi] response without id: %s\n",
                        msg.substr(0, 120).c_str());
                return;
            }
            int id = id_it->get<int>();
            std::lock_guard<std::mutex> lk(impl_->mtx);
            impl_->ok[id] = (type == "success");
            impl_->responses[id] = (type == "success")
                ? (j.contains("result") ? j["result"].dump() : "{}")
                : msg;  /* keep full error envelope for diagnostics */
            impl_->cv_resp.notify_all();
        });

    std::ostringstream url;
    url << "ws://" << host << ":" << port << "/session";
    impl_->ws->open(url.str());

    {
        std::unique_lock<std::mutex> lk(impl_->mtx);
        if (!impl_->cv_open.wait_for(lk, timeout, [this]() {
                return impl_->opened.load() || impl_->closed.load();
            })) {
            /* Caller (BrowserModule) loops with short timeouts during
             * startup; staying silent here keeps the log clean. */
            return false;
        }
    }
    if (impl_->closed.load()) {
        return false;
    }
    fprintf(stderr, "[BiDi] connected to %s\n", url.str().c_str());

    /* session.new */
    {
        std::string err;
        std::string params = R"({"capabilities":{"alwaysMatch":{"acceptInsecureCerts":true}}})";
        std::string result = call("session.new", params, &err, timeout);
        if (result.empty()) {
            fprintf(stderr, "[BiDi] session.new failed: %s\n", err.c_str());
            return false;
        }
        try {
            auto r = json::parse(result);
            std::string sid = get_string(r, "sessionId");
            fprintf(stderr, "[BiDi] session %s on %s %s\n",
                    sid.c_str(),
                    r.value("/capabilities/browserName"_json_pointer,
                            std::string("?")).c_str(),
                    r.value("/capabilities/browserVersion"_json_pointer,
                            std::string("?")).c_str());
        } catch (const std::exception& e) {
            fprintf(stderr, "[BiDi] session.new result parse: %s\n",
                    e.what());
        }
    }

    /* browsingContext.getTree → top context id (Firefox already opened
     * the kiosk URL when it spawned, so a context exists). */
    {
        std::string err;
        std::string result = call("browsingContext.getTree", "{}", &err,
                                  timeout);
        if (result.empty()) {
            fprintf(stderr, "[BiDi] getTree failed: %s\n", err.c_str());
            return false;
        }
        try {
            auto r = json::parse(result);
            auto& contexts = r["contexts"];
            if (!contexts.empty()) {
                impl_->top_context = contexts[0].value("context", "");
                impl_->active_context = impl_->top_context;
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[BiDi] getTree parse: %s\n", e.what());
        }
        fprintf(stderr, "[BiDi] top context = '%s'\n",
                impl_->top_context.c_str());
    }

    /* Subscribe to context lifecycle so popups + new windows route
     * pointer/wheel/keyboard input correctly. The caller's own
     * subscribe() call is additive — both sets stick. */
    {
        std::string err;
        if (!subscribe({"browsingContext.contextCreated",
                        "browsingContext.contextDestroyed"}, &err)) {
            fprintf(stderr, "[BiDi] context lifecycle subscribe failed: %s\n",
                    err.c_str());
        }
    }
    return true;
}

void BidiClient::stop()
{
    if (!impl_->ws) return;
    impl_->ws->close();
    impl_->ws.reset();
    impl_->opened.store(false);
    impl_->closed.store(true);
}

bool BidiClient::is_open() const { return impl_->opened.load(); }
std::string BidiClient::top_context() const { return impl_->top_context; }

/* Snapshot the current input target. Falls back to the original top
 * context when no popup has been adopted yet. Caller threads only —
 * locks impl_->mtx. */
std::string BidiClient::active_context() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->active_context.empty() ? impl_->top_context
                                         : impl_->active_context;
}

void BidiClient::on_event(std::function<void(const std::string&)> cb)
{
    impl_->on_event_cb = std::move(cb);
}

std::string BidiClient::call(const std::string& method,
                             const std::string& params_json,
                             std::string* error,
                             std::chrono::milliseconds timeout)
{
    if (error) error->clear();
    if (!impl_->ws || !impl_->opened.load() || impl_->closed.load()) {
        if (error) *error = "not connected";
        return {};
    }
    int id = impl_->next_id.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream frame;
    frame << R"({"id":)" << id
          << R"(,"method":")" << method
          << R"(","params":)" << (params_json.empty() ? "{}" : params_json)
          << "}";

    impl_->ws->send(frame.str());

    std::unique_lock<std::mutex> lk(impl_->mtx);
    if (!impl_->cv_resp.wait_for(lk, timeout, [&]() {
            return impl_->responses.count(id) || impl_->closed.load();
        })) {
        if (error) *error = "timeout";
        return {};
    }
    if (impl_->closed.load() && !impl_->responses.count(id)) {
        if (error) *error = "connection closed";
        return {};
    }
    bool ok = impl_->ok[id];
    std::string body = std::move(impl_->responses[id]);
    impl_->responses.erase(id);
    impl_->ok.erase(id);
    if (!ok) {
        if (error) *error = body;  /* full error envelope */
        return {};
    }
    return body;
}

bool BidiClient::navigate(const std::string& url, std::string* error)
{
    if (impl_->top_context.empty()) {
        if (error) *error = "no top-level browsing context";
        return false;
    }
    /* Use json::dump for the URL — it can contain ", \, and other
     * characters that need JSON escaping. Hand-rolling the params
     * string broke on quoted attributes inside data: URLs. */
    json params = {
        {"context", impl_->top_context},
        {"url", url},
        {"wait", "complete"},
    };
    std::string err_local;
    std::string result = call("browsingContext.navigate", params.dump(),
                              error ? error : &err_local,
                              std::chrono::seconds(20));
    return !result.empty();
}

bool BidiClient::reload(std::string* error)
{
    if (impl_->top_context.empty()) {
        if (error) *error = "no top-level browsing context";
        return false;
    }
    json p = {{"context", impl_->top_context}, {"wait", "complete"}};
    std::string e;
    return !call("browsingContext.reload", p.dump(),
                 error ? error : &e, std::chrono::seconds(20)).empty();
}

namespace {

bool traverse(const std::string& ctx, int delta, BidiClient* c,
              std::string* error)
{
    json p = {{"context", ctx}, {"delta", delta}};
    std::string e;
    /* call() lives on the client, so route through a public path. */
    return !c->call("browsingContext.traverseHistory", p.dump(),
                    error ? error : &e).empty();
}

}  // namespace

bool BidiClient::go_back(std::string* error)
{
    return traverse(impl_->top_context, -1, this, error);
}
bool BidiClient::go_forward(std::string* error)
{
    return traverse(impl_->top_context, +1, this, error);
}

namespace {

/* Build a single-action input.performActions request with a
 * pointer-source. Each call/method below routes through here. */
json pointer_action(const std::string& ctx,
                    const json& actions)
{
    json src = {
        {"id", "mouse"},
        {"type", "pointer"},
        {"parameters", {{"pointerType", "mouse"}}},
        {"actions", actions},
    };
    return {
        {"context", ctx},
        {"actions", json::array({src})},
    };
}

}  // namespace

bool BidiClient::click(int x, int y, int button, int count,
                       std::string* error)
{
    std::string ctx = active_context();
    if (ctx.empty()) {
        if (error) *error = "no top-level browsing context";
        return false;
    }
    /* Move to (x,y), then alternate pointerDown/pointerUp `count`
     * times. BiDi's pointer-source automatically tracks clickCount
     * across consecutive down/up pairs at the same coords within a
     * short window — sending two pairs back-to-back fires a real
     * `dblclick` event on the page, which is what the user expects
     * for word-select. count==1 → single click. count>=2 → multi.
     * Cap at 3 (triple-click = paragraph select in most editors). */
    if (count < 1) count = 1;
    if (count > 3) count = 3;
    json actions = json::array({
        json{{"type", "pointerMove"}, {"x", x}, {"y", y}, {"duration", 0}},
    });
    for (int i = 0; i < count; i++) {
        actions.push_back(json{{"type", "pointerDown"},
                               {"button", button}});
        actions.push_back(json{{"type", "pointerUp"},
                               {"button", button}});
    }
    json p = pointer_action(ctx, actions);
    std::string e;
    return !call("input.performActions", p.dump(),
                 error ? error : &e).empty();
}

bool BidiClient::mouse_move(int x, int y, std::string* error)
{
    std::string ctx = active_context();
    if (ctx.empty()) {
        if (error) *error = "no top-level browsing context";
        return false;
    }
    json actions = json::array({
        json{{"type", "pointerMove"}, {"x", x}, {"y", y}, {"duration", 0}},
    });
    json p = pointer_action(ctx, actions);
    std::string e;
    return !call("input.performActions", p.dump(),
                 error ? error : &e).empty();
}

bool BidiClient::scroll(int x, int y, int dx, int dy,
                        std::string* error)
{
    std::string ctx = active_context();
    if (ctx.empty()) {
        if (error) *error = "no top-level browsing context";
        return false;
    }
    json src = {
        {"id", "wheel"},
        {"type", "wheel"},
        {"actions", json::array({
            json{{"type", "scroll"}, {"x", x}, {"y", y},
                 {"deltaX", dx},     {"deltaY", dy}},
        })},
    };
    json p = {
        {"context", ctx},
        {"actions", json::array({src})},
    };
    std::string e;
    return !call("input.performActions", p.dump(),
                 error ? error : &e).empty();
}

bool BidiClient::type(const std::string& text, std::string* error)
{
    std::string ctx = active_context();
    if (ctx.empty()) {
        if (error) *error = "no top-level browsing context";
        return false;
    }
    /* BiDi key-source actions take one Unicode code point per action.
     * For a UTF-8 string we send keyDown+keyUp per character; BiDi
     * handles the rest (modifiers from special keys are M5+). */
    json actions = json::array();
    for (size_t i = 0; i < text.size();) {
        /* Decode one UTF-8 codepoint to a single char string. BiDi
         * accepts the UTF-8 bytes verbatim as a string. */
        size_t step = 1;
        unsigned c = (unsigned char)text[i];
        if      ((c & 0x80) == 0x00) step = 1;
        else if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        if (i + step > text.size()) break;
        std::string ch = text.substr(i, step);
        actions.push_back(json{{"type", "keyDown"}, {"value", ch}});
        actions.push_back(json{{"type", "keyUp"},   {"value", ch}});
        i += step;
    }
    json src = {
        {"id", "kbd"},
        {"type", "key"},
        {"actions", actions},
    };
    json p = {
        {"context", ctx},
        {"actions", json::array({src})},
    };
    std::string e;
    return !call("input.performActions", p.dump(),
                 error ? error : &e).empty();
}

bool BidiClient::send_key_with_mods(const std::string& key,
                                    unsigned mods,
                                    std::string* error)
{
    std::string ctx = active_context();
    if (ctx.empty()) {
        if (error) *error = "no top-level browsing context";
        return false;
    }
    /* W3C-WebDriver modifier codepoints. Mac side maps:
     *   shiftKey   → kModShift ()
     *   cmdKey     → kModCtrl  ()  — Cmd on Mac ≡ Ctrl on PC
     *                                       for keyboard shortcuts
     *   optionKey  → kModAlt   ()
     *   controlKey → also kModCtrl
     * Order: press shift/ctrl/alt/meta, press key, release key,
     * release in reverse. Single performActions call. */
    json actions = json::array();
    auto press = [&](const char* k, bool down) {
        actions.push_back(json{{"type", down ? "keyDown" : "keyUp"},
                               {"value", k}});
    };
    if (mods & kModShift) press("\xEE\x80\x88", true);
    if (mods & kModCtrl)  press("\xEE\x80\x89", true);
    if (mods & kModAlt)   press("\xEE\x80\x8A", true);
    if (mods & kModMeta)  press("\xEE\x80\xBD", true);
    press(key.c_str(), true);
    press(key.c_str(), false);
    if (mods & kModMeta)  press("\xEE\x80\xBD", false);
    if (mods & kModAlt)   press("\xEE\x80\x8A", false);
    if (mods & kModCtrl)  press("\xEE\x80\x89", false);
    if (mods & kModShift) press("\xEE\x80\x88", false);

    json src = {
        {"id", "kbd"},
        {"type", "key"},
        {"actions", actions},
    };
    json p = {
        {"context", ctx},
        {"actions", json::array({src})},
    };
    std::string e;
    return !call("input.performActions", p.dump(),
                 error ? error : &e).empty();
}

bool BidiClient::set_viewport(int width, int height, std::string* error)
{
    if (impl_->top_context.empty()) {
        if (error) *error = "no top-level browsing context";
        return false;
    }
    json p = {
        {"context", impl_->top_context},
        {"viewport", {{"width", width}, {"height", height}}},
    };
    std::string e;
    return !call("browsingContext.setViewport", p.dump(),
                 error ? error : &e).empty();
}

std::string BidiClient::evaluate(const std::string& expr,
                                 std::string* error)
{
    if (impl_->top_context.empty()) {
        if (error) *error = "no top-level browsing context";
        return {};
    }
    json p = {
        {"expression", expr},
        {"target",      {{"context", impl_->top_context}}},
        {"awaitPromise", true},
    };
    std::string e;
    return call("script.evaluate", p.dump(), error ? error : &e);
}

bool BidiClient::subscribe(const std::vector<std::string>& events,
                           std::string* error)
{
    json p = {{"events", events}};
    std::string e;
    return !call("session.subscribe", p.dump(),
                 error ? error : &e).empty();
}

bool BidiClient::add_preload_script(const std::string& js,
                                    std::string* error)
{
    /* script.addPreloadScript runs the function on every realm
     * created in any browsing context. We wrap our JS in a thunk
     * because the BiDi spec wants a function expression. */
    std::string thunk = "()=>{\n" + js + "\n}";
    json p = {{"functionDeclaration", thunk}};
    std::string e;
    return !call("script.addPreloadScript", p.dump(),
                 error ? error : &e).empty();
}

}  // namespace browser
