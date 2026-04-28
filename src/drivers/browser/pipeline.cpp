/*
 *  pipeline.cpp — CDP screencast → BrowserShm.fb. See pipeline.h.
 */
#include "pipeline.h"
#include "cdp.h"
#include "shm.h"

#define BR_HOST 1
#include "MacBrowser.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_HDR
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PSD
#define STBI_NO_PNM
#define STBI_NO_TGA
#define STBI_NO_BMP
#include "stb_image.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace browser {

namespace {

CdpClient* g_cdp = nullptr;
std::atomic<bool> g_running{false};
std::mutex g_mtx;

/* Previous frame for diffing. Same layout as the decoded frame:
 * RGBA, top-down, w*4 bytes per row. */
std::vector<uint8_t> g_prev;
int g_prev_w = 0;
int g_prev_h = 0;

uint64_t g_frame_count = 0;

/* Standard base64 decoder. Output buffer must be at least
 * ((input.size() / 4) * 3) bytes. Returns decoded length, or -1 on
 * malformed input. Tolerates whitespace + standard padding. */
int base64_decode(const std::string& in, std::vector<uint8_t>& out)
{
    static const int8_t T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    };
    out.clear();
    out.reserve((in.size() / 4) * 3 + 3);
    int  acc = 0, bits = 0;
    for (unsigned char c : in) {
        if (c <= ' ') continue;
        if (c == '=') break;
        int8_t v = T[c];
        if (v < 0) return -1;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((acc >> bits) & 0xFF));
        }
    }
    return (int)out.size();
}

/* Convert one RGBA8 pixel to big-endian RGB555. */
inline uint16_t rgba_to_rgb555_be(uint32_t rgba)
{
    uint8_t r = (uint8_t)(rgba & 0xFF);
    uint8_t g = (uint8_t)((rgba >>  8) & 0xFF);
    uint8_t b = (uint8_t)((rgba >> 16) & 0xFF);
    uint16_t pix = (uint16_t)(((r & 0xF8) << 7) |
                              ((g & 0xF8) << 2) |
                              (b >> 3));
    return (uint16_t)((pix >> 8) | (pix << 8));
}

/* Walk the new frame vs prev row-by-row to find the smallest bbox
 * containing all changed pixels. Returns false if frames are
 * identical (skip publish). g_prev_w/h must match w/h; caller
 * handles resize case separately. */
bool compute_dirty_bbox(const uint8_t* prev, const uint8_t* cur,
                        int w, int h,
                        int* out_x, int* out_y,
                        int* out_w, int* out_h)
{
    int min_y = h, max_y = -1;
    int min_x = w, max_x = -1;
    int row_bytes = w * 4;
    for (int y = 0; y < h; y++) {
        const uint8_t* a = prev + y * row_bytes;
        const uint8_t* b = cur  + y * row_bytes;
        if (memcmp(a, b, row_bytes) == 0) continue;
        if (y < min_y) min_y = y;
        max_y = y;
        /* Find first/last differing pixel in this row. */
        int x_first = -1, x_last = -1;
        const uint32_t* pa = (const uint32_t*)a;
        const uint32_t* pb = (const uint32_t*)b;
        for (int x = 0; x < w; x++) {
            if (pa[x] != pb[x]) { x_first = x; break; }
        }
        for (int x = w - 1; x >= 0; x--) {
            if (pa[x] != pb[x]) { x_last = x; break; }
        }
        if (x_first >= 0) {
            if (x_first < min_x) min_x = x_first;
            if (x_last  > max_x) max_x = x_last;
        }
    }
    if (max_y < 0) return false;
    *out_x = min_x;
    *out_y = min_y;
    *out_w = (max_x - min_x) + 1;
    *out_h = (max_y - min_y) + 1;
    return true;
}

void on_screencast_frame(const CdpClient::Json& params)
{
    if (!params.contains("data")) return;

    /* "sessionId" inside the screencastFrame params is chromium's
     * per-frame ack token, not the CDP attach session. We must echo
     * it back via Page.screencastFrameAck or chromium throttles. */
    int frame_ack_id = -1;
    if (params.contains("sessionId") && params["sessionId"].is_number()) {
        frame_ack_id = params["sessionId"].get<int>();
    }

    const std::string b64 = params["data"].get<std::string>();
    auto ack = [frame_ack_id]() {
        /* Fire-and-forget — calling cdp->call() here would deadlock,
         * since we're inside the WS receive thread and the ack's own
         * response would never get processed. */
        if (g_cdp && frame_ack_id >= 0) {
            g_cdp->send_no_reply("Page.screencastFrameAck",
                                 {{"sessionId", frame_ack_id}});
        }
    };

    BrowserShm* shm = browser::shm_get();
    if (!shm) { ack(); return; }

    std::vector<uint8_t> png;
    if (base64_decode(b64, png) < 0) {
        fprintf(stderr, "[Pipeline] base64 decode failed\n");
        ack();
        return;
    }

    int w = 0, h = 0, channels = 0;
    uint8_t* px = stbi_load_from_memory(png.data(), (int)png.size(),
                                        &w, &h, &channels, 4);
    if (!px) {
        fprintf(stderr, "[Pipeline] png decode failed: %s\n",
                stbi_failure_reason());
        ack();
        return;
    }

    int cap_w = (w > (int)BR_FB_MAX_W) ? (int)BR_FB_MAX_W : w;
    int cap_h = (h > (int)BR_FB_MAX_H) ? (int)BR_FB_MAX_H : h;

    {
        std::lock_guard<std::mutex> lk(g_mtx);

        if (w != g_prev_w || h != g_prev_h) {
            g_prev.assign((size_t)w * h * 4, 0);
            g_prev_w = w;
            g_prev_h = h;
            br_u16_store(&shm->fb.width,  (uint16_t)cap_w);
            br_u16_store(&shm->fb.height, (uint16_t)cap_h);
            shm->fb.depth = 16;
            fprintf(stderr, "[Pipeline] viewport %dx%d (clipped to %dx%d)\n",
                    w, h, cap_w, cap_h);
        }

        int dx, dy, dw, dh;
        if (compute_dirty_bbox(g_prev.data(), px, w, h, &dx, &dy, &dw, &dh)) {
            if (dx + dw > cap_w) dw = cap_w - dx;
            if (dy + dh > cap_h) dh = cap_h - dy;
            if (dw > 0 && dh > 0) {
                int dst_stride = cap_w * 2;
                for (int y = 0; y < dh; y++) {
                    const uint32_t* src_row =
                        (const uint32_t*)(px + (size_t)(dy + y) * w * 4);
                    uint16_t* dst_row =
                        (uint16_t*)&shm->fb.pixels[
                            (size_t)(dy + y) * dst_stride + (dx * 2)];
                    for (int x = 0; x < dw; x++) {
                        dst_row[x] = rgba_to_rgb555_be(src_row[dx + x]);
                    }
                }

                br_u16_store(&shm->fb.dirty_count, 1);
                br_u16_store((uint16_t*)&shm->fb.dirty[0].top,    dy);
                br_u16_store((uint16_t*)&shm->fb.dirty[0].left,   dx);
                br_u16_store((uint16_t*)&shm->fb.dirty[0].bottom, dy + dh);
                br_u16_store((uint16_t*)&shm->fb.dirty[0].right,  dx + dw);
                BR_FENCE_RELEASE();
                br_u32_store(&shm->fb.seq, (uint32_t)++g_frame_count);
            }
        }

        memcpy(g_prev.data(), px, (size_t)w * h * 4);
    }

    stbi_image_free(px);
    ack();
}

}  // namespace

void pipeline_start(CdpClient* cdp)
{
    if (!cdp) return;
    if (g_running.exchange(true)) return;
    g_cdp = cdp;

    cdp->on_event("Page.screencastFrame", on_screencast_frame);

    /* PNG: lossless. Chromium pushes frames event-driven — only when
     * it actually paints. Static page = zero pushes. */
    auto resp = cdp->call("Page.startScreencast",
                          {
                              {"format",        "png"},
                              {"everyNthFrame", 1},
                          },
                          std::chrono::milliseconds(5000));
    if (!resp.ok) {
        fprintf(stderr, "[Pipeline] Page.startScreencast failed\n");
        g_running.store(false, std::memory_order_release);
        g_cdp = nullptr;
        return;
    }
    fprintf(stderr, "[Pipeline] screencast subscribed (png lossless)\n");
}

void pipeline_stop()
{
    if (!g_running.exchange(false)) return;
    if (g_cdp) {
        g_cdp->call("Page.stopScreencast");
        g_cdp->on_event("Page.screencastFrame", nullptr);
        g_cdp = nullptr;
    }
    std::lock_guard<std::mutex> lk(g_mtx);
    g_prev.clear();
    g_prev_w = g_prev_h = 0;
    g_frame_count = 0;
}

}  // namespace browser
