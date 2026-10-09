#include "gtest/gtest.h"

#include <base/cpu.h>
#include <base/mactype.h>
#include <IntlUtil.h>
#include <MemoryMgr.h>

#include <cstring>
#include <vector>

using namespace Executor;

/* Type Select through a 68k callback, as Finder uses it to select an icon
   by typing its name: Finder's IndexToStringProc reads its list at
   item - 1 and always returns true. */
class TypeSelect : public testing::Test
{
protected:
    /* pascal Boolean getString(short item, ScriptCode *script,
                                StringPtr *str, void *table)
       { *str = ((StringPtr *)table)[item - 1]; return true; } */
    static constexpr uint16_t kGetString[] = {
        0x4e56, 0x0000,         // link   a6,#0
        0x302e, 0x0014,         // move.w 20(a6),d0     item
        0x5340,                 // subq.w #1,d0
        0x48c0,                 // ext.l  d0
        0xe588,                 // lsl.l  #2,d0
        0x206e, 0x0008,         // movea.l 8(a6),a0     table
        0x2270, 0x0800,         // movea.l 0(a0,d0.l),a1
        0x206e, 0x000c,         // movea.l 12(a6),a0    str
        0x2089,                 // move.l a1,(a0)
        0x1d7c, 0x0001, 0x0016, // move.b #1,22(a6)     true
        0x4e5e,                 // unlk   a6
        0x205f,                 // movea.l (sp)+,a0
        0x4fef, 0x000e,         // lea    14(sp),sp
        0x4ed0,                 // jmp    (a0)
    };

    Ptr code = nullptr, table = nullptr;
    std::vector<Ptr> strings;
    TypeSelectRecord tsr;

    TypeSelect()
    {
        code = NewPtr(sizeof kGetString);
        auto p = (GUEST<uint16_t> *)code;
        for(uint16_t w : kGetString)
            *p++ = w;
        destroy_blocks(0, ~0);
        TypeSelectClear(&tsr);
    }
    ~TypeSelect()
    {
        for(Ptr s : strings)
            DisposePtr(s);
        if(table)
            DisposePtr(table);
        DisposePtr(code);
    }

    void items(std::initializer_list<const char *> names)
    {
        table = NewPtr(4 * names.size());
        auto t = (GUEST<Ptr> *)table;
        for(const char *n : names)
        {
            Ptr s = NewPtr(256);
            s[0] = strlen(n);
            memcpy(s + 1, n, s[0]);
            strings.push_back(s);
            *t++ = s;
        }
    }

    INTEGER find(const char *keys)
    {
        tsr.tsrKeyStrokes[0] = strlen(keys);
        memcpy(tsr.tsrKeyStrokes + 1, keys, strlen(keys));
        return TypeSelectFindItem(&tsr, strings.size(), 0, IndexToStringUPP(code), table);
    }
};

TEST_F(TypeSelect, FindsFirstAtOrAfterKeys)
{
    // Not in order: the callback's list is whatever order the caller keeps.
    items({ "SimpleText", "About System 7.5", "MacPerl", "Apple Extras", "MR Browser" });
    EXPECT_EQ(3, find("m"));    // MacPerl: items count from 1
    EXPECT_EQ(5, find("mr"));
    EXPECT_EQ(1, find("s"));
    EXPECT_EQ(2, find("a"));
    EXPECT_EQ(4, find("app"));
    EXPECT_EQ(3, find("b"));    // nothing starts with b: the next name up
}

TEST_F(TypeSelect, PastTheEndPicksTheLast)
{
    items({ "Beta", "Zeta", "Alpha" });
    EXPECT_EQ(2, find("zz"));
}

TEST_F(TypeSelect, IgnoresCase)
{
    items({ "alpha", "Beta" });
    EXPECT_EQ(2, find("BE"));
}
