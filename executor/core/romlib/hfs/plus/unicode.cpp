#include "unicode.h"
#include "unicode_tables.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>

using namespace Executor::hfsplus;

namespace
{

/* TN1150's table lookup: the folded character, 0 for an ignored one. */
inline char16_t fold(char16_t c)
{
    uint16_t sub = tables::kLowerCase[c >> 8];
    return sub ? tables::kLowerCase[sub + (c & 0xFF)] : c;
}

/* Decomposed sequences of two or three code units -> their MacRoman byte;
   single characters (decomposed or as ROMAN.TXT has them) -> byte. */
struct Reverse
{
    std::map<std::u16string, uint8_t> multi;
    std::map<char16_t, uint8_t> single;
    Reverse()
    {
        for(int b = 255; b >= 0; b--)      /* low bytes win: 0x00-0x7F as themselves */
        {
            const uint16_t *d = tables::kMacRomanDecomposed[b];
            std::u16string seq;
            for(int i = 0; i < 3 && (d[i] || i == 0); i++)
                seq += char16_t(d[i]);
            if(seq.size() > 1)
                multi[seq] = uint8_t(b);
            else
                single[seq[0]] = uint8_t(b);
            single.emplace(char16_t(tables::kMacRomanUnicode[b]), uint8_t(b));
        }
    }
};

const Reverse& reverse()
{
    static const Reverse r;
    return r;
}

} // namespace

int Executor::hfsplus::fastUnicodeCompare(const char16_t *a, size_t alen, const char16_t *b, size_t blen)
{
    for(;;)
    {
        char16_t c1 = 0, c2 = 0;
        while(alen && c1 == 0)
        {
            c1 = fold(*a++);
            --alen;
        }
        while(blen && c2 == 0)
        {
            c2 = fold(*b++);
            --blen;
        }
        if(c1 != c2)
            return c1 < c2 ? -1 : 1;
        if(c1 == 0)
            return 0;
    }
}

int Executor::hfsplus::binaryCompare(const char16_t *a, size_t alen, const char16_t *b, size_t blen)
{
    for(size_t i = 0; i < alen && i < blen; i++)
        if(a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return alen == blen ? 0 : alen < blen ? -1 : 1;
}

std::u16string Executor::hfsplus::macRomanToUnicode(const std::string& macRoman)
{
    std::u16string out;
    for(unsigned char b : macRoman)
    {
        const uint16_t *d = tables::kMacRomanDecomposed[b];
        out += char16_t(d[0]);
        for(int i = 1; i < 3 && d[i]; i++)
            out += char16_t(d[i]);
    }
    return out;
}

char16_t Executor::hfsplus::macRomanCharacter(uint8_t b)
{
    return char16_t(tables::kMacRomanUnicode[b]);
}

std::string Executor::hfsplus::unicodeToMacRoman(const std::u16string& name, bool *lossy)
{
    const Reverse& r = reverse();
    std::string out;
    bool lost = false;
    for(size_t i = 0; i < name.size();)
    {
        bool done = false;
        for(size_t n = std::min<size_t>(3, name.size() - i); n >= 2 && !done; n--)
        {
            auto it = r.multi.find(name.substr(i, n));
            if(it != r.multi.end())
            {
                out += char(it->second);
                i += n;
                done = true;
            }
        }
        if(done)
            continue;
        char16_t c = name[i++];
        auto it = r.single.find(c);
        /* ':' is the classic path separator: Mac OS 9 shows one in an
           HFS+ name as '?' */
        if(it == r.single.end() || c == u':')
        {
            out += '?';
            lost = true;
        }
        else
            out += char(it->second);
    }
    if(lossy)
        *lossy = lost;
    return out;
}

std::string Executor::hfsplus::classicName(const std::u16string& name, uint32_t cnid)
{
    bool lossy = false;
    std::string mac = unicodeToMacRoman(name, &lossy);
    if(!lossy && mac.size() <= 31)
        return mac;

    /* As 9.0.4: the extension is the last '.' and what follows, kept when
       it is 2 to 6 characters long. */
    std::string ext;
    size_t dot = mac.rfind('.');
    if(dot != std::string::npos && dot > 0 && mac.size() - dot >= 2 && mac.size() - dot <= 6)
        ext = mac.substr(dot);
    std::string base = mac.substr(0, mac.size() - ext.size());

    char tag[16];
    snprintf(tag, sizeof tag, "#%X", (unsigned)cnid);
    size_t room = 31 - std::min<size_t>(31, std::string(tag).size() + ext.size());
    if(base.size() > room)
        base.resize(room);
    return base + tag + ext;
}

std::optional<uint32_t> Executor::hfsplus::cnidInClassicName(const std::string& mac)
{
    size_t hash = mac.rfind('#');
    if(hash == std::string::npos)
        return std::nullopt;
    uint32_t cnid = 0;
    size_t i = hash + 1;
    size_t digits = 0;
    for(; i < mac.size() && digits < 8; i++, digits++)
    {
        char c = mac[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10
                : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if(v < 0)
            break;
        cnid = cnid << 4 | uint32_t(v);
    }
    if(!digits || (i < mac.size() && mac[i] != '.'))
        return std::nullopt;
    return cnid;
}

std::string Executor::hfsplus::toUTF8(const std::u16string& s)
{
    std::string out;
    for(size_t i = 0; i < s.size(); i++)
    {
        uint32_t c = s[i];
        if(c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
        if(c < 0x80)
            out += char(c);
        else if(c < 0x800)
        {
            out += char(0xC0 | c >> 6);
            out += char(0x80 | (c & 0x3F));
        }
        else if(c < 0x10000)
        {
            out += char(0xE0 | c >> 12);
            out += char(0x80 | (c >> 6 & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
        else
        {
            out += char(0xF0 | c >> 18);
            out += char(0x80 | (c >> 12 & 0x3F));
            out += char(0x80 | (c >> 6 & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}
