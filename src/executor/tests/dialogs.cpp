#include "gtest/gtest.h"

#include "compat.h"
#include <DialogMgr.h>
#include <ControlMgr.h>
#include <MemoryMgr.h>
#include <dial/dial.h>

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
