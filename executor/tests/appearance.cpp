#include "gtest/gtest.h"

#include "compat.h"
#include <Appearance.h>
#include <MemoryMgr.h>
#include <WindowMgr.h>
#include <WindowMgr8.h>
#include <vector>
#include <base/common.h>
#include <base/cpu.h>

using namespace Executor;

namespace
{
Rect titleRect;
int titleCalls;

/* pascal void title(const Rect *bounds, ThemeTabStyle, ThemeTabDirection,
   SInt16 depth, Boolean isColorDev, UInt32 userData) */
syn68k_addr_t title(syn68k_addr_t, void *)
{
    syn68k_addr_t ret = POPADDR();
    EM_A7 += 4 + 2 + 2 + 2 + 2;                         /* userData .. style */
    titleRect = *(Rect *)SYN68K_TO_US(POPUL());
    titleCalls++;
    return ret;
}
} // namespace

/* DrawThemeTab hands its title proc the room between the tab's sloped
   sides, 12 pixels in from each, and the tab's 21 rows: 8.6's tab CDEF
   makes a tab its title's width + 24 and draws the title at that rect's
   left, centred down it (ColorSync's tabs on the G3). */
TEST(Appearance, TabTitleRect)
{
    Rect wr = { 50, 50, 250, 350 };
    WindowPtr w = NewCWindow(nullptr, &wr, (StringPtr) "", false, documentProc,
                             (WindowPtr)-1, false, 0);
    SetPort(w);
    auto proc = (ThemeTabTitleDrawUPP)SYN68K_TO_US(callback_install(title, nullptr));
    titleCalls = 0;
    Rect tab = { 10, 93, 34, 163 };
    EXPECT_EQ(noErr, C_DrawThemeTab(&tab, kThemeTabNonFront, kThemeTabNorth, proc, 0));
    ASSERT_EQ(1, titleCalls);
    EXPECT_EQ(10, titleRect.top);
    EXPECT_EQ(105, titleRect.left);
    EXPECT_EQ(31, titleRect.bottom);
    EXPECT_EQ(151, titleRect.right);
    /* sideways tabs: inset along their length, their 21 rows from the
       outer edge (the front tab is taller: its title keeps the same rows) */
    Rect side = { 93, 10, 163, 34 };
    EXPECT_EQ(noErr, C_DrawThemeTab(&side, kThemeTabFront, kThemeTabEast, proc, 0));
    EXPECT_EQ(105, titleRect.top);
    EXPECT_EQ(151, titleRect.bottom);
    EXPECT_EQ(13, titleRect.left);
    EXPECT_EQ(34, titleRect.right);
    EXPECT_EQ(noErr, C_DrawThemeTab(&side, kThemeTabFront, kThemeTabWest, proc, 0));
    EXPECT_EQ(10, titleRect.left);
    EXPECT_EQ(31, titleRect.right);
    DisposeWindow(w);
}

/* WindowsLib's monitor desktop lists, as the G3's 8.6 answers
   (tests/guest/difftest desk): one 372-byte record a screen from 1, the
   alignment 1 to 5 (5 a flag of its own), paramErr out of range; the
   8.6 Appearance control panel took an empty screen rectangle from the
   record it didn't get and Executor divided by zero mapping onto it. */
TEST(Appearance, MonitorDesktopList)
{
    GUEST<Ptr> list = nullptr;
    ASSERT_EQ(noErr, C_GetCurrentMonitorDesktopList(&list));
    ASSERT_NE(nullptr, (Ptr)list);
    GUEST<UInt32> n = 0;
    EXPECT_EQ(noErr, C_CountMonitorDesktopRecordsInList(list, &n));
    EXPECT_EQ(1u, (UInt32)n);

    std::vector<uint8_t> rec(0x200, 0xA5);
    EXPECT_EQ(paramErr, C_GetMonitorDesktopRecordFromList(list, 0, (Ptr)rec.data()));
    EXPECT_EQ(paramErr, C_GetMonitorDesktopRecordFromList(list, 2, (Ptr)rec.data()));
    EXPECT_EQ(0xA5, rec[0]);
    ASSERT_EQ(noErr, C_GetMonitorDesktopRecordFromList(list, 1, (Ptr)rec.data()));
    EXPECT_EQ(0xA5, rec[0x174]);
    EXPECT_EQ(0, rec[0x173]);
    EXPECT_EQ(LM(MainDevice), *(GUEST<GDHandle> *)&rec[0x18]);
    EXPECT_EQ(nullptr, *(GUEST<Handle> *)&rec[0]);

    GUEST<UInt32> align = 99;
    EXPECT_EQ(noErr, C_GetMonitorDesktopAlignment(list, 1, &align));
    EXPECT_EQ(0u, (UInt32)align);
    EXPECT_EQ(noErr, C_SetMonitorDesktopAlignment(list, 1, 2));
    EXPECT_EQ(paramErr, C_SetMonitorDesktopAlignment(list, 9, 2));
    C_GetMonitorDesktopRecordFromList(list, 1, (Ptr)rec.data());
    EXPECT_EQ(2u, (UInt32) * (GUEST<UInt32> *)&rec[0x0c]);
    EXPECT_EQ(paramErr, C_SetMonitorDesktopToFile(list, 1, nullptr, 0));
    EXPECT_EQ(noErr, C_SetMonitorDesktopToFile(list, 1, nullptr, 5));
    EXPECT_EQ(noErr, C_GetMonitorDesktopAlignment(list, 1, &align));
    EXPECT_EQ(5u, (UInt32)align);
    C_GetMonitorDesktopRecordFromList(list, 1, (Ptr)rec.data());
    EXPECT_EQ(0u, (UInt32) * (GUEST<UInt32> *)&rec[0x0c]);
    EXPECT_EQ(1u, (UInt32) * (GUEST<UInt32> *)&rec[0x10]);
    Boolean b = 0xA5;
    EXPECT_EQ(noErr, C_MonitorDesktopHasPicture(list, 1, &b));
    EXPECT_EQ(0, b);
    EXPECT_EQ(paramErr, C_MonitorDesktopHasPicture(list, 0, &b));

    /* a new list from the current settings: the edits weren't made current */
    GUEST<Ptr> list2 = nullptr;
    EXPECT_EQ(noErr, C_RefreshMonitorDesktopList(list, &list2));
    ASSERT_NE(nullptr, (Ptr)list2);
    EXPECT_NE((Ptr)list, (Ptr)list2);
    C_GetMonitorDesktopAlignment(list2, 1, &align);
    EXPECT_EQ(0u, (UInt32)align);
    EXPECT_EQ(noErr, C_DisposeMonitorDesktopList(list2));
    /* made current, the next list has them */
    EXPECT_EQ(noErr, C_SetCurrentMonitorDesktopList(list));
    EXPECT_EQ(noErr, C_GetCurrentMonitorDesktopList(&list2));
    C_GetMonitorDesktopAlignment(list2, 1, &align);
    EXPECT_EQ(5u, (UInt32)align);
    EXPECT_EQ(noErr, C_DisposeMonitorDesktopList(list2));
    EXPECT_EQ(noErr, C_DisposeMonitorDesktopList(list));
}

TEST(Appearance, SystemFontName)
{
    Str255 name;
    name[0] = 0;
    EXPECT_EQ(noErr, C_GetSystemFont(-1, name));
    EXPECT_GT(name[0], 0);
}
