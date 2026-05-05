/*
 *  qtwebengine_browser.cpp — see qtwebengine_browser.h.
 */
#include "qtwebengine_browser.h"

#include "cdp_client.h"
#include "cmd.h"
#include "shm.h"
#include "emulator_config.h"
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
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QWebEngineDownloadRequest>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
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
// indicator clears on STATUS_READY/ERROR. 250-byte URL cap so the
// payload fits a single ring message comfortably.
void send_status(uint8_t code, const std::string& url)
{
    uint8_t buf[2 + 250];
    buf[0] = code;
    size_t n = std::min(url.size(), (size_t)250);
    buf[1] = (uint8_t)n;
    if (n) memcpy(buf + 2, url.data(), n);
    send_event(BR_EV_STATUS, buf, (uint16_t)(2 + n));
}

// Push BR_EV_TITLE — utf8 title, capped to 250 bytes (single message).
void send_title(const std::string& title)
{
    uint8_t buf[1 + 250];
    size_t n = std::min(title.size(), (size_t)250);
    buf[0] = (uint8_t)n;
    if (n) memcpy(buf + 1, title.data(), n);
    send_event(BR_EV_TITLE, buf, (uint16_t)(1 + n));
}

// Push BR_EV_HISTORY — two booleans for Back/Forward enable.
void send_history(bool can_back, bool can_forward)
{
    uint8_t buf[2];
    buf[0] = can_back     ? 1 : 0;
    buf[1] = can_forward  ? 1 : 0;
    send_event(BR_EV_HISTORY, buf, sizeof(buf));
}

// Push BR_EV_ZOOM — u16 percent (BE).
void send_zoom(uint16_t zoom_pct)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(zoom_pct >> 8);
    buf[1] = (uint8_t)(zoom_pct & 0xFF);
    send_event(BR_EV_ZOOM, buf, sizeof(buf));
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

// Push one captured QImage frame into BrowserShm.fb. Single fused pass:
// each pixel is converted ARGB32 → RGB555 and compared against the host's
// last-published value already in fb.pixels. Dirty pixels mark a 16×16
// tile; tiles coalesce into rectangles via run-detection per tile-row +
// vertical extension.
//
// Why tiles vs the earlier per-row {xmin,xmax}: per-row coalescing
// produced a single bbox-union rect when contiguous rows were dirty —
// fine for one cursor blink, terrible when two animations fired at
// opposite ends of the viewport (rect spanned the full width). Tiles
// give us per-region resolution: each connected blob of changed pixels
// becomes its own rect.
//
// Tradeoffs:
//   - Tile granularity rounds up: a 1-pixel change marks a 16×16 tile.
//     For typical web content (cursor blink, hover, scroll) the rounding
//     is in the noise; the savings from finer x-granularity dominate.
//   - Worst case rect count grows with screen size (1024×768 → 64×48 =
//     3072 tiles). BR_DIRTY_MAX=64 caps emitted rects; overflow extends
//     the last rect's bbox, same as the row-coalescer did.
//
// Returns the {crop_w, crop_h} actually published (zero if no shm/viewport).
struct CropDims { int w; int h; };

constexpr int kTileShift = 4;          // 16×16 tiles
constexpr int kTileSize  = 1 << kTileShift;
constexpr int kMaxTilesX = (BR_FB_MAX_W + kTileSize - 1) >> kTileShift;
constexpr int kMaxTilesY = (BR_FB_MAX_H + kTileSize - 1) >> kTileShift;

// Force-dirty state — when crop_w/h changes (BR_CMD_RESIZE), the existing
// fb.pixels content is at the OLD geometry, so per-pixel diffs against it
// are meaningless. First publish post-resize writes everything.
int s_last_crop_w = 0, s_last_crop_h = 0;

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

    const bool force_full = (crop_w != s_last_crop_w ||
                             crop_h != s_last_crop_h);
    s_last_crop_w = crop_w;
    s_last_crop_h = crop_h;

    const int tiles_x = (w + kTileSize - 1) >> kTileShift;
    const int tiles_y = (h + kTileSize - 1) >> kTileShift;

    // Per-tile dirty bitmap (1 byte per tile — fast, no bit-fiddling).
    // Stack-allocated: 64×48 = 3072 B at 1024×768 ceiling.
    uint8_t tile_dirty[kMaxTilesY][kMaxTilesX];
    std::memset(tile_dirty, 0, (size_t)tiles_y * kMaxTilesX);

    auto t1 = std::chrono::steady_clock::now();
    const int dst_stride = crop_w * 2;
    uint64_t pixels_changed = 0;

    for (int y = 0; y < h; y++) {
        const uint32_t* src_row = (const uint32_t*)img.scanLine(y);
        uint16_t* dst_row =
            (uint16_t*)&shm->fb.pixels[(size_t)y * dst_stride];
        uint8_t* tile_row = tile_dirty[y >> kTileShift];

        if (force_full) {
            for (int x = 0; x < w; x++) {
                dst_row[x] = argb32_to_rgb555_be(src_row[x]);
            }
            // Mark every tile in this tile-row dirty.
            for (int tx = 0; tx < tiles_x; tx++) tile_row[tx] = 1;
            pixels_changed += (uint64_t)w;
        } else {
            // Hoist the redundant tile-mark check out of the inner loop:
            // track the last tile we already marked, only touch the
            // bitmap on tile boundaries.
            int last_tx = -1;
            int run_pixels = 0;
            for (int x = 0; x < w; x++) {
                uint16_t pix = argb32_to_rgb555_be(src_row[x]);
                if (pix != dst_row[x]) {
                    dst_row[x] = pix;
                    int tx = x >> kTileShift;
                    if (tx != last_tx) {
                        tile_row[tx] = 1;
                        last_tx = tx;
                    }
                    run_pixels++;
                }
            }
            pixels_changed += (uint64_t)run_pixels;
        }
    }
    g_perf.blit_us += us_since(t1);

    // Build rectangles from the tile bitmap.
    //
    //   Phase 1: walk top→bottom, for each tile-row find contiguous
    //     runs of dirty tiles. Each run is a candidate rect 16 px tall.
    //   Phase 2: extend a rect downward if the next tile-row has an
    //     identical run (same xmin, xmax). Otherwise close the rect and
    //     open a new one for the new run.
    //
    // This is the standard "scanline → rectangle" decomposition. For
    // the common case of one connected blob (cursor, hover) the result
    // is a single tight rect. For multiple disjoint blobs at the same
    // height it produces one rect per blob — the win this whole change
    // is about.
    BrRect rects[BR_DIRTY_MAX];
    int n_rects = 0;

    // Open rects (matched in the previous tile-row but not yet closed).
    struct Open { int top_ty, xmin_tx, xmax_tx; };
    Open open[BR_DIRTY_MAX];
    int n_open = 0;

    auto emit_rect = [&](int top_ty, int bottom_ty, int xmin_tx, int xmax_tx) {
        int top    = top_ty * kTileSize;
        int bottom = std::min(bottom_ty * kTileSize, crop_h);
        int left   = xmin_tx * kTileSize;
        int right  = std::min((xmax_tx + 1) * kTileSize, crop_w);
        if (n_rects < (int)BR_DIRTY_MAX) {
            BrRect& r = rects[n_rects++];
            r.top = (int16_t)top;
            r.left = (int16_t)left;
            r.bottom = (int16_t)bottom;
            r.right = (int16_t)right;
        } else {
            // Overflow — extend last rect's bbox (mirrors the row coalescer).
            BrRect& r = rects[BR_DIRTY_MAX - 1];
            if (top    < r.top)    r.top    = (int16_t)top;
            if (bottom > r.bottom) r.bottom = (int16_t)bottom;
            if (left   < r.left)   r.left   = (int16_t)left;
            if (right  > r.right)  r.right  = (int16_t)right;
        }
    };

    for (int ty = 0; ty < tiles_y; ty++) {
        // Find runs in this tile-row.
        struct Run { int xmin_tx, xmax_tx; };
        Run cur[kMaxTilesX];
        int n_cur = 0;
        for (int tx = 0; tx < tiles_x; ) {
            if (!tile_dirty[ty][tx]) { tx++; continue; }
            int start = tx;
            while (tx < tiles_x && tile_dirty[ty][tx]) tx++;
            cur[n_cur++] = {start, tx - 1};
        }

        // Match cur runs against open rects. open[]/cur[] are both
        // sorted left-to-right, so a single linear walk suffices.
        Open next_open[BR_DIRTY_MAX];
        int n_next = 0;
        int oi = 0, ci = 0;
        while (oi < n_open && ci < n_cur) {
            if (open[oi].xmin_tx == cur[ci].xmin_tx &&
                open[oi].xmax_tx == cur[ci].xmax_tx) {
                // Exact match — extend down.
                next_open[n_next++] = open[oi];
                oi++; ci++;
            } else if (open[oi].xmin_tx < cur[ci].xmin_tx) {
                // open[oi] has no matching run → close it.
                emit_rect(open[oi].top_ty, ty,
                          open[oi].xmin_tx, open[oi].xmax_tx);
                oi++;
            } else {
                // cur[ci] is a fresh run.
                if (n_next < (int)BR_DIRTY_MAX) {
                    next_open[n_next++] = {ty, cur[ci].xmin_tx, cur[ci].xmax_tx};
                }
                ci++;
            }
        }
        while (oi < n_open) {
            emit_rect(open[oi].top_ty, ty,
                      open[oi].xmin_tx, open[oi].xmax_tx);
            oi++;
        }
        while (ci < n_cur) {
            if (n_next < (int)BR_DIRTY_MAX) {
                next_open[n_next++] = {ty, cur[ci].xmin_tx, cur[ci].xmax_tx};
            }
            ci++;
        }
        std::memcpy(open, next_open, (size_t)n_next * sizeof(Open));
        n_open = n_next;
    }
    // Close any rects still open at the bottom.
    for (int i = 0; i < n_open; i++) {
        emit_rect(open[i].top_ty, tiles_y,
                  open[i].xmin_tx, open[i].xmax_tx);
    }

    if (n_rects == 0) {
        // Static frame — nothing changed. Don't bump seq → guest sees
        // no new frame → zero CopyBits cost.
        return {crop_w, crop_h};
    }

    br_u16_store(&shm->fb.dirty_count, (uint16_t)n_rects);
    for (int i = 0; i < n_rects; i++) {
        br_u16_store((uint16_t*)&shm->fb.dirty[i].top,    rects[i].top);
        br_u16_store((uint16_t*)&shm->fb.dirty[i].left,   rects[i].left);
        br_u16_store((uint16_t*)&shm->fb.dirty[i].bottom, rects[i].bottom);
        br_u16_store((uint16_t*)&shm->fb.dirty[i].right,  rects[i].right);
    }
    BR_FENCE_RELEASE();
    br_u32_store(&shm->fb.seq, ++g_frame_seq);

    g_perf.pixels_out += pixels_changed;
    return {crop_w, crop_h};
}

}  // namespace

QtWebEngineBrowser::QtWebEngineBrowser()
{
    view_ = std::make_unique<QWebEngineView>();
    view_->resize(1024, 768);
    // show() is required for Chromium's compositor to paint into the
    // backing store grab() reads from. WA_DontShowOnScreen suppresses
    // the paint entirely (grab returns all-white pixels). With
    // QT_QPA_PLATFORM=offscreen the show() is harmless — the offscreen
    // platform plugin gives us a backing store but never displays the
    // widget on a real screen.
    view_->show();

    QWebEnginePage* page = view_->page();
    QWebEngineView* raw = view_.get();

    QObject::connect(page, &QWebEnginePage::loadStarted, raw, [raw, this]() {
        std::string url = raw->url().toString().toStdString();
        fprintf(stderr, "[QtWebEngine] loadStarted url=%s\n", url.c_str());
        send_status(BR_STATUS_LOADING, url);
        publish_history();
    });
    QObject::connect(page, &QWebEnginePage::loadFinished, raw, [raw, this](bool ok) {
        std::string url = raw->url().toString().toStdString();
        fprintf(stderr, "[QtWebEngine] loadFinished ok=%d url=%s\n",
                ok ? 1 : 0, url.c_str());
        send_status(ok ? BR_STATUS_READY : BR_STATUS_ERROR, url);
        publish_history();
    });
    QObject::connect(page, &QWebEnginePage::urlChanged, raw,
                     [this](const QUrl& url) {
        fprintf(stderr, "[QtWebEngine] urlChanged %s\n",
                url.toString().toUtf8().constData());
        // Fires for in-page anchors and history.pushState too. Back /
        // Forward enable can flip without a load round-trip, so refresh
        // history here as well.
        publish_history();
    });
    QObject::connect(page, &QWebEnginePage::titleChanged, raw,
                     [this](const QString& title) {
        std::string utf8 = title.toStdString();
        if (utf8 == last_title_) return;
        last_title_ = utf8;
        fprintf(stderr, "[QtWebEngine] titleChanged \"%s\"\n", utf8.c_str());
        send_title(utf8);
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

    // Phase 8h: downloads. Hook the profile's downloadRequested signal
    // and route lifecycle to BR_EV_DOWNLOAD events. Destination matches
    // the supervisor.cpp convention: bridge_dir/Downloads (guest sees
    // it via ExtFS at Host:MacPhoenix:Downloads). Falls back to the
    // system Downloads dir if no bridge.
    QWebEngineProfile* profile = view_->page()->profile();
    QObject::connect(profile, &QWebEngineProfile::downloadRequested,
                     view_.get(),
                     [this](QWebEngineDownloadRequest* req) {
                         handle_download_request(req);
                     });

    // Spin up the CDP client. The Chromium DevTools server is up but
    // discovery + WS handshake takes ~200ms; the client connects in
    // the background and is_connected() flips when ready. Until then
    // dispatch_* falls back to QApplication::postEvent on focusProxy.
    cdp_ = std::make_unique<CdpClient>(12000);
    cdp_->start();

    // Worker thread that drains the g2h ring (guest → host commands)
    // and dispatches via cmd_dispatch — used to live in browser_spike,
    // which 8i deletes. Polls the guest debug log channel on the same
    // tick. Plain std::thread (no Qt event loop needed); cmd_dispatch
    // routes to qt_dispatch_*, which marshals to the GUI thread.
    g2h_running_.store(true, std::memory_order_release);
    g2h_thread_ = std::thread([this]() { g2h_drain_loop(); });
}

QtWebEngineBrowser::~QtWebEngineBrowser()
{
    g2h_running_.store(false, std::memory_order_release);
    if (g2h_thread_.joinable()) g2h_thread_.join();
}

void QtWebEngineBrowser::g2h_drain_loop()
{
    // Wait for the guest handshake before doing real work — Browser.app
    // doesn't NewPtrClear the BrowserShm region until well after Finder.
    // poll_log + cmd_dispatch are no-ops without shm anyway, but the
    // wait avoids unnecessary syscalls.
    while (g2h_running_.load(std::memory_order_acquire)) {
        if (browser::shm_get()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    while (g2h_running_.load(std::memory_order_acquire)) {
        uint16_t cmd_type = 0, cmd_len = 0;
        uint8_t  cmd_buf[1024];
        while (browser::read_command(&cmd_type, cmd_buf,
                                     sizeof(cmd_buf), &cmd_len)) {
            browser::cmd_dispatch(cmd_type, cmd_buf, cmd_len);
        }
        browser::poll_log();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void QtWebEngineBrowser::load(const std::string& url)
{
    fprintf(stderr, "[QtWebEngine] load %s\n", url.c_str());
    view_->setUrl(QUrl(QString::fromStdString(url)));
}

namespace {

// Pack one BR_EV_DOWNLOAD payload and emit. Layout (matches MacBrowser.h):
//   u32 guid, u8 state, u32 bytes, u32 total, u8 plen, u8 path[plen]
void send_download(uint32_t guid, uint8_t state,
                   uint64_t bytes, uint64_t total,
                   const std::string& path)
{
    uint8_t buf[1 + 4 + 4 + 4 + 1 + 250];
    auto put_be32 = [](uint8_t* p, uint32_t v) {
        p[0] = (uint8_t)(v >> 24);
        p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >>  8);
        p[3] = (uint8_t)(v);
    };
    put_be32(buf + 0, guid);
    buf[4] = state;
    put_be32(buf + 5, (uint32_t)std::min(bytes, (uint64_t)0xFFFFFFFFu));
    put_be32(buf + 9, (uint32_t)std::min(total, (uint64_t)0xFFFFFFFFu));
    size_t n = std::min(path.size(), (size_t)250);
    buf[13] = (uint8_t)n;
    if (n) memcpy(buf + 14, path.data(), n);
    send_event(BR_EV_DOWNLOAD, buf, (uint16_t)(14 + n));
}

}  // namespace

void QtWebEngineBrowser::handle_download_request(QWebEngineDownloadRequest* req)
{
    if (!req) return;

    // Resolve destination directory. Bridge dir wins (guest already
    // sees it via ExtFS at Host:MacPhoenix:Downloads); else system
    // Downloads dir. Either way mkdir -p so accept() doesn't fail.
    QString dl_dir;
    {
        const auto& cfg = config::EmulatorConfig::instance();
        if (!cfg.bridge_dir.empty()) {
            dl_dir = QString::fromStdString(cfg.bridge_dir) + "/Downloads";
        } else {
            dl_dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        }
    }
    QDir().mkpath(dl_dir);

    // Pick a filename. QWebEngineDownloadRequest gives us a suggested
    // name; the host directory is ours. Avoid collision by appending
    // a counter if the chosen path exists already.
    QString fn = req->suggestedFileName();
    if (fn.isEmpty()) fn = "download";
    QString path = dl_dir + "/" + fn;
    {
        QFileInfo info(path);
        QString stem = info.completeBaseName();
        QString ext = info.suffix();
        for (int i = 1; QFileInfo::exists(path); i++) {
            path = dl_dir + "/" + stem + QString("-%1").arg(i)
                 + (ext.isEmpty() ? QString() : ("." + ext));
        }
    }
    req->setDownloadDirectory(dl_dir);
    req->setDownloadFileName(QFileInfo(path).fileName());

    // Mint a guid. Per-process counter; fits in u32 for a long time.
    static std::atomic<uint32_t> s_next_guid{1};
    uint32_t guid = s_next_guid.fetch_add(1, std::memory_order_relaxed);

    fprintf(stderr, "[QtWebEngine] download begin guid=%u → %s\n",
            guid, path.toUtf8().constData());
    send_download(guid, BR_DL_BEGIN, 0,
                  (uint64_t)req->totalBytes(), path.toStdString());

    QObject::connect(req, &QWebEngineDownloadRequest::receivedBytesChanged,
                     view_.get(),
                     [guid, req, path]() {
                         send_download(guid, BR_DL_PROGRESS,
                                       (uint64_t)req->receivedBytes(),
                                       (uint64_t)req->totalBytes(),
                                       path.toStdString());
                     });
    QObject::connect(req, &QWebEngineDownloadRequest::isFinishedChanged,
                     view_.get(),
                     [guid, req, path]() {
                         if (!req->isFinished()) return;
                         uint8_t state = BR_DL_DONE;
                         switch (req->state()) {
                             case QWebEngineDownloadRequest::DownloadCompleted:
                                 state = BR_DL_DONE; break;
                             case QWebEngineDownloadRequest::DownloadCancelled:
                                 state = BR_DL_CANCELED; break;
                             case QWebEngineDownloadRequest::DownloadInterrupted:
                                 state = BR_DL_ERROR; break;
                             default: state = BR_DL_DONE; break;
                         }
                         fprintf(stderr, "[QtWebEngine] download finished "
                                 "guid=%u state=%u\n", guid, state);
                         send_download(guid, state,
                                       (uint64_t)req->receivedBytes(),
                                       (uint64_t)req->totalBytes(),
                                       path.toStdString());
                     });

    req->accept();
}

void QtWebEngineBrowser::publish_history()
{
    if (!view_) return;
    QWebEngineHistory* hist = view_->page()->history();
    bool can_back    = hist && hist->canGoBack();
    bool can_forward = hist && hist->canGoForward();
    int8_t b = can_back    ? 1 : 0;
    int8_t f = can_forward ? 1 : 0;
    if (b == last_can_back_ && f == last_can_forward_) return;
    last_can_back_    = b;
    last_can_forward_ = f;
    fprintf(stderr, "[QtWebEngine] history back=%d fwd=%d\n", (int)b, (int)f);
    send_history(can_back, can_forward);
}

void QtWebEngineBrowser::publish_zoom()
{
    if (!view_) return;
    int pct = (int)(view_->zoomFactor() * 100.0 + 0.5);
    if (pct < 0) pct = 0;
    if (pct > 65535) pct = 65535;
    if ((int16_t)pct == last_zoom_pct_) return;
    last_zoom_pct_ = (int16_t)pct;
    fprintf(stderr, "[QtWebEngine] zoom → %d%%\n", pct);
    send_zoom((uint16_t)pct);
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

// Mac VK → CDP key descriptor (W3C `key` + `code` strings + Windows
// virtual key code). Returns true for keys we handle as "special";
// false means "treat the BR_CMD_KEY_DOWN text payload as the
// character to type via Input.dispatchKeyEvent type=char".
struct CdpKey {
    const char* key;
    const char* code;
    int         windows_vk;
};

bool mac_vk_to_cdp(uint16_t vk, CdpKey* out)
{
    switch (vk) {
    case 0x33: *out = {"Backspace",  "Backspace",   8}; return true;
    case 0x75: *out = {"Delete",     "Delete",     46}; return true;
    case 0x24: *out = {"Enter",      "Enter",      13}; return true;
    case 0x4C: *out = {"Enter",      "NumpadEnter",13}; return true;
    case 0x30: *out = {"Tab",        "Tab",         9}; return true;
    case 0x35: *out = {"Escape",     "Escape",     27}; return true;
    case 0x7B: *out = {"ArrowLeft",  "ArrowLeft",  37}; return true;
    case 0x7E: *out = {"ArrowUp",    "ArrowUp",    38}; return true;
    case 0x7C: *out = {"ArrowRight", "ArrowRight", 39}; return true;
    case 0x7D: *out = {"ArrowDown",  "ArrowDown",  40}; return true;
    case 0x73: *out = {"Home",       "Home",       36}; return true;
    case 0x77: *out = {"End",        "End",        35}; return true;
    case 0x74: *out = {"PageUp",     "PageUp",     33}; return true;
    case 0x79: *out = {"PageDown",   "PageDown",   34}; return true;
    case 0x7A: *out = {"F1", "F1", 112}; return true;
    case 0x78: *out = {"F2", "F2", 113}; return true;
    case 0x63: *out = {"F3", "F3", 114}; return true;
    case 0x76: *out = {"F4", "F4", 115}; return true;
    default: return false;
    }
}

// Mac modifier bits → CDP modifier bitfield.
//   CDP: 1=Alt, 2=Ctrl, 4=Meta, 8=Shift
//   Mac: shiftKey=0x0200, optionKey=0x0800, controlKey=0x1000, cmdKey=0x0100
int mac_mods_to_cdp(uint16_t mods)
{
    int out = 0;
    if (mods & 0x0200) out |= 8;  // Shift
    if (mods & 0x0800) out |= 1;  // Option → Alt
    if (mods & 0x1000) out |= 2;  // Control
    if (mods & 0x0100) out |= 4;  // Cmd → Meta
    return out;
}

}  // namespace

namespace {
const char* cdp_button_name(uint8_t btn)
{
    switch (btn) {
    case 0:  return "left";
    case 1:  return "middle";
    case 2:  return "right";
    default: return "left";
    }
}
}  // namespace

void QtWebEngineBrowser::dispatch_click(int x, int y, int button, int count)
{
    const std::string btn = cdp_button_name((uint8_t)button);
    int n = count > 0 ? count : 1;
    for (int i = 0; i < n; i++) {
        cdp_->mouse_event("mousePressed",  x, y, btn, i + 1, 0);
        cdp_->mouse_event("mouseReleased", x, y, btn, i + 1, 0);
    }
}

void QtWebEngineBrowser::dispatch_mouse_move(int x, int y)
{
    cdp_->mouse_event("mouseMoved", x, y, "none", 0, 0);
}

void QtWebEngineBrowser::dispatch_mouse_out()
{
    // CDP has no "pointer left page" event; mirror the legacy
    // behavior of moving the pointer well off any visible region.
    cdp_->mouse_event("mouseMoved", -1, -1, "none", 0, 0);
}

void QtWebEngineBrowser::dispatch_key_down(uint16_t vk, uint16_t mods,
                                           const std::string& text)
{
    int cdp_mods = mac_mods_to_cdp(mods);
    CdpKey k;
    if (mac_vk_to_cdp(vk, &k)) {
        cdp_->key_event("keyDown", k.windows_vk, k.key, k.code, "", cdp_mods);
        cdp_->key_event("keyUp",   k.windows_vk, k.key, k.code, "", cdp_mods);
    } else if (!text.empty()) {
        // Printable: type=char inserts the cooked Unicode text
        // (post-KCHR), fires a real `input` event, and works for
        // <input>, <textarea>, contenteditable, plus IME-aware pages.
        cdp_->type_text(text);
    }
}

void QtWebEngineBrowser::dispatch_key_up(uint16_t /*vk*/)
{
    // dispatch_key_down already emits both press + release in CDP.
    // Standalone KEY_UP from the guest is a no-op.
}

void QtWebEngineBrowser::dispatch_nav(const std::string& url)
{
    fprintf(stderr, "[QtWebEngine] nav %s\n", url.c_str());
    cdp_->navigate(url);
}

void QtWebEngineBrowser::dispatch_reload()
{
    fprintf(stderr, "[QtWebEngine] reload\n");
    cdp_->reload();
}

void QtWebEngineBrowser::dispatch_back()
{
    fprintf(stderr, "[QtWebEngine] back\n");
    cdp_->back();
}

void QtWebEngineBrowser::dispatch_forward()
{
    fprintf(stderr, "[QtWebEngine] forward\n");
    cdp_->forward();
}

void QtWebEngineBrowser::dispatch_stop()
{
    fprintf(stderr, "[QtWebEngine] stop\n");
    cdp_->stop();
}

namespace {
// Same step table cmd.cpp uses for BiDi page zoom — keep parity so a
// session that toggles between backends doesn't see jarring zoom jumps.
constexpr double kZoomSteps[]   = { 0.5, 0.67, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0 };
constexpr int    kZoomStepCount = sizeof(kZoomSteps) / sizeof(kZoomSteps[0]);
constexpr int    kZoomStep100   = 4;
}  // namespace

void QtWebEngineBrowser::dispatch_zoom_in()
{
    if (!view_) return;
    if (zoom_step_ < kZoomStepCount - 1) zoom_step_++;
    view_->setZoomFactor(kZoomSteps[zoom_step_]);
    publish_zoom();
}

void QtWebEngineBrowser::dispatch_zoom_out()
{
    if (!view_) return;
    if (zoom_step_ > 0) zoom_step_--;
    view_->setZoomFactor(kZoomSteps[zoom_step_]);
    publish_zoom();
}

void QtWebEngineBrowser::dispatch_zoom_reset()
{
    if (!view_) return;
    zoom_step_ = kZoomStep100;
    view_->setZoomFactor(kZoomSteps[zoom_step_]);
    publish_zoom();
}

void QtWebEngineBrowser::dispatch_resize(uint16_t w, uint16_t h)
{
    if (!view_) return;
    view_->resize(w, h);
    // Publish the new dims so the capture pipeline + guest both pick
    // them up. The legacy BiDi path also updates fb.width/height here;
    // mirror that so the Mac CopyBits sees the new viewport on the
    // next BR_EV_FRAME.
    if (BrowserShm* s = browser::shm_get()) {
        br_u16_store(&s->fb.width,  w);
        br_u16_store(&s->fb.height, h);
    }
    fprintf(stderr, "[QtWebEngine] resize → %ux%u\n", w, h);
}

void QtWebEngineBrowser::dispatch_get_selection()
{
    // CDP Runtime.evaluate. Reply lands in the WS I/O thread; send_event
    // is safe from any thread (br_ring_push is SPSC and we're the sole
    // producer for h2g). The callback is set once here per call —
    // serialized by the CdpClient since only one Runtime.evaluate is
    // in flight per BR_CMD_GET_SELECTION.
    cdp_->set_selection_cb([](const std::string& text) {
        uint16_t n = (uint16_t)std::min(text.size(), (size_t)4096);
        std::vector<uint8_t> buf(2 + n);
        buf[0] = (uint8_t)(n >> 8);
        buf[1] = (uint8_t)(n & 0xFF);
        if (n) memcpy(buf.data() + 2, text.data(), n);
        send_event(BR_EV_SELECTION, buf.data(), (uint16_t)buf.size());
    });
    cdp_->get_selection();
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
    // BR_CMD_SCROLL exclusively comes from the Mac chrome scrollbars —
    // user-driven scrollbar arrow / page-area / thumb-drag clicks. The
    // intent is "scroll the document by N px"; window.scrollBy hits the
    // root scrolling element directly. (mouse_wheel would route to the
    // hovered element and miss when the cursor is over a scrollable
    // div or fixed-position widget.)
    cdp_->scroll_by(dx, dy);
}

namespace {

std::unique_ptr<QtWebEngineBrowser> g_browser;
std::thread                         g_qt_thread;
std::atomic<QApplication*>          g_app{nullptr};
std::atomic<bool>                   g_stopping{false};

void qt_thread_main(std::string initial_url)
{
    int argc = 1;
    char prog[] = "mac-phoenix-browser";
    char* argv[] = {prog, nullptr};
    QApplication app(argc, argv);
    g_app.store(&app, std::memory_order_release);

    fprintf(stderr,
            "[QtWebEngine] QApplication on tid=%lu (qpa=%s)\n"
            "[QtWebEngine] CHROMIUM_FLAGS=%s\n",
            (unsigned long)pthread_self(),
            qgetenv("QT_QPA_PLATFORM").constData(),
            qgetenv("QTWEBENGINE_CHROMIUM_FLAGS").constData());

    g_browser = std::make_unique<QtWebEngineBrowser>();
    // No initial load. Loading anything (even about:blank) puts it into
    // QWebEngineHistory, which then becomes the abort fallback: a Stop
    // mid-reload, or a failed reload, would silently navigate the page
    // BACK to about:blank, dropping whatever the user was actually
    // looking at. Letting history start empty means Reload/Stop on the
    // first real navigation are no-ops if nothing's loaded, and stay on
    // the current page once something is. The viewport renders a benign
    // empty surface until the guest's first BR_CMD_NAV.
    if (!initial_url.empty()) {
        g_browser->load(initial_url);
    }

    fprintf(stderr, "[QtWebEngine] entering event loop\n");
    app.exec();
    fprintf(stderr, "[QtWebEngine] event loop exited\n");

    g_browser.reset();
    g_app.store(nullptr, std::memory_order_release);
}

}  // namespace

void qtwebengine_module_start(const std::string& initial_url)
{
    if (g_qt_thread.joinable()) return;

    // Chromium flags + QPA + GL-context-sharing must be set BEFORE the
    // FIRST QCoreApplication ctor in this process. main.cpp set them
    // pre-fork when it saw --browser + --ipc; this block is a safety
    // net for direct callers (tests, future entry points).
    if (qgetenv("QTWEBENGINE_CHROMIUM_FLAGS").isEmpty()) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
                "--no-sandbox --disable-gpu-compositing "
                "--in-process-gpu --use-gl=swiftshader "
                "--remote-debugging-port=12000 "
                "--remote-debugging-address=127.0.0.1");
    }
    if (qgetenv("QSG_RHI_BACKEND").isEmpty())  qputenv("QSG_RHI_BACKEND",  "software");
    if (qgetenv("QT_QUICK_BACKEND").isEmpty()) qputenv("QT_QUICK_BACKEND", "software");
    if (qgetenv("DISPLAY").isEmpty() &&
        qgetenv("WAYLAND_DISPLAY").isEmpty() &&
        qgetenv("QT_QPA_PLATFORM").isEmpty()) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    std::string url = initial_url;
    g_qt_thread = std::thread(qt_thread_main, url);
}

void qtwebengine_module_stop()
{
    if (g_stopping.exchange(true)) return;
    if (auto* app = g_app.load(std::memory_order_acquire)) {
        QMetaObject::invokeMethod(app, "quit", Qt::QueuedConnection);
    }
    if (g_qt_thread.joinable()) g_qt_thread.join();
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

bool qt_dispatch_zoom_in()
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, []() {
        if (g_browser) g_browser->dispatch_zoom_in();
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_zoom_out()
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, []() {
        if (g_browser) g_browser->dispatch_zoom_out();
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_zoom_reset()
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, []() {
        if (g_browser) g_browser->dispatch_zoom_reset();
    }, Qt::QueuedConnection);
    return true;
}

bool qt_dispatch_resize(uint16_t w, uint16_t h)
{
    QApplication* app = qapp_if_active();
    if (!app) return false;
    QMetaObject::invokeMethod(app, [w, h]() {
        if (g_browser) g_browser->dispatch_resize(w, h);
    }, Qt::QueuedConnection);
    return true;
}

}  // namespace browser
