/* HyperCard 2.4.1 calls _AE_hdlr_table_alloc (AppleEvents.yaml, trap
   $A816/$092E) with a size argument other than 16, the only value the
   routine used to accept: it crashed with a failed `unknown_1 == 16`
   assertion (AE_hdlr.cpp). Confirm the call now answers instead of
   aborting, whatever size argument it's given. */
#include "gtest/gtest.h"

#include <base/common.h>
#include <AppleEvents.h>
#include <MemoryMgr.h>

using namespace Executor;

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
    OSErr err = C__AE_hdlr_table_alloc(32, 0x80008, 0, 0, &table);
    EXPECT_EQ(noErr, err) << "used to fail a `unknown_1 == 16' assertion";
    EXPECT_NE(nullptr, (AE_hdlr_table_h)table);
    if((AE_hdlr_table_h)table)
        DisposeHandle((Handle)(AE_hdlr_table_h)table);
}
