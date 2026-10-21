/* The System's low-memory routines $7B0 (SelectorTableDispatch) and $668
   (CallDetachedPackage), as docs/executor/cleanroom/system-routines.md
   specifies them. Each call is made as guest code makes it: a trampoline
   `move.l $7B0.w,-(sp); rts` entered with the caller's return address
   (execute68K's exit) on top of the stack and the routine's parameters
   above it. */
#include "gtest/gtest.h"

#include "compat.h"

#include <base/common.h>
#include <base/cpu.h>
#include <rsys/sysroutines.h>
#include <MemoryMgr.h>
#include <Package.h>
#include <ResourceMgr.h>
#include <FileMgr.h>

#include <vector>

using namespace Executor;

namespace
{
/* 68k encodings used below. */
enum : uint16_t
{
    MOVEL_A0_D2 = 0x2408, /* move.l a0,d2 */
    MOVEL_SP_D3 = 0x260F, /* move.l sp,d3 */
    MOVEL_D0_D4 = 0x2800, /* move.l d0,d4 */
    MOVEAL_POP_A0 = 0x205F, /* movea.l (sp)+,a0 */
    MOVEAL_POP_A1 = 0x225F, /* movea.l (sp)+,a1 */
    MOVEAL_POP_A2 = 0x245F, /* movea.l (sp)+,a2 */
    MOVEAL_POP_A3 = 0x265F, /* movea.l (sp)+,a3 */
    MOVEL_POP_D7 = 0x2E1F, /* move.l (sp)+,d7 */
    MOVEW_POP_D1 = 0x321F, /* move.w (sp)+,d1 */
    MOVEW_POP_D2 = 0x341F, /* move.w (sp)+,d2 */
    SUBW_D1_D2 = 0x9441, /* sub.w d1,d2 */
    MOVEW_D2_ATSP = 0x3E82, /* move.w d2,(sp) */
    MOVEW_IMM_ATSP = 0x3EBC, /* move.w #imm,(sp) */
    LEA_D16SP_SP = 0x4FEF, /* lea d16(sp),sp */
    JMP_A1 = 0x4ED1, /* jmp (a1) */
    HGETSTATE = 0xA069, /* _HGetState: a0 handle -> d0 */
    MOVEW_D0_A2INC = 0x34C0, /* move.w d0,(a2)+ */
    MOVEW_A3IND_A2IND = 0x3493, /* move.w (a3),(a2) */
    PUSHL_D7 = 0x2F07, /* move.l d7,-(sp) */
    PUSHL_A0 = 0x2F08, /* move.l a0,-(sp) */
    PUSHL_A1 = 0x2F09, /* move.l a1,-(sp) */
    PUSHL_A2 = 0x2F0A, /* move.l a2,-(sp) */
    MOVEW_IMM_D0 = 0x303C, /* move.w #imm,d0 */
    PEA_PC = 0x487A, /* pea d16(pc) */
    PUSHL_ABSW = 0x2F38, /* move.l abs.w,-(sp) */
    RTS = 0x4E75,
};

const uint16_t sentinel = 0x5A5A; /* above the result word: never touched */

class SysRoutines : public testing::Test
{
public:
    Ptr code;
    size_t used = 0;
    uint32_t sp0;
    uint32_t trampoline7B0, trampoline668;

    SysRoutines()
    {
        ROMlib_install_system_routines();
        code = NewPtrClear(4096);
        sp0 = (ptr_to_longint(LM(MemTop)) - 0x200) & ~3u;
        trampoline7B0 = emit({ PUSHL_ABSW, 0x07B0, RTS });
        trampoline668 = emit({ PUSHL_ABSW, 0x0668, RTS });
    }
    ~SysRoutines()
    {
        DisposePtr(code);
    }

    uint32_t here() const
    {
        return ptr_to_longint(code) + used;
    }

    uint32_t emit(std::initializer_list<uint16_t> words)
    {
        uint32_t at = here();
        for(uint16_t w : words)
        {
            WRITEUW(here(), w);
            used += 2;
        }
        return at;
    }

    /* A routine that records A0 (d2), SP (d3) and D0 (d4) on entry, pops
       `paramWords` words and answers `tag`. */
    uint32_t recorder(int paramWords, uint16_t tag)
    {
        return emit({ MOVEL_A0_D2, MOVEL_SP_D3, MOVEL_D0_D4, MOVEAL_POP_A1,
                      LEA_D16SP_SP, (uint16_t)(paramWords * 2),
                      MOVEW_IMM_ATSP, tag, JMP_A1 });
    }

    /* Stack the parameters (params[0] lowest, i.e. pushed last), the word
       above them and a sentinel, then enter through `trampoline`. Returns
       the word in the result slot; leaves SP in EM_A7. */
    uint16_t call(uint32_t trampoline, const std::vector<uint16_t> &params,
                  uint16_t above, uint32_t d0, uint32_t a0, uint32_t a1 = 0)
    {
        uint32_t p = sp0;
        for(uint16_t w : params)
        {
            WRITEUW(p, w);
            p += 2;
        }
        WRITEUW(p, above);
        WRITEUW(p + 2, sentinel);
        EM_A7 = sp0;
        EM_D0 = d0;
        EM_A0 = a0;
        EM_A1 = a1;
        EM_D2 = EM_D3 = EM_D4 = 0xDEADBEEF;
        destroy_blocks(0, ~0);
        execute68K(trampoline);
        return READUW(p);
    }

    uint32_t resultSlot(size_t paramWords) const
    {
        return sp0 + 2 * paramWords;
    }

    static std::vector<uint16_t> words(size_t n)
    {
        std::vector<uint16_t> v(n);
        for(size_t i = 0; i < n; i++)
            v[i] = 0x1100 + i;
        return v;
    }
};

uint16_t d0_of(int paramWords, int selector)
{
    return (uint16_t)((paramWords << 8) | (uint8_t)selector);
}
}

TEST_F(SysRoutines, LowMemoryPointsAtThem)
{
    EXPECT_NE(nullptr, (void *)LM(SelectorTableDispatch));
    EXPECT_NE(nullptr, (void *)LM(CallDetachedPackage));
}

/* Kind 0: a word offset from the table per selector, here -2..1. */
TEST_F(SysRoutines, DispatchKind0)
{
    uint32_t r[4];
    for(int i = 0; i < 4; i++)
        r[i] = recorder(1, 0x100 + i);
    uint32_t table = emit({ 0, 0xFE01 });
    for(int i = 0; i < 4; i++)
        emit({ (uint16_t)(int16_t)(r[i] - table) });

    for(int sel = -2; sel <= 1; sel++)
    {
        uint32_t d0 = 0x12340000 | d0_of(1, sel);
        uint16_t res = call(trampoline7B0, { 0x4242 }, 0, d0, table);
        int i = sel + 2;
        EXPECT_EQ(0x100 + i, res) << "selector " << sel;
        EXPECT_EQ(r[i], EM_D2) << "A0 is the routine on entry";
        EXPECT_EQ(sp0 - 4, EM_D3) << "the return address still on top";
        EXPECT_EQ(d0, EM_D4) << "D0 unchanged";
        EXPECT_EQ(resultSlot(1), EM_A7);
        EXPECT_EQ(sentinel, READUW(resultSlot(1) + 2));
    }
}

/* Kind 1: word entries, the selectors (0, 2, 4) are byte offsets. */
TEST_F(SysRoutines, DispatchKind1)
{
    uint32_t r[3];
    for(int i = 0; i < 3; i++)
        r[i] = recorder(2, 0x200 + i);
    uint32_t table = emit({ 1, 0x0004 });
    for(int i = 0; i < 3; i++)
        emit({ (uint16_t)(int16_t)(r[i] - table) });

    for(int i = 0; i < 3; i++)
    {
        uint16_t res = call(trampoline7B0, words(2), 0, d0_of(2, 2 * i), table);
        EXPECT_EQ(0x200 + i, res);
        EXPECT_EQ(r[i], EM_D2);
        EXPECT_EQ(sp0 - 4, EM_D3);
        EXPECT_EQ(resultSlot(2), EM_A7);
    }
}

/* Kind 2: absolute addresses, selectors 5..7. */
TEST_F(SysRoutines, DispatchKind2)
{
    uint32_t r[3];
    for(int i = 0; i < 3; i++)
        r[i] = recorder(0, 0x300 + i);
    uint32_t table = emit({ 2, 0x0507 });
    for(int i = 0; i < 3; i++)
        emit({ (uint16_t)(r[i] >> 16), (uint16_t)r[i] });

    for(int i = 0; i < 3; i++)
    {
        uint16_t res = call(trampoline7B0, {}, 0, d0_of(0, 5 + i), table);
        EXPECT_EQ(0x300 + i, res);
        EXPECT_EQ(r[i], EM_D2);
        EXPECT_EQ(sp0 - 4, EM_D3);
        EXPECT_EQ(resultSlot(0), EM_A7);
    }
}

/* Out of range: parameters popped, paramErr in the result slot. */
TEST_F(SysRoutines, DispatchOutOfRange)
{
    uint32_t r = recorder(0, 0x400);
    uint32_t table = emit({ 0, 0xFF01 }); /* -1..1 */
    for(int i = 0; i < 3; i++)
        emit({ (uint16_t)(int16_t)(r - table) });

    for(int n : { 0, 1, 5 })
        for(int sel : { -2, 2, -128, 127 })
        {
            uint16_t res = call(trampoline7B0, words(n), 0x7777, d0_of(n, sel), table);
            EXPECT_EQ((uint16_t)paramErr, res) << n << " words, selector " << sel;
            EXPECT_EQ(resultSlot(n), EM_A7) << n << " words, selector " << sel;
            EXPECT_EQ(sentinel, READUW(resultSlot(n) + 2));
            EXPECT_EQ(0xDEADBEEF, EM_D2) << "the routine didn't run";
        }
}

/* A 255-word parameter list is popped whole. */
TEST_F(SysRoutines, DispatchOutOfRangeLargeParams)
{
    uint32_t table = emit({ 2, 0x0101 });
    emit({ 0, 0 });
    sp0 -= 0x400;
    uint16_t res = call(trampoline7B0, words(255), 0, d0_of(255, 3), table);
    EXPECT_EQ((uint16_t)paramErr, res);
    EXPECT_EQ(resultSlot(255), EM_A7);
}

namespace
{
/* A package: a 10-byte header, then a kind-0 table of three routines.
     0: function(a, b: INTEGER): INTEGER = a - b
     1: procedure(h: Handle; count, out: Ptr): out^ = HGetState(h), count^
     2: function(h: Handle; count, out: Ptr): INTEGER = $1234, after
        calling routine 1 through $668 with its own parameters */
std::vector<uint16_t> package_code()
{
    std::vector<uint16_t> c = { 0x6000, 0x0008, 0, 0, 0 }; /* header */
    size_t table = c.size();
    c.insert(c.end(), { 0, 0x0002, 0, 0, 0 });
    auto entry = [&](int i) { c[table + 2 + i] = (uint16_t)((c.size() - table) * 2); };

    entry(0);
    c.insert(c.end(), { MOVEAL_POP_A1, MOVEW_POP_D1, MOVEW_POP_D2, SUBW_D1_D2,
                        MOVEW_D2_ATSP, JMP_A1 });
    entry(1);
    c.insert(c.end(), { MOVEAL_POP_A1, MOVEAL_POP_A2, MOVEAL_POP_A3, MOVEAL_POP_A0,
                        HGETSTATE, MOVEW_D0_A2INC, MOVEW_A3IND_A2IND, JMP_A1 });
    entry(2);
    c.insert(c.end(), { MOVEL_POP_D7, MOVEAL_POP_A2, MOVEAL_POP_A1, MOVEAL_POP_A0,
                        PUSHL_D7, PUSHL_A0, PUSHL_A1, PUSHL_A2,
                        MOVEW_IMM_D0, 0x0601,
                        PEA_PC, 0x0008, /* the return: after the rts */
                        PUSHL_ABSW, 0x0668, RTS,
                        MOVEAL_POP_A1, MOVEW_IMM_ATSP, 0x1234, JMP_A1 });
    return c;
}

Handle new_package()
{
    std::vector<uint16_t> c = package_code();
    Handle h = NewHandle(c.size() * 2);
    for(size_t i = 0; i < c.size(); i++)
        ((GUEST<uint16_t> *)*h)[i] = c[i];
    return h;
}

bool locked(Handle h)
{
    return HGetState(h) & 0x80;
}

std::vector<uint16_t> longs(std::initializer_list<uint32_t> ls)
{
    /* Pascal order: the first parameter is pushed first, so it is highest. */
    std::vector<uint16_t> v;
    for(uint32_t l : ls)
    {
        v.insert(v.begin(), (uint16_t)l);
        v.insert(v.begin(), (uint16_t)(l >> 16));
    }
    return v;
}
}

class DetachedPackage : public SysRoutines
{
public:
    Handle pkg;
    GUEST<int16_t> *count;
    GUEST<uint16_t> *out;

    DetachedPackage()
    {
        pkg = new_package();
        count = (GUEST<int16_t> *)NewPtrClear(2);
        out = (GUEST<uint16_t> *)NewPtrClear(4);
    }
    ~DetachedPackage()
    {
        DisposeHandle(pkg);
        DisposePtr((Ptr)count);
        DisposePtr((Ptr)out);
    }

    uint32_t addr(const void *p)
    {
        return US_TO_SYN68K(p);
    }
};

TEST_F(DetachedPackage, Function)
{
    /* a - b: a = 10 pushed first (higher), b = 3 */
    uint16_t res = call(trampoline668, { 3, 10 }, 0xFFFF, d0_of(2, 0),
                        addr(pkg), addr(count));
    EXPECT_EQ(7, res);
    EXPECT_EQ(resultSlot(2), EM_A7);
    EXPECT_EQ(sentinel, READUW(resultSlot(2) + 2));
    EXPECT_EQ(0, *count);
    EXPECT_FALSE(locked(pkg));

    res = call(trampoline668, { 50, 8 }, 0, d0_of(2, 0), addr(pkg), addr(count));
    EXPECT_EQ((uint16_t)-42, res);
}

TEST_F(DetachedPackage, NilHandle)
{
    for(int n : { 0, 1, 3 })
    {
        uint16_t res = call(trampoline668, words(n), 0, d0_of(n, 0), 0, addr(count));
        EXPECT_EQ((uint16_t)memFullErr, res);
        EXPECT_EQ(resultSlot(n), EM_A7);
        EXPECT_EQ(sentinel, READUW(resultSlot(n) + 2));
        EXPECT_EQ(0, *count);
    }
}

/* Locked for the call only; a procedure leaves the word above its
   parameters as it was. */
TEST_F(DetachedPackage, LockedDuringCall)
{
    EXPECT_FALSE(locked(pkg));
    uint16_t above = call(trampoline668, longs({ addr(pkg), addr(count), addr(out) }),
                          0xBEEF, d0_of(6, 1), addr(pkg), addr(count));
    EXPECT_EQ(0xBEEF, above);
    EXPECT_EQ(resultSlot(6), EM_A7);
    EXPECT_EQ(sentinel, READUW(resultSlot(6) + 2));
    EXPECT_TRUE(out[0] & 0x80) << "locked during the call";
    EXPECT_EQ(1, out[1]) << "lock count during the call";
    EXPECT_EQ(0, *count);
    EXPECT_FALSE(locked(pkg));
}

/* Already counted in by an outer caller: not unlocked on return. */
TEST_F(DetachedPackage, LockCountKeptByOthers)
{
    HLock(pkg);
    *count = 1;
    call(trampoline668, longs({ addr(pkg), addr(count), addr(out) }), 0,
         d0_of(6, 1), addr(pkg), addr(count));
    EXPECT_EQ(2, out[1]);
    EXPECT_EQ(1, *count);
    EXPECT_TRUE(locked(pkg));
    HUnlock(pkg);
    *count = 0;
}

/* Routine 2 calls routine 1 through $668: count 2 and locked inside, the
   outer call's stack intact, unlocked when the outer call returns. */
TEST_F(DetachedPackage, Nested)
{
    uint16_t res = call(trampoline668, longs({ addr(pkg), addr(count), addr(out) }),
                        0, d0_of(6, 2), addr(pkg), addr(count));
    EXPECT_EQ(0x1234, res);
    EXPECT_EQ(resultSlot(6), EM_A7);
    EXPECT_EQ(sentinel, READUW(resultSlot(6) + 2));
    EXPECT_TRUE(out[0] & 0x80);
    EXPECT_EQ(2, out[1]);
    EXPECT_EQ(0, *count);
    EXPECT_FALSE(locked(pkg));
}

TEST_F(DetachedPackage, OutOfRangeSelector)
{
    uint16_t res = call(trampoline668, words(2), 0, d0_of(2, 3), addr(pkg), addr(count));
    EXPECT_EQ((uint16_t)paramErr, res);
    EXPECT_EQ(resultSlot(2), EM_A7);
}

/* A purged handle that isn't a resource stays purged: ResErr is the
   result, nothing locked or counted. */
TEST_F(DetachedPackage, PurgedNotReloadable)
{
    HPurge(pkg);
    EmptyHandle(pkg);
    ASSERT_EQ(nullptr, *pkg);
    uint16_t res = call(trampoline668, { 3, 10 }, 0, d0_of(2, 0), addr(pkg), addr(count));
    EXPECT_EQ((uint16_t)LM(ResErr), res);
    EXPECT_NE(noErr, (int16_t)res);
    EXPECT_EQ(resultSlot(2), EM_A7);
    EXPECT_EQ(0, *count);
    EXPECT_EQ(nullptr, *pkg);
}

/* A purgeable resource: emptied, then reloaded by the call. */
TEST_F(DetachedPackage, PurgedResourceReloads)
{
    StringPtr name = PSTR("test-detached-pkg");
    CreateResFile(name);
    INTEGER ref = OpenResFile(name);
    ASSERT_NE(-1, ref);

    Handle h = new_package();
    AddResource(h, 'TPKG', 128, PSTR(""));
    ASSERT_EQ(noErr, ResError());
    WriteResource(h);
    UpdateResFile(ref);
    ASSERT_EQ(noErr, ResError());

    HPurge(h);
    EmptyHandle(h);
    ASSERT_EQ(nullptr, *h);

    LM(RomMapInsert) = 0;
    LM(TmpResLoad) = 0;
    uint16_t res = call(trampoline668, { 3, 10 }, 0, d0_of(2, 0), addr(h), addr(count));
    EXPECT_EQ(7, res);
    EXPECT_EQ(resultSlot(2), EM_A7);
    EXPECT_NE(nullptr, *h);
    EXPECT_EQ(0, *count);
    EXPECT_FALSE(locked(h));

    CloseResFile(ref);
    FSDelete(name, 0);
}
