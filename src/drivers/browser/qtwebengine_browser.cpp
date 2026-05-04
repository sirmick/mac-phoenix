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
#include <QEvent>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QWheelEvent>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace browser {

namespace {

// Push a BR_EV_STATUS event into the h2g ring. Guest's URL bar loading
// indicator clears on STATUS_READY/ERROR. Mirrors send_status() in
// cmd.cpp (also a 250-byte URL cap to fit a single ring message).
void send_status(uint8_t code, const std::string& url)
{
    uint8_t buf[2 + 250];
    buf[0] = code;
    size_t n = std::min(url.size(), (size_t)250);
    buf[1] = (uint8_t)n;
    if (n) memcpy(buf + 2, url.data(), n);
    send_event(BR_EV_STATUS, buf, (uint16_t)(2 + n));
}

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
        std::string url = raw->url().toString().toStdString();
        fprintf(stderr, "[QtWebEngine] loadStarted url=%s\n", url.c_str());
        send_status(BR_STATUS_LOADING, url);
    });
    QObject::connect(page, &QWebEnginePage::loadFinished, raw, [raw](bool ok) {
        std::string url = raw->url().toString().toStdString();
        fprintf(stderr, "[QtWebEngine] loadFinished ok=%d url=%s\n",
                ok ? 1 : 0, url.c_str());
        send_status(ok ? BR_STATUS_READY : BR_STATUS_ERROR, url);
    });
    QObject::connect(page, &QWebEnginePage::urlChanged, raw, [](const QUrl& url) {
        fprintf(stderr, "[QtWebEngine] urlChanged %s\n",
                url.toString().toUtf8().constData());
        // urlChanged fires for in-page anchors and history.pushState too;
        // the guest URL bar is driven from loadStarted/loadFinished, so
        // just log here. If the URL bar starts looking stale we can also
        // emit a STATUS_READY on urlChanged after the initial load — for
        // now, the BiDi-side parity wins.
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

    // 4 Hz page-metrics poll. Same cadence and JS as the BiDi mouse_poll
    // path. Only re-emits BR_EV_PAGE_METRICS when at least one of the six
    // values changed, so a static page produces no ring traffic.
    metrics_timer_ = std::make_unique<QTimer>();
    metrics_timer_->setInterval(250);
    QObject::connect(metrics_timer_.get(), &QTimer::timeout, view_.get(),
                     [this]() { metrics_tick(); });
    metrics_timer_->start();
}

QtWebEngineBrowser::~QtWebEngineBrowser() = default;

void QtWebEngineBrowser::load(const std::string& url)
{
    fprintf(stderr, "[QtWebEngine] load %s\n", url.c_str());
    view_->setUrl(QUrl(QString::fromStdString(url)));
}

void QtWebEngineBrowser::metrics_tick()
{
    if (!view_) return;
    QWebEnginePage* page = view_->page();
    if (!page) return;
    // Six numbers as a JSON-serializable array. Mirrors mouse_poll.cpp's
    // BiDi script. document.scrollingElement is null only during very
    // early page load; null-guard so the timer doesn't spam errors.
    static const char* kJs =
        "(()=>{const e=document.scrollingElement"
        "||document.documentElement||document.body;"
        "if(!e)return [0,0,0,0,0,0];"
        "return [e.scrollWidth|0, e.scrollHeight|0, "
        "(window.scrollX|0), (window.scrollY|0), "
        "(window.innerWidth|0), (window.innerHeight|0)];})()";
    page->runJavaScript(QString::fromLatin1(kJs),
        [this](const QVariant& result) {
            QVariantList arr = result.toList();
            if (arr.size() < 6) return;
            uint32_t pw = arr.at(0).toUInt();
            uint32_t ph = arr.at(1).toUInt();
            uint32_t sx = arr.at(2).toUInt();
            uint32_t sy = arr.at(3).toUInt();
            uint32_t vw = arr.at(4).toUInt();
            uint32_t vh = arr.at(5).toUInt();
            if (pw == last_pw_ && ph == last_ph_ &&
                sx == last_sx_ && sy == last_sy_ &&
                vw == last_vw_ && vh == last_vh_) return;
            last_pw_ = pw; last_ph_ = ph;
            last_sx_ = sx; last_sy_ = sy;
            last_vw_ = vw; last_vh_ = vh;
            uint8_t buf[24];
            auto put_be32 = [](uint8_t* p, uint32_t v) {
                p[0] = (uint8_t)(v >> 24);
                p[1] = (uint8_t)(v >> 16);
                p[2] = (uint8_t)(v >>  8);
                p[3] = (uint8_t)(v);
            };
            put_be32(buf +  0, pw);
            put_be32(buf +  4, ph);
            put_be32(buf +  8, sx);
            put_be32(buf + 12, sy);
            put_be32(buf + 16, vw);
            put_be32(buf + 20, vh);
            send_event(BR_EV_PAGE_METRICS, buf, 24);
        });
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

// Mac VK → Qt::Key mapping. Keys not in this table fall back to the text
// content of the BR_CMD_KEY_DOWN payload (Mac KCHR-translated UTF-8) — see
// dispatch_key_down for the routing logic. Mirrors mac_vk_to_w3c_key in
// cmd.cpp (which targets W3C WebDriver private-use codepoints).
Qt::Key mac_vk_to_qt_key(uint16_t vk)
{
    switch (vk) {
    case 0x33: return Qt::Key_Backspace;     // Mac "Delete" = Backspace
    case 0x75: return Qt::Key_Delete;        // Forward Delete
    case 0x24: return Qt::Key_Return;
    case 0x4C: return Qt::Key_Enter;         // Numpad enter
    case 0x30: return Qt::Key_Tab;
    case 0x35: return Qt::Key_Escape;
    case 0x7B: return Qt::Key_Left;
    case 0x7C: return Qt::Key_Right;
    case 0x7E: return Qt::Key_Up;
    case 0x7D: return Qt::Key_Down;
    case 0x73: return Qt::Key_Home;
    case 0x77: return Qt::Key_End;
    case 0x74: return Qt::Key_PageUp;
    case 0x79: return Qt::Key_PageDown;
    case 0x7A: return Qt::Key_F1;
    case 0x78: return Qt::Key_F2;
    case 0x63: return Qt::Key_F3;
    case 0x76: return Qt::Key_F4;
    default:   return Qt::Key_unknown;
    }
}

// Mac modifier bits → Qt::KeyboardModifiers. Only shift currently flows
// through to the page (cmd is intercepted by the Mac menu bar; option
// produces pre-cooked text). Mirrors cmd.cpp's kMacShift comment.
Qt::KeyboardModifiers mac_mods_to_qt(uint16_t mods)
{
    constexpr uint16_t kMacShift = 0x0200;
    Qt::KeyboardModifiers out = Qt::NoModifier;
    if (mods & kMacShift) out |= Qt::ShiftModifier;
    return out;
}

// Map BR_CMD_CLICK button code (0=left, 1=middle, 2=right) to Qt enum.
Qt::MouseButton br_button_to_qt(uint8_t btn)
{
    switch (btn) {
    case 0:  return Qt::LeftButton;
    case 1:  return Qt::MiddleButton;
    case 2:  return Qt::RightButton;
    default: return Qt::LeftButton;
    }
}

}  // namespace

void QtWebEngineBrowser::dispatch_click(int x, int y, int button, int count)
{
    if (!view_) return;
    QPointF pt(x, y);
    Qt::MouseButton qbtn = br_button_to_qt((uint8_t)button);
    int n = count > 0 ? count : 1;
    for (int i = 0; i < n; i++) {
        // Press → Release pair per click. Qt detects double-click from
        // timing of consecutive press events on the same widget.
        QApplication::postEvent(view_.get(), new QMouseEvent(
            QEvent::MouseButtonPress, pt, pt,
            qbtn, qbtn, Qt::NoModifier));
        QApplication::postEvent(view_.get(), new QMouseEvent(
            QEvent::MouseButtonRelease, pt, pt,
            qbtn, Qt::NoButton, Qt::NoModifier));
    }
}

void QtWebEngineBrowser::dispatch_mouse_move(int x, int y)
{
    if (!view_) return;
    QPointF pt(x, y);
    QApplication::postEvent(view_.get(), new QMouseEvent(
        QEvent::MouseMove, pt, pt,
        Qt::NoButton, Qt::NoButton, Qt::NoModifier));
}

void QtWebEngineBrowser::dispatch_mouse_out()
{
    // No direct Qt event for "pointer left widget" without showing a
    // widget — best effort is to move pointer well off any visible
    // page region, matching the BiDi mouse_out(-1, -1) behavior.
    dispatch_mouse_move(-1, -1);
}

void QtWebEngineBrowser::dispatch_key_down(uint16_t vk, uint16_t mods,
                                           const std::string& text)
{
    if (!view_) return;
    Qt::KeyboardModifiers qmods = mac_mods_to_qt(mods);
    Qt::Key qk = mac_vk_to_qt_key(vk);

    QString qtext;
    int qkey = (int)qk;
    if (qk == Qt::Key_unknown) {
        // Printable character path: the Mac sends the post-KCHR UTF-8
        // text. Use the first character's code as Qt::Key (sufficient
        // for ASCII; Qt::Key just identifies the key, the actual text
        // comes via QKeyEvent's text parameter).
        if (text.empty()) return;
        qtext = QString::fromUtf8(text.data(), (int)text.size());
        if (qtext.isEmpty()) return;
        qkey = qtext.at(0).unicode();
    }

    // Press, then immediately Release. Mac's BR_CMD_KEY_UP is currently
    // a no-op (the BiDi path comment notes per-character keyDown+keyUp
    // is already done in BR_CMD_KEY_DOWN); replicate that here so the
    // page sees a complete key event.
    QApplication::postEvent(view_.get(), new QKeyEvent(
        QEvent::KeyPress, qkey, qmods, qtext));
    QApplication::postEvent(view_.get(), new QKeyEvent(
        QEvent::KeyRelease, qkey, qmods, qtext));
}

void QtWebEngineBrowser::dispatch_key_up(uint16_t /*vk*/)
{
    // Per dispatch_key_down: KEY_DOWN already emits both press + release.
    // Standalone KEY_UP from the guest is a no-op, matching the BiDi path.
}

void QtWebEngineBrowser::dispatch_nav(const std::string& url)
{
    if (!view_) return;
    fprintf(stderr, "[QtWebEngine] nav %s\n", url.c_str());
    view_->setUrl(QUrl(QString::fromStdString(url)));
}

void QtWebEngineBrowser::dispatch_reload()
{
    if (!view_) return;
    view_->page()->triggerAction(QWebEnginePage::Reload);
}

void QtWebEngineBrowser::dispatch_back()
{
    if (!view_) return;
    view_->page()->triggerAction(QWebEnginePage::Back);
}

void QtWebEngineBrowser::dispatch_forward()
{
    if (!view_) return;
    view_->page()->triggerAction(QWebEnginePage::Forward);
}

void QtWebEngineBrowser::dispatch_stop()
{
    if (!view_) return;
    view_->page()->triggerAction(QWebEnginePage::Stop);
}

void QtWebEngineBrowser::dispatch_get_selection()
{
    if (!view_) return;
    QWebEnginePage* page = view_->page();
    if (!page) return;
    page->runJavaScript(
        "(window.getSelection&&window.getSelection().toString())||''",
        [](const QVariant& result) {
            std::string text = result.toString().toStdString();
            uint16_t n = (uint16_t)std::min(text.size(), (size_t)4096);
            std::vector<uint8_t> buf(2 + n);
            buf[0] = (uint8_t)(n >> 8);
            buf[1] = (uint8_t)(n & 0xFF);
            if (n) memcpy(buf.data() + 2, text.data(), n);
            send_event(BR_EV_SELECTION, buf.data(), (uint16_t)buf.size());
        });
}

void QtWebEngineBrowser::dispatch_select_all()
{
    if (!view_) return;
    // QWebEnginePage::SelectAll handles the focused element correctly
    // for inputs, textareas, contentEditable, and the document itself.
    view_->page()->triggerAction(QWebEnginePage::SelectAll);
}

void QtWebEngineBrowser::dispatch_paste(const std::string& text)
{
    if (!view_) return;
    // Replicates the BiDi-path JS in cmd.cpp's BR_CMD_PASTE: handle
    // <input>/<textarea> via setRangeText (fires input/change events),
    // contentEditable via execCommand('insertText'), no-op otherwise.
    // Going via JS instead of QWebEnginePage::Paste because the latter
    // pulls from the Qt clipboard — we'd need a separate sync from
    // the Mac scrap, and a JS injection is simpler.
    QByteArray wrapped = QJsonDocument(QJsonArray{
        QJsonValue(QString::fromStdString(text))
    }).toJson(QJsonDocument::Compact);
    std::string lit(wrapped.constData() + 1, wrapped.size() - 2);
    std::string js =
        "(()=>{const t=" + lit + ";"
        "const el=document.activeElement;"
        "if(!el)return false;"
        "const tag=(el.tagName||'').toLowerCase();"
        "if(tag==='input'||tag==='textarea'){"
        "  const s=el.selectionStart||0,e=el.selectionEnd||0;"
        "  if(typeof el.setRangeText==='function'){"
        "    el.setRangeText(t,s,e,'end');"
        "    el.dispatchEvent(new Event('input',{bubbles:true}));"
        "    el.dispatchEvent(new Event('change',{bubbles:true}));"
        "    return true;"
        "  }"
        "  el.value=el.value.slice(0,s)+t+el.value.slice(e);"
        "  el.selectionStart=el.selectionEnd=s+t.length;"
        "  el.dispatchEvent(new Event('input',{bubbles:true}));"
        "  return true;"
        "}"
        "if(el.isContentEditable){"
        "  document.execCommand('insertText',false,t);"
        "  return true;"
        "}"
        "return false;})()";
    view_->page()->runJavaScript(QString::fromStdString(js));
}

void QtWebEngineBrowser::dispatch_scroll(int dx, int dy)
{
    if (!view_) return;
    // Wheel events deliver pixelDelta (CSS pixels). dy positive = scroll
    // toward the top of the page; in BR_CMD_SCROLL the guest sends raw
    // dx/dy in CSS px so we forward unchanged. Origin = viewport center
    // (matches the BiDi path's hardcoded 320,240 from the original 640x480
    // viewport assumption — refine when the guest reports cursor pos).
    QPointF pos(view_->width() / 2, view_->height() / 2);
    QPoint pixelDelta(dx, dy);
    QPoint angleDelta(dx * 8, dy * 8);  // 1px ≈ 1/8 of a wheel notch
    QApplication::postEvent(view_.get(), new QWheelEvent(
        pos, view_->mapToGlobal(pos.toPoint()),
        pixelDelta, angleDelta,
        Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false));
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

namespace {

// Helper: returns the QApplication if active, else nullptr. Used by the
// dispatch wrappers to gate routing to the QtWebEngine path.
QApplication* qapp_if_active()
{
    return qobject_cast<QApplication*>(QCoreApplication::instance());
}

}  // namespace

bool qt_dispatch_click(int x, int y, int button, int count)
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, [x, y, button, count]() {
        if (g_browser) g_browser->dispatch_click(x, y, button, count);
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_mouse_move(int x, int y)
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, [x, y]() {
        if (g_browser) g_browser->dispatch_mouse_move(x, y);
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_mouse_out()
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, []() {
        if (g_browser) g_browser->dispatch_mouse_out();
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_key_down(uint16_t vk, uint16_t mods,
                          const uint8_t* text, uint8_t text_len)
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    std::string text_copy(reinterpret_cast<const char*>(text), text_len);
    QMetaObject::invokeMethod(app,
        [vk, mods, text_copy = std::move(text_copy)]() {
            if (g_browser) g_browser->dispatch_key_down(vk, mods, text_copy);
        }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_key_up(uint16_t vk)
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, [vk]() {
        if (g_browser) g_browser->dispatch_key_up(vk);
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_scroll(int dx, int dy)
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, [dx, dy]() {
        if (g_browser) g_browser->dispatch_scroll(dx, dy);
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_nav(const std::string& url)
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, [url]() {
        if (g_browser) g_browser->dispatch_nav(url);
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_reload()
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, []() {
        if (g_browser) g_browser->dispatch_reload();
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_back()
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, []() {
        if (g_browser) g_browser->dispatch_back();
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_forward()
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, []() {
        if (g_browser) g_browser->dispatch_forward();
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_stop()
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, []() {
        if (g_browser) g_browser->dispatch_stop();
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_get_selection()
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, []() {
        if (g_browser) g_browser->dispatch_get_selection();
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_select_all()
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, []() {
        if (g_browser) g_browser->dispatch_select_all();
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_paste(const std::string& text)
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, [text]() {
        if (g_browser) g_browser->dispatch_paste(text);
    }, Qt::QueuedConnection);
    return true;
}

}  // namespace browser
