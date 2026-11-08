/* MacPhoenix: what Apple's QuickTime components need from Executor
   without QuickTime's INIT (docs/executor/QuickTime.md, route B): the
   Component Manager's cmpFastDispatch, _HeapDispatch's handle selectors,
   and Gestalt 'qtim'/'icmp' once Executor's own QuickTime is there. Each
   from 68k code, as the components call them. */
#include "gtest/gtest.h"
#include "compat.h"

#include <Components.h>
#include <Gestalt.h>
#include <MemoryMgr.h>
#include <QuickTime.h>
#include <base/cpu.h>
#include <rsys/quicktime.h>

#include <initializer_list>

using namespace Executor;

namespace
{
/* 68k words into a new block; its address */
uint32_t code(std::initializer_list<uint32_t> words)
{
    Ptr p = NewPtrClear(words.size() * 2 + 16);
    uint32_t at = ptr_to_longint(p);
    for(uint32_t w : words)
    {
        WRITEUW(at, w);
        at += 2;
    }
    return ptr_to_longint(p);
}

uint32_t run(uint32_t entry)
{
    EM_A7 = ptr_to_longint(LM(MemTop)) - 0x100;
    uint32_t sp = EM_A7;
    destroy_blocks(0, ~0);
    execute68K(entry);
    return sp;
}
}

/* A fast-dispatch component: entered with D0 the header long, the caller's
   parameters on the stack with the storage after them and the result
   above. It saves D0, answers p1 + storage for a call of its own (0 for
   the Component Manager's open and close) and pops its arguments as a
   Pascal routine would. */
TEST(QTGroundwork, FastDispatchComponent)
{
    Ptr seen = NewPtrClear(4);
    uint32_t s = ptr_to_longint(seen);
    uint32_t entry = code({
        0x205F,                        /* movea.l (sp)+,a0 */
        0x23C0, s >> 16, s & 0xFFFF,   /* move.l d0,seen */
        0x2200, 0x4841, 0x0241, 0x00FF, /* move.l d0,d1; swap d1; andi.w #$FF,d1 */
        0x7400,                        /* moveq #0,d2 */
        0x4A40, 0x6B06,                /* tst.w d0; bmi.s done */
        0x2417,                        /* move.l (sp),d2 */
        0xD4B7, 0x1000,                /* add.l 0(sp,d1.w),d2 */
        0x4FF7, 0x1004,                /* done: lea 4(sp,d1.w),sp */
        0x2E82,                        /* move.l d2,(sp) */
        0x4ED0,                        /* jmp (a0) */
    });
    ComponentDescription *cd = (ComponentDescription *)NewPtrClear(sizeof(ComponentDescription));
    cd->componentType = "qtgw"_4;
    cd->componentSubType = "fast"_4;
    cd->componentManufacturer = "test"_4;
    cd->componentFlags = cmpFastDispatch;
    Component c = C_RegisterComponent(cd, (ComponentRoutineUPP)SYN68K_TO_US(entry), 0, nullptr, nullptr,
                                      nullptr);
    ASSERT_NE(nullptr, c);
    ComponentInstance ci = C_OpenComponent(c);
    ASSERT_NE(nullptr, ci) << "open answered 0";
    EXPECT_EQ(0x0004FFFFu, (uint32_t)*(GUEST<uint32_t> *)seen) << "the open call's header in D0";
    C_SetComponentInstanceStorage(ci, (Handle)SYN68K_TO_US(0x1000));

    /* clr.l -(sp); move.l #ci,-(sp); move.l #$123,-(sp);
       move.l #$00040007,-(sp); moveq #0,d0; _ComponentDispatch;
       move.l (sp)+,d3; rts */
    uint32_t i = ptr_to_longint(ci);
    uint32_t call = code({ 0x42A7, 0x2F3C, i >> 16, i & 0xFFFF, 0x2F3C, 0, 0x123, 0x2F3C, 0x0004, 0x0007,
                           0x7000, 0xA82A, 0x261F, 0x4E75 });
    uint32_t sp = run(call);
    EXPECT_EQ(sp, EM_A7);
    EXPECT_EQ(0x1123u, EM_D3) << "p1 + storage";
    EXPECT_EQ(0x00040007u, (uint32_t)*(GUEST<uint32_t> *)seen);

    EXPECT_EQ(noErr, C_CloseComponent(ci));
    EXPECT_EQ(noErr, C_UnregisterComponent(c));
}

/* _HeapDispatch 0-5: HLock .. HSetState on A0 (the state in A1), MemErr
   left alone; 6 and 8 accepted */
TEST(QTGroundwork, HeapDispatchHandleSelectors)
{
    Handle h = NewHandle(16);
    uint32_t a = ptr_to_longint(h);
    auto call = [&](int sel, uint32_t a1 = 0) {
        /* movea.l #h,a0; movea.l #a1,a1; moveq #sel,d0; _HeapDispatch; rts */
        uint32_t c = code({ 0x207C, a >> 16, a & 0xFFFF, 0x227C, a1 >> 16, a1 & 0xFFFF, 0x7000u | sel, 0xA0A4,
                            0x4E75 });
        LM(MemErr) = 123;
        run(c);
        EXPECT_EQ(123, LM(MemErr)) << "selector " << sel;
        return EM_D0;
    };
    call(0);
    EXPECT_TRUE(HGetState(h) & 0x80) << "locked";
    call(2);
    EXPECT_TRUE(HGetState(h) & 0x40) << "purgeable";
    EXPECT_EQ(0xC0u, call(4) & 0xFF) << "HGetState in D0";
    call(3);
    call(1);
    EXPECT_EQ(0, HGetState(h) & 0xC0);
    call(5, 0x80);
    EXPECT_TRUE(HGetState(h) & 0x80) << "HSetState from A1";
    EXPECT_EQ(0u, call(6, 0) & 0xFFFF);
    EXPECT_EQ(0u, call(8, a) & 0xFFFF);
    HUnlock(h);
    DisposeHandle(h);
}

/* No QuickTime yet: Gestalt says so, and the Movie Toolbox's stubs fail
   instead of pretending. */
/* QuickTime 2.5's answers, Executor's Movie Toolbox and ICM being there */
TEST(QTGroundwork, QuickTimeAnswers)
{
    ASSERT_TRUE(ROMlib_movie_toolbox_present());
    GUEST<LONGINT> v;
    EXPECT_EQ(noErr, Gestalt("qtim"_4, &v));
    EXPECT_EQ(0x02508000, v);
    EXPECT_EQ(ROMlib_icm_present() ? noErr : gestaltUndefSelectorErr, Gestalt("icmp"_4, &v));
    EXPECT_EQ(noErr, C_EnterMovies());
    /* a file that isn't open: no movie */
    GUEST<Movie> m = (Movie)SYN68K_TO_US(0x1234);
    GUEST<INTEGER> id = 0;
    EXPECT_NE(noErr, C_NewMovieFromFile(&m, 0, &id, nullptr, 0, nullptr));
    EXPECT_EQ(nullptr, (Movie)m);
}
