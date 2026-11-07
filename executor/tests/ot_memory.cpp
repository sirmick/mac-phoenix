/* MacPhoenix: Open Transport's client memory (OTAllocMem) is not the
   application's heap: REALbasic's runtime takes 128K blocks while FreeMem
   stays over 256K (GameBox Classic on 8.6); from the heap that left the
   program nothing. The blocks are given back when the client closes. */
#include "gtest/gtest.h"

#include <MemoryMgr.h>
#include <rsys/process.h>
#include <mman/mman.h>
#include <opentransport/ot_internal.h>

using namespace Executor;

namespace Executor::ot
{
uint32_t init_priv(const Call &);
uint32_t close_priv(const Call &);
uint32_t alloc_mem_priv(const Call &);
uint32_t free_mem_priv(const Call &);
}

namespace
{
/* A C call's frame: a return address, then the arguments. */
uint32_t ot_call(uint32_t (*f)(const ot::Call &), const char *sig, std::initializer_list<uint32_t> args)
{
    GUEST<uint32_t> *frame = (GUEST<uint32_t> *)NewPtrClear(4 * (1 + args.size()));
    int i = 1;
    for(uint32_t a : args)
        frame[i++] = a;
    uint32_t r = f(ot::Call(US_TO_SYN68K(frame), ot::Conv::C, sig));
    DisposePtr((Ptr)frame);
    return r;
}
}

TEST(OTMemory, ClientMemoryLeavesTheHeapAndGoesBackAtClose)
{
    if(!ROMlib_pm_zone)
        GTEST_SKIP() << "no temporary memory here";
    GUEST<uint32_t> *record = (GUEST<uint32_t> *)NewPtrClear(36);
    uint32_t rec = US_TO_SYN68K(record);
    ot_call(ot::init_priv, "LL", { 3, rec });

    /* the program's heap: a zone of its own (here the application zone is
       the Process Manager's) */
    const Size zone_size = 64 * 1024;
    Ptr zone_mem = NewPtr(zone_size);
    InitZone(nullptr, 16, zone_mem + zone_size, (THz)zone_mem);
    THz program_zone = (THz)zone_mem;
    int32_t heap, temp;
    {
        TheZoneGuard guard(program_zone);
        heap = FreeMem();
    }
    {
        TheZoneGuard guard(ROMlib_pm_zone);
        temp = FreeMem();
    }
    uint32_t a, b;
    {
        TheZoneGuard guard(program_zone);
        a = ot_call(ot::alloc_mem_priv, "LL", { 128 * 1024, rec });
        b = ot_call(ot::alloc_mem_priv, "LL", { 128 * 1024, rec });
        EXPECT_EQ(heap, FreeMem());
    }
    ASSERT_NE(0u, a);
    ASSERT_NE(0u, b);
    ot_call(ot::free_mem_priv, "L", { a }); /* OTFreeMem */
    ot_call(ot::close_priv, "L", { rec });  /* b goes with the client */
    {
        TheZoneGuard guard(ROMlib_pm_zone);
        EXPECT_EQ(temp, FreeMem());
    }
    DisposePtr(zone_mem);
    DisposePtr((Ptr)record);
}
