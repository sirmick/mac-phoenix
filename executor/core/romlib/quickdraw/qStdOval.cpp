/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>
#include <quickdraw/cquick.h>
#include <quickdraw/picture.h>
#include <quickdraw/region.h>

using namespace Executor;



std::vector<int16_t> bresenham(int64_t width, int64_t height)
{
    std::vector<int16_t> steps;
    steps.reserve(height);

    int64_t a = height * height;
    int64_t b = width * width;

    bool oddWidth = width % 2;
    bool oddHeight = height % 2;

    int64_t d = (oddWidth ? 4 : 1) * a + (1 - 2 * height) * b;

    int x = 0, y = height/2;

    int64_t deltaD;
    do
    {
        while(d < 0)
        {
            d += 4 * a * (2 * x + 1 + (oddWidth ? 2 : 1));
            ++x;
        }
        
        steps.push_back(x);
        deltaD = 4 * a * (2 * x + 1 + (oddWidth ? 2 : 1)) - 4 * b * (2 * y - (oddHeight ? 1 : 2));
        d += deltaD;
        --y;
        ++x;
    } while(deltaD <= 0);
    
    return steps;
}

RgnHandle Executor::ROMlib_circrgn(const Rect& rect)
{
    RgnVector rgn;

    int64_t width = int(rect.right) - rect.left;
    int64_t height = int(rect.bottom) - rect.top;

    if(width >= 3 && height >= 3)
    {
        auto xsteps = bresenham(width, height);
        auto ysteps = bresenham(height, width);

#if 0
        std::cout << "CIRCLE " << width << "x" << height << std::endl;
        for(auto x : xsteps)
            std::cout << " " << x;
        std::cout << "\n";
        for(auto y : ysteps)
            std::cout << " " << y;
        std::cout << "\n";
#endif

        int16_t centl = rect.left + width / 2;
        int16_t centr = rect.right - width / 2;
        int16_t centt = rect.top + height / 2;
        int16_t centb = rect.bottom - height / 2;

        int16_t ox = -1;

        auto pointUpper = [&ox, &rgn, centl, centr](int16_t x, int16_t y) {
            if(x == ox)
                return;
            rgn.push_back(y);
            rgn.push_back(centl - x);
            if(ox >= 0)
            {
                rgn.push_back(centl - ox);
                rgn.push_back(centr + ox);
            }
            rgn.push_back(centr + x);
            rgn.push_back(RGN_STOP);
            ox = x;
        };
        auto pointLower = [&ox, &rgn, centl, centr](int16_t x, int16_t y) {
            if(x == ox)
                return;
            rgn.push_back(y);
            rgn.push_back(centl - ox);
            rgn.push_back(centl - x);
            rgn.push_back(centr + x);
            rgn.push_back(centr + ox);
            rgn.push_back(RGN_STOP);
            ox = x;
        };

        rgn.reserve(12 * xsteps.size() + 12 * ysteps.size() + 5 /* end */ - 2 /* no ox at start */);

        for(int i = 0; i < xsteps.size(); i++)
            pointUpper(xsteps[i], rect.top + i);

        for(int i = ysteps.size() - 1; i >= 0; i--)
            pointUpper(width / 2 - i, centt - ysteps[i]);

        for(int i = 0; i < ysteps.size()-1; i++)
            pointLower(width / 2 - i - 1, centb + ysteps[i]);

        for(int i = xsteps.size() - 1; i >= 0; i--)
            pointLower(xsteps[i], rect.bottom - i - 1);

        rgn.push_back(rect.bottom);
        rgn.push_back(centl - ox);
        rgn.push_back(centr + ox);
        rgn.push_back(RGN_STOP);
        rgn.push_back(RGN_STOP);
    }

    size_t sz = sizeof(int16_t) * rgn.size() + 10;
    RgnHandle rgnH = rgn.release();
    (*rgnH)->rgnBBox = rect;
    (*rgnH)->rgnSize = sz;
    return rgnH;
}

void Executor::C_StdOval(GrafVerb v, const Rect *rp)
{
    Rect r;
    RgnHandle rh, rh2;
    PAUSEDECL;

    if(!EmptyRect(rp))
    {
        if(qdGlobals().thePort->picSave)
        {
            ROMlib_drawingverbrectpicupdate(v, rp);
            PICOP(OP_frameOval + (int)v);
            PICWRITE(rp, sizeof(*rp));
        };

        PAUSERECORDING;
        if(rp->bottom - rp->top < 4 && rp->right - rp->left < 4)
            StdRect(v, rp);
        else
        {
            rh = ROMlib_circrgn(*rp);
            switch(v)
            {
                case frame:
                    if(PORT_REGION_SAVE(qdGlobals().thePort))
                        XorRgn(rh,
                               (RgnHandle)PORT_REGION_SAVE(qdGlobals().thePort),
                               (RgnHandle)PORT_REGION_SAVE(qdGlobals().thePort));
                    if(PORT_PEN_VIS(qdGlobals().thePort) >= 0)
                    {
                        r.top = rp->top + PORT_PEN_SIZE(qdGlobals().thePort).v;
                        r.left = rp->left + PORT_PEN_SIZE(qdGlobals().thePort).h;
                        r.bottom = rp->bottom - PORT_PEN_SIZE(qdGlobals().thePort).v;
                        r.right = rp->right - PORT_PEN_SIZE(qdGlobals().thePort).h;
                        if(r.top < r.bottom && r.left < r.right)
                        {
                            rh2 = ROMlib_circrgn(r);
                            XorRgn(rh, rh2, rh);
                            DisposeRgn(rh2);
                        }
                        StdRgn(paint, rh);
                    }
                    break;
                case paint:
                case erase:
                case invert:
                case fill:
                    StdRgn(v, rh);
            }
            DisposeRgn(rh);
        }
        RESUMERECORDING;
    }
}
