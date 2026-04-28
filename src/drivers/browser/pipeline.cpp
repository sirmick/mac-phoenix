/*
 *  pipeline.cpp — XShm → BrowserShm.fb. See pipeline.h.
 */
#include "pipeline.h"
#include "xshm.h"
#include "shm.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace browser {

namespace {

XShmCapture*       g_capture = nullptr;
std::atomic<bool>  g_running{false};
std::thread        g_thread;

/* Rolling perf counters; reported once per second. */
struct PerfCounters {
    uint64_t frames     = 0;
    uint64_t pull_us    = 0;   /* xcb_shm_get_image round-trip */
    uint64_t blit_us    = 0;   /* BGRX → RGB555 + BrowserShm write */
    uint64_t pixels_out = 0;
} g_perf;
std::chrono::steady_clock::time_point g_last_report;

inline uint16_t bgrx_to_rgb555_be(uint32_t bgrx)
{
    uint8_t b = (uint8_t)(bgrx & 0xFF);
    uint8_t g = (uint8_t)((bgrx >>  8) & 0xFF);
    uint8_t r = (uint8_t)((bgrx >> 16) & 0xFF);
    uint16_t pix = (uint16_t)(((r & 0xF8) << 7) |
                              ((g & 0xF8) << 2) |
                              (b >> 3));
    return (uint16_t)((pix >> 8) | (pix << 8));
}

template <typename Tp>
inline uint64_t us_since(const Tp& t0)
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
}

void maybe_report()
{
    auto now = std::chrono::steady_clock::now();
    if (g_last_report.time_since_epoch().count() == 0) {
        g_last_report = now;
        return;
    }
    auto window_us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        now - g_last_report).count();
    if (window_us < 1'000'000) return;

    if (g_perf.frames > 0 && g_capture) {
        double fps      = (double)g_perf.frames * 1e6 / (double)window_us;
        double avg_pull = (double)g_perf.pull_us / g_perf.frames;
        double avg_blit = (double)g_perf.blit_us / g_perf.frames;
        double dirty_pct = (g_capture->width() && g_capture->height())
            ? (double)g_perf.pixels_out * 100.0
                / (g_perf.frames * g_capture->width() * g_capture->height())
            : 0.0;
        BrowserShm* shm = browser::shm_get();
        uint16_t fb_w = shm ? br_u16_load(&shm->fb.width)  : 0;
        uint16_t fb_h = shm ? br_u16_load(&shm->fb.height) : 0;
        /* Cross-stack dump: canvas (Xvfb root, fixed) vs crop (current
         * Mac viewport from shm.fb). Disagreement on crop is normal —
         * crop is just the visible Firefox sub-region. Diagonal smear
         * would mean the guest's PixMap rowBytes ≠ host dst_stride,
         * which is impossible now that both derive from fb.width. */
        fprintf(stderr,
                "[Pipeline] %.1f fps  pull=%.1fms blit=%.2fms  "
                "dirty=%.1f%%  canvas=%dx%d  crop=%ux%u\n",
                fps, avg_pull / 1000.0, avg_blit / 1000.0, dirty_pct,
                g_capture->width(), g_capture->height(), fb_w, fb_h);
    }
    g_perf = {};
    g_last_report = now;
}

void run()
{
    /* Wait for both the guest BrowserShm handshake and a non-zero
     * capture geometry. */
    BrowserShm* shm = nullptr;
    while (g_running.load(std::memory_order_acquire)) {
        shm = browser::shm_get();
        if (shm && g_capture && g_capture->width() > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!shm) return;

    fprintf(stderr, "[Pipeline] starting at viewport %dx%d\n",
            g_capture->width(), g_capture->height());

    BrowserShm* last_shm = nullptr;
    uint32_t frame = 0;
    while (g_running.load(std::memory_order_acquire)) {
        /* Re-resolve shm + capture dims every iteration: the watcher
         * thread may swap g_shm under us when MacBrowser relaunches,
         * and a future BR_CMD_RESIZE can grow Xvfb's root mid-stream.
         * Cheap (load + getter), and skipping it caused the fb.width/
         * stride desync that produced diagonal smears after relaunch. */
        BrowserShm* live = browser::shm_get();
        if (!live) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        /* Crop region: what the guest currently wants visible. cmd.cpp
         * writes fb.width/height when BR_CMD_RESIZE arrives; we read
         * here every iteration. The Xvfb root is a fixed 1024×768
         * canvas — any pixels outside the crop region are unrendered
         * X background and we simply ignore them. dst_stride = crop_w*2
         * matches the guest's PixMap rowBytes exactly, so no shearing
         * regardless of how the guest resizes. */
        int crop_w = (int)br_u16_load(&live->fb.width);
        int crop_h = (int)br_u16_load(&live->fb.height);
        if (crop_w == 0 || crop_h == 0) {
            /* Guest hasn't published a viewport yet — fall back to the
             * Xvfb root size so the initial frame still gets through. */
            crop_w = g_capture->width();
            crop_h = g_capture->height();
            if (crop_w > (int)BR_FB_MAX_W) crop_w = (int)BR_FB_MAX_W;
            if (crop_h > (int)BR_FB_MAX_H) crop_h = (int)BR_FB_MAX_H;
            br_u16_store(&live->fb.width,  (uint16_t)crop_w);
            br_u16_store(&live->fb.height, (uint16_t)crop_h);
        }
        if (crop_w > (int)BR_FB_MAX_W) crop_w = (int)BR_FB_MAX_W;
        if (crop_h > (int)BR_FB_MAX_H) crop_h = (int)BR_FB_MAX_H;
        live->fb.depth = 16;
        if (live != last_shm) {
            fprintf(stderr, "[Pipeline] (re)bound shm=%p crop=%dx%d "
                    "(canvas=%dx%d)\n",
                    (void*)live, crop_w, crop_h,
                    g_capture->width(), g_capture->height());
            last_shm = live;
            frame = br_u32_load(&live->fb.seq);
        }
        shm = live;

        auto t0 = std::chrono::steady_clock::now();
        const uint8_t* img = nullptr;
        XShmCapture::Rect r{ 0, 0, 0, 0 };

        if (!g_capture->drain(&img, &r)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        uint64_t pull_us = us_since(t0);

        /* Source data in `img` is packed at the ORIGINAL r.w stride
         * (drain() did `xcb_shm_get_image(r.x, r.y, r.w, r.h)` then
         * laid the result down with stride r.w*4). We must remember
         * that width before clipping — if we recompute src_stride
         * from the clipped r.w we'd step by less than the actual row
         * size and every row beyond the first lands offset by
         * `(orig_rw - clipped_rw) * 4` bytes, which presents as a
         * diagonal tear. Caused the shrink-side smear we just hit. */
        const int src_row_w   = r.w;
        const int src_stride  = src_row_w * 4;
        const int src_x0      = (r.x < 0) ? -r.x : 0;
        const int src_y0      = (r.y < 0) ? -r.y : 0;

        /* Clip damage rect to the crop region. Anything outside is
         * unrendered X background and shouldn't reach BrowserShm. */
        if (r.x < 0) { r.w = (uint16_t)(r.w + r.x); r.x = 0; }
        if (r.y < 0) { r.h = (uint16_t)(r.h + r.y); r.y = 0; }
        if ((int)r.x + r.w > crop_w) r.w = (uint16_t)(crop_w - r.x);
        if ((int)r.y + r.h > crop_h) r.h = (uint16_t)(crop_h - r.y);
        if ((int)r.w <= 0 || (int)r.h <= 0) continue;

        auto t1 = std::chrono::steady_clock::now();
        const int dst_stride = crop_w * 2;
        for (int y = 0; y < r.h; y++) {
            const uint32_t* src_row =
                (const uint32_t*)(img + (size_t)(src_y0 + y) * src_stride);
            uint16_t* dst_row =
                (uint16_t*)&shm->fb.pixels[
                    (size_t)(r.y + y) * dst_stride + (r.x * 2)];
            for (int x = 0; x < r.w; x++) {
                dst_row[x] = bgrx_to_rgb555_be(src_row[src_x0 + x]);
            }
        }
        uint64_t blit_us = us_since(t1);

        br_u16_store(&shm->fb.dirty_count, 1);
        br_u16_store((uint16_t*)&shm->fb.dirty[0].top,    r.y);
        br_u16_store((uint16_t*)&shm->fb.dirty[0].left,   r.x);
        br_u16_store((uint16_t*)&shm->fb.dirty[0].bottom, r.y + r.h);
        br_u16_store((uint16_t*)&shm->fb.dirty[0].right,  r.x + r.w);
        BR_FENCE_RELEASE();
        br_u32_store(&shm->fb.seq, ++frame);

        g_perf.frames++;
        g_perf.pull_us    += pull_us;
        g_perf.blit_us    += blit_us;
        g_perf.pixels_out += (uint64_t)r.w * (uint64_t)r.h;
        maybe_report();

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    fprintf(stderr, "[Pipeline] stopped after %u frames\n", frame);
}

}  // namespace

void pipeline_start(XShmCapture* capture)
{
    if (!capture) return;
    if (g_running.exchange(true)) return;
    g_capture = capture;
    g_thread = std::thread(run);
}

void pipeline_stop()
{
    if (!g_running.exchange(false)) return;
    if (g_thread.joinable()) g_thread.join();
    g_capture = nullptr;
    g_perf = {};
    g_last_report = {};
}

}  // namespace browser
