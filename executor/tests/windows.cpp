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

/* GetNewCWindow attaches the 'pltt' of the window's ID, and the window's
   disposal leaves that palette in the heap: a 7.6.1 Quadra keeps 4144
   bytes per window (not purgeable, PurgeMem gives nothing back) whatever
   the window's visibility, storage (nil or a pointer of the caller's, as
   Speedometer 4.0 gives), CloseWindow or DisposeWindow, or the palette's
   private word (Speedometer's pltt 507 has 0x2004). Speedometer opens 24
   such windows over its five colour depths on the Quadra, about 72 per
   depth on Executor (faster QuickDraw, more repetitions): its heap fills
   only there. */
static long retained_per_window(bool visible, bool own_storage, bool close_only, unsigned short priv, std::string *steps)
{
    StringPtr name = PSTR("test-wind-pltt");
    CreateResFile(name);
    short ref = OpenResFile(name);
    if(ref == -1)
        return -1;

    unsigned char wind[18 + 2] = {};
    const short wr[4] = { 60, 60, 160, 260 };
    for(int i = 0; i < 4; i++)
    {
        wind[2 * i] = wr[i] >> 8;
        wind[2 * i + 1] = wr[i] & 0xFF;
    }
    wind[9] = 4;                                /* noGrowDocProc, no go-away */
    wind[11] = visible;
    wind[18] = 1;
    wind[19] = 'W';
    Handle w_h = NewHandle(sizeof wind);
    memcpy(*w_h, wind, sizeof wind);
    AddResource(w_h, 'WIND', 200, PSTR(""));

    const int entries = 256;
    Handle p_h = NewHandleClear(16 + 16 * entries);
    (*p_h)[0] = entries >> 8;
    (*p_h)[1] = entries & 0xFF;
    (*p_h)[6] = priv >> 8;                      /* the private word, as in the resource */
    (*p_h)[7] = priv & 0xFF;
    for(int i = 0; i < entries; i++)
    {
        unsigned char *e = (unsigned char *)*p_h + 16 + 16 * i;
        e[0] = e[1] = i;                        /* a grey ramp, pmTolerant */
        e[2] = e[3] = i;
        e[4] = e[5] = i;
        e[7] = 2;
    }
    AddResource(p_h, 'pltt', 200, PSTR(""));
    WriteResource(w_h);
    WriteResource(p_h);

    int state = -1;
    auto once = [&] {
        /* own storage as Speedometer 4.0 gives it: a pointer of its own,
           which DisposeWindow then frees */
        Ptr storage = own_storage ? NewPtr(170) : nullptr;
        WindowPtr w = GetNewCWindow(200, storage, (WindowPtr)-1);
        if(!w)
            return false;
        bool attached = GetPalette(w) != nullptr;
        if(attached)
            state = HGetState((Handle)GetPalette(w));
        if(close_only)
            CloseWindow(w);
        else
            DisposeWindow(w);
        if(close_only)
            DisposePtr((Ptr)w);
        return attached;
    };
    MaxApplZone();
    long per = -1;
    if(once())
    {
        long before = FreeMem();
        for(int i = 0; i < 10; i++)
        {
            long f = FreeMem();
            once();
            *steps += " " + std::to_string(f - FreeMem());
        }
        per = (before - FreeMem()) / 10;
        long f = FreeMem();
        PurgeMem(0x7FFFFFFF);
        *steps += " | state " + std::to_string(state) + ", PurgeMem gave " + std::to_string(FreeMem() - f)
            + ", CompactMem " + std::to_string(CompactMem(0x7FFFFFFF));
    }
    CloseResFile(ref);
    FSDelete(name, 0);
    return per;
}

TEST(Windows, AutomaticPaletteAfterDisposal)
{
    struct { bool visible, own, close; unsigned short priv; } ways[] = {
        { false, false, false, 0 }, { true, true, false, 0 },
        { true, true, false, 0x2004 }, { true, true, false, 0x2000 }, { true, true, false, 0x0004 },
        { false, false, false, 0x2000 }, { true, false, true, 0x2000 },
    };
    std::string report;
    for(auto &w : ways)
    {
        std::string steps;
        long per = retained_per_window(w.visible, w.own, w.close, w.priv, &steps);
        report = std::string("visible ") + (w.visible ? "1" : "0") + " storage " + (w.own ? "own" : "nil")
            + (w.close ? " CloseWindow" : " DisposeWindow") + " private " + std::to_string(w.priv) + ": " + std::to_string(per) + " per window;" + steps;
        EXPECT_GE(per, 4096) << report;
    }
}
