/*
 *  cdp.cpp — Chrome DevTools Protocol client over libdatachannel ws.
 *  See cdp.h.
 */
#include "cdp.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>
#include <variant>

#include <rtc/websocket.hpp>

namespace browser {

using Json = CdpClient::Json;

CdpClient::CdpClient() = default;
CdpClient::~CdpClient() { close(); }

bool CdpClient::open(const std::string& browser_ws_url,
                     std::chrono::milliseconds timeout)
{
    if (open_.load()) return true;

    ws_ = std::make_unique<rtc::WebSocket>();

    /* Callbacks capture only `this` — destructor calls close() which
     * resets ws_, joining its internal threads before pending_/handlers_
     * disappear. No reference-into-stack-frame race. */
    ws_->onError([](std::string err) {
        fprintf(stderr, "[CDP] websocket error: %s\n", err.c_str());
    });
    ws_->onClosed([this]() {
        open_.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lk(mtx_);
        cv_.notify_all();
    });
    ws_->onMessage([this](rtc::message_variant data) {
        if (std::holds_alternative<std::string>(data)) {
            handle_message(std::get<std::string>(data));
        }
        /* CDP only uses text frames; ignore any binary. */
    });

    fprintf(stderr, "[CDP] connecting to %s\n", browser_ws_url.c_str());
    ws_->open(browser_ws_url);

    /* Poll readyState until Open or timeout. The libdatachannel WS
     * runs its own internal thread; we just wait for it to make
     * progress. */
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto state = ws_->readyState();
        if (state == rtc::WebSocket::State::Open) break;
        if (state == rtc::WebSocket::State::Closed) {
            fprintf(stderr, "[CDP] ws closed before becoming open\n");
            ws_.reset();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (ws_->readyState() != rtc::WebSocket::State::Open) {
        fprintf(stderr, "[CDP] timeout waiting for ws open\n");
        ws_.reset();
        return false;
    }

    open_.store(true, std::memory_order_release);

    /* Discover and attach to the first page target. flatten:true
     * routes per-target events through this same socket with a
     * sessionId field, instead of opening a new WS per target. */
    auto targets = call("Target.getTargets", Json::object(), timeout);
    if (!targets.ok) {
        fprintf(stderr, "[CDP] Target.getTargets failed\n");
        close();
        return false;
    }

    std::string target_id;
    if (targets.result.contains("targetInfos") &&
        targets.result["targetInfos"].is_array()) {
        for (const auto& t : targets.result["targetInfos"]) {
            if (t.value("type", "") == "page") {
                target_id = t.value("targetId", "");
                break;
            }
        }
    }
    if (target_id.empty()) {
        fprintf(stderr, "[CDP] no page target found in browser\n");
        close();
        return false;
    }

    Json attach_params = {
        {"targetId", target_id},
        {"flatten",  true}
    };
    auto attach = call("Target.attachToTarget", attach_params, timeout);
    if (!attach.ok || !attach.result.contains("sessionId")) {
        fprintf(stderr, "[CDP] Target.attachToTarget failed\n");
        close();
        return false;
    }
    session_id_ = attach.result["sessionId"].get<std::string>();

    fprintf(stderr, "[CDP] attached to target %s (session %s)\n",
            target_id.c_str(), session_id_.c_str());
    return true;
}

void CdpClient::close()
{
    if (!ws_) return;
    open_.store(false, std::memory_order_release);
    try { ws_->close(); } catch (...) {}
    ws_.reset();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& kv : pending_) {
            kv.second->done = true;
            kv.second->resp.ok = false;
        }
        cv_.notify_all();
        pending_.clear();
        handlers_.clear();
        session_id_.clear();
    }
}

CdpClient::Response CdpClient::call(const std::string& method,
                                    const Json& params,
                                    std::chrono::milliseconds timeout)
{
    Response resp;
    if (!open_.load() || !ws_) return resp;

    Json req = {
        {"method", method},
        {"params", params},
    };
    int id;
    auto pending = std::make_shared<Pending>();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        id = next_id_++;
        req["id"] = id;
        if (!session_id_.empty() &&
            method != "Target.getTargets" &&
            method != "Target.attachToTarget" &&
            method.rfind("Browser.", 0) != 0) {
            req["sessionId"] = session_id_;
        }
        pending_[id] = pending;
    }

    std::string text = req.dump();
    if (!ws_->send(text)) {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_.erase(id);
        return resp;
    }

    std::unique_lock<std::mutex> lk(mtx_);
    if (!cv_.wait_for(lk, timeout, [&]{ return pending->done; })) {
        pending_.erase(id);
        fprintf(stderr, "[CDP] timeout on %s (id=%d)\n", method.c_str(), id);
        return resp;
    }
    pending_.erase(id);
    return pending->resp;
}

void CdpClient::on_event(const std::string& method, EventHandler handler)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (handler) handlers_[method] = std::move(handler);
    else         handlers_.erase(method);
}

void CdpClient::handle_message(const std::string& text)
{
    Json msg;
    try {
        msg = Json::parse(text);
    } catch (const std::exception& e) {
        fprintf(stderr, "[CDP] bad JSON from chromium: %s\n", e.what());
        return;
    }

    if (msg.contains("id")) {
        /* Response to one of our calls. */
        int id = msg["id"].get<int>();
        std::shared_ptr<Pending> p;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = pending_.find(id);
            if (it == pending_.end()) {
                /* Stale or duplicate; ignore. */
                return;
            }
            p = it->second;
            p->done = true;
            if (msg.contains("error")) {
                p->resp.ok = false;
                p->resp.error = msg["error"];
                fprintf(stderr, "[CDP] error on id=%d: %s\n", id,
                        p->resp.error.dump().c_str());
            } else {
                p->resp.ok = true;
                if (msg.contains("result")) p->resp.result = msg["result"];
            }
        }
        cv_.notify_all();
        return;
    }

    if (msg.contains("method")) {
        std::string method = msg["method"].get<std::string>();
        EventHandler handler;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = handlers_.find(method);
            if (it != handlers_.end()) handler = it->second;
        }
        if (handler) {
            Json params = msg.contains("params") ? msg["params"]
                                                 : Json::object();
            try { handler(params); }
            catch (const std::exception& e) {
                fprintf(stderr, "[CDP] event handler for %s threw: %s\n",
                        method.c_str(), e.what());
            }
        }
    }
}

}  // namespace browser
