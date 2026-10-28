#pragma once
#include <syn68k_public.h>
#include <initializer_list>

class PowerCore;

namespace Executor
{
    enum class CPUMode
    {
        none,
        m68k,
        ppc
    };

    PowerCore& getPowerCore();

    extern CPUMode currentCPUMode;
    extern syn68k_addr_t currentM68KPC;

    // Guest address of the A-line trap instruction currently being dispatched
    // through alinehandler().  This is the only reliable source for the trap
    // call site: a trap callback is entered with MAGIC_EXIT_EMULATOR_ADDRESS on
    // top of the stack, not the guest return address.
    extern uint32_t currentTrapPC;

    void execute68K(syn68k_addr_t addr);
    void executePPC(syn68k_addr_t addr);

    /* MacPhoenix: call the PowerPC routine behind transition vector `tv`
       (code, TOC) with integer arguments in r3..., on a frame below the
       stacks in use; the registers are as they were afterwards. The result
       is r3. From host code in a PowerPC system call (qsort's comparison
       routine) or from 68k code (a fragment's init routine). */
    uint32_t callPPC(uint32_t tv, std::initializer_list<uint32_t> args);
    /* ... the same for a routine's code and TOC, n arguments. */
    uint32_t callPPC(uint32_t code, uint32_t toc, const uint32_t *args, int n);

    /* MacPhoenix: the CPU state that is a process's own, not the machine's:
       registered with the Process Manager (process.cpp), so a switch keeps
       it per process. */
    void cpu_register_process_state();

    /* MacPhoenix: the same state per Thread Manager thread (process.cpp):
       a thread switched away from inside PowerPC code (YieldToThread from
       a PowerPC application) comes back to its registers, mode and depth. */
    size_t cpu_thread_state_size();
    void cpu_save_thread_state(void *to);
    void cpu_restore_thread_state(const void *from);

    /* ... and the PowerPC routine behind `tv` run on a stack of its own
       (a PowerPC thread's entry), top `stack_top`. */
    uint32_t callPPCOnStack(uint32_t tv, uint32_t stack_top, std::initializer_list<uint32_t> args);

    unsigned long ROMlib_destroy_blocks(syn68k_addr_t start, uint32_t count,
                                        bool flush_only_faulty_checksums);
}