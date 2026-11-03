#include "gtest/gtest.h"

#include "compat.h"

#ifdef EXECUTOR
#include <WindowMgr.h>
#include <ControlMgr.h>
#include <ToolboxEvent.h>
#include <MemoryMgr.h>
#else
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#endif

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
