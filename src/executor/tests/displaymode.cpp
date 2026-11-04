#include "gtest/gtest.h"

#include "compat.h"
#include <Displays.h>
#include <AppleEvents.h>
#include <MemoryMgr.h>
#include <WindowMgr.h>
#include <QuickDraw.h>
#include <base/common.h>
#include <base/cpu.h>
#include <VDriver.h>
#include <ToolboxEvent.h>
#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include <rsys/displaymode.h>
#include <vdriver/vdriver.h>

#include <string>
#include <vector>

/* The screen's resolution changed live, as the Display Manager does
   (quickdraw/displays.cpp; measured with tests/guest/dmprobe on the
   Quadra's 7.6.1 and the G3's 8.6, docs/executor/DisplayModes.md). */

using namespace Executor;

namespace
{
Rect R(int top, int left, int bottom, int right)
{
    return Rect{ (INTEGER)top, (INTEGER)left, (INTEGER)bottom, (INTEGER)right };
}

const Rect big = R(0, 0, 768, 1024), small = R(0, 0, 480, 640);

Point offset(Rect s, int content_top, Rect from = big, Rect to = small)
{
    return ROMlib_dm_window_offset(s, content_top, from, to);
}
}

/* Where the Mac put each window, 1024x768 to 640x480 (struct rects; the
   content's top is the title bar's bottom). */
TEST(DisplayMode, WindowOffsetMeasured)
{
    /* title bar on screen: stays (SimpleText at 304,340) */
    Point d = offset(R(321, 303, 1051, 921), 340);
    EXPECT_EQ(0, d.h);
    EXPECT_EQ(0, d.v);
    /* below the new bottom: up to it, left by what ran past the right
       edge (SimpleText at 504,640 -> 120,480; at 400,530 -> 23,480) */
    d = offset(R(621, 503, 1351, 1121), 640);
    EXPECT_EQ(-384, d.h);
    EXPECT_EQ(-160, d.v);
    d = offset(R(511, 399, 1241, 1017), 530);
    EXPECT_EQ(-377, d.h);
    EXPECT_EQ(-50, d.v);
    /* past the new right edge only (SimpleText at 700,110 -> 316,110) */
    d = offset(R(91, 699, 821, 1317), 110);
    EXPECT_EQ(-384, d.h);
    EXPECT_EQ(0, d.v);
    /* left edge on screen already: only up (SimpleText at 0,580) */
    d = offset(R(561, -1, 1291, 617), 580);
    EXPECT_EQ(0, d.h);
    EXPECT_EQ(-100, d.v);
    /* the probe's window on 8.6 (3840x2160 to 640x480): 2018,3534 -> 458,347 */
    d = offset(R(2018, 3534, 2147, 3827), 2040, R(0, 0, 2160, 3840));
    EXPECT_EQ(347 - 3534, d.h);
    EXPECT_EQ(458 - 2018, d.v);
}

/* A larger screen moves nothing. */
TEST(DisplayMode, WindowOffsetGrowing)
{
    Point d = offset(R(461, 357, 582, 640), 480, small, big);
    EXPECT_EQ(0, d.h);
    EXPECT_EQ(0, d.v);
}

namespace
{
std::vector<std::string> calls;

std::string ae_rect(AppleEvent *ae, OSType outer)
{
    AEDesc dspl, display, rec;
    std::string out;
    if(AEGetParamDesc(ae, "dspl"_4, typeAERecord, &dspl) != noErr)
        return "no dspl";
    if(AEGetParamDesc(&dspl, 1, typeAERecord, &display) == noErr)
    {
        if(AEGetParamDesc(&display, outer, typeAERecord, &rec) == noErr)
        {
            GUEST<Rect> r;
            GUEST<DescType> t;
            GUEST<Size> n;
            if(AEGetParamPtr(&rec, "dddr"_4, "qdrt"_4, &t, &r, sizeof r, &n) == noErr)
            {
                Rect rr = r;
                out = std::to_string(rr.bottom) + "x" + std::to_string(rr.right);
            }
            AEDisposeDesc(&rec);
        }
        AEDisposeDesc(&display);
    }
    AEDisposeDesc(&dspl);
    return out;
}

/* pascal void (AppleEvent *theEvent) */
syn68k_addr_t notify(syn68k_addr_t, void *)
{
    syn68k_addr_t ret = POPADDR();
    AppleEvent *ae = (AppleEvent *)SYN68K_TO_US(POPUL());
    GUEST<OSType> v;
    GUEST<DescType> t;
    GUEST<Size> n;
    std::string id = "?";
    if(AEGetAttributePtr(ae, keyEventIDAttr, typeType, &t, &v, 4, &n) == noErr)
    {
        OSType o = v;
        id = std::string{ char(o >> 24), char(o >> 16), char(o >> 8), char(o) };
    }
    calls.push_back("notify " + id + " " + ae_rect(ae, "dold"_4) + "->" + ae_rect(ae, "dnew"_4));
    return ret;
}

/* pascal void (void *userData, short message, void *notifyData) */
syn68k_addr_t ext_notify(syn68k_addr_t, void *)
{
    syn68k_addr_t ret = POPADDR();
    uint32_t data = POPUL();
    int16_t message = POPUW();
    uint32_t user = POPUL();
    calls.push_back("ext " + std::to_string(message) + (data ? " data" : "") + " user "
                    + std::to_string(user));
    return ret;
}
}

/* DMSetDisplayMode to another resolution of the list: the GDevice, the
   screen's port, screenBits, GrayRgn and the windows have the new screen
   when it returns, and this process's procs have been told, extended ones
   first (Prep, Dependents, Event), with an 'aevt'/'cnfg' event. */
TEST(DisplayMode, SetDisplayModeChangesTheScreen)
{
    GDHandle gd = LM(MainDevice);
    Rect before = GD_RECT(gd);
    ASSERT_EQ(512, before.right);
    ASSERT_EQ(342, before.bottom);

    /* a window near the bottom right */
    Rect wr = R(280, 400, 330, 500);
    WindowPtr w = NewWindow(nullptr, &wr, (StringPtr) "\x01W", true, documentProc,
                            (WindowPtr)-1, false, 0);
    ASSERT_NE(nullptr, w);

    calls.clear();
    auto np = (DMNotificationUPP)SYN68K_TO_US(callback_install(notify, nullptr));
    auto xp = (ProcPtr)SYN68K_TO_US(callback_install(ext_notify, nullptr));
    ProcessSerialNumber me = { 0, kCurrentProcess };
    EXPECT_EQ(noErr, DMRegisterNotifyProc(np, &me));
    EXPECT_EQ(noErr, DMRegisterExtendedNotifyProc(xp, (void *)SYN68K_TO_US(1234), 0, &me));
    ASSERT_EQ(1u, calls.size());
    EXPECT_EQ("ext 1 user 1234", calls[0]);         /* kDMNotifyInstalled */

    /* the list has the classic sizes, 640x480 among them */
    uint32_t id640 = 0;
    for(const DisplayResolution &r : ROMlib_dm_resolutions())
        if(r.width == 640 && r.height == 480)
            id640 = r.id;
    ASSERT_NE(0u, id640);
    GUEST<uint32_t> count;
    GUEST<Ptr> list;
    ASSERT_EQ(noErr, DMNewDisplayModeList(1, 0, 0, &count, &list));
    EXPECT_EQ(ROMlib_dm_resolutions().size(), (size_t)count);
    DMDisposeList(list);

    calls.clear();
    GUEST<uint32_t> depth = 0;
    ASSERT_EQ(noErr, DMSetDisplayMode(gd, id640, &depth, 0, nullptr));

    EXPECT_EQ(640, GD_RECT(gd).right);
    EXPECT_EQ(480, GD_RECT(gd).bottom);
    EXPECT_EQ(640, vdriver->width());
    EXPECT_EQ(640, qd.screenBits.bounds.right);
    EXPECT_EQ(480, qd.screenBits.bounds.bottom);
    EXPECT_EQ(480, (*LM(GrayRgn))->rgnBBox.bottom);
    EXPECT_EQ(640, (*LM(GrayRgn))->rgnBBox.right);
    EXPECT_EQ(640, PORT_RECT(LM(WMgrPort)).right);
    EXPECT_EQ(480, PORT_RECT(LM(WMgrPort)).bottom);
    /* the window stays: its title bar is still on the larger screen */
    EXPECT_EQ(wr.top, (*WINDOW_CONT_REGION(w))->rgnBBox.top);

    GUEST<VDSwitchInfoRec> sw;
    ASSERT_EQ(noErr, DMGetDisplayMode(gd, (Ptr)&sw));
    EXPECT_EQ(id640, ((VDSwitchInfoRec &)sw).csData);

    ASSERT_EQ(4u, calls.size());
    EXPECT_EQ("ext 4 user 1234", calls[0]);         /* Prep */
    EXPECT_EQ("ext 6 data user 1234", calls[1]);    /* Dependents */
    EXPECT_EQ("ext 2 data user 1234", calls[2]);    /* Event */
    EXPECT_EQ("notify cnfg 342x512->480x640", calls[3]);

    /* back to 512x342: the window, below it now, comes up */
    calls.clear();
    MoveWindow(w, 560, 400, false);
    uint32_t id512 = 0;
    for(const DisplayResolution &r : ROMlib_dm_resolutions())
        if(r.width == 512 && r.height == 342)
            id512 = r.id;
    ASSERT_EQ(noErr, DMSetDisplayMode(gd, id512, &depth, 0, nullptr));
    EXPECT_EQ(342, qd.screenBits.bounds.bottom);
    Rect c = (*WINDOW_CONT_REGION(w))->rgnBBox;
    EXPECT_EQ(342, c.top);                  /* title bar just above the bottom */
    /* and left by what ran past 640 of it (its part on the old screen) */
    EXPECT_EQ(560 - (640 - 512), c.left);
    EXPECT_EQ("notify cnfg 480x640->342x512", calls.back());

    EXPECT_EQ(noErr, DMRemoveExtendedNotifyProc(xp, (void *)SYN68K_TO_US(1234), &me, 0));
    EXPECT_EQ("ext 3 user 1234", calls.back());     /* kDMNotifyRemoved */
    EXPECT_EQ(noErr, DMRemoveNotifyProc(np, &me));
    DisposeWindow(w);
}

/* Any size the host asks for (Settings, the browser window: the request
   itself, taken at the front process's event call, is the tab cell's,
   web/tests/tab_dynres.mjs); the list then names it. */
TEST(DisplayMode, CustomSize)
{
    EXPECT_TRUE(ROMlib_dm_set_screen_size(700, 500));
    EXPECT_EQ(700, GD_RECT(LM(MainDevice)).right);
    EXPECT_EQ(500, qd.screenBits.bounds.bottom);
    bool listed = false;
    for(const DisplayResolution &r : ROMlib_dm_resolutions())
        listed |= r.width == 700 && r.height == 500;
    EXPECT_TRUE(listed);

    /* out of range: refused, the screen stays */
    EXPECT_FALSE(ROMlib_dm_set_screen_size(100, 100));
    EXPECT_EQ(700, GD_RECT(LM(MainDevice)).right);

    EXPECT_TRUE(ROMlib_dm_set_screen_size(512, 342));
    EXPECT_EQ(512, qd.screenBits.bounds.right);
}

/* In a configuration the GDevice changes at once, screenBits and GrayRgn
   at its end (measured). */
TEST(DisplayMode, ConfigurationFinishesAtItsEnd)
{
    GDHandle gd = LM(MainDevice);
    GUEST<Handle> state;
    ASSERT_EQ(noErr, DMBeginConfigureDisplays(&state));
    EXPECT_TRUE(ROMlib_dm_set_screen_size(640, 480));
    EXPECT_EQ(640, GD_RECT(gd).right);
    EXPECT_EQ(342, (*LM(GrayRgn))->rgnBBox.bottom);
    ASSERT_EQ(noErr, DMEndConfigureDisplays(state));
    EXPECT_EQ(480, (*LM(GrayRgn))->rgnBBox.bottom);
    EXPECT_EQ(480, qd.screenBits.bounds.bottom);
    EXPECT_TRUE(ROMlib_dm_set_screen_size(512, 342));
}
