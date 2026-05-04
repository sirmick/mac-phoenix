/*
 *  mouse_poll.cpp — host-side mouse poller. See mouse_poll.h.
 */
#include "mouse_poll.h"
#include "bidi.h"
#include "shm.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include "../common/include/sysdeps.h"
#include "../common/include/cpu_emulation.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace browser {

namespace {

/* Mac OS low-memory globals we read each tick. */
constexpr uint32_t kAddrMouse      = 0x082C;   /* Point: v(int16), h(int16) */
constexpr uint32_t kAddrMBState    = 0x0172;   /* u8: 0xFF up, 0x00 down    */
constexpr uint32_t kAddrCurApName  = 0x0910;   /* Pascal string, 32 bytes   */

/* Poll cadence: 60 Hz matches the emulator's VBL clock. */
constexpr int kTickMs              = 16;

/* Page-metrics poll cadence: 250 ms. We BiDi-evaluate to read
 * scrollHeight + scrollY; doing it every 16 ms would saturate
 * the WS round-trip budget on slower machines. 4 Hz is plenty for
 * scrollbar-thumb tracking — Firefox is the source of truth and
 * the guest's own BR_CMD_SCROLL clicks are what change the value
 * 99% of the time. */
constexpr int kMetricsPollMs       = 250;

/* Click-coalescing rule: emit at most this many bidi.click calls per
 * second (bidi.click runs an input.performActions round-trip across
 * a WS connection — synchronous, ~30 ms). Stops a runaway emulated
 * click from queuing up. */
constexpr int kMaxClicksPerSec     = 8;

std::thread       g_thread;
std::atomic<bool> g_running{false};
BidiClient*       g_bidi = nullptr;

bool curapname_is_macbrowser()
{
    if (RAMSize == 0) return false;
    uint8_t namelen = ReadMacInt8(kAddrCurApName);
    if (namelen == 0 || namelen > 31) return false;
    /* "MacBrowser" = 10 chars; require an exact match of the first
     * 10 bytes (and either end-of-string or trailing whitespace
     * after — Mac apps sometimes pad). */
    static const char kName[] = "MacBrowser";
    constexpr size_t kLen = sizeof(kName) - 1;
    if (namelen < kLen) return false;
    for (size_t i = 0; i < kLen; i++) {
        if ((char)ReadMacInt8(kAddrCurApName + 1 + i) != kName[i])
            return false;
    }
    return true;
}

void poll_loop()
{
    int  last_x = -1, last_y = -1;
    bool last_btn_down = false;
    auto last_click_window_start = std::chrono::steady_clock::now();
    int  clicks_this_window = 0;

    /* Multi-click detection. A second mouse-down within 500 ms and
     * within 5 px of the prior one bumps the click count. Mac OS
     * ROM uses a similar rule for kDoubleTime / DoubleTime. Gives
     * the page a real "doubleclick" event so word-select on text
     * works. Tracked separately from rate-limiting; the limiter
     * counts physical clicks (so a rapid double counts as 2). */
    auto last_click_at = std::chrono::steady_clock::time_point{};
    int  last_click_x = -10000, last_click_y = -10000;
    int  multi_count  = 0;
    constexpr auto kMultiClickWindow = std::chrono::milliseconds(500);
    constexpr int  kMultiClickRadius = 5;

    /* Page-metrics state: poll every kMetricsPollMs and only push a
     * BR_EV_PAGE_METRICS event when something actually changed.
     * `last_metrics_shm` is the BrowserShm pointer the cached values
     * were captured against — if shm_get() returns a different pointer
     * (MacBrowser relaunched into a fresh BrowserShm), the cache is
     * stale and would suppress the first paint of metrics on the new
     * instance. */
    auto last_metrics_at = std::chrono::steady_clock::time_point{};
    BrowserShm* last_metrics_shm = nullptr;
    uint32_t last_pw = 0, last_ph = 0, last_sx = 0, last_sy = 0,
             last_vw = 0, last_vh = 0;

    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));

        BrowserShm* shm = browser::shm_get();
        if (!shm) continue;
        if (!g_bidi || !g_bidi->is_open()) continue;

        if (!curapname_is_macbrowser()) {
            last_btn_down = false;
            continue;
        }

        /* Read Mac.Mouse globals. The Point at $082C is (v, h) and
         * is updated by the ADB driver each VBL. */
        int mouse_v = (int16_t)ReadMacInt16(kAddrMouse + 0);
        int mouse_h = (int16_t)ReadMacInt16(kAddrMouse + 2);
        uint8_t mb  = ReadMacInt8(kAddrMBState);
        bool btn_down = (mb == 0x00);

        /* Page coords = mouse - viewport top-left. The viewport
         * struct fields are int16 in BE on the wire; on host we
         * byte-swap on read. */
        int viewport_x = (int16_t)br_u16_load((uint16_t*)&shm->viewport_screen_left);
        int viewport_y = (int16_t)br_u16_load((uint16_t*)&shm->viewport_screen_top);
        int page_x = mouse_h - viewport_x;
        int page_y = mouse_v - viewport_y;

        int fb_w = (int)br_u16_load(&shm->fb.width);
        int fb_h = (int)br_u16_load(&shm->fb.height);
        bool inside = (page_x >= 0 && page_x < fb_w &&
                       page_y >= 0 && page_y < fb_h);

        /* Click on a button-down transition (only if cursor is in
         * the page). Rate-limit to kMaxClicksPerSec to avoid a stuck
         * mouse button queueing thousands of calls. */
        if (inside && btn_down && !last_btn_down) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_click_window_start >= std::chrono::seconds(1)) {
                last_click_window_start = now;
                clicks_this_window = 0;
            }
            if (clicks_this_window < kMaxClicksPerSec) {
                clicks_this_window++;
                /* Multi-click count: extend if within the time + space
                 * window, else reset to 1. Capped at 3 (triple-click =
                 * select paragraph in most editors; further is rare).*/
                int dx = page_x - last_click_x;
                int dy = page_y - last_click_y;
                int sq = dx * dx + dy * dy;
                bool within_radius = sq <=
                    kMultiClickRadius * kMultiClickRadius;
                bool within_time = (now - last_click_at) <
                    kMultiClickWindow;
                if (within_radius && within_time && multi_count < 3) {
                    multi_count++;
                } else {
                    multi_count = 1;
                }
                last_click_at = now;
                last_click_x  = page_x;
                last_click_y  = page_y;

                std::string err;
                if (!g_bidi->click(page_x, page_y, 0, multi_count, &err)) {
                    fprintf(stderr, "[MousePoll] click err: %s\n",
                            err.c_str());
                }
            }
        }
        last_btn_down = btn_down;

        /* Hover. Only emit if position actually changed and we're
         * inside the viewport. mouse_move is async fire-and-forget;
         * BiDi handles it cheaply. */
        if (inside && (page_x != last_x || page_y != last_y)) {
            last_x = page_x;
            last_y = page_y;
            std::string err;
            (void)g_bidi->mouse_move(page_x, page_y, &err);
        }

        /* Page metrics — slower than the per-tick mouse poll. */
        auto now = std::chrono::steady_clock::now();
        if (shm != last_metrics_shm) {
            /* BrowserShm pointer changed (re-handshake). The diff
             * suppression below would otherwise compare the new
             * page's metrics against the previous instance's cached
             * values and silently drop the first paint. */
            last_metrics_shm = shm;
            last_pw = last_ph = last_sx = last_sy =
                last_vw = last_vh = 0xFFFFFFFFu;
        }
        if (now - last_metrics_at >=
            std::chrono::milliseconds(kMetricsPollMs)) {
            last_metrics_at = now;
            std::string err;
            /* Single round-trip: ask Firefox for page + scroll +
             * viewport dims as one JSON-serializable array. Guard
             * against frame load races by null-checking documentElement. */
            std::string r = g_bidi->evaluate(
                "(()=>{const e=document.scrollingElement"
                "||document.documentElement||document.body;"
                "if(!e)return [0,0,0,0,0,0];"
                "return [e.scrollWidth|0, e.scrollHeight|0, "
                "(window.scrollX|0), (window.scrollY|0), "
                "(window.innerWidth|0), (window.innerHeight|0)];})()",
                &err);
            if (!r.empty()) {
                /* BiDi script.evaluate returns:
                 *   {"type":"array","value":[
                 *      {"type":"number","value":N}, ...
                 *   ]}
                 * Pull the six numbers; if anything's malformed, just
                 * skip this tick. */
                try {
                    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(r));
                    QJsonArray v = doc.object().value("result").toObject().value("value").toArray();
                    if (v.size() >= 6) {
                        uint32_t pw = (uint32_t)v.at(0).toObject().value("value").toDouble();
                        uint32_t ph = (uint32_t)v.at(1).toObject().value("value").toDouble();
                        uint32_t sx = (uint32_t)v.at(2).toObject().value("value").toDouble();
                        uint32_t sy = (uint32_t)v.at(3).toObject().value("value").toDouble();
                        uint32_t vw = (uint32_t)v.at(4).toObject().value("value").toDouble();
                        uint32_t vh = (uint32_t)v.at(5).toObject().value("value").toDouble();
                        if (pw != last_pw || ph != last_ph ||
                            sx != last_sx || sy != last_sy ||
                            vw != last_vw || vh != last_vh) {
                            last_pw = pw; last_ph = ph;
                            last_sx = sx; last_sy = sy;
                            last_vw = vw; last_vh = vh;
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
                        }
                    }
                } catch (...) {
                    /* malformed BiDi reply — skip */
                }
            }
        }
    }
}

}  // namespace

void mouse_poll_start(BidiClient* bidi)
{
    if (g_running.load()) return;
    g_bidi = bidi;
    g_running.store(true, std::memory_order_release);
    g_thread = std::thread(poll_loop);
}

void mouse_poll_stop()
{
    if (!g_running.exchange(false, std::memory_order_acq_rel)) return;
    if (g_thread.joinable()) g_thread.join();
    g_bidi = nullptr;
}

}  // namespace browser
