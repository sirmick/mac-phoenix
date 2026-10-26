/* MacPhoenix: StdCLib's printf and scanf conversions (docs/executor/
 * PPCLibs.md), as the C standard (C89) describes them, for arguments from a
 * PowerPC call or va_list. int and long are 32 bits; long double is a double
 * (MPW's float.h for PowerPC); '\n' is CR. Numbers are converted by the
 * host's C library in the "C" locale. */

#include "clib.h"

#include <algorithm>
#include <climits>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace Executor;
using namespace Executor::clib;

namespace
{

/* The host's number conversions in the "C" locale, whatever the process's. */
struct CLocale
{
    locale_t old;
    CLocale()
    {
        static locale_t c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
        old = uselocale(c);
    }
    ~CLocale() { uselocale(old); }
};

struct Spec
{
    bool left = false, plus = false, space = false, alt = false, zero = false;
    int width = 0;
    int precision = -1;
    char length = 0;   /* 'h', 'l', 'L' or 0 */
    char conv = 0;
};

/* The host printf directive for one conversion. */
std::string hostFormat(const Spec &s, const char *conv)
{
    std::string f = "%";
    if(s.left) f += '-';
    if(s.plus) f += '+';
    if(s.space) f += ' ';
    if(s.alt) f += '#';
    if(s.zero) f += '0';
    if(s.width) f += std::to_string(s.width);
    if(s.precision >= 0)
        f += "." + std::to_string(s.precision);
    f += conv;
    return f;
}

template<typename T>
std::string hostPrintf(const std::string &format, T value)
{
    char small[128];
    int n = snprintf(small, sizeof small, format.c_str(), value);
    if(n < (int)sizeof small)
        return std::string(small, n > 0 ? n : 0);
    std::string big(n + 1, 0);
    snprintf(&big[0], big.size(), format.c_str(), value);
    big.resize(n);
    return big;
}

std::string pad(const std::string &s, const Spec &spec)
{
    if((int)s.size() >= spec.width)
        return s;
    std::string fill(spec.width - s.size(), ' ');
    return spec.left ? s + fill : fill + s;
}

/* A zero in hexadecimal with its prefix: the precision's digits, then the
   width, zero-filled after the prefix with the 0 flag. */
std::string zeroWithPrefix(const Spec &s, const char *prefix)
{
    std::string digits(std::max(s.precision, 1), '0');
    std::string t = prefix + digits;
    if(s.zero && !s.left && s.precision < 0 && (int)t.size() < s.width)
        return prefix + std::string(s.width - t.size(), '0') + digits;
    return pad(t, s);
}

}

std::string Executor::clib::formatPrintf(const char *format, VarArgs &args)
{
    CLocale c;
    std::string out;
    if(!format)
        return out;
    for(const char *f = format; *f; f++)
    {
        if(*f != '%')
        {
            out += *f;
            continue;
        }
        const char *start = f;
        Spec s;
        for(f++;; f++)
        {
            if(*f == '-') s.left = true;
            else if(*f == '+') s.plus = true;
            else if(*f == ' ') s.space = true;
            else if(*f == '#') s.alt = true;
            else if(*f == '0') s.zero = true;
            else break;
        }
        if(*f == '*')
        {
            s.width = (int32_t)args.word();
            if(s.width < 0)
            {
                s.left = true;
                s.width = -s.width;
            }
            f++;
        }
        else
            while(cIsDigit(*f))
                s.width = s.width * 10 + (*f++ - '0');
        if(*f == '.')
        {
            f++;
            s.precision = 0;
            if(*f == '*')
            {
                s.precision = (int32_t)args.word();
                f++;
            }
            else
                while(cIsDigit(*f))
                    s.precision = s.precision * 10 + (*f++ - '0');
        }
        if(*f == 'h' || *f == 'l' || *f == 'L')
            s.length = *f++;
        s.conv = *f;
        if(s.left)
            s.zero = false;
        switch(s.conv)
        {
            case 'd':
            case 'i':
            {
                int32_t v = (int32_t)args.word();
                if(s.length == 'h')
                    v = (int16_t)v;
                out += hostPrintf(hostFormat(s, "d"), (int)v);
                break;
            }
            case 'o':
            case 'u':
            case 'x':
            case 'X':
            {
                uint32_t v = args.word();
                if(s.length == 'h')
                    v = (uint16_t)v;
                char conv[2] = { s.conv, 0 };
                /* Apple's prefixes zero too: "%#x" of 0 is "0x0". */
                if(s.alt && !v && (s.conv == 'x' || s.conv == 'X'))
                {
                    out += zeroWithPrefix(s, s.conv == 'x' ? "0x" : "0X");
                    break;
                }
                out += hostPrintf(hostFormat(s, conv), (unsigned)v);
                break;
            }
            case 'c':
            {
                Spec p = s;
                p.precision = -1;
                out += pad(std::string(1, (char)args.word()), p);
                break;
            }
            case 's':
            {
                uint32_t a = args.word();
                std::string str;
                if(a)
                {
                    const char *p = host<char>(a);
                    size_t n = s.precision >= 0 ? strnlen(p, s.precision) : strlen(p);
                    str.assign(p, n);
                }
                /* A null pointer prints nothing, as Apple's. */
                out += pad(str, s);
                break;
            }
            case 'p':
            {
                Spec p = s;
                p.alt = true;
                uint32_t v = args.word();
                out += v ? hostPrintf(hostFormat(p, "x"), (unsigned)v) : zeroWithPrefix(p, "0x");
                break;
            }
            case 'n':
            {
                uint32_t a = args.word();
                if(a)
                {
                    if(s.length == 'h')
                        write16(a, (uint16_t)out.size());
                    else
                        write32(a, (uint32_t)out.size());
                }
                break;
            }
            case 'e':
            case 'E':
            case 'f':
            case 'g':
            case 'G':
            {
                double v = args.dbl();
                if(!std::isfinite(v))
                {
                    /* Apple's: INF, NAN(code) with SANE's NaN code, upper case
                       whatever the conversion; padded with blanks. */
                    std::string t = std::signbit(v) ? "-" : s.plus ? "+" : s.space ? " " : "";
                    if(std::isinf(v))
                        t += "INF";
                    else
                    {
                        uint64_t bits;
                        memcpy(&bits, &v, sizeof bits);
                        char code[16];
                        snprintf(code, sizeof code, "NAN(%03u)", unsigned(bits >> 37) & 0xFF);
                        t += code;
                    }
                    out += pad(t, s);
                    break;
                }
                char conv[2] = { s.conv, 0 };
                out += hostPrintf(hostFormat(s, conv), v);
                break;
            }
            case '%':
                out += '%';
                break;
            case 0:
                out.append(start, f);
                f--;
                break;
            default:
                out.append(start, f + 1);
                break;
        }
    }
    return out;
}

/* ---- scanf ---------------------------------------------------------------------- */

namespace
{

struct Input
{
    CharSource &src;
    int count = 0;     /* characters consumed, for %n */
    int get()
    {
        int c = src.get();
        if(c >= 0)
            count++;
        return c;
    }
    void unget(int c)
    {
        if(c >= 0)
        {
            src.unget(c);
            count--;
        }
    }
    /* Skips white space; the next character, or -1. */
    int skipSpace()
    {
        int c;
        while(cIsSpace(c = get()))
            ;
        unget(c);
        return c;
    }
};

bool isDigitIn(int c, int base)
{
    if(c >= '0' && c <= '9')
        return c - '0' < base;
    if(base == 16)
        return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    return false;
}

/* An integer's characters, up to `width`. */
std::string scanInteger(Input &in, int width, int &base)
{
    std::string s;
    int c = in.get();
    if((c == '+' || c == '-') && (int)s.size() < width)
    {
        s += (char)c;
        c = in.get();
    }
    if(c == '0' && (int)s.size() < width && (base == 0 || base == 16))
    {
        s += '0';
        c = in.get();
        if((c == 'x' || c == 'X') && (int)s.size() < width)
        {
            int next = in.get();
            if(isDigitIn(next, 16))
            {
                s += (char)c;
                base = 16;
                c = next;
            }
            else
            {
                /* "0x" with nothing after: the 0 is the number. */
                in.unget(next);
                in.unget(c);
                if(base == 0)
                    base = 8;
                return s;
            }
        }
        else if(base == 0)
            base = 8;
    }
    if(base == 0)
        base = 10;
    while(isDigitIn(c, base) && (int)s.size() < width)
    {
        s += (char)c;
        c = in.get();
    }
    in.unget(c);
    return s;
}

std::string scanFloat(Input &in, int width)
{
    std::string s;
    int c = in.get();
    auto take = [&]() {
        s += (char)c;
        c = in.get();
    };
    if((c == '+' || c == '-') && (int)s.size() < width)
        take();
    bool digits = false;
    while(cIsDigit(c) && (int)s.size() < width)
    {
        take();
        digits = true;
    }
    if(c == '.' && (int)s.size() < width)
    {
        take();
        while(cIsDigit(c) && (int)s.size() < width)
        {
            take();
            digits = true;
        }
    }
    if(digits && (c == 'e' || c == 'E') && (int)s.size() < width)
    {
        size_t mark = s.size();
        take();
        if((c == '+' || c == '-') && (int)s.size() < width)
            take();
        if(!cIsDigit(c))
        {
            /* Not an exponent after all: give back what we took. */
            in.unget(c);
            for(size_t i = s.size(); i > mark + 1; i--)
                in.unget((unsigned char)s[i - 1]);
            c = (unsigned char)s[mark];
            s.resize(mark);
        }
        while(cIsDigit(c) && (int)s.size() < width)
            take();
    }
    in.unget(c);
    return digits ? s : std::string();
}

}

int Executor::clib::formatScanf(CharSource &src, const char *format, VarArgs &args)
{
    CLocale cl;
    Input in{src};
    int assigned = 0;
    bool converted = false;
    if(!format)
        return 0;
    for(const char *f = format; *f; f++)
    {
        if(cIsSpace((unsigned char)*f))
        {
            in.skipSpace();
            continue;
        }
        if(*f != '%' || f[1] == '%')
        {
            if(*f == '%')
            {
                f++;
                in.skipSpace();
            }
            int c = in.get();
            if(c != (unsigned char)*f)
            {
                in.unget(c);
                return (c < 0 && !converted) ? -1 : assigned;
            }
            continue;
        }
        f++;
        bool suppress = false;
        if(*f == '*')
        {
            suppress = true;
            f++;
        }
        int width = 0;
        while(cIsDigit(*f))
            width = width * 10 + (*f++ - '0');
        char length = 0;
        if(*f == 'h' || *f == 'l' || *f == 'L')
            length = *f++;
        char conv = *f;
        if(!conv)
            break;
        if(conv != 'c' && conv != '[' && conv != 'n')
        {
            if(in.skipSpace() < 0)
                return converted ? assigned : -1;
        }
        int w = width ? width : INT_MAX;
        switch(conv)
        {
            case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': case 'p':
            {
                int base = conv == 'd' || conv == 'u' ? 10 : conv == 'i' ? 0 : conv == 'o' ? 8 : 16;
                std::string s = scanInteger(in, w, base);
                if(s.empty() || s == "+" || s == "-")
                    return (in.skipSpace() < 0 && !converted) ? -1 : assigned;
                converted = true;
                if(!suppress)
                {
                    long long v = strtoll(s.c_str(), nullptr, base);
                    uint32_t a = args.word();
                    if(length == 'h')
                        write16(a, (uint16_t)v);
                    else
                        write32(a, (uint32_t)v);
                    assigned++;
                }
                break;
            }
            case 'e': case 'E': case 'f': case 'g': case 'G':
            {
                std::string s = scanFloat(in, w);
                if(s.empty())
                    return (in.skipSpace() < 0 && !converted) ? -1 : assigned;
                converted = true;
                if(!suppress)
                {
                    double v = strtod(s.c_str(), nullptr);
                    uint32_t a = args.word();
                    if(length == 'l' || length == 'L')
                        writeDouble(a, v);
                    else
                        writeFloat(a, (float)v);
                    assigned++;
                }
                break;
            }
            case 's':
            {
                uint32_t a = suppress ? 0 : args.word();
                int n = 0, c;
                while(n < w && (c = in.get()) >= 0 && !cIsSpace(c))
                {
                    if(a)
                        host(a)[n] = (uint8_t)c;
                    n++;
                }
                if(n < w)
                    in.unget(c);
                if(!n)
                    return converted ? assigned : -1;
                converted = true;
                if(a)
                {
                    host(a)[n] = 0;
                    assigned++;
                }
                break;
            }
            case 'c':
            {
                uint32_t a = suppress ? 0 : args.word();
                int n = 0, c = 0;
                int count = width ? width : 1;
                while(n < count && (c = in.get()) >= 0)
                {
                    if(a)
                        host(a)[n] = (uint8_t)c;
                    n++;
                }
                if(n < count)
                    return (n == 0 && !converted) ? -1 : assigned;
                converted = true;
                if(a)
                    assigned++;
                break;
            }
            case '[':
            {
                bool set[256] = {};
                bool negate = false;
                f++;
                if(*f == '^')
                {
                    negate = true;
                    f++;
                }
                if(*f == ']')
                {
                    set[(unsigned char)']'] = true;
                    f++;
                }
                for(; *f && *f != ']'; f++)
                {
                    if(f[1] == '-' && f[2] && f[2] != ']' && (unsigned char)f[2] >= (unsigned char)f[0])
                    {
                        for(int ch = (unsigned char)f[0]; ch <= (unsigned char)f[2]; ch++)
                            set[ch] = true;
                        f += 2;
                    }
                    else
                        set[(unsigned char)*f] = true;
                }
                if(!*f)
                    f--;
                uint32_t a = suppress ? 0 : args.word();
                int n = 0, c = 0;
                while(n < w && (c = in.get()) >= 0 && set[c] != negate)
                {
                    if(a)
                        host(a)[n] = (uint8_t)c;
                    n++;
                }
                if(n < w)
                    in.unget(c);
                if(!n)
                    return (c < 0 && !converted) ? -1 : assigned;
                converted = true;
                if(a)
                {
                    host(a)[n] = 0;
                    assigned++;
                }
                break;
            }
            case 'n':
                if(!suppress)
                {
                    uint32_t a = args.word();
                    if(length == 'h')
                        write16(a, (uint16_t)in.count);
                    else
                        write32(a, (uint32_t)in.count);
                }
                break;
            default:
                return assigned;
        }
    }
    return assigned;
}
