#include "gtest/gtest.h"

#include "compat.h"

#ifdef EXECUTOR
#include <WindowMgr.h>
#include <ControlMgr.h>
#include <ToolboxEvent.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <FileMgr.h>
#include <CQuickDraw.h>
#else
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#include <Resources.h>
#include <Files.h>
#include <Palettes.h>
#endif
#include <string.h>
#include <string>

/* Window and control behaviour measured on the Quadra (7.5.5, 7.6.1). */

/* NewWindow allocates a WindowRecord whatever the title says: 156 bytes
   for "Modal" ones too, not a DialogRecord. */
TEST(Windows, NewWindowStorageIsAWindowRecord)
{
    Rect r = { 50, 50, 150, 250 };
    WindowPtr w = NewWindow(nullptr, &r, PSTR("Modal Dialog"), false, 0,
                            (WindowPtr)-1, false, 0);
    ASSERT_NE(nullptr, w);
    EXPECT_EQ(156, GetPtrSize((Ptr)w));
    DisposeWindow(w);
}

/* TrackControl from a point outside the control answers 0, whatever the
   mouse's position (no second look at it). */
TEST(Windows, TrackControlOutsideIsNoPart)
{
    Rect r = { 40, 0, 400, 600 };
    WindowPtr w = NewWindow(nullptr, &r, PSTR(""), true, plainDBox,
                            (WindowPtr)-1, false, 0);
    ASSERT_NE(nullptr, w);
    SetPort(w);
    GUEST<Point> gm;
    GetMouse(&gm);
    Point m = gm;
    Rect cr = { (short)(m.v - 10), (short)(m.h - 20), (short)(m.v + 10), (short)(m.h + 20) };
    ControlHandle c = NewControl(w, &cr, PSTR("B"), true, 0, 0, 1, 0, 0);
    ASSERT_NE(nullptr, c);
    Point far = { (short)(m.v + 200), (short)(m.h + 300) };
    EXPECT_EQ(10, TestControl(c, m)); /* inButton */
    EXPECT_EQ(0, TestControl(c, far));
    EXPECT_EQ(0, TrackControl(c, far, nullptr));
    DisposeWindow(w);
}

/* GetNewCWindow attaches the 'pltt' of the window's ID, and DisposeWindow
   leaves that palette in the heap: a 7.6.1 Quadra keeps 4144 bytes per
   window with a 256-colour palette (and Executor about the same).
   Speedometer 4.0's Color QuickDraw tests open such a window (WIND/pltt
   507) about 214 times, so its heap fills up on both machines. */
TEST(Windows, DisposeWindowKeepsItsAutomaticPalette)
{
    StringPtr name = PSTR("test-wind-pltt");
    CreateResFile(name);
    short ref = OpenResFile(name);
    ASSERT_NE(-1, ref);

    unsigned char wind[18 + 2] = {};
    const short wr[4] = { 60, 60, 160, 260 };
    for(int i = 0; i < 4; i++)
    {
        wind[2 * i] = wr[i] >> 8;
        wind[2 * i + 1] = wr[i] & 0xFF;
    }
    wind[9] = 4;                                /* noGrowDocProc; invisible, no go-away */
    wind[18] = 1;
    wind[19] = 'W';
    Handle w_h = NewHandle(sizeof wind);
    memcpy(*w_h, wind, sizeof wind);
    AddResource(w_h, 'WIND', 200, PSTR(""));
    ASSERT_EQ(noErr, ResError());

    const int entries = 256;
    Handle p_h = NewHandleClear(16 + 16 * entries);
    (*p_h)[0] = entries >> 8;
    (*p_h)[1] = entries & 0xFF;
    for(int i = 0; i < entries; i++)
    {
        unsigned char *e = (unsigned char *)*p_h + 16 + 16 * i;
        e[0] = e[1] = i;                        /* a grey ramp, pmTolerant */
        e[2] = e[3] = i;
        e[4] = e[5] = i;
        e[7] = 2;
    }
    AddResource(p_h, 'pltt', 200, PSTR(""));
    ASSERT_EQ(noErr, ResError());
    WriteResource(w_h);
    WriteResource(p_h);

    auto once = [] {
        WindowPtr w = GetNewCWindow(200, nullptr, (WindowPtr)-1);
        if(!w)
            return false;
        bool attached = GetPalette(w) != nullptr;
        DisposeWindow(w);
        return attached;
    };
    MaxApplZone();
    ASSERT_TRUE(once()) << "the pltt is attached";
    long before = FreeMem();
    std::string steps;
    for(int i = 0; i < 10; i++)
    {
        long f = FreeMem();
        once();
        steps += " " + std::to_string(f - FreeMem());
    }
    long after = FreeMem();

    CloseResFile(ref);
    FSDelete(name, 0);
    EXPECT_LE(after, before - 10 * 4096) << "before " << before << ", after " << after << "; each:" << steps;
}
