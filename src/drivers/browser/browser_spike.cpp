/*
 *  browser_spike.cpp — M1 host-side gradient writer.
 *
 *  Waits for browser::shm_get() to return non-null (set by the ExtFS
 *  handshake — see shm.cpp), then writes a 640x480 RGB555 animated
 *  gradient into BrowserShm.fb.pixels every ~100 ms and bumps fb.seq.
 *  The guest BrowserSpike app polls fb.seq and CopyBits the pixels
 *  into its window.
 */
#include "browser_spike.h"
#include "shm.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

std::thread g_thread;
std::atomic<bool> g_running{false};
bool g_paint_gradient = false;

constexpr uint16_t kSpikeWidth  = 640;
constexpr uint16_t kSpikeHeight = 480;

uint16_t rgb555(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 7) | ((g & 0xF8) << 2) | (b >> 3));
}

void run()
{
    /* Wait for handshake. Cheap busy-wait — this thread starts at boot
     * but the guest's Browser.app doesn't exist until well after Finder
     * launches it. Bail if the shutdown signal arrives first. */
    BrowserShm* shm = nullptr;
    while (g_running.load(std::memory_order_acquire)) {
        shm = browser::shm_get();
        if (shm) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!shm) return;

    if (g_paint_gradient) {
        /* Reset framebuffer header to spike geometry. */
        br_u16_store(&shm->fb.width,  kSpikeWidth);
        br_u16_store(&shm->fb.height, kSpikeHeight);
        shm->fb.depth = 16;
        br_u16_store(&shm->fb.dirty_count, 1);
        br_u16_store((uint16_t*)&shm->fb.dirty[0].top,    0);
        br_u16_store((uint16_t*)&shm->fb.dirty[0].left,   0);
        br_u16_store((uint16_t*)&shm->fb.dirty[0].bottom, kSpikeHeight);
        br_u16_store((uint16_t*)&shm->fb.dirty[0].right,  kSpikeWidth);
        fprintf(stderr, "[BrowserSpike] gradient writer running (%dx%d, "
                "RGB555, shm=%p)\n", kSpikeWidth, kSpikeHeight, (void*)shm);
    } else {
        fprintf(stderr, "[BrowserSpike] ring/log loop only (pipeline owns fb)\n");
    }

    uint32_t frame = 0;
    uint32_t status_count = 0;
    uint32_t cmd_count = 0;
    auto last_status = std::chrono::steady_clock::now();
    while (g_running.load(std::memory_order_acquire)) {
        if (g_paint_gradient) {
            uint8_t phase = (uint8_t)(frame * 4);
            for (int y = 0; y < kSpikeHeight; y++) {
                uint16_t* row = (uint16_t*)&shm->fb.pixels[y * kSpikeWidth * 2];
                uint8_t b = (uint8_t)(y * 255 / (kSpikeHeight - 1));
                for (int x = 0; x < kSpikeWidth; x++) {
                    uint8_t r = (uint8_t)(((x + phase) * 255) / (kSpikeWidth - 1));
                    uint8_t g = (uint8_t)(((x ^ y) + phase) & 0xFF);
                    uint16_t pix = rgb555(r, g, b);
                    /* BE on wire — guest is native big-endian m68k. */
                    row[x] = (uint16_t)((pix >> 8) | (pix << 8));
                }
            }
            BR_FENCE_RELEASE();
            br_u32_store(&shm->fb.seq, frame + 1);
        }
        frame++;

        /* Drain any g2h commands the guest pushed since last tick. */
        uint16_t cmd_type = 0, cmd_len = 0;
        uint8_t  cmd_buf[256];
        while (browser::read_command(&cmd_type, cmd_buf,
                                     sizeof(cmd_buf), &cmd_len)) {
            cmd_count++;
            uint32_t guest_counter = 0;
            if (cmd_len >= 4) {
                memcpy(&guest_counter, cmd_buf, 4);
                /* Guest is BE; on x86 host swap. */
                guest_counter = __builtin_bswap32(guest_counter);
            }
            fprintf(stderr, "[BrowserSpike] host got cmd type=0x%x len=%u "
                    "(guest_counter=%u, host_total=%u)\n",
                    cmd_type, cmd_len, guest_counter, cmd_count);
        }

        /* Push a BR_EV_STATUS once per second so the guest can prove
         * it's draining h2g. Payload = 1 status byte + ASCII counter. */
        auto now = std::chrono::steady_clock::now();
        if (now - last_status >= std::chrono::seconds(1)) {
            last_status = now;
            uint8_t status_payload[40];
            int n = snprintf((char*)status_payload + 2, sizeof(status_payload) - 2,
                             "tick %u", status_count);
            status_payload[0] = BR_STATUS_READY;
            status_payload[1] = (uint8_t)n;
            uint16_t total = (uint16_t)(2 + n);
            if (browser::send_event(BR_EV_STATUS, status_payload, total)) {
                status_count++;
            }
        }

        /* Poll the guest's debug log channel each tick. Cheap; will move
         * to the VBL hook in M3 once we wire that up. */
        browser::poll_log();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fprintf(stderr, "[BrowserSpike] writer stopped after %u frames\n", frame);
}

}  // namespace

extern "C" void browser_spike_start(int with_gradient)
{
    if (g_running.exchange(true)) return;
    g_paint_gradient = (with_gradient != 0);
    g_thread = std::thread(run);
}

extern "C" void browser_spike_stop()
{
    if (!g_running.exchange(false)) return;
    if (g_thread.joinable()) g_thread.join();
}
