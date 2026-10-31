/* AEDuplicateDesc of a descriptor without data (the null descriptor:
   typeNull, nil dataHandle) copies it as it is. BBEdit Lite 6.1 does this
   at startup; Executor dereferenced the nil handle (a host crash). */
#include "gtest/gtest.h"

#include <base/common.h>
#include <AppleEvents.h>
#include <MemoryMgr.h>

using namespace Executor;

TEST(AEDesc, DuplicateNullDescriptor)
{
    AEDesc src = { typeNull, nullptr };
    AEDesc dst = { typeChar, nullptr };
    EXPECT_EQ(noErr, C_AEDuplicateDesc(&src, &dst));
    EXPECT_EQ((DescType)typeNull, (DescType)dst.descriptorType);
    EXPECT_EQ(nullptr, (Handle)dst.dataHandle);
}

TEST(AEDesc, DuplicateCopiesData)
{
    AEDesc src, dst;
    EXPECT_EQ(noErr, C_AECreateDesc(typeChar, "abc", 3, &src));
    EXPECT_EQ(noErr, C_AEDuplicateDesc(&src, &dst));
    EXPECT_EQ((DescType)typeChar, (DescType)dst.descriptorType);
    ASSERT_NE(nullptr, (Handle)dst.dataHandle);
    EXPECT_NE((Handle)src.dataHandle, (Handle)dst.dataHandle);
    EXPECT_EQ(3, GetHandleSize(dst.dataHandle));
    EXPECT_EQ(0, memcmp(*dst.dataHandle, "abc", 3));
    C_AEDisposeDesc(&src);
    C_AEDisposeDesc(&dst);
}
