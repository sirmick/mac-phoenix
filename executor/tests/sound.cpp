/* GetSoundHeaderOffset (romlib/sound/soundIMVI.cpp) answers as Apple's 8.6
   does on a G3 (tests/guest/difftest/snd.c, ref/snd-g3-8.6.txt): the first
   flagged soundCmd or bufferCmd's offset, as it is. Ares (Ambrosia) asks
   it for every sound; Executor compared the command words signed and
   answered badFormat to all of them. */
#include "gtest/gtest.h"

#include <base/common.h>
#include <MemoryMgr.h>
#include <SoundMgr.h>

#include <vector>

using namespace Executor;

namespace
{
struct Snd
{
    std::vector<uint8_t> b;
    Snd &w16(int v) { b.push_back(v >> 8); b.push_back(v); return *this; }
    Snd &w32(uint32_t v) { return w16(v >> 16).w16(v & 0xFFFF); }
    Snd &cmd(int c, int p1, uint32_t p2) { return w16(c).w16(p1).w32(p2); }
    /* format 1, n sampledSynth modifiers */
    static Snd format1(int mods)
    {
        Snd s;
        s.w16(1).w16(mods);
        for(int i = 0; i < mods; i++)
            s.w16(sampledSynth).w32(0xA0);
        return s;
    }
    Snd &header() { return w32(0).w32(4).w32(0x56220000).w32(0).w32(0).w16(60).w32(0x80808080); }
};

/* the error and offset GetSoundHeaderOffset answers for s */
std::pair<OSErr, uint32_t> ask(const Snd &s)
{
    Handle h = NewHandle(s.b.size());
    memcpy(*h, s.b.data(), s.b.size());
    GUEST<LONGINT> offset = (LONGINT)0xA5A5A5A5;
    OSErr err = C_GetSoundHeaderOffset(h, &offset);
    DisposeHandle(h);
    return {err, (uint32_t)(LONGINT)offset};
}
}

TEST(Sound, HeaderOffsetFormat1)
{
    using P = std::pair<OSErr, uint32_t>;
    EXPECT_EQ(P(noErr, 20), ask(Snd::format1(1).w16(1).cmd(0x8000 | bufferCmd, 0, 20).header())); // Ares's
    EXPECT_EQ(P(noErr, 14), ask(Snd::format1(0).w16(1).cmd(0x8000 | bufferCmd, 0, 14).header()));
    EXPECT_EQ(P(noErr, 26), ask(Snd::format1(2).w16(1).cmd(0x8000 | bufferCmd, 0, 26).header()));
    EXPECT_EQ(P(noErr, 20), ask(Snd::format1(1).w16(1).cmd(0x8000 | soundCmd, 0, 20).header()));
    EXPECT_EQ(P(badFormat, 0), ask(Snd::format1(1).w16(1).cmd(bufferCmd, 0, 20).header()));
    EXPECT_EQ(P(badFormat, 0), ask(Snd::format1(1).w16(1).cmd(soundCmd, 0, 20).header()));
    EXPECT_EQ(P(noErr, 36), ask(Snd::format1(1).w16(3).cmd(ampCmd, 128, 0).cmd(quietCmd, 0, 0)
                                    .cmd(0x8000 | bufferCmd, 0, 36).header()));
    EXPECT_EQ(P(noErr, 100), ask(Snd::format1(1).w16(1).cmd(0x8000 | bufferCmd, 0, 100).header()));
    EXPECT_EQ(P(badFormat, 0), ask(Snd::format1(1).w16(1).cmd(ampCmd, 128, 0).header()));
    EXPECT_EQ(P(badFormat, 0), ask(Snd::format1(1).w16(0).header()));
}

TEST(Sound, HeaderOffsetOtherFormats)
{
    using P = std::pair<OSErr, uint32_t>;
    EXPECT_EQ(P(noErr, 14), ask(Snd().w16(2).w16(0).w16(1).cmd(0x8000 | bufferCmd, 0, 14).header()));
    EXPECT_EQ(P(noErr, 14), ask(Snd().w16(2).w16(3).w16(1).cmd(0x8000 | soundCmd, 0, 14).header()));
    EXPECT_EQ(P(badFormat, 0), ask(Snd().w16(2).w16(0).w16(1).cmd(soundCmd, 0, 14).header()));
    // any format but 1 is read as format 2
    EXPECT_EQ(P(noErr, 14), ask(Snd().w16(3).w16(0).w16(1).cmd(0x8000 | bufferCmd, 0, 14).header()));
    EXPECT_EQ(P(noErr, 14), ask(Snd().w16(0).w16(0).w16(1).cmd(0x8000 | bufferCmd, 0, 14).header()));

    Handle h = NewEmptyHandle();
    GUEST<LONGINT> offset = (LONGINT)0xA5A5A5A5;
    EXPECT_EQ(nilHandleErr, C_GetSoundHeaderOffset(h, &offset));
    EXPECT_EQ(0xA5A5A5A5u, (uint32_t)(LONGINT)offset);
    DisposeHandle(h);
}
