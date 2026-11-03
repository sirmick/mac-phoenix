#include "gtest/gtest.h"
#include <base/cpu.h>
#include <base/functions.impl.h>
#include <base/mactype.h>
#include <PowerCore.h>
#include <base/builtinlibs.h>
#include <AppleEvents.h>
#include <appleevent/apple_events.h>

#define ABSOLUTE_ADDR 0

#if ABSOLUTE_ADDR
#define BASE 0x1000
#else
#include <MemoryMgr.h>
#include <time/time.h>

uint32_t BASE;
#endif
#define WORDS (ptr_from_longint<GUEST<uint16_t>*>(BASE))
#define LONGS (ptr_from_longint<GUEST<uint32_t>*>(BASE))

using namespace Executor;

thread_local int breakHit;
thread_local syn68k_addr_t stoppedAddr;

class CPU : public testing::Test
{
public:
    void m68k(std::initializer_list<uint16_t> code)
    {
        auto p = ptr_from_longint<GUEST<uint16_t>*>(BASE);
        for(auto w : code)
            *p++ = w;
        
        m68k();
    }
    void m68k()
    {
        destroy_blocks(0, ~0);
        execute68K(BASE);
    }

    void ppc(std::initializer_list<uint32_t> code)
    {
        auto p = ptr_from_longint<GUEST<uint32_t>*>(BASE);
        for(auto w : code)
            *p++ = w;

        ppc();
    }

    void ppc()
    {
        getPowerCore().flushCache();
        executePPC(BASE);
    }

#if !ABSOLUTE_ADDR
    Ptr basePtr;

    CPU()
    {
        basePtr = NewPtrClear(4096);
        BASE = ptr_to_longint(basePtr);
        EM_A7 = getPowerCore().r[1] = ptr_to_longint(LM(MemTop)) - 4;
    }
    ~CPU()
    {
        DisposePtr(basePtr);
    }
#endif
};

class CPUSingleStep : public CPU
{
public:
    CPUSingleStep()
    {
        breakHit = 0;
        stoppedAddr = 0;

        syn68k_debugger_callbacks.getNextBreakpoint = [](syn68k_addr_t addr) -> syn68k_addr_t {
            if(stoppedAddr == addr)
                return addr + 1;
            else
                return addr;
        };
        syn68k_debugger_callbacks.debugger = [](syn68k_addr_t addr) -> syn68k_addr_t {
            breakHit++;
            stoppedAddr = addr;
            destroy_blocks(0, ~0);
            return addr;
        };
    }

    ~CPUSingleStep()
    {
        syn68k_debugger_callbacks.getNextBreakpoint = nullptr;
        syn68k_debugger_callbacks.debugger = nullptr;
    }
};

TEST_F(CPU, execute68K)
{
    EM_D0 = 0;

    m68k({
        0x4E71,  //  NOP
        0x303C,  //  MOVE.W ___, d0
        0x002A,  //         #42
        0x4E75   //  RTS
    });

    EXPECT_EQ(42, EM_D0);
}

TEST_F(CPU, executePPC)
{
    getPowerCore().r[3] = 0;
    ppc({
        0x60000000,  //  nop
        0x38600036,  //  li r3, 54
        0x3860002a,  //  li r4, 42
        0x4e800020   //  blr
    });

    EXPECT_EQ(42, getPowerCore().r[3]);
}


/* Apple's ObjectSupportLib tells the AppleEvent Manager it is there with
   handlers that are no address: AEInstallEventHandler('Berk', 'eley',
   'Win ', 0, false), AERemoveEventHandler('Stan', 'ford', 'Bite', true).
   From PowerPC they are answered, not a fatal "host pointer is not
   guest-addressable" where the value's window is small (the tab's 64 MB
   pool: CodeWarrior IDE 3.2 with PowerPC, M1 #6). */
static void ppcCall(CPU& t, const char *name, std::initializer_list<uint32_t> args)
{
    auto sc = builtinlibs::callbackAddresses(name);
    ASSERT_FALSE(sc.empty()) << name;
    auto& cpu = getPowerCore();
    int r = 3;
    for(uint32_t a : args)
        cpu.r[r++] = a;
    t.ppc({
        0x3D800000 | (sc[0] >> 16),     //  lis r12, sc@h
        0x618C0000 | (sc[0] & 0xFFFF),  //  ori r12, r12, sc@l
        0x7D8903A6,                     //  mtctr r12
        0x4E800420                      //  bctr
    });
}

TEST_F(CPU, ppcAEHandlersThatAreNoAddress)
{
    if(!ROMlib_ae_private())
    {
        AE_init();
        AE_reinit();
    }
    ppcCall(*this, "AERemoveEventHandler", { "Stan"_4, "ford"_4, "Bite"_4, 1 });
    EXPECT_EQ(errAEHandlerNotFound, (int16_t)getPowerCore().r[3]);
    ppcCall(*this, "AEInstallEventHandler", { "Berk"_4, "eley"_4, "Win "_4, 0, 0 });
    EXPECT_EQ(noErr, (int16_t)getPowerCore().r[3]);
    /* where the value can be a pointer (natively: the pool's window is
       1 GB), it is in the AppleEvent Manager's table, as on a Mac
       (AppleWorks 5 with its PowerPC code stops without it) */
    if(syn68k_is_guest_addr_in_window("Win "_4, 2))
    {
        GUEST<AEEventHandlerUPP> hdlr;
        GUEST<int32_t> refcon;
        ASSERT_EQ(noErr, AEGetEventHandler("Berk"_4, "eley"_4, &hdlr, &refcon, false));
        EXPECT_EQ("Win "_4, hdlr.raw_host_order());
    }
    /* removed as installed (AppleWorks 5 does it straight away) */
    ppcCall(*this, "AERemoveEventHandler", { "Berk"_4, "eley"_4, "Win "_4, 0 });
    EXPECT_EQ(noErr, (int16_t)getPowerCore().r[3]);
    ppcCall(*this, "AERemoveEventHandler", { "Berk"_4, "eley"_4, "Win "_4, 0 });
    EXPECT_EQ(errAEHandlerNotFound, (int16_t)getPowerCore().r[3]);
}

TEST_F(CPU, breakpoint68K)
{
    thread_local syn68k_addr_t stoppedD0;
    breakHit = 0;
    stoppedAddr = stoppedD0 = 0;

    syn68k_debugger_callbacks.getNextBreakpoint = [](syn68k_addr_t addr) -> syn68k_addr_t {
        if(breakHit == 0 && addr <= BASE + 6)
            return BASE + 6;
        else
            return 0xFFFFFFFF;
    };
    syn68k_debugger_callbacks.debugger = [](syn68k_addr_t addr) -> syn68k_addr_t {
        breakHit++;
        stoppedAddr = addr;
        stoppedD0 = EM_D0;
        destroy_blocks(0, ~0);
        return addr;
    };

    EM_D0 = 0;
    m68k({
        0x4E71,  //  NOP
        0x303C,  //  MOVE.W ___, d0
        0x0036,  //         #54
        0x303C,  //  MOVE.W ___, d0
        0x002A,  //         #42
        0x4E75   //  RTS
    });

    EXPECT_EQ(42, EM_D0);
    EXPECT_EQ(1, breakHit);
    EXPECT_EQ(54, stoppedD0);
    EXPECT_EQ(BASE + 6, stoppedAddr);
}

TEST_F(CPU, breakpointPPC)
{
    GTEST_SKIP() << "mame-ppc steps a recompiled block, not an instruction: "
                    "breakpoints are only seen where a block starts";
    auto& cpu = getPowerCore();
    cpu.r[3] = 0;

    thread_local int breakHit;
    thread_local syn68k_addr_t stoppedAddr, stoppedR3;
    breakHit = 0;
    stoppedAddr = stoppedR3 = 0;

    cpu.getNextBreakpoint = [](syn68k_addr_t addr) -> syn68k_addr_t {
        if(breakHit == 0 && addr <= BASE + 8)
            return BASE + 8;
        else
            return 0xFFFFFFFF;
    };
    cpu.debugger = [](PowerCore& cpu) {
        breakHit++;
        stoppedAddr = cpu.CIA;
        stoppedR3 = cpu.r[3];
        cpu.flushCache();
    };

    ppc({
        0x60000000,  //  nop
        0x38600036,  //  li r3, 54
        0x3860002a,  //  li r4, 42
        0x4e800020   //  blr
    });

    EXPECT_EQ(42, cpu.r[3]);
    EXPECT_EQ(1, breakHit);
    EXPECT_EQ(54, stoppedR3);
    EXPECT_EQ(BASE + 8, stoppedAddr);
}

TEST_F(CPUSingleStep, basic68K)
{
    m68k({
        0x702a,  //          moveq #42, %d0
        0x7215,  //          moveq #21, %d1
        0x4e75   //          rts
    });
    EXPECT_EQ(42, EM_D0);
    EXPECT_EQ(21, EM_D1);
    EXPECT_EQ(3, breakHit);
}

TEST_F(CPUSingleStep, braSinglestep68K)
{
    m68k({
        0x6002,  //         bra.s label
        0x4e71,  //         nop
        0x4e75   // label:  rts
    });
    EXPECT_EQ(2, breakHit);
}

TEST_F(CPUSingleStep, beqSinglestep68K)
{
    EM_D0 = 0;
    m68k({
        0x4a00,  //         tst.b %d0
        0x6702,  //         beq.s label
        0x4e71,  //         nop
        0x4e75   // label:  rts
    });
    EXPECT_EQ(3, breakHit);

    EM_D0 = 1;
    breakHit = 0;
    m68k();
    EXPECT_EQ(4, breakHit);
}

TEST_F(CPUSingleStep, fib68K)
{
    EM_D0 = 7;

    m68k({
        0x7200,  //         moveq #0, %d1
        0x7401,  //         moveq #1, %d2
        0x4a00,  // loop:   tst.b %d0
        0x6708,  //         beq.s end
        0xd282,  //         add.l %d2, %d1
        0xc541,  //         exg %d2, %d1
        0x5340,  //         subq #1, %d0
        0x60f4,  //         bra.s loop
        0x4e75   // end:    rts
    });

    EXPECT_EQ(13, EM_D1);
}

namespace
{
struct TwoWordStruct
{
    GUEST_STRUCT;
    GUEST<uint32_t> first;
    GUEST<uint32_t> second;
};

struct PartialWordStruct
{
    GUEST_STRUCT;
    GUEST<uint16_t> value;
};
}

TEST_F(CPU, ppcArgumentsStructInRegisters)
{
    auto& cpu = getPowerCore();
    cpu.r[3] = 0x11223344;
    cpu.r[4] = 0xaabbccdd;

    uint8_t params[8] = {};
    callfromPPC::ParameterPasser args{cpu, params};
    TwoWordStruct value;
    (void)(args >> value);

    EXPECT_EQ(0x11223344u, value.first.get());
    EXPECT_EQ(0xaabbccddu, value.second.get());
}

TEST_F(CPU, ppcArgumentsStructStraddlingRegistersAndStack)
{
    auto& cpu = getPowerCore();
    cpu.r[10] = 0x11223344;

    uint8_t params[64] = {};
    params[32] = 0xaa;
    params[33] = 0xbb;
    params[34] = 0xcc;
    params[35] = 0xdd;

    callfromPPC::ParameterPasser args{cpu, params};
    uint32_t ignored;
    TwoWordStruct value;
    (void)(args >> ignored >> ignored >> ignored >> ignored >> ignored
                >> ignored >> ignored >> value);

    EXPECT_EQ(0x11223344u, value.first.get());
    EXPECT_EQ(0xaabbccddu, value.second.get());
}

TEST_F(CPU, ppcArgumentsStructOnStack)
{
    auto& cpu = getPowerCore();

    uint8_t params[64] = {};
    params[32] = 0x11;
    params[33] = 0x22;
    params[34] = 0x33;
    params[35] = 0x44;
    params[36] = 0xaa;
    params[37] = 0xbb;
    params[38] = 0xcc;
    params[39] = 0xdd;

    callfromPPC::ParameterPasser args{cpu, params};
    uint32_t ignored;
    TwoWordStruct value;
    (void)(args >> ignored >> ignored >> ignored >> ignored >> ignored
                >> ignored >> ignored >> ignored >> value);

    EXPECT_EQ(0x11223344u, value.first.get());
    EXPECT_EQ(0xaabbccddu, value.second.get());
}

TEST_F(CPU, ppcArgumentsPartialWordStruct)
{
    auto& cpu = getPowerCore();
    // A composite shorter than a word starts at the most significant byte.
    cpu.r[3] = 0x12340000;

    uint8_t params[4] = {};
    callfromPPC::ParameterPasser args{cpu, params};
    PartialWordStruct value;
    (void)(args >> value);

    EXPECT_EQ(0x1234, value.value.get());
}

TEST_F(CPU, ppcArgumentsStructWrittenToRegisters)
{
    auto& cpu = getPowerCore();
    cpu.r[3] = cpu.r[4] = 0;

    TwoWordStruct value;
    value.first = 0x11223344;
    value.second = 0xaabbccdd;

    uint8_t params[8] = {};
    callfromPPC::ParameterPasser args{cpu, params};
    (void)(args << value);

    EXPECT_EQ(0x11223344u, cpu.r[3]);
    EXPECT_EQ(0xaabbccddu, cpu.r[4]);
}

TEST_F(CPU, ppcArgumentsStructWrittenToStack)
{
    auto& cpu = getPowerCore();

    TwoWordStruct value;
    value.first = 0x11223344;
    value.second = 0xaabbccdd;

    uint8_t params[64] = {};
    callfromPPC::ParameterPasser args{cpu, params};
    uint32_t ignored = 0;
    (void)(args << ignored << ignored << ignored << ignored << ignored
                << ignored << ignored << ignored << value);

    EXPECT_EQ(0x11, params[32]);
    EXPECT_EQ(0x22, params[33]);
    EXPECT_EQ(0x33, params[34]);
    EXPECT_EQ(0x44, params[35]);
    EXPECT_EQ(0xaa, params[36]);
    EXPECT_EQ(0xbb, params[37]);
    EXPECT_EQ(0xcc, params[38]);
    EXPECT_EQ(0xdd, params[39]);
}

/* $FE09, a Power Mac 68K emulator instruction: the time base, upper word
   in A0 and lower in D0, nothing else touched (the G3 on 9.0.4). Executor
   counts at 25 MHz on Microseconds' clock: the G3's Name Registry says
   timebase-frequency 25000000 (a quarter of its 100 MHz bus). */
TEST_F(CPU, timeBaseFE09)
{
    EM_D0 = 0x11111111;
    EM_D1 = 0x22222222;
    EM_A0 = 0xA0A0A0A0;
    EM_A1 = 0xA1A1A1A1;
    uint64_t before = msecs_elapsed();
    m68k({
        0xFE09,
        0x2400,  //  MOVE.L d0, d2
        0xFE09,
        0x4E75   //  RTS
    });
    EXPECT_EQ(0x22222222u, EM_D1);
    EXPECT_EQ(0xA1A1A1A1u, EM_A1);
    EXPECT_NE(0xA0A0A0A0u, EM_A0);
    uint64_t tb = (uint64_t)EM_A0 << 32 | EM_D0;
    uint64_t after = msecs_elapsed();
    EXPECT_GE((uint32_t)tb, EM_D2);
    EXPECT_GE(tb, before * 25000);
    EXPECT_LE(tb, after * 25000);
}
