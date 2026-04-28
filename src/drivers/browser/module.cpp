/*
 *  module.cpp — BrowserModule lifecycle. See module.h.
 */
#include "module.h"

#include <cstdio>
#include <mutex>

namespace browser {

namespace {
std::mutex g_module_mtx;
std::unique_ptr<BrowserModule> g_module;
}  // namespace

void browser_module_start(const std::string& initial_url)
{
    std::lock_guard<std::mutex> lk(g_module_mtx);
    if (g_module) return;

    auto m = std::make_unique<BrowserModule>();
    if (!m->start()) {
        fprintf(stderr, "[BrowserModule] start failed; --browser-url "
                "navigation will be skipped\n");
        return;  /* leave g_module null so callers see "not running" */
    }

    if (!initial_url.empty()) {
        if (!m->navigate(initial_url)) {
            fprintf(stderr, "[BrowserModule] initial navigate(%s) failed\n",
                    initial_url.c_str());
            /* Non-fatal — keep the module up; later code can retry. */
        }
    }

    g_module = std::move(m);
}

void browser_module_stop()
{
    std::lock_guard<std::mutex> lk(g_module_mtx);
    if (!g_module) return;
    g_module->stop();
    g_module.reset();
}

BrowserModule* browser_module_get()
{
    std::lock_guard<std::mutex> lk(g_module_mtx);
    return g_module.get();
}

BrowserModule::BrowserModule() = default;

BrowserModule::~BrowserModule() { stop(); }

bool BrowserModule::start()
{
    if (running_) return true;

    supervisor_ = std::make_unique<Supervisor>();
    if (!supervisor_->start()) {
        fprintf(stderr, "[BrowserModule] supervisor failed to start\n");
        supervisor_.reset();
        return false;
    }

    cdp_ = std::make_unique<CdpClient>();
    if (!cdp_->open(supervisor_->browser_ws_url())) {
        fprintf(stderr, "[BrowserModule] CDP open failed\n");
        cdp_.reset();
        supervisor_->stop();
        supervisor_.reset();
        return false;
    }

    /* Enable Page-domain events. Page.frameNavigated fires when the
     * URL changes; Page.loadEventFired marks "DOMContentLoaded-ish."
     * Both will be useful for status text in M5; for M3 we just log
     * them as proof-of-life. */
    auto enable = cdp_->call("Page.enable");
    if (!enable.ok) {
        fprintf(stderr, "[BrowserModule] Page.enable failed\n");
        /* Non-fatal — we can still call Page.navigate without it. */
    }

    cdp_->on_event("Page.frameNavigated", [](const CdpClient::Json& params) {
        if (params.contains("frame") && params["frame"].contains("url")) {
            fprintf(stderr, "[BrowserModule] frameNavigated: %s\n",
                    params["frame"]["url"].get<std::string>().c_str());
        }
    });
    cdp_->on_event("Page.loadEventFired",
                   [](const CdpClient::Json&) {
        fprintf(stderr, "[BrowserModule] loadEventFired\n");
    });

    running_ = true;
    fprintf(stderr, "[BrowserModule] running (cdp=%d, ws=%s)\n",
            supervisor_->cdp_port(),
            supervisor_->browser_ws_url().c_str());
    return true;
}

void BrowserModule::stop()
{
    running_ = false;
    if (cdp_) {
        cdp_->close();
        cdp_.reset();
    }
    if (supervisor_) {
        supervisor_->stop();
        supervisor_.reset();
    }
}

bool BrowserModule::navigate(const std::string& url)
{
    if (!running_ || !cdp_) return false;
    CdpClient::Json params = {{"url", url}};
    auto resp = cdp_->call("Page.navigate", params);
    if (!resp.ok) {
        fprintf(stderr, "[BrowserModule] Page.navigate(%s) failed: %s\n",
                url.c_str(), resp.error.dump().c_str());
        return false;
    }
    fprintf(stderr, "[BrowserModule] navigate(%s) -> %s\n",
            url.c_str(), resp.result.dump().c_str());
    return true;
}

}  // namespace browser
