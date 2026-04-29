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
        if (bidi_->start("127.0.0.1", supervisor_->bidi_port(),
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
        cmd_set_display(supervisor_->display());
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
                 * load event.
                 *
                 * Exception: when navigationCommitted points at
                 * about:neterror (Firefox's internal DNS-fail / TLS-fail
                 * page), Firefox will never fire browsingContext.load
                 * for the inner error frame, leaving the guest stuck
                 * on LOADING. Treat it as a terminal ERROR state. */
                std::string url = j["params"].value("url", "");
                bool is_neterror =
                    method == "browsingContext.navigationCommitted" &&
                    url.compare(0, 14, "about:neterror") == 0;
                fprintf(stderr, "[BiDi event] %s url=%s%s\n",
                        method.c_str(), url.c_str(),
                        is_neterror ? " (treating as ERROR)" : "");
                uint8_t buf[2 + 250];
                buf[0] = is_neterror ? BR_STATUS_ERROR : BR_STATUS_LOADING;
                size_t n = std::min(url.size(), (size_t)250);
                buf[1] = (uint8_t)n;
                memcpy(buf + 2, url.data(), n);
                send_event(BR_EV_STATUS, buf, (uint16_t)(2 + n));
            } else if (method == "log.entryAdded") {
                /* Forward Firefox console output for debugging — only
                 * messages from our preload (prefixed [mac-phoenix])
                 * to avoid drowning in page-side console noise. */
                std::string text = j["params"].value("text", "");
                if (text.find("[mac-phoenix]") != std::string::npos) {
                    fprintf(stderr, "[FF console] %s\n", text.c_str());
                }
                return;
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
                               "browsingContext.load",
                               "log.entryAdded"},
                              &sub_err)) {
            fprintf(stderr, "[BrowserModule] BiDi subscribe failed: %s\n",
                    sub_err.c_str());
        }

        /* Hide Firefox's internal scrollbars on every page — Mac's
         * scroll bars own the UI. Inject a global stylesheet via
         * script.addPreloadScript so it runs before each page's own
         * scripts and applies to nested frames too.
         *
         * Three layers, because `scrollbar-width:none` alone leaves a
         * pale gutter visible at the right edge once Firefox's overlay
         * scrollbar has been activated by a scroll event:
         *   1. scrollbar-color: transparent transparent  — neuters the
         *      track + thumb colors so even if Firefox decides to paint
         *      the overlay on top, it's invisible
         *   2. scrollbar-width: none + ::-webkit-scrollbar{display:none}
         *      — the original "no widget" hint, kept for cross-engine
         *   3. Dedicated <style> element appended to documentElement
         *      before any page script runs, with !important to outrank
         *      page styles (some pages set scrollbar-color themselves).
         *
         * Plus: an injected 1-px black border drawn at the viewport edges
         * via a fixed-position ::before pseudo-element on the html. If
         * any of {Xvfb root, Firefox layout viewport, BiDi setViewport,
         * xshm capture rect, pipeline dst_stride, BrowserShm fb.width,
         * Mac PixMap rowBytes} disagrees with the others, the border on
         * one or more sides will appear cropped, missing, or shifted.
         * It's a visual sync probe across the whole pipe. */
        std::string ps_err;
        /* Single-quoted JS string — keep CSS rules separated by spaces
         * (browser parses just fine, easier to read in stderr if we
         * ever console.log it). */
        /* Note: each line below is one JS string concatenated to the
         * next via JS `+`. Adjacent C-string concat fuses them into a
         * single C string, but JS still needs explicit `+` to glue
         * adjacent quoted JS literals — without it, only the first
         * literal lands and the rest are dead expression statements. */
        const char* hide_scrollbars =
            "console.log('[mac-phoenix] preload firing');"
            "const inject=()=>{"
              "const s=document.createElement('style');"
              "s.id='mac-phoenix-injected';"
              "s.textContent="
                "'html,body{scrollbar-width:none!important;'+"
                "'scrollbar-color:transparent transparent!important;'+"
                "'-ms-overflow-style:none!important;}'+"
                "'::-webkit-scrollbar{display:none!important;'+"
                "'width:0!important;height:0!important;}'+"
                /* Sync probe: a fixed-position <div> ringing the viewport
                 * with a 1-px black border. If any of {Xvfb root, FF
                 * layout viewport, BiDi setViewport, xshm capture,
                 * pipeline dst_stride, BrowserShm fb.width, Mac PixMap
                 * rowBytes} disagrees, the border on the disagreeing
                 * side appears clipped, missing, or shifted. */
                "'body>#mac-phoenix-probe{position:fixed!important;'+"
                "'top:0!important;left:0!important;'+"
                "'right:0!important;bottom:0!important;'+"
                "'border:1px solid black!important;'+"
                "'pointer-events:none!important;'+"
                "'z-index:2147483647!important;}';"
              "(document.head||document.documentElement).appendChild(s);"
              "if(document.body && !document.getElementById('mac-phoenix-probe')){"
                "const d=document.createElement('div');"
                "d.id='mac-phoenix-probe';"
                "document.body.appendChild(d);"
                "console.log('[mac-phoenix] probe div planted');"
              "}"
            "};"
            /* document_start fires before <body> exists, so retry on
             * DOMContentLoaded to actually plant the probe div. */
            "if(document.readyState==='loading'){"
              "document.addEventListener('DOMContentLoaded',inject,{once:true});"
            "}"
            "inject();";
        if (!bidi_->add_preload_script(hide_scrollbars, &ps_err)) {
            fprintf(stderr, "[BrowserModule] preload script failed: %s\n",
                    ps_err.c_str());
        } else {
            fprintf(stderr, "[BrowserModule] preload script registered "
                    "(scrollbar suppression + sync-probe border)\n");
        }

        /* Seed the guest's URL bar with whatever Firefox is currently
         * showing. Pages loaded before subscription don't get a
         * synthetic event, so without this the URL bar stays empty
         * until the user navigates. Run on its own thread because
         * we have to wait for the guest's handshake (which happens
         * after this function returns) and we don't want to block
         * the boot path. */
        BidiClient* bp = bidi_.get();
        std::thread([bp]() {
            /* Wait up to 30 s for guest handshake. */
            for (int i = 0; i < 300 && !browser::shm_get(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!browser::shm_get() || !bp || !bp->is_open()) return;

            std::string err;
            std::string r = bp->evaluate("window.location.href", &err);
            if (r.empty()) return;
            std::string url;
            try {
                auto j = nlohmann::json::parse(r);
                url = j.at("result").at("value").get<std::string>();
            } catch (...) { return; }
            if (url.empty() || url == "about:blank") return;

            uint8_t buf[2 + 250];
            buf[0] = BR_STATUS_READY;
            size_t n = std::min(url.size(), (size_t)250);
            buf[1] = (uint8_t)n;
            memcpy(buf + 2, url.data(), n);
            send_event(BR_EV_STATUS, buf, (uint16_t)(2 + n));
            fprintf(stderr, "[BrowserModule] seeded URL bar with %s\n",
                    url.c_str());
        }).detach();
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
    cmd_set_display(-1);
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
void init_thread_main()
{
    auto m = std::make_unique<BrowserModule>();
    if (!m->start(std::string{})) {
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

void browser_module_start()
{
    {
        std::lock_guard<std::mutex> lk(g_module_mtx);
        if (g_module) return;
    }
    if (g_init_running.exchange(true, std::memory_order_acq_rel)) return;
    if (g_init_thread.joinable()) g_init_thread.join();
    g_init_thread = std::thread(init_thread_main);
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
