/* InputSprocketLib (romlib/inputsprocket.cpp): exported for PowerPC with
   every routine Slithereens and Mars Rising import, and answering as a Mac
   with InputSprocket and no devices (M1.md #15). */

#include "gtest/gtest.h"

#include "compat.h"

#include <base/common.h>
#include <base/builtinlibs.h>
#include <InputSprocket.h>
#include <MemoryMgr.h>

#include <algorithm>

using namespace Executor;

TEST(InputSprocket, ExportsTheGamesImports)
{
    auto table = builtinlibs::exportTable("pwpc"_4);
    auto it = table.find("InputSprocketLib");
    ASSERT_NE(table.end(), it);
    for(const char *name : { "ISpDevices_Activate", "ISpInit", "ISpElement_Flush", "ISpElementList_New",
                             "ISpShutdown", "ISpDevices_ExtractByClass", "ISpElement_GetSimpleState",
                             "ISpStop", "ISpDevices_Deactivate", "ISpConfigure",
                             "ISpElement_NewVirtualFromNeeds", "ISpSuspend", "ISpTickle", "ISpResume",
                             "ISpDevices_ActivateClass" })
        EXPECT_NE(it->second.end(), std::find(it->second.begin(), it->second.end(), name)) << name;
}

TEST(InputSprocket, NoDevicesIdleElements)
{
    GUEST<UInt32> count = 99;
    EXPECT_EQ(noErr, C_ISpDevices_ExtractByClass(kISpDeviceClass_Joystick, 4, &count, nullptr));
    EXPECT_EQ(0u, (UInt32)count);

    ISpNeed *needs = (ISpNeed *)NewPtrClear(3 * sizeof(ISpNeed));
    needs[0].theKind = kISpElementKind_Button;
    needs[1].theKind = kISpElementKind_Axis;
    needs[2].theKind = kISpElementKind_Movement;
    GUEST<ISpElementReference> *els = (GUEST<ISpElementReference> *)NewPtrClear(3 * 4);
    ASSERT_EQ(noErr, C_ISpElement_NewVirtualFromNeeds(3, needs, els, 0));
    EXPECT_EQ(noErr, C_ISpInit(3, needs, els, "test"_4, 0, 0, 0, 0));

    GUEST<UInt32> state = 99;
    EXPECT_EQ(noErr, C_ISpElement_GetSimpleState(els[0], &state));
    EXPECT_EQ((UInt32)kISpButtonUp, (UInt32)state);
    EXPECT_EQ(noErr, C_ISpElement_GetSimpleState(els[1], &state));
    EXPECT_EQ((UInt32)kISpAxisMiddle, (UInt32)state);
    uint8_t move[12];
    memset(move, 0xAA, sizeof move);
    EXPECT_EQ(noErr, C_ISpElement_GetComplexState(els[2], sizeof move, move));
    /* idle movement: both axes centred ($7FFFFFFF), the pad idle (0) */
    const uint8_t idle[12] = { 0x7F, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0 };
    EXPECT_EQ(0, memcmp(idle, move, sizeof move));

    GUEST<ISpElementListReference> list;
    ASSERT_EQ(noErr, C_ISpElementList_New(3, els, &list, 0));
    ISpElementEvent *ev = (ISpElementEvent *)NewPtrClear(sizeof(ISpElementEvent));
    Boolean was = true;
    EXPECT_EQ(noErr, C_ISpElementList_GetNextEvent(list, sizeof(ISpElementEvent), ev, &was));
    EXPECT_FALSE(was);
    EXPECT_EQ(noErr, C_ISpElementList_Dispose(list));
    EXPECT_EQ(noErr, C_ISpElement_DisposeVirtual(3, els));
    EXPECT_EQ(paramErr, C_ISpElement_GetSimpleState(nullptr, &state));
    DisposePtr((Ptr)ev);
    DisposePtr((Ptr)els);
    DisposePtr((Ptr)needs);
}
