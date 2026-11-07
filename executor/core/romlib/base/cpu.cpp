#include <base/cpu.h>
#include <base/common.h>
#include <base/builtinlibs.h>
#include <PowerCore.h>
#include <prefs/prefs.h>
#include <rsys/process.h>
#include <rsys/cfm.h>
#include <error/error.h>
#include <time/time.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

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

/* MacPhoenix: --executor-debug pcsample. The PowerPC PC at the end of a
   time slice, at most every 5 ms; every 5 s the most frequent addresses
   and where they are (fragment, section, nearest export). For code that
   spins without calling the Toolbox, so the trap log can't say where. */
namespace
{
std::unordered_map<uint32_t, uint32_t> pc_samples;
unsigned long pc_sample_last, pc_report_last;
uint32_t pc_sample_total;

void pc_sample(PowerCore &, uint32_t pc)
{
    unsigned long now = Executor::msecs_elapsed();
    if(now - pc_sample_last < 5)
        return;
    pc_sample_last = now;
    pc_samples[pc]++;
    pc_sample_total++;
    if(!pc_report_last)
        pc_report_last = now;
    if(now - pc_report_last < 5000)
        return;
    pc_report_last = now;
    std::vector<std::pair<uint32_t, uint32_t>> top(pc_samples.begin(), pc_samples.end());
    std::sort(top.begin(), top.end(), [](auto &a, auto &b) { return a.second > b.second; });
    fprintf(stderr, "[pcsample] %u samples in 5 s\n", (unsigned)pc_sample_total);
    for(size_t i = 0; i < top.size() && i < 12; i++)
        fprintf(stderr, "[pcsample] %5.1f%% $%08X %s\n", 100.0 * top[i].second / pc_sample_total,
                (unsigned)top[i].first, Executor::ROMlib_cfm_describe(top[i].first).c_str());
    pc_samples.clear();
    pc_sample_total = 0;
}
}

void Executor::executePPC(uint32_t addr)
{
    currentCPUMode = CPUMode::ppc;
    auto& cpu = getPowerCore();
    /* MacPhoenix: library calls from the first PowerPC code that runs, a
       fragment's init routine before the application's main. */
    if(!cpu.syscall)
        cpu.syscall = &builtinlibs::handleSC;
    if(!cpu.sliceHook && ERROR_ENABLED_P(ERROR_PC_SAMPLE))
        cpu.sliceHook = &pc_sample;
    if(ERROR_ENABLED_P(ERROR_PPC_INTERP))
        cpu.jit = false;    /* read when the core is made, at the first run */
    if(ERROR_ENABLED_P(ERROR_PPC_MMU))
        cpu.mmuIdentity = true;
    cpu.CIA = addr;
    cpu.lr = cpu.exitAddress();
    struct Depth
    {
        Depth() { ++ppcDepth; }
        ~Depth() { --ppcDepth; }
    } depth;
    cpu.execute();
}

/* The mode the process runs in, the PowerPC registers and its depth in
   PowerPC code: a process switched away from inside a PowerPC call
   (WaitNextEvent from a PowerPC application) comes back to them, and the
   68k process that runs meanwhile doesn't run in PowerPC mode on its stack. */
void Executor::cpu_register_process_state()
{
    ROMlib_process_register_state(&currentCPUMode, sizeof currentCPUMode);
    ROMlib_process_register_state(static_cast<PowerCoreState *>(&getPowerCore()),
                                  sizeof(PowerCoreState));
    ROMlib_process_register_state(&ppcDepth, sizeof ppcDepth);
}

namespace
{
struct ThreadCPUState
{
    PowerCoreState ppc;
    Executor::CPUMode mode;
    int depth;
};
}

size_t Executor::cpu_thread_state_size()
{
    return sizeof(ThreadCPUState) + getPowerCore().execContextSize();
}

/* The registers, mode and depth, then the PowerPC core's execution in
   progress (the JIT's host stack and the like). */
void Executor::cpu_save_thread_state(void *to)
{
    ThreadCPUState s { static_cast<PowerCoreState &>(getPowerCore()), currentCPUMode, ppcDepth };
    memcpy(to, &s, sizeof s);
    getPowerCore().saveExecContext((uint8_t *)to + sizeof s);
}

void Executor::cpu_restore_thread_state(const void *from)
{
    ThreadCPUState s;
    memcpy(&s, from, sizeof s);
    static_cast<PowerCoreState &>(getPowerCore()) = s.ppc;
    currentCPUMode = s.mode;
    ppcDepth = s.depth;
    getPowerCore().restoreExecContext((const uint8_t *)from + sizeof s);
}

bool Executor::ppcInCall()
{
    return ppcDepth > 0;
}

uint32_t Executor::callPPCOnStack(uint32_t tv, uint32_t stack_top, std::initializer_list<uint32_t> args)
{
    /* callPPC puts its frame below the stacks in use: here, this one. */
    getPowerCore().r[1] = stack_top;
    EM_A7 = stack_top;
    return callPPC(tv, args);
}

uint32_t Executor::callPPC(uint32_t tv, std::initializer_list<uint32_t> args)
{
    auto word = [](uint32_t addr) -> GUEST<uint32_t> & { return *ptr_from_longint<GUEST<uint32_t> *>(addr); };
    return callPPC(word(tv), word(tv + 4), args.begin(), (int)args.size(), tv);
}

uint32_t Executor::callPPC(uint32_t code, uint32_t toc, const uint32_t *args, int n, uint32_t tv)
{
    PowerCore &cpu = getPowerCore();
    PowerCoreState saved = cpu;
    uint32_t savedA7 = EM_A7;
    CPUMode savedMode = currentCPUMode;

    /* A frame (linkage area, a parameter area of at least 8 words) below
       both stacks the caller may have in use. */
    uint32_t sp = (ppcDepth ? std::min<uint32_t>(cpu.r[1], EM_A7) : (uint32_t)EM_A7) - 64;
    uint32_t words = std::max<uint32_t>(8, (uint32_t)n);
    uint32_t frame = (sp - (24 + 4 * words)) & ~15u;
    auto word = [](uint32_t addr) -> GUEST<uint32_t> & { return *ptr_from_longint<GUEST<uint32_t> *>(addr); };
    word(frame) = saved.r[1];
    for(int i = 0; i < n; i++)
    {
        if(i < 8)
            cpu.r[3 + i] = args[i];
        word(frame + 24 + 4 * i) = args[i];
    }
    cpu.r[1] = frame;
    cpu.r[2] = toc;
    if(tv)
        cpu.r[12] = tv;
    EM_A7 = frame;
    executePPC(code);
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