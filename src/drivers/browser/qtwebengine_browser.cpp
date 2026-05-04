/*
 *  qtwebengine_browser.cpp — see qtwebengine_browser.h.
 */
#include "qtwebengine_browser.h"

#include "shm.h"
#define BR_HOST 1
#include "MacBrowser.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineView>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <memory>

namespace browser {

namespace {

// ARGB32 (Qt's QImage::Format_ARGB32 / RGB32) and BGRX share byte layout
// on little-endian: B at byte 0, G at 1, R at 2, A/X at 3 — the same
// uint32 pattern. We can reuse the BGRX-to-RGB555 lambda from pipeline.cpp
// without a swizzle.
inline uint16_t argb32_to_rgb555_be(uint32_t argb)
{
    uint8_t b = (uint8_t)(argb & 0xFF);
    uint8_t g = (uint8_t)((argb >>  8) & 0xFF);
    uint8_t r = (uint8_t)((argb >> 16) & 0xFF);
    uint16_t pix = (uint16_t)(((r & 0xF8) << 7) |
                              ((g & 0xF8) << 2) |
                              (b >> 3));
    return (uint16_t)((pix >> 8) | (pix << 8));
}

// Rolling perf counters; reported once per second. Mirrors pipeline.cpp's
// PerfCounters so log output reads identically across the two backends.
struct CapturePerf {
    uint64_t frames     = 0;
    uint64_t grab_us    = 0;
    uint64_t blit_us    = 0;
    uint64_t pixels_out = 0;
};
CapturePerf g_perf;
std::chrono::steady_clock::time_point g_last_report;
uint32_t g_frame_seq = 0;

template <typename Tp>
inline uint64_t us_since(const Tp& t0)
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
}

void maybe_report(int crop_w, int crop_h)
{
    auto now = std::chrono::steady_clock::now();
    if (g_last_report.time_since_epoch().count() == 0) {
        g_last_report = now;
        return;
    }
    auto window_us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        now - g_last_report).count();
    if (window_us < 1'000'000) return;

    if (g_perf.frames > 0) {
        double fps      = (double)g_perf.frames * 1e6 / (double)window_us;
        double avg_grab = (double)g_perf.grab_us / g_perf.frames;
        double avg_blit = (double)g_perf.blit_us / g_perf.frames;
        double dirty_pct = (crop_w && crop_h)
            ? (double)g_perf.pixels_out * 100.0
                / (g_perf.frames * crop_w * crop_h)
            : 0.0;
        fprintf(stderr,
                "[QtCapture] %.1f fps  grab=%.1fms blit=%.2fms  "
                "dirty=%.1f%%  crop=%dx%d\n",
                fps, avg_grab / 1000.0, avg_blit / 1000.0, dirty_pct,
                crop_w, crop_h);
    }
    g_perf = {};
    g_last_report = now;
}

// Push one captured QImage frame into BrowserShm.fb if a guest handshake
// has happened. Whole image treated as one dirty rect — per-region diff
// is a follow-up optimization once the smoothness gate passes.
//
// Returns the {crop_w, crop_h} actually published (zero if no shm/viewport).
struct CropDims { int w; int h; };
CropDims push_frame_to_browser_shm(const QImage& img)
{
    BrowserShm* shm = browser::shm_get();
    if (!shm) return {0, 0};

    int crop_w = (int)br_u16_load(&shm->fb.width);
    int crop_h = (int)br_u16_load(&shm->fb.height);
    if (crop_w == 0 || crop_h == 0) return {0, 0};
    if (crop_w > (int)BR_FB_MAX_W) crop_w = (int)BR_FB_MAX_W;
    if (crop_h > (int)BR_FB_MAX_H) crop_h = (int)BR_FB_MAX_H;
    shm->fb.depth = 16;

    const int w = std::min(img.width(),  crop_w);
    const int h = std::min(img.height(), crop_h);
    if (w <= 0 || h <= 0) return {crop_w, crop_h};

    auto t1 = std::chrono::steady_clock::now();
    const int dst_stride = crop_w * 2;
    for (int y = 0; y < h; y++) {
        const uint32_t* src_row = (const uint32_t*)img.scanLine(y);
        uint16_t* dst_row =
            (uint16_t*)&shm->fb.pixels[(size_t)y * dst_stride];
        for (int x = 0; x < w; x++) {
            dst_row[x] = argb32_to_rgb555_be(src_row[x]);
        }
    }
    g_perf.blit_us += us_since(t1);

    br_u16_store(&shm->fb.dirty_count, 1);
    br_u16_store((uint16_t*)&shm->fb.dirty[0].top,    0);
    br_u16_store((uint16_t*)&shm->fb.dirty[0].left,   0);
    br_u16_store((uint16_t*)&shm->fb.dirty[0].bottom, (uint16_t)h);
    br_u16_store((uint16_t*)&shm->fb.dirty[0].right,  (uint16_t)w);
    BR_FENCE_RELEASE();
    br_u32_store(&shm->fb.seq, ++g_frame_seq);

    g_perf.pixels_out += (uint64_t)w * (uint64_t)h;
    return {crop_w, crop_h};
}

}  // namespace

QtWebEngineBrowser::QtWebEngineBrowser()
{
    view_ = std::make_unique<QWebEngineView>();
    // Hidden but rendered: WA_DontShowOnScreen keeps the view off any
    // real display while still serving paint events into the backing
    // store (QWidget::grab reads from there).
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

    // 60 Hz capture timer. QWidget::grab() forces a synchronous render
    // pass into the backing store and copies it out — at 1024x768 ARGB32
    // that's ~1-2ms on the GUI thread. Per-region dirty diff is a follow-up
    // (8c-2); this skeleton publishes whole frames.
    capture_timer_ = std::make_unique<QTimer>();
    capture_timer_->setInterval(16);  // ~60Hz
    QObject::connect(capture_timer_.get(), &QTimer::timeout, view_.get(),
                     [this]() { capture_tick(); });
    capture_timer_->start();
}

QtWebEngineBrowser::~QtWebEngineBrowser() = default;

void QtWebEngineBrowser::load(const std::string& url)
{
    fprintf(stderr, "[QtWebEngine] load %s\n", url.c_str());
    view_->setUrl(QUrl(QString::fromStdString(url)));
}

void QtWebEngineBrowser::capture_tick()
{
    auto t0 = std::chrono::steady_clock::now();
    QPixmap pm = view_->grab();
    if (pm.isNull()) return;
    QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    g_perf.grab_us += us_since(t0);
    g_perf.frames++;
    CropDims cd = push_frame_to_browser_shm(img);
    maybe_report(cd.w ? cd.w : pm.width(),
                 cd.h ? cd.h : pm.height());
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
        QMetaObject::invokeMethod(app, [initial_url]() {
            qtwebengine_module_start(initial_url);
        }, Qt::QueuedConnection);
        return;
    }

    g_browser = std::make_unique<QtWebEngineBrowser>();
    std::string url = initial_url.empty()
        ? "data:text/html,<html><body><h1>QtWebEngine 8c smoke</h1>"
          "<p style=\"font-size:48px\">capture pipeline live</p></body></html>"
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
