#include "gtest/gtest.h"

#include "compat.h"
#include <Appearance.h>
#include <MemoryMgr.h>
#include <WindowMgr.h>
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
    DisposeWindow(w);
}
