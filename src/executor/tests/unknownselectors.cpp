/* Selectors no Executor routine answers, where the selector says how many
   argument bytes the caller pushed: answered as the Mac's dispatcher
   answers a selector it has no routine for, the stack left balanced
   (base/traps.cpp, unknown_selector_68k). */
#include "gtest/gtest.h"

#include "compat.h"

#include <base/common.h>
#include <base/cpu.h>
#include <MemoryMgr.h>

using namespace Executor;

namespace
{
class UnknownSelector : public testing::Test
{
public:
    Ptr code = NewPtrClear(256);
    ~UnknownSelector() { DisposePtr(code); }

    /* Runs `words` then `rts`, returns D1. */
    uint32_t run(std::initializer_list<uint16_t> words)
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
        return EM_D1;
    }
};
}

/* subq.w #2,sp; move.l #$12345678,-(sp); move.w #$02F0,d0;
   _DisplayDispatch; move.w (sp)+,d1 -- two words of parameters */
TEST_F(UnknownSelector, DisplayDispatchWords)
{
    uint32_t d1 = run({ 0x554F, 0x2F3C, 0x1234, 0x5678, 0x303C, 0x02F0, 0xABEB, 0x321F });
    EXPECT_EQ(0xFFCE, d1 & 0xFFFF) << "paramErr";
}

/* subq.w #2,sp; move.w #1,-(sp); move.w #$02F0,d0; _PaletteDispatch;
   move.w (sp)+,d1 -- the Palette Manager counts bytes */
TEST_F(UnknownSelector, PaletteDispatchBytes)
{
    uint32_t d1 = run({ 0x554F, 0x3F3C, 0x0001, 0x303C, 0x02F0, 0xAAA2, 0x321F });
    EXPECT_EQ(0xFFCE, d1 & 0xFFFF) << "paramErr";
}

/* move.w #$7777,-(sp) (result space, to see it cleared); move.w #7,-(sp);
   move.l #$8202FF00,-(sp); _ScriptUtil; move.w (sp)+,d1 */
TEST_F(UnknownSelector, ScriptUtil)
{
    uint32_t d1 = run({ 0x3F3C, 0x7777, 0x3F3C, 0x0007, 0x2F3C, 0x8202, 0xFF00, 0xA8B5, 0x321F });
    EXPECT_EQ(0, d1 & 0xFFFF);
}
