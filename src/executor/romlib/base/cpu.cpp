#include <base/cpu.h>
#include <base/common.h>
#include <base/builtinlibs.h>
#include <PowerCore.h>
#include <prefs/prefs.h>

#include <algorithm>

namespace Executor
{
    CPUMode currentCPUMode = CPUMode::none;
    syn68k_addr_t currentM68KPC = 0;
    uint32_t currentTrapPC = 0;
}

PowerCore& Executor::getPowerCore()
{
    return PowerCore::instance();
}

void Executor::execute68K(uint32_t addr)
{
    currentCPUMode = CPUMode::m68k;
    CALL_EMULATOR(addr);
}
/* MacPhoenix: how deep this thread is in PowerPC code, so a call into it
   knows whether r1 is a stack in use. */
static thread_local int ppcDepth = 0;

void Executor::executePPC(uint32_t addr)
{
    currentCPUMode = CPUMode::ppc;
    auto& cpu = getPowerCore();
    /* MacPhoenix: library calls from the first PowerPC code that runs, a
       fragment's init routine before the application's main. */
    if(!cpu.syscall)
        cpu.syscall = &builtinlibs::handleSC;
    cpu.CIA = addr;
    cpu.lr = cpu.exitAddress();
    struct Depth
    {
        Depth() { ++ppcDepth; }
        ~Depth() { --ppcDepth; }
    } depth;
    cpu.execute();
}

uint32_t Executor::callPPC(uint32_t tv, std::initializer_list<uint32_t> args)
{
    PowerCore &cpu = getPowerCore();
    PowerCoreState saved = cpu;
    uint32_t savedA7 = EM_A7;
    CPUMode savedMode = currentCPUMode;

    /* A frame (linkage area, a parameter area of at least 8 words) below
       both stacks the caller may have in use. */
    uint32_t sp = (ppcDepth ? std::min<uint32_t>(cpu.r[1], EM_A7) : (uint32_t)EM_A7) - 64;
    uint32_t words = std::max<uint32_t>(8, (uint32_t)args.size());
    uint32_t frame = (sp - (24 + 4 * words)) & ~15u;
    auto word = [](uint32_t addr) -> GUEST<uint32_t> & { return *ptr_from_longint<GUEST<uint32_t> *>(addr); };
    word(frame) = saved.r[1];
    int i = 0;
    for(uint32_t a : args)
    {
        if(i < 8)
            cpu.r[3 + i] = a;
        word(frame + 24 + 4 * i) = a;
        i++;
    }
    cpu.r[1] = frame;
    cpu.r[2] = word(tv + 4);
    EM_A7 = frame;
    executePPC(word(tv));
    uint32_t result = cpu.r[3];
    static_cast<PowerCoreState &>(cpu) = saved;
    EM_A7 = savedA7;
    currentCPUMode = savedMode;
    return result;
}


/*
 * NOTE: ROMlib_destroy_blocks is a wrapper routine that either destroys
 *	 a limited range or flushes the entire cache.  Apple's semantics
 *	 say that you must flush the entire cache in certain circumstances.
 *	 However, 99% of all programs need to destroy only a range of
 *	 addresses.
 */

unsigned long Executor::ROMlib_destroy_blocks(
    syn68k_addr_t start, uint32_t count, bool flush_only_faulty_checksums)
{
    unsigned long num_blocks_destroyed;

    if(ROMlib_flushoften)
    {
        start = 0;
        count = ~0;
    }
    if(count)
    {
        if(flush_only_faulty_checksums)
            num_blocks_destroyed = destroy_blocks_with_checksum_mismatch(start, count);
        else
            num_blocks_destroyed = destroy_blocks(start, count);
    }
    else
        num_blocks_destroyed = 0;

    return num_blocks_destroyed;
}