/* The private ResourceDispatch selectors of System 7.5.5, as
   docs/executor/cleanroom/resource-private-selectors.md specifies them:
   called directly and through _ResourceDispatch ($A822) with D0 set. */
#include "gtest/gtest.h"

#include "compat.h"

#include <base/common.h>
#include <base/cpu.h>
#include <res/resource.h>
#include <ResourceMgr.h>
#include <FileMgr.h>
#include <ProcessMgr.h>
#include <rsys/process.h>

#include <string>
#include <vector>

using namespace Executor;

namespace
{
class ResPrivate : public testing::Test
{
public:
    std::vector<std::pair<StringPtr, INTEGER>> files;
    Ptr code;

    ResPrivate()
    {
        code = NewPtrClear(256);
    }

    ~ResPrivate()
    {
        for(auto it = files.rbegin(); it != files.rend(); ++it)
        {
            CloseResFile(it->second);
            FSDelete(it->first, 0);
        }
        DisposePtr(code);
    }

    /* Opens a new resource file: it goes on top of the chain. */
    INTEGER open(const char *name, bool override_map = false)
    {
        std::string full = std::string("test-respriv-") + name;
        StringPtr fn = PSTR(full.c_str());
        CreateResFile(fn);
        INTEGER ref = OpenResFile(fn);
        EXPECT_NE(-1, ref);
        files.push_back({ fn, ref });
        if(override_map)
            SetResFileAttrs(ref, GetResFileAttrs(ref) | 2);
        return ref;
    }

    static Handle map_of(INTEGER ref)
    {
        return (Handle)ROMlib_rntohandl(ref, nullptr);
    }

    static uint32_t guest(Handle h)
    {
        return h ? US_TO_SYN68K(h) : 0;
    }

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

    /* subq.l #4,sp; move.w #ref,-(sp); move.w #$0100,d0; _ResourceDispatch;
       move.l (sp)+,d1 */
    Handle trap_GetResourceMapHandle(INTEGER ref)
    {
        uint32_t r = run({ 0x598F, 0x3F3C, (uint16_t)ref, 0x303C, 0x0100, 0xA822, 0x221F });
        return r ? (Handle)SYN68K_TO_US(r) : nullptr;
    }

    /* subq.w #2,sp; move.l #arg,-(sp); move.w #d0,d0; _ResourceDispatch;
       move.w (sp)+,d1 -- a byte result is the high byte of the word. */
    int8_t trap_byte_of_handle(uint16_t d0, Handle arg)
    {
        uint32_t a = guest(arg);
        uint32_t r = run({ 0x554F, 0x2F3C, (uint16_t)(a >> 16), (uint16_t)a,
                           0x303C, d0, 0xA822, 0x321F });
        return (int8_t)(r >> 8);
    }

    /* subq.l #4,sp; move.l #map,-(sp); move.w #$0206,d0; _ResourceDispatch;
       move.l (sp)+,d1 */
    Handle trap_GetOverrideMap(Handle map)
    {
        uint32_t a = guest(map);
        uint32_t r = run({ 0x598F, 0x2F3C, (uint16_t)(a >> 16), (uint16_t)a,
                           0x303C, 0x0206, 0xA822, 0x221F });
        return r ? (Handle)SYN68K_TO_US(r) : nullptr;
    }
};
}

TEST_F(ResPrivate, GetResourceMapHandle)
{
    INTEGER ref = open("a");
    ROMlib_setreserr(-1);
    EXPECT_EQ(map_of(ref), GetResourceMapHandle(ref));
    EXPECT_EQ(noErr, ResError());
    EXPECT_EQ(ref, (*(resmaphand)GetResourceMapHandle(ref))->resfn);

    ROMlib_setreserr(-1);
    EXPECT_EQ(map_of(ref), trap_GetResourceMapHandle(ref));
    EXPECT_EQ(noErr, ResError());
}

TEST_F(ResPrivate, GetResourceMapHandleSystem)
{
    if(!LM(SysMapHndl))
        GTEST_SKIP() << "no System file in this environment";
    ROMlib_setreserr(-1);
    EXPECT_EQ(LM(SysMapHndl), GetResourceMapHandle(0));
    EXPECT_EQ(noErr, ResError());
    EXPECT_EQ(LM(SysMapHndl), trap_GetResourceMapHandle(0));
    EXPECT_EQ(noErr, ResError());
}

TEST_F(ResPrivate, GetResourceMapHandleNotOpen)
{
    INTEGER ref = open("a");
    INTEGER bogus = ref + 1234;
    EXPECT_EQ(nullptr, GetResourceMapHandle(bogus));
    EXPECT_EQ(resFNotFound, ResError());
    ROMlib_setreserr(noErr);
    EXPECT_EQ(nullptr, trap_GetResourceMapHandle(bogus));
    EXPECT_EQ(resFNotFound, ResError());
}

TEST_F(ResPrivate, GetResourceMapPrivateFlags)
{
    INTEGER ref = open("a");
    Handle map = map_of(ref);

    SetResFileAttrs(ref, 0xFF);
    ROMlib_setreserr(-1);
    EXPECT_EQ(0x1E, GetResourceMapPrivateFlags(map));
    EXPECT_EQ(noErr, ResError());
    ROMlib_setreserr(-1);
    EXPECT_EQ(0x1E, trap_byte_of_handle(0x0208, map));
    EXPECT_EQ(noErr, ResError());

    SetResFileAttrs(ref, mapReadOnly | mapChanged | 0x04 | 0x01);
    EXPECT_EQ(0x04, GetResourceMapPrivateFlags(map));
    EXPECT_EQ(0x04, trap_byte_of_handle(0x0208, map));

    SetResFileAttrs(ref, 0);
    EXPECT_EQ(0, GetResourceMapPrivateFlags(map));
    EXPECT_EQ(noErr, ResError());
}

TEST_F(ResPrivate, GetResourceMapPrivateFlagsNil)
{
    ROMlib_setreserr(noErr);
    EXPECT_EQ(0, GetResourceMapPrivateFlags(nullptr));
    EXPECT_EQ(nilHandleErr, ResError());
    ROMlib_setreserr(noErr);
    EXPECT_EQ(0, trap_byte_of_handle(0x0208, nullptr));
    EXPECT_EQ(nilHandleErr, ResError());
}

/* The spec's table, chains listed from the top. */
TEST_F(ResPrivate, GetOverrideMapPlain)
{
    INTEGER b = open("b");
    open("a");
    ROMlib_setreserr(-1);
    EXPECT_EQ(map_of(b), GetOverrideMap(map_of(b)));
    EXPECT_EQ(noErr, ResError());
    ROMlib_setreserr(-1);
    EXPECT_EQ(map_of(b), trap_GetOverrideMap(map_of(b)));
    EXPECT_EQ(noErr, ResError());
}

TEST_F(ResPrivate, GetOverrideMapOne)
{
    INTEGER b = open("b");
    INTEGER o1 = open("o1", true);
    EXPECT_EQ(map_of(o1), GetOverrideMap(map_of(b)));
    EXPECT_EQ(noErr, ResError());
    EXPECT_EQ(map_of(o1), trap_GetOverrideMap(map_of(b)));
    /* An override map at the top has nothing above it. */
    EXPECT_EQ(map_of(o1), GetOverrideMap(map_of(o1)));
}

TEST_F(ResPrivate, GetOverrideMapRun)
{
    INTEGER b = open("b");
    INTEGER o2 = open("o2", true);
    INTEGER o1 = open("o1", true);
    EXPECT_EQ(map_of(o1), GetOverrideMap(map_of(b)));
    EXPECT_EQ(noErr, ResError());
    EXPECT_EQ(map_of(o1), trap_GetOverrideMap(map_of(b)));
    EXPECT_EQ(map_of(o1), GetOverrideMap(map_of(o2)));
}

TEST_F(ResPrivate, GetOverrideMapRunBroken)
{
    INTEGER b = open("b");
    INTEGER o2 = open("o2", true);
    INTEGER a = open("a");
    open("o1", true);
    EXPECT_EQ(map_of(o2), GetOverrideMap(map_of(b)));
    EXPECT_EQ(noErr, ResError());
    EXPECT_EQ(map_of(o2), trap_GetOverrideMap(map_of(b)));
    (void)a;
}

TEST_F(ResPrivate, GetOverrideMapNotInChain)
{
    open("c");
    open("o1", true);
    open("a");
    Handle notamap = NewHandleClear(32);
    ROMlib_setreserr(noErr);
    EXPECT_EQ(nullptr, GetOverrideMap(notamap));
    EXPECT_EQ(resFNotFound, ResError());
    ROMlib_setreserr(noErr);
    EXPECT_EQ(nullptr, trap_GetOverrideMap(notamap));
    EXPECT_EQ(resFNotFound, ResError());
    EXPECT_EQ(nullptr, GetOverrideMap(nullptr));
    EXPECT_EQ(resFNotFound, ResError());
    DisposeHandle(notamap);
}

TEST_F(ResPrivate, ResourceMapIsSystem)
{
    INTEGER ref = open("a");
    ROMlib_setreserr(-1);
    EXPECT_FALSE(ResourceMapIsSystem(map_of(ref)));
    EXPECT_EQ(noErr, ResError());
    ROMlib_setreserr(-1);
    EXPECT_EQ(0, trap_byte_of_handle(0x02FF, map_of(ref)));
    EXPECT_EQ(noErr, ResError());

    if(LM(SysMapHndl))
    {
        ROMlib_setreserr(-1);
        EXPECT_TRUE(ResourceMapIsSystem(LM(SysMapHndl)));
        EXPECT_EQ(noErr, ResError());
        EXPECT_NE(0, trap_byte_of_handle(0x02FF, LM(SysMapHndl)));
        EXPECT_EQ(noErr, ResError());
    }
}

TEST_F(ResPrivate, ResourceMapIsSystemNil)
{
    ROMlib_setreserr(noErr);
    EXPECT_FALSE(ResourceMapIsSystem(nullptr));
    EXPECT_EQ(resFNotFound, ResError());
    ROMlib_setreserr(noErr);
    EXPECT_EQ(0, trap_byte_of_handle(0x02FF, nullptr));
    EXPECT_EQ(resFNotFound, ResError());
}

/* A file opened in system mode belongs to the system. */
TEST_F(ResPrivate, ResourceMapIsSystemSystemMode)
{
    BeginSystemMode();
    INTEGER ref = open("sys");
    EndSystemMode();
    ASSERT_TRUE(ROMlib_system_resource_file_p(ref));
    ROMlib_setreserr(-1);
    EXPECT_TRUE(ResourceMapIsSystem(map_of(ref)));
    EXPECT_EQ(noErr, ResError());
    EXPECT_NE(0, trap_byte_of_handle(0x02FF, map_of(ref)));
}
