#include <wind/wind.h>
#include <vdriver/vdriver.h>
#include <MenuMgr.h>

#include <iostream>
#include <vector>

using namespace Executor;

static std::vector<Rect> rootlessMenus;

// #define LOG_ROOTLESS

void Executor::ROMlib_rootless_update(RgnHandle extra)
{
#ifdef LOG_ROOTLESS
    std::cout << "ROMlib_rootless_update\n";
#endif
    if(vdriver->isRootless())
    {
        RgnHandle rgn = NewRgn();
        SetRectRgn(rgn, 0,0, qdGlobals().screenBits.bounds.right, LM(MBarHeight));

        for(WindowPeek wp = LM(WindowList); wp; wp = WINDOW_NEXT_WINDOW(wp))
        {
            if(WINDOW_VISIBLE(wp))
            {
                UnionRgn(rgn, WINDOW_STRUCT_REGION(wp), rgn);
            }
        }
        RgnHandle tmp = NewRgn();
        for(Rect r : rootlessMenus)
        {   // N.B: do not declare "r" as Rect&,
            // as we aren't allowed to pass pointers into vectors(host heap)
            // as parameters to toolbox functions
            RectRgn(tmp, &r);
            UnionRgn(rgn, tmp, rgn);
        }
        if(extra)
            UnionRgn(rgn, extra, rgn);

        vdriver->setRootlessRegion(rgn);
        DisposeRgn(tmp);
        DisposeRgn(rgn);
    }
}

void Executor::ROMlib_rootless_openmenu(Rect r)
{
#ifdef LOG_ROOTLESS
    std::cout << "ROMlib_rootless_openmenu\n";
#endif
    if(vdriver->isRootless())
    {
        r.left   = r.left   - 1;
        r.top    = r.top    - 1;
        r.right  = r.right  + 2;
        r.bottom = r.bottom + 2;
        rootlessMenus.push_back(r);
        ROMlib_rootless_update();
    }
}
void Executor::ROMlib_rootless_closemenu()
{
#ifdef LOG_ROOTLESS
    std::cout << "ROMlib_rootless_closemenu\n";
#endif
    if(vdriver->isRootless())
    {
        if(rootlessMenus.empty())
            return;
        rootlessMenus.pop_back();
        ROMlib_rootless_update();
    }
}

bool Executor::ROMlib_rootless_drawdesk(RgnHandle desk)
{
    if(vdriver->isRootless())
    {
        EraseRgn(desk);
        return true;
    }
    else
        return false;
}
