/*
 *  module.h — top-level lifecycle for the MacBrowser host pipeline.
 *
 *  When --browser is enabled, BrowserModule:
 *    1. Spawns Xvfb + Chromium via Supervisor.
 *    2. Connects a CdpClient to the browser-target WS.
 *    3. Enables Page-domain events so Chromium reports navigation
 *       progress, lifecycle events, and (eventually) frame updates.
 *
 *  M3 deliverable: the supervisor + CDP client run successfully and
 *  Chromium navigates to whatever URL the host hands it. M4 adds the
 *  XShm pixel pipeline that drains paint events into BrowserShm.fb.
 */
#ifndef DRIVERS_BROWSER_MODULE_H
#define DRIVERS_BROWSER_MODULE_H

#include <memory>
#include <string>

#include "supervisor.h"
#include "cdp.h"

namespace browser {

class BrowserModule {
public:
    BrowserModule();
    ~BrowserModule();

    /* Bring up Xvfb + Chromium and attach the CDP client. Returns true
     * iff every stage succeeded. Logs progress to stderr. */
    bool start();

    /* Tear everything down. Idempotent. */
    void stop();

    /* Convenience for the M3 end-to-end probe — issue a Page.navigate
     * to the given URL on the attached page target. Returns true on
     * success. */
    bool navigate(const std::string& url);

    Supervisor* supervisor() { return supervisor_.get(); }
    CdpClient*  cdp()        { return cdp_.get(); }

private:
    std::unique_ptr<Supervisor> supervisor_;
    std::unique_ptr<CdpClient>  cdp_;
    bool running_ = false;
};

/* Convenience entry points used by main.cpp. browser_module_start
 * brings up Xvfb + Chromium + CDP; if `initial_url` is non-empty, also
 * issues a Page.navigate to it. browser_module_stop tears everything
 * down on emulator exit. Both are no-ops if already in the requested
 * state. */
void browser_module_start(const std::string& initial_url);
void browser_module_stop();

/* Optional accessor for downstream code (M4 pixel pipeline, M5 input
 * forwarding). Returns nullptr before start / after stop. */
BrowserModule* browser_module_get();

}  // namespace browser

#endif  // DRIVERS_BROWSER_MODULE_H
