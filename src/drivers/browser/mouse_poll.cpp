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
        int viewport_x = (int16_t)br_u16_load((uint16_t*)&shm->viewport.screen_left);
        int viewport_y = (int16_t)br_u16_load((uint16_t*)&shm->viewport.screen_top);
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
                std::string err;
                if (!g_bidi->click(page_x, page_y, 0, 1, &err)) {
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
