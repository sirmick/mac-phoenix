/*
 *  module.cpp — BrowserModule lifecycle. See module.h.
 */
#include "module.h"
#include "bidi.h"
#include "cmd.h"
#include "mouse_poll.h"
#include "pipeline.h"
#include "shm.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace browser {

BrowserModule::BrowserModule()  = default;
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

    capture_ = std::make_unique<XShmCapture>();
    if (!capture_->start(supervisor_->display())) {
        fprintf(stderr, "[BrowserModule] XShm capture failed\n");
        capture_.reset();
        supervisor_->stop();
        supervisor_.reset();
        return false;
    }

    pipeline_start(capture_.get());

    /* Connect BiDi to Firefox's --remote-debugging-port. Firefox needs
     * a moment after spawn to bind the port; loop until it's up. */
    bidi_ = std::make_unique<BidiClient>();
    bool bidi_up = false;
    for (int i = 0; i < 50; i++) {  /* up to 10 s */
        if (bidi_->start("127.0.0.1", 9222,
                         std::chrono::milliseconds(500))) {
            bidi_up = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!bidi_up) {
        fprintf(stderr, "[BrowserModule] BiDi never came up — control "
                "channel disabled, pixel pipeline still works\n");
        bidi_.reset();
    } else {
        fprintf(stderr, "[BrowserModule] BiDi connected\n");
        cmd_set_bidi(bidi_.get());
        mouse_poll_start(bidi_.get());

        /* Subscribe to navigation events so the guest can drive a
         * loading-state UI in M5. The on_event callback runs on the
         * WebSocket I/O thread; send_event is mutex-protected. */
        bidi_->on_event([](const std::string& json_text) {
            using nlohmann::json;
            json j;
            try { j = json::parse(json_text); }
            catch (...) { return; }
            std::string method = j.value("method", "");
            if (method == "browsingContext.navigationStarted" ||
                method == "browsingContext.navigationCommitted") {
                /* Send a brief STATUS message; the guest's URL bar
                 * displays "Loading…" while waiting for the matching
                 * load event. */
                std::string url = j["params"].value("url", "");
                fprintf(stderr, "[BiDi event] %s url=%s\n",
                        method.c_str(), url.c_str());
                uint8_t buf[2 + 250];
                buf[0] = BR_STATUS_LOADING;
                size_t n = std::min(url.size(), (size_t)250);
                buf[1] = (uint8_t)n;
                memcpy(buf + 2, url.data(), n);
                send_event(BR_EV_STATUS, buf, (uint16_t)(2 + n));
            } else if (method == "browsingContext.load") {
                /* Fire READY status. The page dimensions + title go
                 * out as a follow-up BR_EV_PAGE message; the guest
                 * uses BR_EV_FRAME (already published by pipeline)
                 * to know to repaint. */
                std::string url = j["params"].value("url", "");
                fprintf(stderr, "[BiDi event] load url=%s\n", url.c_str());
                uint8_t buf[2 + 250];
                buf[0] = BR_STATUS_READY;
                size_t n = std::min(url.size(), (size_t)250);
                buf[1] = (uint8_t)n;
                memcpy(buf + 2, url.data(), n);
                send_event(BR_EV_STATUS, buf, (uint16_t)(2 + n));
            }
        });
        std::string sub_err;
        if (!bidi_->subscribe({"browsingContext.navigationStarted",
                               "browsingContext.navigationCommitted",
                               "browsingContext.load"},
                              &sub_err)) {
            fprintf(stderr, "[BrowserModule] BiDi subscribe failed: %s\n",
                    sub_err.c_str());
        }

    }

    running_ = true;
    fprintf(stderr, "[BrowserModule] running (display=:%d, bidi=%s)\n",
            supervisor_->display(), bidi_ ? "yes" : "no");
    return true;
}

void BrowserModule::stop()
{
    running_ = false;
    mouse_poll_stop();
    cmd_set_bidi(nullptr);
    if (bidi_)       { bidi_->stop();       bidi_.reset(); }
    pipeline_stop();
    if (capture_)    { capture_->stop();    capture_.reset(); }
    if (supervisor_) { supervisor_->stop(); supervisor_.reset(); }
}

namespace {

std::mutex                     g_module_mtx;
std::unique_ptr<BrowserModule> g_module;
std::thread                    g_init_thread;
std::atomic<bool>              g_init_running{false};

/* Initialization is slow:
 *   spawn Xvfb (~50 ms) + Firefox (~1-2 s) + XShm attach (~50 ms).
 * The IPC child's main thread runs cpu_execute right after this
 * is called, so blocking it would delay guest boot. Off-load to a
 * background thread; the guest boots in parallel. */
void init_thread_main(std::string initial_url)
{
    auto m = std::make_unique<BrowserModule>();
    if (!m->start(initial_url)) {
        fprintf(stderr, "[BrowserModule] start failed\n");
        g_init_running.store(false, std::memory_order_release);
        return;
    }
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
    if (g_init_thread.joinable()) g_init_thread.join();
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

}  // namespace browser
