/*
 * frames.cpp - frames as RGBA bytes (session.h rects_rgba, frame_rgba):
 * 0xAARRGGBB to R, G, B, A bytes, 0xAABBGGRR on little-endian.
 */
#include "internal.h"

namespace executor_host::session {

namespace {

PhoenixVideoDriver *driver()
{
    return dynamic_cast<PhoenixVideoDriver *>(Executor::vdriver.get());
}

uint32_t rgba(uint32_t p)
{
    return (p & 0xFF00FF00) | (p >> 16 & 0xFF) | (p & 0xFF) << 16;
}

}

void rects_rgba(const uint32_t *argb, int width, const std::vector<PhoenixVideoDriver::Rect> &rects,
                std::vector<uint32_t> &out_pixels, std::vector<int32_t> &rect_list)
{
    size_t total = 0;
    for(const auto &r : rects)
        total += (size_t)(r.bottom - r.top) * (r.right - r.left);
    out_pixels.resize(total);
    rect_list.clear();
    uint32_t *out = out_pixels.data();
    for(const auto &r : rects)
    {
        rect_list.insert(rect_list.end(), { r.top, r.left, r.bottom, r.right });
        uint32_t *start = out;
        for(int y = r.top; y < r.bottom; y++)
        {
            const uint32_t *row = argb + (size_t)y * width;
            for(int x = r.left; x < r.right; x++)
                *out++ = rgba(row[x]);
        }
        if(auto *v = driver())
            v->drawCursor(start, r);
    }
}

bool frame_rgba(std::vector<uint32_t> &px, int &width, int &height)
{
    if(!driver() || !driver()->copyFrame(px, width, height))
        return false;
    for(uint32_t &p : px)
        p = rgba(p);
    return true;
}

}  // namespace executor_host::session
