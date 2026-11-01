#include "gtest/gtest.h"

#include <MemoryMgr.h>
#include <mman/mman.h>
#include <mman/mman_private.h>
#include <base/cpu.h>

#include <string.h>

using namespace Executor;

/* Where NewPtr puts a nonrelocatable block and what it moves, purges or
   asks the grow-zone procedure for (mmansubr.cpp ROMlib_reserve), as the
   difftest probe sys (section heap) measured on 8.6. Each test runs in a
   zone of its own made with InitZone in a block of the application heap. */
namespace
{
class HeapZone : public ::testing::Test
{
protected:
    static constexpr Size kSize = 256 * 1024;
    Ptr mem = nullptr;
    THz zone = nullptr;
    GUEST<THz> saved;

    void SetUp() override
    {
        saved = LM(TheZone);
        mem = NewPtr(kSize);
        ASSERT_NE(nullptr, mem);
        InitZone(nullptr, 64, mem + kSize, (THz)mem);
        zone = (THz)mem;
        SetZone(zone);
    }
    void TearDown() override
    {
        LM(TheZone) = saved;
        DisposePtr(mem);
    }
    int32_t zfree() { return zone->zcbFree; }
};

int gz_calls;
Handle gz_watch;
bool gz_watch_purged;

syn68k_addr_t test_gz(syn68k_addr_t, void *)
{
    syn68k_addr_t ret = POPADDR();
    (void)POPUL();
    if(gz_calls++ == 0 && gz_watch)
        gz_watch_purged = *gz_watch == nullptr;
    WRITEUL(EM_A7, 0);
    return ret;
}
}

/* An unlocked handle under the free space moves up, into the free block
   just above it; the pointer takes the bottom. */
TEST_F(HeapZone, PointerGoesBelowUnlockedHandle)
{
    Handle h = NewHandle(8192);
    ASSERT_NE(nullptr, h);
    memset(*h, 0x5A, 8192);
    Ptr was = *h;
    Ptr p = NewPtr(1024);
    ASSERT_NE(nullptr, p);
    EXPECT_LT(p, *h);
    EXPECT_EQ(was, p);  /* the very bottom: where the handle was */
    EXPECT_GE(*h, was + 8192);
    EXPECT_LT(*h, was + 8192 + 64);
    for(int i = 0; i < 8192; i++)
        ASSERT_EQ(0x5A, (uint8_t)(*h)[i]);
}

TEST_F(HeapZone, LockedHandleStays)
{
    Handle h = NewHandle(8192);
    HLock(h);
    Ptr was = *h;
    Ptr p = NewPtr(1024);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(was, *h);
    EXPECT_GT(p, *h);
}

/* A purgeable handle with room to move is moved, not purged. */
TEST_F(HeapZone, PurgeableMovedWhenItCan)
{
    Handle h = NewHandle(8192);
    HPurge(h);
    Ptr p = NewPtr(1024);
    ASSERT_NE(nullptr, *h);
    EXPECT_LT(p, *h);
}

/* No free block holds the handle: it slides up over its own old place. */
TEST_F(HeapZone, HandleSlidesWhenNoFreeBlockHoldsIt)
{
    int32_t f = zfree();
    Handle h = NewHandle(f * 6 / 10);
    ASSERT_NE(nullptr, h);
    memset(*h, 0x33, GetHandleSize(h));
    Ptr p = NewPtr(f * 3 / 10);
    ASSERT_NE(nullptr, p);
    EXPECT_LT(p, *h);
    Size n = GetHandleSize(h);
    for(Size i = 0; i < n; i += 997)
        ASSERT_EQ(0x33, (uint8_t)(*h)[i]);
}

/* Free space above a locked block is used before a purgeable block below
   it is purged. */
TEST_F(HeapZone, FreeSpaceBeforePurging)
{
    int32_t f = zfree();
    Handle lo = NewHandle(f / 2);
    HPurge(lo);
    Handle l = NewHandle(4096);
    HLock(l);
    Ptr p = NewPtr(f * 4 / 10);
    ASSERT_NE(nullptr, p);
    EXPECT_GT(p, *l);
    EXPECT_NE(nullptr, *lo);
}

/* Only the purgeable block in the way is purged: the lowest one. */
TEST_F(HeapZone, PurgesOnlyWhatIsInTheWay)
{
    int32_t f = zfree();
    Handle lo = NewHandle(f * 45 / 100);
    HPurge(lo);
    Handle l = NewHandle(4096);
    HLock(l);
    Handle hi = NewHandle(f * 45 / 100);
    HPurge(hi);
    Ptr p = NewPtr(f * 4 / 10);
    ASSERT_NE(nullptr, p);
    EXPECT_LT(p, *l);
    EXPECT_EQ(nullptr, *lo);
    EXPECT_NE(nullptr, *hi);
}

/* The grow-zone procedure: not called when purging makes room; called,
   with nothing purged first, for a request purging can't satisfy. */
TEST_F(HeapZone, GrowZoneAfterPurging)
{
    SetGrowZone((GrowZoneUPP)SYN68K_TO_US(callback_install(test_gz, nullptr)));
    int32_t f = zfree();
    Handle hl = NewHandle(f * 3 / 10);
    HLock(hl);
    Handle hp = NewHandle(f * 3 / 10);
    HPurge(hp);
    gz_calls = 0;
    gz_watch = hp;
    Ptr p = NewPtr(f * 6 / 10);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(0, gz_calls);
    EXPECT_EQ(nullptr, *hp);
    DisposePtr(p);

    hp = NewHandle(1024);
    HPurge(hp);
    gz_calls = 0;
    gz_watch = hp;
    gz_watch_purged = true;
    p = NewPtr(kSize);
    EXPECT_EQ(nullptr, p);
    EXPECT_EQ(memFullErr, MemError());
    EXPECT_GT(gz_calls, 0);
    EXPECT_FALSE(gz_watch_purged);
    gz_watch = nullptr;
}

/* Slithereens' sounds: HLockHi, NewPtr, HUnlock, HPurge per sound. The
   pointers end up together at the bottom, below the sounds. */
TEST_F(HeapZone, SoundLoadingKeepsPointersLow)
{
    Handle h[5];
    Ptr p[5];
    for(int i = 0; i < 5; i++)
    {
        h[i] = NewHandle(12000 + 1000 * i);
        ASSERT_NE(nullptr, h[i]);
        HLockHi(h[i]);
        p[i] = NewPtr(13000 + 1000 * i);
        ASSERT_NE(nullptr, p[i]);
        HUnlock(h[i]);
        HPurge(h[i]);
    }
    for(int i = 1; i < 5; i++)
    {
        EXPECT_GT(p[i], p[i - 1]);
        EXPECT_LE(p[i] - p[i - 1], GetPtrSize(p[i - 1]) + 32);
    }
    for(int i = 0; i < 5; i++)
        for(int j = 0; j < 5; j++)
            if(*h[j])
                EXPECT_LT(p[i], *h[j]);
}

/* A guest value past what its window maps, handed through a host pointer
   and back (a Ptr argument from PowerPC: LMSetCurrentA5), is the same
   value, not a fatal error (syn68k_public.h, US_TO_SYN68K_FUN). */
TEST(GuestAddress, RoundTripPastTheMappedPart)
{
    if(ROMlib_sizes[0] + 0x10000 >= (1ull << 30))
        GTEST_SKIP() << "window 0 maps all of its range";
    uint32_t g = (uint32_t)ROMlib_sizes[0] + 0x1000;
    EXPECT_EQ(g, US_TO_SYN68K(SYN68K_TO_US(g)));
    LMSetCurrentA5((Ptr)SYN68K_TO_US(g));
    EXPECT_EQ(g, (uint32_t)READUL(0x904)) << "CurrentA5 holds the value";
}
