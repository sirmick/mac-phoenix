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
#include <ctl/ctl.h>
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

#ifdef EXECUTOR
/* The scroll bar's thumbCntl: the mouse, held where the thumb was taken,
   goes as far as puts the thumb against either arrow and no further (a
   Finder window's bar, 7.6.1 Quadra: thumb x 26..42 at the left, its
   outline x 491..507 at the right, the right arrow's edge at 507, the
   bar 11..523). The horizontal's right limit added the grab offset: the
   outline went over the right arrow. */
TEST(Windows, ScrollBarThumbLimitsStopAtTheArrows)
{
    using namespace Executor;
    Rect r = { 40, 0, 300, 400 };
    WindowPtr w = NewWindow(nullptr, &r, PSTR(""), true, plainDBox,
                            (WindowPtr)-1, false, 0);
    ASSERT_NE(nullptr, w);
    SetPort(w);
    Rect hr = { 200, 11, 216, 323 };
    ControlHandle h = NewControl(w, &hr, PSTR(""), true, 0, 0, 1000, scrollBarProc, 0);
    Rect vr = { 50, 322, 201, 338 };
    ControlHandle v = NewControl(w, &vr, PSTR(""), true, 0, 0, 1000, scrollBarProc, 0);
    ASSERT_NE(nullptr, h);
    ASSERT_NE(nullptr, v);
    Draw1Control(h);
    Draw1Control(v);

    /* the thumbs at the minimum: x 27..43, y 66..82 */
    Point ph = { 208, 34 }; /* 7 right of the thumb's left, 9 left of its right */
    EXPECT_EQ(inThumb, TestControl(h, ph));
    thumbstr t;
    t._tlimit.left = ph.h;
    t._tlimit.top = ph.v;
    C_cdef16(0, h, thumbCntl, ptr_to_longint(&t));
    EXPECT_EQ(hAxisOnly, t._taxis);
    EXPECT_EQ(34, t._tlimit.left);
    EXPECT_EQ(299, t._tlimit.right); /* last 298: thumb 291..307, the arrow from 307 */

    Point pv = { 73, 330 };
    EXPECT_EQ(inThumb, TestControl(v, pv));
    t._tlimit.left = pv.h;
    t._tlimit.top = pv.v;
    C_cdef16(0, v, thumbCntl, ptr_to_longint(&t));
    EXPECT_EQ(vAxisOnly, t._taxis);
    EXPECT_EQ(73, t._tlimit.top);
    EXPECT_EQ(177, t._tlimit.bottom); /* last 176: thumb 169..185, the arrow from 185 */
    DisposeWindow(w);
}

/* The scroll bar's posCntl: the value from where the thumb was dropped,
   rounded, the maximum at the end of the track. The Quadra's Finder
   window bar, 512 wide (a travel of 464), -21566..-20: 100 pixels from the
   minimum is -16922; Executor answered -16933, and -66 at the end, so the
   Finder kept a sliver of range where the Mac's bar goes inactive. */
TEST(Windows, ScrollBarThumbPositionGivesTheValue)
{
    using namespace Executor;
    Rect r = { 40, 0, 300, 512 };
    WindowPtr w = NewWindow(nullptr, &r, PSTR(""), true, plainDBox,
                            (WindowPtr)-1, false, 0);
    ASSERT_NE(nullptr, w);
    SetPort(w);
    Rect hr = { 200, 0, 216, 512 };
    ControlHandle h = NewControl(w, &hr, PSTR(""), true, -21566, -21566, -20, scrollBarProc, 0);
    ASSERT_NE(nullptr, h);
    Draw1Control(h);

    C_cdef16(0, h, posCntl, 100);   /* (v << 16) | h */
    EXPECT_EQ(-16922, GetControlValue(h));

    SetControlValue(h, -21566);
    C_cdef16(0, h, posCntl, 464);
    EXPECT_EQ(-20, GetControlValue(h));

    SetControlValue(h, -21566);
    C_cdef16(0, h, posCntl, 0);
    EXPECT_EQ(-21566, GetControlValue(h));
    DisposeWindow(w);
}
#endif

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
