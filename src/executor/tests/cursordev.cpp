/* The Cursor Device Manager (romlib/cursordev.cpp): one device, the mouse,
   first in the list; its fields set by the calls 9.0.4's Mouse control
   panel makes at startup (through _CursorDeviceDispatch from its PowerPC
   INIT); devices a program adds and disposes of. */
#include "gtest/gtest.h"

#include "compat.h"

#include <base/common.h>
#include <base/cpu.h>
#include <CursorDevices.h>
#include <MemoryMgr.h>

using namespace Executor;

static CursorDevicePtr first_device()
{
    GUEST<CursorDevicePtr> d = nullptr;
    EXPECT_EQ(noErr, CursorDeviceNextDevice(&d));
    return d;
}

TEST(CursorDevices, TheMouse)
{
    CursorDevicePtr mouse = first_device();
    ASSERT_NE(nullptr, mouse);
    EXPECT_EQ(kDeviceClassMouse, mouse->devClass);
    EXPECT_EQ(1, mouse->cntButtons);

    EXPECT_EQ(noErr, CursorDeviceDoubleTime(mouse, 32));
    EXPECT_EQ(32u, (uint32_t)mouse->doubleClickTime);
    EXPECT_EQ(noErr, CursorDeviceSetAcceleration(mouse, 0x8000));
    EXPECT_EQ(0x8000, (Fixed)mouse->acceleration);

    EXPECT_EQ(paramErr, CursorDeviceDoubleTime((CursorDevicePtr)NewPtrClear(sizeof(CursorDevice)), 1))
        << "not a device the manager knows";
}

TEST(CursorDevices, NewAndDispose)
{
    CursorDevicePtr mouse = first_device();
    GUEST<CursorDevicePtr> added = nullptr;
    ASSERT_EQ(noErr, CursorDeviceNewDevice(&added));
    ASSERT_NE(nullptr, (CursorDevicePtr)added);

    GUEST<CursorDevicePtr> d = mouse;
    int n = 1;
    bool found = false;
    while(CursorDeviceNextDevice(&d) == noErr && (CursorDevicePtr)d)
    {
        found |= (CursorDevicePtr)d == (CursorDevicePtr)added;
        n++;
    }
    EXPECT_TRUE(found);
    EXPECT_GE(n, 2);

    EXPECT_EQ(noErr, CursorDeviceDisposeDevice(added));
    EXPECT_EQ(paramErr, CursorDeviceDisposeDevice(mouse)) << "the mouse stays";
}

/* subq.w #2,sp; move.l #mouse,-(sp); move.l #45,-(sp); moveq #9,d0;
   _CursorDeviceDispatch; move.w (sp)+,d1 -- CursorDeviceDoubleTime from
   68k code, the stack balanced */
TEST(CursorDevices, DispatcherFrom68k)
{
    CursorDevicePtr mouse = first_device();
    uint32_t m = ptr_to_longint(mouse);
    Ptr code = NewPtrClear(64);
    uint32_t at = ptr_to_longint(code);
    for(uint16_t w : { 0x558F, 0x2F3C, (int)(m >> 16), (int)(m & 0xFFFF), 0x2F3C, 0x0000, 0x002D,
                       0x7009, 0xAADB, 0x321F, 0x4E75 })
    {
        WRITEUW(at, w);
        at += 2;
    }
    EM_A7 = ptr_to_longint(LM(MemTop)) - 0x100;
    uint32_t sp = EM_A7;
    destroy_blocks(0, ~0);
    execute68K(ptr_to_longint(code));
    EXPECT_EQ(sp, EM_A7);
    EXPECT_EQ(0, EM_D1 & 0xFFFF);
    EXPECT_EQ(45u, (uint32_t)mouse->doubleClickTime);
    DisposePtr(code);
}
