#pragma once
/*
 * phoenix.h - MacPhoenix front end for the Executor core.
 *
 * Renders the guest framebuffer into a packed 32-bit ARGB buffer on the
 * front-end thread at up to 60 Hz. That buffer is what the IPC transport
 * publishes to the mac-phoenix parent (M0d). Standalone, it can write a
 * screenshot after a delay, for headless checks.
 */
#include <vdriver/vdriver.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class PhoenixVideoDriver : public Executor::VideoDriver
{
public:
    using VideoDriver::VideoDriver;

    struct Options
    {
        std::string screenshotPath;   // empty: none
        double screenshotAfter = 5.0; // seconds after start
        bool exitAfterScreenshot = false;
    };
    static Options options;

    // Called on the front-end thread after each rendered frame, with the
    // cursor composited in. Pixels are 0xAARRGGBB (BGRA bytes on LE).
    static std::function<void(const uint32_t *argb, int width, int height)> frameHook;

    // Host-side pointer position (guest pixels), used to draw the cursor.
    static std::atomic<int> cursorX, cursorY;

    bool setMode(int width, int height, int bpp, bool grayscale_p) override;
    void setCursor(char *cursor_data, uint16_t cursor_mask[16],
                   int hotspot_x, int hotspot_y) override;
    void setCursorVisible(bool show_p) override;
    void beepAtUser() override;
    void runEventLoop() override;
    void endEventLoop() override;

protected:
    void requestUpdate() override;

private:
    void render(bool full);
    void publish();
    void writePPM(const std::string& path);

    std::mutex frameMutex_;          // guards argb_ and its size
    std::vector<uint32_t> argb_;
    int argbWidth_ = 0, argbHeight_ = 0;

    std::mutex wakeMutex_;
    std::condition_variable wake_;
    bool updateRequested_ = false;
    std::atomic<bool> quit_{false};

    // Cursor: 16x16, MSB first. Drawn into a copy of the frame on publish.
    std::mutex cursorMutex_;
    uint8_t cursorData_[32] = {};
    uint8_t cursorMask_[32] = {};
    int hotX_ = 0, hotY_ = 0;
    bool cursorValid_ = false;
    bool cursorVisible_ = true;
    std::vector<uint32_t> composed_;
    int lastCursorX_ = -1, lastCursorY_ = -1;
    std::chrono::steady_clock::time_point lastPublish_{};
};
