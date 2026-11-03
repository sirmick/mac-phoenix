/* The Folder Manager's notification procedures (Folders.h, InterfaceLib
   9.0; AliasDispatch $2F and $30), called from 68k code as 9.0.4's Apple
   Menu Options INIT calls them: the selector in D0's low byte, with or
   without the argument size the header's glue puts in its high byte. */
#include "gtest/gtest.h"

#include "compat.h"

#include <base/common.h>
#include <base/cpu.h>
#include <MemoryMgr.h>

using namespace Executor;

namespace
{
class FolderNotification : public testing::Test
{
public:
    Ptr code = NewPtrClear(256);
    ~FolderNotification() { DisposePtr(code); }

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

/* subq.w #2,sp; move.l #$00ABCD00,-(sp) (the procedure); move.l
   #$12345678,-(sp) (refCon); clr.l -(sp) (options); moveq #$2F,d0;
   _AliasDispatch; move.w (sp)+,d1 -- Apple Menu Options' call. Then
   unregistering it ($30, as Folders.h's glue: move.w #$0430,d0) twice:
   the second finds nothing. */
TEST_F(FolderNotification, RegisterUnregister)
{
    EXPECT_EQ(0, run({ 0x558F, 0x2F3C, 0x00AB, 0xCD00, 0x2F3C, 0x1234, 0x5678, 0x42A7,
                       0x702F, 0xA823, 0x321F }) & 0xFFFF) << "noErr";
    uint16_t err = run({ 0x558F, 0x2F3C, 0x00AB, 0xCD00, 0x2F3C, 0x1234, 0x5678,
                         0x303C, 0x0430, 0xA823, 0x321F }) & 0xFFFF;
    EXPECT_EQ(0, err) << "noErr";
    err = run({ 0x558F, 0x2F3C, 0x00AB, 0xCD00, 0x2F3C, 0x1234, 0x5678,
                0x303C, 0x0430, 0xA823, 0x321F }) & 0xFFFF;
    EXPECT_EQ(0xFFCE, err) << "paramErr: not registered";
}

/* The header's form of the call, move.w #$062F,d0. */
TEST_F(FolderNotification, RegisterWithSizeInSelector)
{
    EXPECT_EQ(0, run({ 0x558F, 0x2F3C, 0x00AB, 0xCE00, 0x42A7, 0x42A7,
                       0x303C, 0x062F, 0xA823, 0x321F }) & 0xFFFF) << "noErr";
    EXPECT_EQ(0, run({ 0x558F, 0x2F3C, 0x00AB, 0xCE00, 0x42A7,
                       0x303C, 0x0430, 0xA823, 0x321F }) & 0xFFFF) << "noErr";
}
