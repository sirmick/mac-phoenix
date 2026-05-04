/*
 *  qtwebengine_browser.cpp — see qtwebengine_browser.h.
 *
 *  This module assumes a QApplication is already running on the main thread
 *  (set up in main.cpp when --browser is detected). All QtWebEngine objects
 *  must be created and accessed from that thread.
 */
#include "qtwebengine_browser.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QThread>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineView>

#include <atomic>
#include <cstdio>
#include <memory>

namespace browser {

QtWebEngineBrowser::QtWebEngineBrowser()
{
    view_ = std::make_unique<QWebEngineView>();
    // Hidden but rendered: the widget allocates a backing store and Chromium
    // fires paintEvent on actual repaints (event-driven, matches XDamage
    // semantics — drives the 8c capture path). WA_DontShowOnScreen keeps
    // the window off any real display while still serving paint events.
    view_->setAttribute(Qt::WA_DontShowOnScreen);
    view_->resize(1024, 768);

    QWebEnginePage* page = view_->page();
    QWebEngineView* raw = view_.get();

    QObject::connect(page, &QWebEnginePage::loadStarted, raw, [raw]() {
        fprintf(stderr, "[QtWebEngine] loadStarted url=%s\n",
                raw->url().toString().toUtf8().constData());
    });
    QObject::connect(page, &QWebEnginePage::loadFinished, raw, [raw](bool ok) {
        fprintf(stderr, "[QtWebEngine] loadFinished ok=%d url=%s\n",
                ok ? 1 : 0, raw->url().toString().toUtf8().constData());
    });
    QObject::connect(page, &QWebEnginePage::urlChanged, raw, [](const QUrl& url) {
        fprintf(stderr, "[QtWebEngine] urlChanged %s\n",
                url.toString().toUtf8().constData());
    });
}

QtWebEngineBrowser::~QtWebEngineBrowser() = default;

void QtWebEngineBrowser::load(const std::string& url)
{
    fprintf(stderr, "[QtWebEngine] load %s\n", url.c_str());
    view_->setUrl(QUrl(QString::fromStdString(url)));
}

namespace {

std::unique_ptr<QtWebEngineBrowser> g_browser;

}  // namespace

void qtwebengine_module_start(const std::string& initial_url)
{
    if (g_browser) return;

    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        fprintf(stderr, "[QtWebEngine] no QCoreApplication — skipping start "
                "(needs --browser at process startup)\n");
        return;
    }
    if (!qobject_cast<QApplication*>(app)) {
        fprintf(stderr, "[QtWebEngine] QApplication not active — skipping "
                "(headless QCoreApplication can't host QWebEngineView)\n");
        return;
    }
    if (QThread::currentThread() != app->thread()) {
        // QtWebEngine objects must be created on the GUI (main) thread.
        // Marshal across.
        QMetaObject::invokeMethod(app, [initial_url]() {
            qtwebengine_module_start(initial_url);
        }, Qt::QueuedConnection);
        return;
    }

    g_browser = std::make_unique<QtWebEngineBrowser>();
    std::string url = initial_url.empty()
        ? "data:text/html,<html><body><h1>QtWebEngine 8b smoke</h1></body></html>"
        : initial_url;
    g_browser->load(url);
}

void qtwebengine_module_stop()
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) { g_browser.reset(); return; }
    if (QThread::currentThread() != app->thread()) {
        QMetaObject::invokeMethod(app, []() { g_browser.reset(); },
                                  Qt::BlockingQueuedConnection);
        return;
    }
    g_browser.reset();
}

}  // namespace browser
