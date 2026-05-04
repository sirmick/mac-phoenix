/*
 *  module.h — BrowserModule lifecycle wrapper.
 *
 *  Pre-Phase-8 BrowserModule orchestrated Xvfb + Firefox via Supervisor
 *  + XShmCapture + BidiClient. 8i deleted that whole layer; this file
 *  is now a thin shim around qtwebengine_module_start/stop and the
 *  mouse poller, kept so main.cpp's call sites don't have to fan out.
 */
#ifndef DRIVERS_BROWSER_MODULE_H
#define DRIVERS_BROWSER_MODULE_H

namespace browser {

// Spawn QtWebEngineBrowser (loads its smoke URL) + the mouse poller.
// Idempotent. Requires QApplication on the main thread (set up in
// main.cpp when --browser is in argv).
void browser_module_start();

// Tear down both. Idempotent.
void browser_module_stop();

}  // namespace browser

#endif  // DRIVERS_BROWSER_MODULE_H
