/*
 *  mouse_poll.cpp — host-side mouse poller. See mouse_poll.h.
 */
#include "mouse_poll.h"
#include "qtwebengine_browser.h"
#include "shm.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include "../common/include/sysdeps.h"
#include "../common/include/cpu_emulation.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace browser {

namespace {

// Mac OS low-memory globals we read each tick.
constexpr uint32_t kAddrMouse      = 0x082C;   // Point: v(int16), h(int16)
constexpr uint32_t kAddrMBState    = 0x0172;   // u8: 0xFF up, 0x00 down
constexpr uint32_t kAddrCurApName  = 0x0910;   // Pascal string, 32 bytes

// Poll cadence: 60 Hz matches the emulator's VBL clock.
constexpr int kTickMs = 16;

// Click-coalescing: emit at most this many qt_dispatch_click calls per
// second. Stops a runaway emulated click from queuing up (every queued
// event still goes to Chromium via QApplication::postEvent).
constexpr int kMaxClicksPerSec = 8;

std::thread       g_thread;
std::atomic<bool> g_running{false};

bool curapname_is_macbrowser()
{
    if (RAMSize == 0) return false;
    uint8_t namelen = ReadMacInt8(kAddrCurApName);
    if (namelen == 0 || namelen > 31) return false;
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

    // Multi-click detection. A second mouse-down within 500 ms and
    // within 5 px of the prior one bumps the click count. Mac OS ROM
    // uses a similar rule for kDoubleTime / DoubleTime. Capped at 3
    // (triple-click = select paragraph; further is rare).
    auto last_click_at = std::chrono::steady_clock::time_point{};
    int  last_click_x = -10000, last_click_y = -10000;
    int  multi_count  = 0;
    constexpr auto kMultiClickWindow = std::chrono::milliseconds(500);
    constexpr int  kMultiClickRadius = 5;

    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));

        BrowserShm* shm = browser::shm_get();
        if (!shm) continue;

        if (!curapname_is_macbrowser()) {
            last_btn_down = false;
            continue;
        }

        // Read Mac.Mouse globals. The Point at $082C is (v, h) and is
        // updated by the ADB driver each VBL.
        int mouse_v = (int16_t)ReadMacInt16(kAddrMouse + 0);
        int mouse_h = (int16_t)ReadMacInt16(kAddrMouse + 2);
        uint8_t mb  = ReadMacInt8(kAddrMBState);
        bool btn_down = (mb == 0x00);

        int viewport_x = (int16_t)br_u16_load((uint16_t*)&shm->viewport_screen_left);
        int viewport_y = (int16_t)br_u16_load((uint16_t*)&shm->viewport_screen_top);
        int page_x = mouse_h - viewport_x;
        int page_y = mouse_v - viewport_y;

        int fb_w = (int)br_u16_load(&shm->fb.width);
        int fb_h = (int)br_u16_load(&shm->fb.height);
        bool inside = (page_x >= 0 && page_x < fb_w &&
                       page_y >= 0 && page_y < fb_h);

        if (inside && btn_down && !last_btn_down) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_click_window_start >= std::chrono::seconds(1)) {
                last_click_window_start = now;
                clicks_this_window = 0;
            }
            if (clicks_this_window < kMaxClicksPerSec) {
                clicks_this_window++;
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

                qt_dispatch_click(page_x, page_y, 0, multi_count);
            }
        }
        last_btn_down = btn_down;

        if (inside && (page_x != last_x || page_y != last_y)) {
            last_x = page_x;
            last_y = page_y;
            qt_dispatch_mouse_move(page_x, page_y);
        }
    }
}

}  // namespace

void mouse_poll_start()
{
    if (g_running.load()) return;
    g_running.store(true, std::memory_order_release);
    g_thread = std::thread(poll_loop);
}

void mouse_poll_stop()
{
    if (!g_running.exchange(false, std::memory_order_acq_rel)) return;
    if (g_thread.joinable()) g_thread.join();
}

}  // namespace browser
