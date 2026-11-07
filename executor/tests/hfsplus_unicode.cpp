/* The HFS+ reader's names (romlib/hfs/plus/unicode.h): TN1150's
   case-insensitive order, MacRoman <-> decomposed Unicode, and the short
   names Mac OS 9.0.4 gives long or unconvertible ones
   (tests/guest/hfsplus/oracle-9.0.4/longnames.txt). */
#include "gtest/gtest.h"

#include <hfs/plus/unicode.h>

using namespace Executor::hfsplus;

namespace
{
int cmp(const std::u16string& a, const std::u16string& b)
{
    int r = fastUnicodeCompare(a, b);
    return r < 0 ? -1 : r > 0 ? 1 : 0;
}
}

TEST(HFSPlusUnicode, CaseInsensitiveOrder)
{
    EXPECT_EQ(cmp(u"System Folder", u"system folder"), 0);
    EXPECT_EQ(cmp(u"apple", u"Bob"), -1);          /* folded: a < b */
    EXPECT_EQ(cmp(u"abc", u"abcd"), -1);
    EXPECT_EQ(cmp(u"ΑΒ", u"αβ"), 0);     /* Greek folds */
    EXPECT_EQ(cmp(u"a‌b", u"ab"), 0);         /* ignorable */
    EXPECT_EQ(cmp(u"É", u"é"), -1);      /* precomposed letters don't fold (never stored): by value */
    EXPECT_EQ(cmp(u"É", u"é"), 0);
    EXPECT_EQ(cmp(std::u16string(1, u'\0'), u""), 1);       /* NUL is a character */
}

TEST(HFSPlusUnicode, MacRomanRoundTrip)
{
    for(int b = 0; b < 256; b++)
    {
        if(b == ':')
            continue;                               /* the classic separator: '?' */
        std::string s(1, char(b));
        bool lossy = true;
        EXPECT_EQ(unicodeToMacRoman(macRomanToUnicode(s), &lossy), s) << "byte " << b;
        EXPECT_FALSE(lossy) << "byte " << b;
    }
}

TEST(HFSPlusUnicode, Decomposed)
{
    EXPECT_EQ(macRomanToUnicode("Caf\x8E"), u"Café");
    EXPECT_EQ(macRomanToUnicode("\xAD"), u"≠");          /* U+2000-2FFF stay whole */
    EXPECT_EQ(macRomanToUnicode("\xF0"), u"");          /* the Apple logo */
    EXPECT_EQ(unicodeToMacRoman(u"Café"), "Caf\x8E");    /* precomposed, from other writers */
}

TEST(HFSPlusUnicode, Lossy)
{
    bool lossy = false;
    EXPECT_EQ(unicodeToMacRoman(u"Greek αβγ", &lossy), "Greek ???");
    EXPECT_TRUE(lossy);
    /* one '?' per code unit that doesn't compose: alpha + combining acute */
    EXPECT_EQ(unicodeToMacRoman(u"ά"), "??");
    EXPECT_EQ(unicodeToMacRoman(u"Colon:in a name", &lossy), "Colon?in a name");
    EXPECT_TRUE(lossy);
}

TEST(HFSPlusUnicode, ClassicNamesAs904)
{
    struct
    {
        const char16_t *name;
        uint32_t cnid;
        const char *mac;
    } cases[] = {
        { u"This is a file with a rather long name.txt", 0x16, "This is a file with a ra#16.txt" },
        { u"A long name with no extension at all here", 0x17, "A long name with no extensio#17" },
        { u"Exactly thirty-one characters!!", 0x18, "Exactly thirty-one characters!!" },
        { u"Exactly thirty-two characters!!!", 0x19, "Exactly thirty-two character#19" },
        { u"Café crème — accented and long name.txt", 0x1A, "Caf\x8E" " cr\x8F" "me \xD1 accented an#1A.txt" },
        { u"Short Café.txt", 0x1B, "Short Caf\x8E.txt" },
        { u"Greek αβγ.txt", 0x1C, "Greek ???#1C.txt" },
        { u"日本語 long Japanese name.txt", 0x1D, "??? long Japanese name#1D.txt" },
        { u"Twins with the same long prefix - one.txt", 0x1E, "Twins with the same long#1E.txt" },
        { u"Archive.with.many.dots.and.a.long.name.tar.gz", 0x20, "Archive.with.many.dots.an#20.gz" },
        { u"Long named document with both forks.rsrc", 0x21, "Long named document wit#21.rsrc" },
        { u"A long name with a six char extension.abcde", 0x27, "A long name with a six#27.abcde" },
        { u"A long name with a seven char extension.abcdef", 0x28, "A long name with a seven cha#28" },
        { u"A long name that ends with a dot, nothing after.", 0x2A, "A long name that ends with a#2A" },
        { u".A leading dot and a name long enough to cut", 0x2C, ".A leading dot and a name lo#2C" },
        { u"Colon:in a name", 0x2E, "Colon?in a name#2E" },
        { u"Slash/in a name", 0x2F, "Slash/in a name" },
    };
    for(const auto& c : cases)
    {
        EXPECT_EQ(classicName(c.name, c.cnid), c.mac) << toUTF8(c.name);
        if(std::string(c.mac).find('#') != std::string::npos)
            EXPECT_EQ(cnidInClassicName(c.mac), std::optional<uint32_t>(c.cnid)) << c.mac;
    }
    EXPECT_EQ(cnidInClassicName("Short Caf\x8E.txt"), std::nullopt);
    EXPECT_EQ(cnidInClassicName("Price #1 list"), std::nullopt);
}
