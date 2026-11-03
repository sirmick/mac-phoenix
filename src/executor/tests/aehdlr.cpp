/* HyperCard 2.4.1 calls _AE_hdlr_table_alloc (AppleEvents.yaml, trap
   $A816/$092E) with a size argument other than 16, the only value the
   routine used to accept: it crashed with a failed `unknown_1 == 16`
   assertion (AE_hdlr.cpp). The argument is the table's initial
   *capacity* (the ROM always passes 16; HyperCard preallocates for 175
   and 105 handlers so its many AE_hdlr_install calls don't grow the
   handle one element at a time). Confirm the call reserves that
   capacity up front, and that AE_hdlr_install only grows the handle
   once the reservation is used up. */
#include "gtest/gtest.h"

#include <base/common.h>
#include <AppleEvents.h>
#include <MemoryMgr.h>

using namespace Executor;

static int32_t table_header_size()
{
    GUEST<AE_hdlr_table_h> table = nullptr;
    C__AE_hdlr_table_alloc(0, 0x80008, 0, 0, &table);
    Size size = GetHandleSize((Handle)(AE_hdlr_table_h)table);
    DisposeHandle((Handle)(AE_hdlr_table_h)table);
    return size;
}

TEST(AEHdlr, TableAllocAcceptsTheDocumentedSize)
{
    GUEST<AE_hdlr_table_h> table = nullptr;
    OSErr err = C__AE_hdlr_table_alloc(16, 0x80008, 0, 0, &table);
    EXPECT_EQ(noErr, err);
    EXPECT_NE(nullptr, (AE_hdlr_table_h)table);
    if((AE_hdlr_table_h)table)
        DisposeHandle((Handle)(AE_hdlr_table_h)table);
}

TEST(AEHdlr, TableAllocAcceptsHyperCardsSize)
{
    GUEST<AE_hdlr_table_h> table = nullptr;
    OSErr err = C__AE_hdlr_table_alloc(175, 0x80008, 0, 0, &table);
    EXPECT_EQ(noErr, err) << "used to fail a `unknown_1 == 16' assertion";
    EXPECT_NE(nullptr, (AE_hdlr_table_h)table);
    if((AE_hdlr_table_h)table)
        DisposeHandle((Handle)(AE_hdlr_table_h)table);
}

TEST(AEHdlr, TableAllocReservesCapacity)
{
    GUEST<AE_hdlr_table_h> table = nullptr;
    OSErr err = C__AE_hdlr_table_alloc(105, 0x80008, 0, 0, &table);
    ASSERT_EQ(noErr, err);
    Size expected = table_header_size() + 105 * sizeof(AE_hdlr_table_elt_t);
    EXPECT_EQ(expected, GetHandleSize((Handle)(AE_hdlr_table_h)table));
    DisposeHandle((Handle)(AE_hdlr_table_h)table);
}

TEST(AEHdlr, InstallHonoursReservedCapacity)
{
    GUEST<AE_hdlr_table_h> table = nullptr;
    ASSERT_EQ(noErr, C__AE_hdlr_table_alloc(2, 0x80008, 0, 0, &table));
    Size reserved = GetHandleSize((Handle)(AE_hdlr_table_h)table);

    AE_hdlr_selector_t sel1 = { 1, 0 }, sel2 = { 2, 0 }, sel3 = { 3, 0 };
    AE_hdlr_t hdlr = {};

    /* Two installs fit the reserved capacity: no growth. */
    EXPECT_EQ(noErr, C__AE_hdlr_install((AE_hdlr_table_h)table, 0, &sel1, &hdlr));
    EXPECT_EQ(reserved, GetHandleSize((Handle)(AE_hdlr_table_h)table));
    EXPECT_EQ(noErr, C__AE_hdlr_install((AE_hdlr_table_h)table, 0, &sel2, &hdlr));
    EXPECT_EQ(reserved, GetHandleSize((Handle)(AE_hdlr_table_h)table));

    /* A third exceeds it: the handle grows. */
    EXPECT_EQ(noErr, C__AE_hdlr_install((AE_hdlr_table_h)table, 0, &sel3, &hdlr));
    EXPECT_GT(GetHandleSize((Handle)(AE_hdlr_table_h)table), reserved);

    DisposeHandle((Handle)(AE_hdlr_table_h)table);
}

/* Pack8 $0830 (a lookup beside install $0831 and lookup $0833): HyperCard
   2.4.1 calls it on its fresh tables; unknown, it stopped the session. */
TEST(AEHdlr, Selector0830LooksUp)
{
    GUEST<AE_hdlr_table_h> table = nullptr;
    ASSERT_EQ(noErr, C__AE_hdlr_table_alloc(175, 0x80008, 0, 0, &table));
    AE_hdlr_selector_t oapp = { "aevt"_4, "oapp"_4 };
    AE_hdlr_t got = {}, put = {};
    put.refcon = 42;
    EXPECT_EQ(errAEHandlerNotFound, C__AE_hdlr_0830((AE_hdlr_table_h)table, 0, &oapp, &got));
    EXPECT_EQ(noErr, C__AE_hdlr_install((AE_hdlr_table_h)table, 0, &oapp, &put));
    EXPECT_EQ(noErr, C__AE_hdlr_0830((AE_hdlr_table_h)table, 0, &oapp, &got));
    EXPECT_EQ(42, (int32_t)got.refcon);
    DisposeHandle((Handle)(AE_hdlr_table_h)table);
}
