/*
 *  module.cpp — BrowserModule lifecycle. See module.h.
 */
#include "module.h"
#include "pipeline.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

namespace browser {

namespace {
std::mutex g_module_mtx;
std::unique_ptr<BrowserModule> g_module;
std::thread g_init_thread;
std::atomic<bool> g_init_running{false};

/* Initialization is slow:
 *   spawn Xvfb (~50ms) + chromium (~500ms warm cache, ~3s cold)
 *   + CDP attach (~100ms)
 *   + Page.navigate to a real URL (~1-3s incl DNS + TLS + first paint).
 * Total: 4-7 seconds typical, 30s worst case for navigate timeout.
 *
 * The IPC child's main thread runs cpu_execute right after this is
 * called, so blocking it would delay guest boot by 4+ seconds. Off-
 * load to a background thread; the guest boots in parallel with
 * Chromium spinning up. By the time the guest hits Finder, Chromium
 * is usually already showing the page. */
void init_thread_main(std::string initial_url)
{
    auto m = std::make_unique<BrowserModule>();
    if (!m->start(initial_url)) {
        fprintf(stderr, "[BrowserModule] start failed; --browser-url "
                "navigation will be skipped\n");
        g_init_running.store(false, std::memory_order_release);
        return;
    }
    /* Initial URL is loaded by chromium itself on its command line —
     * skip the Page.navigate dance. Subsequent navs from the guest
     * (BR_CMD_NAV → Page.navigate) hit a fully-warmed page and work
     * normally. */

    {
        std::lock_guard<std::mutex> lk(g_module_mtx);
        g_module = std::move(m);
    }
    g_init_running.store(false, std::memory_order_release);
}

}  // namespace

void browser_module_start(const std::string& initial_url)
{
    {
        std::lock_guard<std::mutex> lk(g_module_mtx);
        if (g_module) return;
    }
    if (g_init_running.exchange(true, std::memory_order_acq_rel)) return;
    if (g_init_thread.joinable()) g_init_thread.join();
    g_init_thread = std::thread(init_thread_main, initial_url);
}

void browser_module_stop()
{
    if (g_init_thread.joinable()) {
        /* Wait for any in-flight init to finish before tearing down,
         * so we don't race start() with stop(). */
        g_init_thread.join();
    }
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

bool BrowserModule::start(const std::string& initial_url)
{
    if (running_) return true;

    supervisor_ = std::make_unique<Supervisor>();
    if (!supervisor_->start(initial_url)) {
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

    /* Subscribe to Page.screencastFrame events so chromium pushes us
     * pixels as it paints. Failure here is non-fatal — CDP for
     * navigation still works, just no pixels reach the guest. */
    pipeline_start(cdp_.get());

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
    pipeline_stop();
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
    fprintf(stderr, "[BrowserModule] navigate(%s) issuing CDP request\n",
            url.c_str());
    CdpClient::Json params = {{"url", url}};
    /* 30s budget — first hit to a real site can take well over the
     * 5s default while Chromium warms up (DNS, TLS handshake, page
     * parse). The CDP response itself is "navigation initiated," not
     * "page loaded," but Chromium still blocks the response on
     * dispatching the network request. */
    auto resp = cdp_->call("Page.navigate", params,
                           std::chrono::milliseconds(30000));
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
