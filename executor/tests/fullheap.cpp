#include "gtest/gtest.h"

#include <MemoryMgr.h>
#include <QuickDraw.h>
#include <mman/mman.h>
#include <mman/mman_private.h>
#include <mman/fullheap.h>
#include <base/cpu.h>
#include <base/traps.h>

#include <initializer_list>
#include <vector>
#include <string.h>

using namespace Executor;

/* A full application heap (docs/executor/FullHeap.md): the guest's own
   NewHandle, NewPtr and NewRgn answer nil and MemErr -108, as on the
   Quadra 650 (tests/guest/fullheap); an allocation the Toolbox makes while
   serving another trap comes from the System heap, counted; one that would
   take the System heap below its floor ends the session with system
   error 25. Each test fills a zone of its own (InitZone in a block of the
   application heap) and makes it current. */
namespace
{
class FullHeap : public ::testing::Test
{
protected:
    static constexpr Size kSize = 32 * 1024;
    Ptr mem = nullptr;
    THz zone = nullptr;
    Ptr code = nullptr;
    GUEST<THz> saved;
    uint32_t fallbacks0 = 0;

    void SetUp() override
    {
        saved = LM(TheZone);
        code = NewPtrSys(64);
        ASSERT_NE(nullptr, code);
        mem = NewPtr(kSize);
        ASSERT_NE(nullptr, mem);
        InitZone(nullptr, 4, mem + kSize, (THz)mem);
        zone = (THz)mem;
        SetZone(zone);
        /* filled as the guest's own NewPtr would: no fallback meanwhile */
        traps::ServedTrap own(traps::entrypoints.at("NewPtr"));
        for(Size s : { 4096, 512, 64, 12, 1 })
            while(NewPtr(s))
                ;
        ASSERT_EQ(memFullErr, LM(MemErr));
        fallbacks0 = ROMlib_full_heap_fallbacks();
    }
    void TearDown() override
    {
        LM(TheZone) = saved;
        DisposePtr(mem);
        DisposePtr(code);
    }

    /* Runs `words` then `rts` as guest code. */
    void run(std::initializer_list<uint16_t> words)
    {
        uint32_t at = ptr_to_longint(code);
        for(uint16_t w : words)
        {
            WRITEUW(at, w);
            at += 2;
        }
        WRITEUW(at, 0x4E75); /* rts */
        EM_A7 = ptr_to_longint(LM(MemTop)) - 0x100;
        uint32_t sp = EM_A7;
        destroy_blocks(0, ~0);
        execute68K(ptr_to_longint(code));
        EXPECT_EQ(sp, EM_A7) << "the call left the stack balanced";
    }
};
}

TEST(FullHeapNames, EveryGuestAllocatorIsAnEntrypoint)
{
    for(const char *const *p = traps::guest_allocators; *p; p++)
    {
        auto it = traps::entrypoints.find(*p);
        ASSERT_NE(traps::entrypoints.end(), it) << *p;
        EXPECT_TRUE(it->second->guestAllocator) << *p;
    }
    EXPECT_FALSE(traps::entrypoints.at("TENew")->guestAllocator);
}

/* move.l #1024,d0; _NewHandle; move.l a0,d1 */
TEST_F(FullHeap, GuestNewHandleIsNil)
{
    EM_D1 = 0xDEADBEEF;
    run({ 0x203C, 0x0000, 0x0400, 0xA122, 0x2208 });
    EXPECT_EQ(0u, EM_D1);
    EXPECT_EQ(memFullErr, (int16_t)EM_D0);
    EXPECT_EQ(fallbacks0, ROMlib_full_heap_fallbacks());
}

/* move.l #1024,d0; _NewPtr; move.l a0,d1 */
TEST_F(FullHeap, GuestNewPtrIsNil)
{
    EM_D1 = 0xDEADBEEF;
    run({ 0x203C, 0x0000, 0x0400, 0xA11E, 0x2208 });
    EXPECT_EQ(0u, EM_D1);
    EXPECT_EQ(memFullErr, (int16_t)EM_D0);
    EXPECT_EQ(fallbacks0, ROMlib_full_heap_fallbacks());
}

/* subq.l #4,sp; _NewRgn; move.l (sp)+,d1 */
TEST_F(FullHeap, GuestNewRgnIsNil)
{
    EM_D1 = 0xDEADBEEF;
    run({ 0x598F, 0xA8D8, 0x221F });
    EXPECT_EQ(0u, EM_D1);
    EXPECT_EQ(memFullErr, LM(MemErr));
    EXPECT_EQ(fallbacks0, ROMlib_full_heap_fallbacks());
}

/* A trap whose own allocation isn't one of the guest's: NewString
   (subq.l #4,sp; pea str; _NewString; move.l (sp)+,d1) makes its handle
   in the System heap. */
TEST_F(FullHeap, ToolboxAllocationFallsBackToSystemHeap)
{
    uint32_t str = ptr_to_longint(code) + 40;
    WRITEUB(str, 3);
    WRITEUB(str + 1, 'a');
    WRITEUB(str + 2, 'b');
    WRITEUB(str + 3, 'c');
    run({ 0x598F, 0x4879, (uint16_t)(str >> 16), (uint16_t)str, 0xA906, 0x221F });
    ASSERT_NE(0u, EM_D1);
    Handle h = (Handle)SYN68K_TO_US(EM_D1);
    EXPECT_EQ(LM(SysZone), HandleZone(h));
    EXPECT_EQ(3, (*h)[0]);
    EXPECT_EQ(fallbacks0 + 1, ROMlib_full_heap_fallbacks());
    DisposeHandle(h);
}

/* Host code serving a drawing trap: its region comes from the System
   heap; serving the guest's NewRgn it is nil. */
TEST_F(FullHeap, RegionByTheTrapServed)
{
    {
        traps::ServedTrap t(traps::entrypoints.at("FrameOval"));
        RgnHandle r = NewRgn();
        ASSERT_NE(nullptr, r);
        EXPECT_EQ(LM(SysZone), HandleZone((Handle)r));
        DisposeRgn(r);
    }
    {
        traps::ServedTrap t(traps::entrypoints.at("NewRgn"));
        EXPECT_EQ(nullptr, NewRgn());
    }
    EXPECT_EQ(fallbacks0 + 1, ROMlib_full_heap_fallbacks());
}

/* With the zone's master pointers used up, MoreMasters' block stays in the
   zone: from the System heap it would put masters of the zone's handles
   outside it (Speedometer's fifth depth: a corrupt heap in DrawPicture).
   The handle comes from the System heap whole. */
TEST_F(FullHeap, MasterPointersStayInTheirZone)
{
    {
        traps::ServedTrap own(traps::entrypoints.at("NewEmptyHandle"));
        while(NewEmptyHandle())
            ;
    }
    ASSERT_EQ(nullptr, (Ptr)ZONE_HFST_FREE(zone));
    traps::ServedTrap t(traps::entrypoints.at("TENew"));
    Handle h = NewHandle(16);
    ASSERT_NE(nullptr, h);
    EXPECT_EQ(LM(SysZone), HandleZone(h));
    EXPECT_EQ(nullptr, (Ptr)ZONE_HFST_FREE(zone)) << "no master pointers from elsewhere";
    EXPECT_EQ(fallbacks0 + 1, ROMlib_full_heap_fallbacks());
    DisposeHandle(h);
}

TEST_F(FullHeap, FloorDecides)
{
    int32_t room = ROMlib_full_heap_system_room();
    ASSERT_GT(room, FULL_HEAP_SYSTEM_FLOOR) << "the test's System heap has room above the floor";
    {
        traps::ServedTrap t(traps::entrypoints.at("TENew"));
        EXPECT_EQ(full_heap_answer::system_heap, ROMlib_full_heap_decide(1024));
        EXPECT_EQ(full_heap_answer::exhausted, ROMlib_full_heap_decide(room - FULL_HEAP_SYSTEM_FLOOR));
        EXPECT_EQ(full_heap_answer::fail, ROMlib_full_heap_decide(room));
    }
    /* no routine served: Executor's own code, the Memory Manager's answer */
    EXPECT_EQ(full_heap_answer::fail, ROMlib_full_heap_decide(1024));
    {
        traps::ServedTrap t(traps::entrypoints.at("NewHandle"));
        EXPECT_EQ(full_heap_answer::fail, ROMlib_full_heap_decide(1024));
        EXPECT_EQ(full_heap_answer::fail, ROMlib_full_heap_decide(room));
    }
}

#if GTEST_HAS_DEATH_TEST
/* Past the floor: system error 25, the session ends; nothing gets nil. */
TEST_F(FullHeap, PastTheFloorIsSystemError25)
{
    int32_t room = ROMlib_full_heap_system_room();
    EXPECT_EXIT(
        {
            traps::ServedTrap t(traps::entrypoints.at("TENew"));
            NewPtr(room - FULL_HEAP_SYSTEM_FLOOR);
        },
        ::testing::ExitedWithCode(70), "System error 25 \\(dsMemFullErr\\): TENew");
}
#endif

/* Once the application heap has run out, its last
   ROMlib_full_heap_appl_reserve stay the guest's: a Toolbox allocation that
   would go into them comes from the System heap, the guest's own NewHandle
   still from the application heap. Before, the Toolbox's stays at home. */
TEST(FullHeapReserve, ToolboxLeavesTheReserveToTheGuest)
{
    GUEST<THz> saved = LM(TheZone);
    SetZone(LM(ApplZone));
    std::vector<Ptr> ps;
    {
        traps::ServedTrap own(traps::entrypoints.at("NewPtr"));
        for(Size s = 1024 * 1024; s >= 1024; s /= 2)
            while(LM(ApplZone)->zcbFree - s > ROMlib_full_heap_appl_reserve(LM(ApplZone)) / 2)
            {
                Ptr p = NewPtr(s);
                if(!p)
                    break;
                ps.push_back(p);
            }
    }
    ASSERT_LT(LM(ApplZone)->zcbFree, ROMlib_full_heap_appl_reserve(LM(ApplZone)));
    ASSERT_GT(LM(ApplZone)->zcbFree, 8192);
    uint32_t f0 = ROMlib_full_heap_fallbacks();
    Handle toolbox, guest;
    {
        /* not run out yet: the Mac's heap, the Toolbox's handle at home */
        traps::ServedTrap t(traps::entrypoints.at("TENew"));
        Handle h = NewHandle(1024);
        ASSERT_NE(nullptr, h);
        EXPECT_EQ(LM(ApplZone), HandleZone(h));
        DisposeHandle(h);
    }
    EXPECT_EQ(f0, ROMlib_full_heap_fallbacks());
    ROMlib_full_heap_ran_out(LM(ApplZone));
    {
        traps::ServedTrap t(traps::entrypoints.at("TENew"));
        toolbox = NewHandle(1024);
    }
    {
        traps::ServedTrap t(traps::entrypoints.at("NewHandle"));
        guest = NewHandle(1024);
    }
    ASSERT_NE(nullptr, toolbox);
    ASSERT_NE(nullptr, guest);
    EXPECT_EQ(LM(SysZone), HandleZone(toolbox));
    EXPECT_EQ(LM(ApplZone), HandleZone(guest));
    EXPECT_EQ(f0 + 1, ROMlib_full_heap_fallbacks());
    DisposeHandle(toolbox);
    DisposeHandle(guest);
    ROMlib_full_heap_zone_reset(LM(ApplZone));
    for(Ptr p : ps)
        DisposePtr(p);
    LM(TheZone) = saved;
}
