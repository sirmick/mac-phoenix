#pragma once

#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <rsys/filesystem.h>
#include <base/mactype.h>

namespace Executor
{

/* MacPhoenix: character traits for Mac byte strings. The standard only
 * provides std::char_traits for char and the wide types; libc++ has
 * dropped its unsigned char one. Bytes compare as unsigned. */
struct mac_char_traits
{
    using char_type = unsigned char;
    using int_type = int;
    using off_type = std::streamoff;
    using pos_type = std::streampos;
    using state_type = std::mbstate_t;

    static constexpr void assign(char_type &r, const char_type &a) noexcept { r = a; }
    static constexpr bool eq(char_type a, char_type b) noexcept { return a == b; }
    static constexpr bool lt(char_type a, char_type b) noexcept { return a < b; }
    static constexpr int compare(const char_type *a, const char_type *b, size_t n) noexcept
    {
        for(size_t i = 0; i < n; i++)
            if(a[i] != b[i])
                return a[i] < b[i] ? -1 : 1;
        return 0;
    }
    static constexpr size_t length(const char_type *s) noexcept
    {
        size_t n = 0;
        while(s[n])
            n++;
        return n;
    }
    static constexpr const char_type *find(const char_type *s, size_t n, const char_type &c) noexcept
    {
        for(size_t i = 0; i < n; i++)
            if(s[i] == c)
                return s + i;
        return nullptr;
    }
    static char_type *move(char_type *d, const char_type *s, size_t n) noexcept
    {
        return n ? (char_type *)memmove(d, s, n) : d;
    }
    static char_type *copy(char_type *d, const char_type *s, size_t n) noexcept
    {
        return n ? (char_type *)memcpy(d, s, n) : d;
    }
    static char_type *assign(char_type *d, size_t n, char_type c) noexcept
    {
        return n ? (char_type *)memset(d, c, n) : d;
    }
    static constexpr int_type not_eof(int_type c) noexcept { return c == eof() ? 0 : c; }
    static constexpr char_type to_char_type(int_type c) noexcept { return (char_type)c; }
    static constexpr int_type to_int_type(char_type c) noexcept { return c; }
    static constexpr bool eq_int_type(int_type a, int_type b) noexcept { return a == b; }
    static constexpr int_type eof() noexcept { return -1; }
};

using mac_basic_string_view = std::basic_string_view<unsigned char, mac_char_traits>;

struct mac_string_view : mac_basic_string_view
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

    mac_string_view(mac_basic_string_view v)
        : basic_string_view(std::move(v))
    {
    }
    mac_string_view(const std::basic_string<unsigned char, mac_char_traits>& v)
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

using mac_string = std::basic_string<unsigned char, mac_char_traits>;

std::u32string toUnicode(mac_string_view sv);
mac_string toMacRoman(const std::u32string& s);

fs::path toUnicodeFilename(mac_string_view sv);
mac_string toMacRomanFilename(const fs::path& s, int index = 0);

bool matchesMacRomanFilename(const fs::path& s, mac_string_view sv);

bool equalCaseInsensitive(mac_string_view s1, mac_string_view s2);
}
