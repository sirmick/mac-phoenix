#include "phoenix.h"

#include <base/common.h>
#include <ResourceMgr.h>
#include <SoundMgr.h>
#include <prefs/prefs.h>

#include <chrono>
#include <cstdio>
#include <cstring>

using namespace Executor;

PhoenixVideoDriver::Options PhoenixVideoDriver::options;
std::function<void(const uint32_t *, int, int)> PhoenixVideoDriver::frameHook;
std::atomic<int> PhoenixVideoDriver::cursorX{0}, PhoenixVideoDriver::cursorY{0};

void PhoenixVideoDriver::setCursor(char *cursor_data, uint16_t cursor_mask[16],
                                   int hotspot_x, int hotspot_y)
{
    std::lock_guard lk(cursorMutex_);
    if(cursor_data)
    {
        memcpy(cursorData_, cursor_data, 32);
        memcpy(cursorMask_, cursor_mask, 32);
        for(int i = 0; i < 32; i++)
            cursorMask_[i] |= cursorData_[i];
        hotX_ = hotspot_x;
        hotY_ = hotspot_y;
        cursorValid_ = true;
    }
    cursorVisible_ = true;
}

void PhoenixVideoDriver::setCursorVisible(bool show_p)
{
    std::lock_guard lk(cursorMutex_);
    cursorVisible_ = show_p;
}

// Copy the frame, draw the cursor on top, hand it to the host.
void PhoenixVideoDriver::publish()
{
    if(!frameHook)
        return;
    composed_ = argb_;
    int cx = cursorX.load(), cy = cursorY.load();
    {
        std::lock_guard lk(cursorMutex_);
        if(cursorValid_ && cursorVisible_)
        {
            for(int y = 0; y < 16; y++)
            {
                int py = cy - hotY_ + y;
                if(py < 0 || py >= argbHeight_)
                    continue;
                uint16_t d = (cursorData_[2 * y] << 8) | cursorData_[2 * y + 1];
                uint16_t m = (cursorMask_[2 * y] << 8) | cursorMask_[2 * y + 1];
                for(int x = 0; x < 16; x++)
                {
                    int px = cx - hotX_ + x;
                    if(px < 0 || px >= argbWidth_)
                        continue;
                    uint16_t bit = 0x8000 >> x;
                    if(m & bit)
                        composed_[(size_t)py * argbWidth_ + px] =
                            (d & bit) ? 0xFF000000 : 0xFFFFFFFF;
                }
            }
        }
    }
    lastCursorX_ = cx;
    lastCursorY_ = cy;
    lastPublish_ = std::chrono::steady_clock::now();
    frameHook(composed_.data(), argbWidth_, argbHeight_);
}

bool PhoenixVideoDriver::setMode(int width, int height, int bpp, bool)
{
    if(width == 0 || height == 0)
    {
        width = 640;
        height = 480;
    }
    if(bpp == 0)
        bpp = 8;

    std::lock_guard lk(frameMutex_);
    framebuffer_ = Framebuffer(width, height, bpp);
    argb_.assign((size_t)width * height, 0xFF000000);
    argbWidth_ = width;
    argbHeight_ = height;
    return true;
}

void PhoenixVideoDriver::requestUpdate()
{
    {
        std::lock_guard lk(wakeMutex_);
        updateRequested_ = true;
    }
    wake_.notify_one();
}

void PhoenixVideoDriver::endEventLoop()
{
    quit_ = true;
    wake_.notify_one();
}

void PhoenixVideoDriver::render(bool full)
{
    std::lock_guard flk(frameMutex_);
    if(!framebuffer_.data || argb_.empty())
        return;

    DirtyRects::Rects rects;
    {
        std::lock_guard lk(mutex_);
        rects = dirtyRects_.getAndClear();
    }
    if(full)
    {
        rects.clear();
        rects.push_back({ 0, 0, argbHeight_, argbWidth_ });
    }
    if(rects.empty())
    {
        // Nothing drew, but the pointer may have moved.
        if(cursorX.load() != lastCursorX_ || cursorY.load() != lastCursorY_)
            publish();
        return;
    }

    updateBuffer(framebuffer_, argb_.data(), argbWidth_, argbHeight_, rects);
    publish();
}

void PhoenixVideoDriver::writePPM(const std::string& path)
{
    std::lock_guard flk(frameMutex_);
    FILE *f = fopen(path.c_str(), "wb");
    if(!f)
    {
        perror(path.c_str());
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", argbWidth_, argbHeight_);
    std::vector<uint8_t> row((size_t)argbWidth_ * 3);
    for(int y = 0; y < argbHeight_; y++)
    {
        const uint32_t *src = &argb_[(size_t)y * argbWidth_];
        for(int x = 0; x < argbWidth_; x++)
        {
            row[x * 3 + 0] = (src[x] >> 16) & 0xFF;
            row[x * 3 + 1] = (src[x] >> 8) & 0xFF;
            row[x * 3 + 2] = src[x] & 0xFF;
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    fprintf(stderr, "[executor] screenshot written: %s\n", path.c_str());
}

void PhoenixVideoDriver::runEventLoop()
{
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto frame = std::chrono::microseconds(16667);
    bool shotTaken = options.screenshotPath.empty();

    while(!quit_)
    {
        {
            std::unique_lock lk(wakeMutex_);
            wake_.wait_for(lk, frame, [this] { return updateRequested_ || quit_.load(); });
            updateRequested_ = false;
        }
        if(quit_)
            break;

        render(false);

        // Idle heartbeat: re-send the last frame so viewers that connect
        // while the guest is idle still get a picture (and keyframes).
        if(clock::now() - lastPublish_ >= std::chrono::milliseconds(100))
        {
            std::lock_guard flk(frameMutex_);
            if(!argb_.empty())
                publish();
        }

        if(!shotTaken && clock::now() - start
                >= std::chrono::duration<double>(options.screenshotAfter))
        {
            render(true);
            writePPM(options.screenshotPath);
            shotTaken = true;
            if(options.exitAfterScreenshot && callbacks_)
                callbacks_->requestQuit();
        }
    }
}

/* SysBeep (osutil.cpp): with sound on, the alert sound is the System's
   'snd ' 1, "Simple Beep", played through the Sound Manager; otherwise
   nothing, as Executor's other front ends do. The Sound control panel's
   choice of alert sound is not read yet. */
void PhoenixVideoDriver::beepAtUser()
{
    Executor::Handle h = Executor::ROMlib_PretendSound == Executor::soundon
        ? Executor::GetResource("snd "_4, 1) : nullptr;
    static bool announced;
    if(!announced)
    {
        announced = true;
        fprintf(stderr, "[Executor] SysBeep: sound %s, 'snd ' 1 %s\n",
                Executor::ROMlib_PretendSound == Executor::soundon ? "on" : "pretend/off",
                h ? "found" : "missing");
    }
    if(h)
        Executor::SndPlay(nullptr, h, false);
}
