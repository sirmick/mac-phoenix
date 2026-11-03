#include "gtest/gtest.h"

#include "compat.h"

#ifdef EXECUTOR
#include <WindowMgr.h>
#include <MemoryMgr.h>
#else
#include <Windows.h>
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
