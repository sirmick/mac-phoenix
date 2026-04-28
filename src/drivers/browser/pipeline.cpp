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
        fprintf(stderr,
                "[Pipeline] %.1f fps  pull=%.1fms blit=%.2fms  dirty=%.1f%%\n",
                fps, avg_pull / 1000.0, avg_blit / 1000.0, dirty_pct);
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

    int cap_w = g_capture->width();
    int cap_h = g_capture->height();
    if (cap_w > (int)BR_FB_MAX_W) cap_w = (int)BR_FB_MAX_W;
    if (cap_h > (int)BR_FB_MAX_H) cap_h = (int)BR_FB_MAX_H;
    br_u16_store(&shm->fb.width,  (uint16_t)cap_w);
    br_u16_store(&shm->fb.height, (uint16_t)cap_h);
    shm->fb.depth = 16;
    fprintf(stderr, "[Pipeline] viewport %dx%d (clipped to %dx%d)\n",
            g_capture->width(), g_capture->height(), cap_w, cap_h);

    uint32_t frame = 0;
    while (g_running.load(std::memory_order_acquire)) {
        auto t0 = std::chrono::steady_clock::now();
        const uint8_t* img = nullptr;
        XShmCapture::Rect r{ 0, 0, 0, 0 };

        if (!g_capture->drain(&img, &r)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        uint64_t pull_us = us_since(t0);

        /* Clip rect to the BrowserShm fb geometry. */
        if (r.x < 0) { r.w = (uint16_t)(r.w + r.x); r.x = 0; }
        if (r.y < 0) { r.h = (uint16_t)(r.h + r.y); r.y = 0; }
        if ((int)r.x + r.w > cap_w) r.w = (uint16_t)(cap_w - r.x);
        if ((int)r.y + r.h > cap_h) r.h = (uint16_t)(cap_h - r.y);
        if (r.w == 0 || r.h == 0) continue;

        auto t1 = std::chrono::steady_clock::now();
        /* drain() places pixels at offset 0 of the SHM, packed at
         * stride r.w*4 (NOT capture->width()*4). */
        int src_stride = r.w * 4;
        int dst_stride = cap_w * 2;
        for (int y = 0; y < r.h; y++) {
            const uint32_t* src_row =
                (const uint32_t*)(img + (size_t)y * src_stride);
            uint16_t* dst_row =
                (uint16_t*)&shm->fb.pixels[
                    (size_t)(r.y + y) * dst_stride + (r.x * 2)];
            for (int x = 0; x < r.w; x++) {
                dst_row[x] = bgrx_to_rgb555_be(src_row[x]);
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
