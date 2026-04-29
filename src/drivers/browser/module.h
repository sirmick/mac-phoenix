/*
 *  module.h — top-level lifecycle for the MacBrowser host pipeline.
 *
 *  When --browser is enabled, BrowserModule:
 *    1. Spawns Xvfb + Firefox via Supervisor.
 *    2. Opens an XShmCapture against the Xvfb display.
 *    3. Starts the pixel pump (XShm → BrowserShm.fb).
 *
 *  Runtime control (URL navigation, clicks, keys) is M5+ work — for
 *  now Firefox loads the URL passed on its command line and stays
 *  on it.
 */
#ifndef DRIVERS_BROWSER_MODULE_H
#define DRIVERS_BROWSER_MODULE_H

#include <memory>
#include <string>

#include "supervisor.h"
#include "xshm.h"

namespace browser {

class BidiClient;

class BrowserModule {
public:
    BrowserModule();
    ~BrowserModule();

    bool start(const std::string& initial_url);
    void stop();

    Supervisor*  supervisor() { return supervisor_.get(); }
    XShmCapture* capture()    { return capture_.get(); }
    BidiClient*  bidi()       { return bidi_.get(); }

private:
    std::unique_ptr<Supervisor>  supervisor_;
    std::unique_ptr<XShmCapture> capture_;
    std::unique_ptr<BidiClient>  bidi_;
    bool running_ = false;
};

void browser_module_start();
void browser_module_stop();
BrowserModule* browser_module_get();

}  // namespace browser

#endif  // DRIVERS_BROWSER_MODULE_H
