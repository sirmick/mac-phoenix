#pragma once

#include <string>
#include <string_view>
#include <rsys/filesystem.h>
#include <base/mactype.h>

namespace Executor
{

//using mac_string_view = std::basic_string_view<unsigned char>;

struct mac_string_view : std::basic_string_view<unsigned char>
{
    mac_string_view() = default;
    mac_string_view(const unsigned char* pascalString)
    {
        if(pascalString)
            *this = mac_string_view(pascalString + 1, pascalString[0]);
    }
    mac_string_view(GUEST<unsigned char*> pascalString)
        : mac_string_view(toHost(pascalString))
    {
    }
    mac_string_view(GUEST<const unsigned char*> pascalString)
        : mac_string_view(toHost(pascalString))
    {
    }

    mac_string_view(const unsigned char* p, size_t n)
        : basic_string_view(p,n)
    {
    }

    mac_string_view(std::basic_string_view<unsigned char> v)
        : basic_string_view(std::move(v))
    {
    }
    mac_string_view(const std::basic_string<unsigned char>& v)
        : basic_string_view(v)
    {
    }

    template<class It>
    mac_string_view(It p, It q)
        : basic_string_view(&*p,q-p)
    {
    }
};

inline mac_string_view PascalStringView(const unsigned char *s)
{
    if(s)
        return mac_string_view(s+1, (size_t)s[0]);
    else
        return mac_string_view();
}

unsigned char *assignPString(unsigned char* s, mac_string_view v, int max = 255);

using mac_string = std::basic_string<unsigned char>;

std::u32string toUnicode(mac_string_view sv);
mac_string toMacRoman(const std::u32string& s);

fs::path toUnicodeFilename(mac_string_view sv);
mac_string toMacRomanFilename(const fs::path& s, int index = 0);

bool matchesMacRomanFilename(const fs::path& s, mac_string_view sv);

bool equalCaseInsensitive(mac_string_view s1, mac_string_view s2);
}
