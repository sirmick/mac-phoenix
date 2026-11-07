/* MacPhoenix: NameRegistryLib (romlib/nameregistry.cpp): the root and
   "Devices", and the answers a G3 gives on 8.6 (a probe through CFM):
   errors, the end of an iteration, a buffer too small. */
#include "gtest/gtest.h"

#include <base/common.h>
#include <NameRegistry.h>

#include <cstring>

using namespace Executor;

TEST(NameRegistry, RootAndDevices)
{
    RegEntryID invalid, root, dev, found;
    ASSERT_EQ(noErr, C_RegistryEntryIDInit(&invalid));
    EXPECT_EQ(0xFFFE0000u, (uint32_t)invalid.contents[0]);
    GUEST<RegPathNameSize> size = 77;
    EXPECT_EQ(nrInvalidNodeErr, C_RegistryEntryToPathSize(&invalid, &size));
    EXPECT_EQ(0u, (uint32_t)size);

    RegEntryIter it;
    Boolean done = 9;
    ASSERT_EQ(noErr, C_RegistryEntryIterateCreate(&it));
    ASSERT_EQ(noErr, C_RegistryEntryIterate(&it, kRegIterRoot, &root, &done));
    EXPECT_FALSE(done);
    char path[64];
    ASSERT_EQ(noErr, C_RegistryCStrEntryToPath(&root, path, sizeof path));
    EXPECT_STREQ("root", path);
    EXPECT_EQ(nrPathBufferTooSmall, C_RegistryCStrEntryToPath(&root, path, 2));

    ASSERT_EQ(noErr, C_RegistryEntryIterate(&it, kRegIterChildren, &dev, &done));
    EXPECT_FALSE(done);
    ASSERT_EQ(noErr, C_RegistryCStrEntryToPath(&dev, path, sizeof path));
    EXPECT_STREQ("Devices", path);
    EXPECT_EQ(noErr, C_RegistryEntryIterate(&it, kRegIterContinue, &found, &done));
    EXPECT_TRUE(done);
    EXPECT_EQ(noErr, C_RegistryEntryIterate(&it, kRegIterChildren, &found, &done));
    EXPECT_TRUE(done) << "nothing below Devices";
    EXPECT_EQ(noErr, C_RegistryEntryIterateDispose(&it));

    /* properties: "name" only */
    GUEST<RegPropertyValueSize> psize = 77;
    ASSERT_EQ(noErr, C_RegistryPropertyGetSize(&root, "name", &psize));
    EXPECT_EQ(5u, (uint32_t)psize);
    psize = 77;
    EXPECT_EQ(nrNotFoundErr, C_RegistryPropertyGetSize(&root, "no-such-property", &psize));
    EXPECT_EQ(77u, (uint32_t)psize);
    char value[8] = {};
    psize = 2;
    ASSERT_EQ(noErr, C_RegistryPropertyGet(&root, "name", value, &psize));
    EXPECT_EQ(5u, (uint32_t)psize);
    EXPECT_STREQ("ro", value);
    psize = 8;
    EXPECT_EQ(nrInvalidNodeErr, C_RegistryPropertyGet(&invalid, "name", value, &psize));
    EXPECT_EQ(0u, (uint32_t)psize);

    /* a search, lookups */
    ASSERT_EQ(noErr, C_RegistryEntryIterateCreate(&it));
    ASSERT_EQ(noErr, C_RegistryEntrySearch(&it, kRegIterDescendants, &found, &done, "name", "Devices", 8));
    EXPECT_FALSE(done);
    EXPECT_TRUE(C_RegistryEntryIDCompare(&found, &dev));
    ASSERT_EQ(noErr, C_RegistryEntrySearch(&it, kRegIterContinue, &found, &done, "name", "Devices", 8));
    EXPECT_TRUE(done);
    C_RegistryEntryIterateDispose(&it);
    ASSERT_EQ(noErr, C_RegistryCStrEntryLookup(nullptr, "Devices", &found));
    EXPECT_TRUE(C_RegistryEntryIDCompare(&found, &dev));
    EXPECT_EQ(nrPathNotFound, C_RegistryCStrEntryLookup(nullptr, "devices", &found));
    EXPECT_EQ(paramErr, C_RegistryCStrEntryLookup(&root, "Devices", &found));
    EXPECT_EQ(noErr, C_RegistryCStrEntryLookup(&root, ":Devices", &found));

    ASSERT_EQ(noErr, C_RegistryEntryIDDispose(&found));
    EXPECT_EQ(0u, (uint32_t)found.contents[0]);
}
