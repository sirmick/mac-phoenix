/*
 * PowerCore.h - PPC CPU facade for the Executor core.
 *
 * Executor's PowerCore interpreter is not imported (it carries no licence).
 * This header keeps the shape Executor's code uses; PowerCore.cpp runs it on
 * MAME's PowerPC recompiler (cpu::Core "mame-ppc"), over the same guest
 * memory as the 68k core.
 *
 * The registers below are the host's copy: Executor's code reads and writes
 * them while host code runs (system calls, interrupts, the debugger), and
 * execute() moves them into the core and back around guest code.
 *
 * Two PowerPC host ops (cpu::ppc::is_host_op) stand in for upstream's magic:
 * the return address executePPC() gives guest code is a cell holding
 * POWERCORE_OP_EXIT (upstream: LR = 0xFFFFFFFC), and builtinlibs' entry
 * stubs hold POWERCORE_OP_SYSCALL where upstream had `sc`; syscall() is
 * called with CIA on the op and returns where to go on.
 */
#pragma once

#include <stdint.h>
#include <atomic>

constexpr uint32_t POWERCORE_OP_EXIT = 0x18000000;
constexpr uint32_t POWERCORE_OP_SYSCALL = 0x18000001;

struct PowerCoreState
{
    uint32_t r[32] = { 0 };
    double f[32] = { 0.0 };
    uint32_t cr = 0;
    uint32_t lr = 0;
    uint32_t ctr = 0;

    uint32_t CIA = 0;

    // XER
    bool SO = false;
    bool OV = false;
    bool CA = false;
};

class PowerCore : public PowerCoreState
{
public:
    PowerCore() { interruptFlag.test_and_set(); }

    /* The one PowerPC of the process (Executor's getPowerCore()). */
    static PowerCore &instance();

    void *memoryBases[4] = { 0, 0, 0, 0 };

    /* Clear: an interrupt is pending. */
    std::atomic_flag interruptFlag = ATOMIC_FLAG_INIT;

    uint32_t getXER() const
    {
        return (SO ? 0x80000000u : 0) | (OV ? 0x40000000u : 0) | (CA ? 0x20000000u : 0);
    }
    void setXER(uint32_t xer)
    {
        SO = (xer >> 31) & 1;
        OV = (xer >> 30) & 1;
        CA = (xer >> 29) & 1;
    }

    uint32_t (*syscall)(PowerCore&) = nullptr;
    void (*handleInterrupt)(PowerCore&) = nullptr;

    /* Any thread. */
    void requestInterrupt();
    void cancelInterrupt() { interruptFlag.test_and_set(); }

    void (*debugger)(PowerCore&) = nullptr;
    uint32_t (*getNextBreakpoint)(uint32_t addr) = nullptr;

    /* Run guest code from CIA until it returns to exitAddress(). */
    void execute();
    /* Guest address of the exit cell. */
    uint32_t exitAddress();
    /* The recompiler checks each block's code before running it, so
     * changed code is recompiled without being told. */
    void flushCache() {}
    void flushCache(uint32_t, uint32_t) {}
};
