/*
 *  module.cpp — BrowserModule lifecycle. See module.h.
 */
#include "module.h"
#include "bidi.h"
#include "pipeline.h"

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
    }

    running_ = true;
    fprintf(stderr, "[BrowserModule] running (display=:%d, bidi=%s)\n",
            supervisor_->display(), bidi_ ? "yes" : "no");
    return true;
}

void BrowserModule::stop()
{
    running_ = false;
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
