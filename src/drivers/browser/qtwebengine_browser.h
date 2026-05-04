/*
 *  qtwebengine_browser.h — host-side browser via in-process QWebEnginePage.
 *
 *  Replaces the supervisor.cpp / xshm.cpp / bidi.cpp pipeline (Firefox on
 *  Xvfb, captured via XShm+XDamage, controlled via WebDriver-BiDi) with
 *  an in-process Chromium running under Qt6 WebEngine.
 *
 *  Threading: QApplication + QWebEngineView live on a dedicated browser
 *  GUI thread (qtwebengine_module_start spawns it). The CPU thread keeps
 *  running cpu_execute_fast() unaffected; cross-thread commands marshal
 *  via QMetaObject::invokeMethod(Qt::QueuedConnection).
 *
 *  Smoothness: software rasterization is forced via QTWEBENGINE_CHROMIUM_FLAGS
 *  before QApplication construction so <video>/WebGL frames land in the
 *  widget's backing store (Chromium hardware overlays would otherwise bypass
 *  any read-back path).
 *
 *  Phase 8b: skeleton only — constructs the view, loads a URL, logs the
 *  navigation lifecycle. Pixel capture is 8c; input synthesis is 8d; etc.
 */
#pragma once

#include <memory>
#include <string>

class QWebEngineView;

namespace browser {

class QtWebEngineBrowser {
public:
    QtWebEngineBrowser();
    ~QtWebEngineBrowser();

    QtWebEngineBrowser(const QtWebEngineBrowser&) = delete;
    QtWebEngineBrowser& operator=(const QtWebEngineBrowser&) = delete;

    void load(const std::string& url);

private:
    std::unique_ptr<QWebEngineView> view_;
};

// Spawn the browser GUI thread. Constructs QApplication + QtWebEngineBrowser,
// loads `initial_url` (defaults to https://example.com for the 8b smoke), then
// runs QApplication::exec() until qtwebengine_module_stop() is called.
//
// Idempotent: a second call before stop is a no-op.
void qtwebengine_module_start(const std::string& initial_url = {});

// Post quit() to the browser GUI thread's event loop and join. Safe to call
// from any thread; safe to call multiple times.
void qtwebengine_module_stop();

}  // namespace browser
