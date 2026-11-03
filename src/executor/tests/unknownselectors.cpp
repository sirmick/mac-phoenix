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

/* subq.w #2,sp (result space); move.w #$5555,-(sp) (one word of params);
   move.w #$0109,d0 (selector 9, one parameter word); _ResourceDispatch;
   move.w (sp)+,d1 -- the signed low byte of D0 is the selector, the high
   byte the parameter size in words (ResourceMgr.yaml's ResourceDispatch
   dispatcher comment) */
TEST_F(UnknownSelector, ResourceDispatchWords)
{
    uint32_t d1 = run({ 0x554F, 0x3F3C, 0x5555, 0x303C, 0x0109, 0xA822, 0x321F });
    EXPECT_EQ(0xFFCE, d1 & 0xFFFF) << "paramErr";
}

/* subq.w #6,sp (2 bytes result + 4 bytes the pea below points into);
   pea.l 2(sp) (one 4-byte pointer argument); moveq #$24,d0;
   _TranslationDispatch -- THINK C's, THINK Pascal's and MacPerl's launches
   on 8.6 all reach this exact sequence (base/traps.cpp's comment); the
   callee pops only its own 4-byte argument, so the test cleans up the
   other 4 bytes of its own reservation itself, same as the real caller
   would. move.w (sp)+,d1 reads the 2-byte result. */
TEST_F(UnknownSelector, TranslationDispatchSelector24)
{
    uint32_t d1 = run({ 0x5D4F, 0x486F, 0x0002, 0x7024, 0xABFC, 0x321F, 0x584F });
    EXPECT_EQ(0xF42F, d1 & 0xFFFF) << "noTranslationPathErr";
}

/* subq.w #2,sp (result); clr.w -(sp); pea.l (sp) twice (two pointers, as
   the resident System code's pea $A92(a6); pea $A8E(a6)); moveq #$20,d0;
   _TranslationDispatch; move.w (sp)+,d1 -- 10 argument bytes, a word
   result (ResEdit's launch from the Finder on 8.6) */
TEST_F(UnknownSelector, TranslationDispatchSelector20)
{
    uint32_t d1 = run({ 0x554F, 0x4267, 0x4857, 0x4857, 0x7020, 0xABFC, 0x321F });
    EXPECT_EQ(0xF42F, d1 & 0xFFFF) << "noTranslationPathErr";
}

/* movea.l #pb,a0; moveq #$C,d0; _HFSDispatch; move.w d0,d1 -- a hole in
   the File Manager's table (MPW Shell's File > New): paramErr in D0 and
   in the parameter block's ioResult, a register call */
TEST_F(UnknownSelector, FSDispatchHole)
{
    Ptr pb = NewPtrClear(128);
    uint32_t a = ptr_to_longint(pb);
    uint32_t d1 = run({ 0x207C, uint16_t(a >> 16), uint16_t(a), 0x700C, 0xA260, 0x3200 });
    EXPECT_EQ(0xFFCE, d1 & 0xFFFF) << "paramErr";
    EXPECT_EQ(0xFFCE, READUW(a + 16)) << "ioResult";
    DisposePtr(pb);
}

/* movea.l #pb,a0; moveq #1,d0; _A08E; move.w d0,d1 -- the System's
   B*-tree trap, which 8.6's Navigation Services calls unchecked (BBEdit
   Lite 6.1's Save): paramErr, in D0 and ioResult */
TEST_F(UnknownSelector, BTreeTrapA08E)
{
    Ptr pb = NewPtrClear(80);
    uint32_t a = ptr_to_longint(pb);
    uint32_t d1 = run({ 0x207C, (uint16_t)(a >> 16), (uint16_t)a, 0x7001, 0xA08E, 0x3200 });
    EXPECT_EQ(0xFFCE, d1 & 0xFFFF) << "paramErr";
    EXPECT_EQ(0xFFCE, READUW(a + 0x10)) << "ioResult";
    DisposePtr(pb);
}
