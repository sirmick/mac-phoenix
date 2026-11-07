#pragma once

/* MacPhoenix: InterfaceLib's lowercase C glue (numtostring, drawstring,
   ptinrect, ...), `cglue:` in the definitions (multiversal/executor.rb):
   the Pascal routine called with C strings. An input string goes as a
   Pascal copy (a Mac's glue converts the caller's string and back; a copy
   reads the same and leaves a constant alone), an output string is made a
   C string in place after the call. */

#include <algorithm>
#include <cstring>

namespace Executor
{
namespace cglue
{
class CString
{
public:
    CString(const char *s)
        : null(s == nullptr)
    {
        size_t n = s ? std::min<size_t>(strlen(s), 255) : 0;
        buf[0] = (unsigned char)n;
        if(n)
            memcpy(buf + 1, s, n);
    }
    operator unsigned char *() { return null ? nullptr : buf; }

private:
    unsigned char buf[256];
    bool null;
};

/* A string the routine wrote as a Pascal string (or read as one: c2p
   before an in-out call). */
inline void p2c(char *s)
{
    if(!s)
        return;
    size_t n = (unsigned char)s[0];
    memmove(s, s + 1, n);
    s[n] = 0;
}

inline unsigned char *c2p(char *s)
{
    if(!s)
        return nullptr;
    size_t n = std::min<size_t>(strlen(s), 255);
    memmove(s + 1, s, n);
    s[0] = (char)n;
    return (unsigned char *)s;
}
}
}
