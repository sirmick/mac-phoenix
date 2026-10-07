#pragma once

#include <util/static_vector.h>

#include <array>

namespace Executor
{

class DirtyRects
{
public:
    constexpr static int MAX_DIRTY_RECTS = 5;

    struct Rect
    {
        int top, left, bottom, right;
    };

    using Rects = static_vector<Rect, MAX_DIRTY_RECTS>;

    void add(int top, int left, int bottom, int right);

    bool empty() const { return rects_.empty(); }

    Rects getAndClear();
    void clear() { rects_.clear(); }

private:
    Rects rects_;
};

extern void dirty_rect_accrue(int top, int left, int bottom, int right);
}
