/*
 * PowerCore.h - PPC CPU facade for the Executor core.
 *
 * Executor's PowerCore interpreter is not imported (it carries no licence).
 * This header keeps the shape Executor's code uses so the PPC paths
 * compile. execute() fails loudly. KPX will implement this facade.
 */
#pragma once

#include <stdint.h>
#include <atomic>

/* Defined while PPC execution is a stub; tests use it to skip PPC cases. */
#define POWERCORE_STUB 1

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
    void *memoryBases[4] = { 0, 0, 0, 0 };

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

    void requestInterrupt() { interruptFlag.clear(); }
    void cancelInterrupt() { interruptFlag.test_and_set(); }

    void (*debugger)(PowerCore&) = nullptr;
    uint32_t (*getNextBreakpoint)(uint32_t addr) = nullptr;

    void execute();
    void flushCache() {}
    void flushCache(uint32_t, uint32_t) {}
};
