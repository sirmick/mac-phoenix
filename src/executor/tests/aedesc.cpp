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

/* AECountItems of what isn't a list (8.6's Navigation Services counts a
   null descriptor when GameBox adds a game): errAEWrongDataType, not a
   read through the nil data handle (a host crash). */
TEST(AEDesc, CountItemsOfNonList)
{
    GUEST<int32_t> count = 7;
    AEDesc null = { typeNull, nullptr };
    EXPECT_EQ(errAEWrongDataType, C_AECountItems(&null, &count));
    AEDesc text;
    ASSERT_EQ(noErr, C_AECreateDesc(typeChar, "abc", 3, &text));
    EXPECT_EQ(errAEWrongDataType, C_AECountItems(&text, &count));
    EXPECT_EQ(7, (int32_t)count);
    C_AEDisposeDesc(&text);

    AEDescList list;
    ASSERT_EQ(noErr, C_AECreateList(nullptr, 0, false, &list));
    ASSERT_EQ(noErr, C_AEPutPtr(&list, 1, typeChar, "x", 1));
    EXPECT_EQ(noErr, C_AECountItems(&list, &count));
    EXPECT_EQ(1, (int32_t)count);
    C_AEDisposeDesc(&list);
}
