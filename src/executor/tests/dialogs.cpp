#include "gtest/gtest.h"

#include "compat.h"
#include <DialogMgr.h>
#include <ControlMgr.h>
#include <MemoryMgr.h>
#include <dial/dial.h>
#include <ProcessMgr.h>
#include <quickdraw/cquick.h>
#include <wind/wind.h>

using namespace Executor;

/* A dialog with OK (1) and Cancel (2) buttons. */
struct Dialogs : public testing::Test
{
    DialogPtr dialog;

    Dialogs()
    {
        static const uint8_t items[] = {
            0x00, 0x01,                                     /* 2 items */
            0, 0, 0, 0, 0x00, 0x50, 0x00, 0xA0, 0x00, 0x64, 0x00, 0xF0,
            ctrlItem | btnCtrl, 2, 'O', 'K',
            0, 0, 0, 0, 0x00, 0x50, 0x00, 0x20, 0x00, 0x64, 0x00, 0x70,
            ctrlItem | btnCtrl, 6, 'C', 'a', 'n', 'c', 'e', 'l',
        };
        Handle h = NewHandle(sizeof(items));
        memcpy(*h, items, sizeof(items));
        Rect r = { 50, 50, 170, 350 };
        dialog = NewDialog(nullptr, &r, (StringPtr) "", false, dBoxProc,
                           (WindowPtr)-1, false, 0, h);
    }

    ~Dialogs()
    {
        DisposeDialog(dialog);
    }

    static EventRecord key(char c, int16_t keycode, int16_t modifiers = 0)
    {
        EventRecord evt = {};
        evt.what = keyDown;
        evt.message = (keycode << 8) | (uint8_t)c;
        evt.modifiers = modifiers;
        return evt;
    }
};

/* The standard filter (ROMlib_myfilt) presses this item on a cancel event
   (its button flash needs the 60 Hz tick, which the tests don't have). */
TEST_F(Dialogs, CancelItemIsKeptPerDialog)
{
    EXPECT_EQ(0, ROMlib_dialog_cancel_item(dialog));
    SetDialogCancelItem(dialog, 2);
    EXPECT_EQ(2, ROMlib_dialog_cancel_item(dialog));
    SetDialogCancelItem(dialog, 0);
    EXPECT_EQ(0, ROMlib_dialog_cancel_item(dialog));
}

TEST_F(Dialogs, EscapeAndCommandPeriodCancel)
{
    EventRecord evt = key(0x1B, 0x35);
    EXPECT_TRUE(IsCancelEvent(&evt));
    evt = key('.', 0x2F, cmdKey);
    EXPECT_TRUE(IsCancelEvent(&evt));
    evt = key('.', 0x2F);
    EXPECT_FALSE(IsCancelEvent(&evt));
    evt = key('\r', 0x24);
    EXPECT_FALSE(IsCancelEvent(&evt));
}

/* 8.6's Navigation Services (BBEdit Lite 6.1's Save) makes an invisible,
   empty window with no title and no storage. */
TEST(Windows, NewWindowWithNilTitle)
{
    Rect r = { 0, 0, -1, -1 };
    WindowPtr w = NewWindow(nullptr, &r, nullptr, false, 0, nullptr, false, 0);
    ASSERT_NE(nullptr, w);
    GUEST<StringHandle> h = ((WindowPeek)w)->titleHandle;
    ASSERT_NE(nullptr, (StringHandle)h);
    EXPECT_EQ(0, (*(StringHandle)h)[0]);
    DisposeWindow(w);
}

/* LayerDispatch 19, which 8.6's Drag Manager asks for the window under the
   mouse in any process: the window itself and its part; nothing over the
   desktop (a drop there is the desktop's). */
TEST(Windows, FindWindowInAnyLayer)
{
    Rect r = { 100, 100, 200, 300 };
    WindowPtr w = NewWindow(nullptr, &r, (StringPtr) "", true, documentProc,
                            (WindowPtr)-1, false, 0);
    ASSERT_NE(nullptr, w);
    GUEST<WindowPtr> found = (WindowPtr)-1;
    EXPECT_EQ(inContent, FindWindowInAnyLayer(Point{ 150, 200 }, &found));
    EXPECT_EQ(w, (WindowPtr)found);
    found = (WindowPtr)-1;
    EXPECT_EQ(inDesk, FindWindowInAnyLayer(Point{ 300, 400 }, &found));
    EXPECT_EQ(nullptr, (WindowPtr)found);
    DisposeWindow(w);
}

/* Finder's desktop window takes the desktop layer's visRgn (TakeDesktopLayer)
   and has no structure of its own; a window opened in front takes its area
   out of that visRgn all the same (a disk put in then drew its icon, in the
   desktop's update, over the window). */
TEST(Windows, DesktopWindowLosesWhatAWindowInFrontCovers)
{
    GUEST<WindowPtr> layer;
    ASSERT_EQ(noErr, C_TakeDesktopLayer(&layer));
    Rect all = { 20, 0, 480, 640 };
    WindowPtr desk = NewWindow(nullptr, &all, (StringPtr) "", true, plainDBox, nullptr, false, 0);
    ASSERT_NE(nullptr, desk);
    RgnHandle own = PORT_VIS_REGION(desk);
    PORT_VIS_REGION(desk) = PORT_VIS_REGION((WindowPtr)layer);
    SetEmptyRgn(WINDOW_STRUCT_REGION(desk));
    CalcVis((WindowPeek)desk);
    EXPECT_TRUE(PtInRgn(Point{ 150, 200 }, PORT_VIS_REGION(desk)));

    Rect r = { 100, 100, 200, 300 };
    WindowPtr front = NewWindow(nullptr, &r, (StringPtr) "", true, documentProc, (WindowPtr)-1, false, 0);
    ASSERT_NE(nullptr, front);
    /* as when the window came up: the desktop's visRgn the whole desktop,
       what's behind the window worked out again */
    CopyRgn(LM(GrayRgn), PORT_VIS_REGION(desk));
    CalcVisBehind((WindowPeek)front, WINDOW_STRUCT_REGION(front));
    EXPECT_FALSE(PtInRgn(Point{ 150, 200 }, PORT_VIS_REGION(desk)));
    EXPECT_TRUE(PtInRgn(Point{ 300, 400 }, PORT_VIS_REGION(desk)));

    DisposeWindow(front);
    PORT_VIS_REGION(desk) = own;
    DisposeWindow(desk);
}
