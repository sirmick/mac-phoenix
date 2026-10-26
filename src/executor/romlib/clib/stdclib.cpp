/* MacPhoenix: StdCLib for PowerPC fragments, everything but stdio and the
 * printf/scanf engines (docs/executor/PPCLibs.md). The routines and data an
 * MPW application imports, from the MPW C headers (stdlib.h, string.h,
 * ctype.h, setjmp.h, signal.h, time.h, locale.h, errno.h, float.h, IntEnv.h,
 * PLStringFuncs.h) and the C standard. */

#include "clib.h"

#include <base/traps.h>
#include <MemoryMgr.h>
#include <OSUtil.h>
#include <FileMgr.h>
#include <AliasMgr.h>
#include <SegmentLdr.h>
#include <ToolboxEvent.h>
#include <rsys/cfm.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern unsigned char XPRAM[8192];

using namespace Executor;
using namespace Executor::clib;

namespace
{

const char LIB[] = "StdCLib";

/* ---- data ------------------------------------------------------------------ */

/* Imports bind to these; static storage is guest-addressable. Big-endian,
   so written through write32. */
alignas(8) uint8_t errno_[4];
alignas(8) uint8_t macOSErr_[4];
alignas(8) uint8_t pCType_[4];          /* const char *__p_CType */
alignas(8) uint8_t ctypeData_[260];     /* what it points at */
alignas(8) uint8_t intEnv_[256];        /* _IntEnv: an MPW tool's environment */
alignas(8) uint8_t cPhase_[4];          /* __C_phase */
alignas(8) uint8_t targetForExit_[256]; /* __target_for_exit: a jmp_buf */
alignas(8) uint8_t exitStatus_[4];      /* _exit_status */
alignas(8) uint8_t nubAt3_[4];          /* __NubAt3: no debugger nub */
alignas(8) uint8_t standAlone_[4];      /* StandAlone */
alignas(8) uint8_t noMoreDebugStr_[4];
alignas(8) uint8_t zeroData_[8][256];   /* internal blocks nothing should read */
alignas(8) uint8_t dblEpsilon_[16], dblMax_[16], dblMin_[16];
alignas(8) uint8_t fltEpsilon_[4], fltMax_[4], fltMin_[4];
alignas(8) uint8_t ldblEpsilon_[16], ldblMax_[16], ldblMin_[16];

/* Returned by pointer: strerror, asctime, localtime, localeconv, strtok's. */
alignas(8) char strerrorBuf_[128];
alignas(8) char asctimeBuf_[32];
alignas(8) uint8_t tmBuf_[36];
alignas(8) uint8_t lconv_[48];
alignas(8) char lconvStrings_[8];        /* "." and "" */
alignas(8) char localeName_[16];         /* "#0#0#1#1#1" */
alignas(8) char tmpnamBuf_[16];
alignas(8) char ecvtBuf_[400];

uint32_t clientZone = 0;   /* the application zone of StdCLib's client */
uint32_t strtokNext = 0;
uint32_t randNext = 1;
uint32_t startTicks = 0;
std::vector<uint32_t> atexitProcs;

/* signal(): the six signals are bits (signal.h). */
const int SIGNALS[] = { 1, 2, 4, 8, 16, 32 };
uint32_t signalHandlers[6];
constexpr uint32_t SIG_DFL_ = 1, SIG_IGN_ = 0, SIG_ERR_ = 0xFFFFFFFF;

int signalIndex(int sig)
{
    for(int i = 0; i < 6; i++)
        if(SIGNALS[i] == sig)
            return i;
    return -1;
}

}

/* ---- the C type table ------------------------------------------------------ */

namespace
{
constexpr uint8_t UPP_ = 1, LOW_ = 2, DIG_ = 4, WSP_ = 8, PUN_ = 16, CTL_ = 32,
                  BLA_ = 64, HEX_ = 128;

constexpr uint8_t ctypeOf(int c)
{
    uint8_t t = 0;
    if(c >= 'A' && c <= 'Z') t |= UPP_;
    if(c >= 'a' && c <= 'z') t |= LOW_;
    if(c >= '0' && c <= '9') t |= DIG_ | HEX_;
    if((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) t |= HEX_;
    if(c == ' ' || (c >= 9 && c <= 13)) t |= WSP_;
    if(c == ' ') t |= BLA_;
    if(c < 32 || c == 127) t |= CTL_;
    if(c > 32 && c < 127 && !(t & (UPP_ | LOW_ | DIG_))) t |= PUN_;
    return t;
}

struct CTypeTable
{
    uint8_t t[256];
    constexpr CTypeTable() : t()
    {
        for(int c = 0; c < 128; c++)
            t[c] = ctypeOf(c);
    }
};
constexpr CTypeTable ctypeInit;
}

const uint8_t Executor::clib::ctypeTable[256] = {
#define C8(n) ctypeInit.t[n], ctypeInit.t[n+1], ctypeInit.t[n+2], ctypeInit.t[n+3], \
              ctypeInit.t[n+4], ctypeInit.t[n+5], ctypeInit.t[n+6], ctypeInit.t[n+7]
    C8(0), C8(8), C8(16), C8(24), C8(32), C8(40), C8(48), C8(56),
    C8(64), C8(72), C8(80), C8(88), C8(96), C8(104), C8(112), C8(120),
#undef C8
};

/* ---- errno ------------------------------------------------------------------ */

void Executor::clib::setErrno(int e)
{
    write32(guest(errno_), (uint32_t)e);
}

int Executor::clib::getErrno()
{
    return (int32_t)read32(guest(errno_));
}

void Executor::clib::setMacOSErr(int16_t err)
{
    write16(guest(macOSErr_), (uint16_t)err);
}

/* errno.h's numbers for the File Manager's errors. */
int Executor::clib::errnoFromOSErr(int16_t err)
{
    switch(err)
    {
        case noErr: return 0;
        case fnfErr: case dirNFErr: case nsvErr: case bdNamErr: return 2;   /* ENOENT */
        case tmfoErr: return 24;                                            /* EMFILE */
        case wPrErr: case vLckdErr: return 30;                              /* EROFS */
        case fLckdErr: case permErr: case wrPermErr: return 13; /* EACCES */
        case opWrErr: case fBsyErr: return 16;                              /* EBUSY */
        case dupFNErr: return 17;                                           /* EEXIST */
        case dskFulErr: case dirFulErr: return 28;                          /* ENOSPC */
        case rfNumErr: case fnOpnErr: return 9;                             /* EBADF */
        case memFullErr: return 12;                                         /* ENOMEM */
        case paramErr: case posErr: return 22;                              /* EINVAL */
        default: return 5;                                                  /* EIO */
    }
}

/* ---- exports ---------------------------------------------------------------- */

void Executor::clib::exportRaw(const char *library, const char *name,
                               std::function<uint32_t(PowerCore &)> code)
{
    builtinlibs::addPPCEntrypoint(library, name, [code](PowerCore &cpu) {
        /* Host code that runs 68k code (a patched trap) pushes below the
           caller's frame, as callfromPPC::Invoker has it. */
        EM_A7 = cpu.r[1];
        return code(cpu);
    });
}

void Executor::clib::exportData(const char *library, const char *name, void *address)
{
    builtinlibs::addPPCData(library, name, address);
}

/* ---- string.h ----------------------------------------------------------------- */

namespace
{
char *c_memcpy(char *d, const char *s, uint32_t n) { memmove(d, s, n); return d; }
char *c_memmove(char *d, const char *s, uint32_t n) { memmove(d, s, n); return d; }
char *c_memset(char *d, int32_t c, uint32_t n) { memset(d, (unsigned char)c, n); return d; }
int32_t c_memcmp(const unsigned char *a, const unsigned char *b, uint32_t n)
{
    for(uint32_t i = 0; i < n; i++)
        if(a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}
char *c_memchr(char *s, int32_t c, uint32_t n) { return (char *)memchr(s, (unsigned char)c, n); }
char *c_memccpy(char *d, const char *s, int32_t c, uint32_t n)
{
    for(uint32_t i = 0; i < n; i++)
    {
        d[i] = s[i];
        if((unsigned char)s[i] == (unsigned char)c)
            return d + i + 1;
    }
    return nullptr;
}
uint32_t c_strlen(const char *s) { return (uint32_t)strlen(s); }
char *c_strcpy(char *d, const char *s) { memmove(d, s, strlen(s) + 1); return d; }
char *c_strncpy(char *d, const char *s, uint32_t n)
{
    uint32_t i = 0;
    for(; i < n && s[i]; i++)
        d[i] = s[i];
    for(; i < n; i++)
        d[i] = 0;
    return d;
}
char *c_strcat(char *d, const char *s) { c_strcpy(d + strlen(d), s); return d; }
char *c_strncat(char *d, const char *s, uint32_t n)
{
    char *e = d + strlen(d);
    uint32_t i = 0;
    for(; i < n && s[i]; i++)
        e[i] = s[i];
    e[i] = 0;
    return d;
}
int32_t c_strcmp(const unsigned char *a, const unsigned char *b)
{
    for(;; a++, b++)
        if(*a != *b || !*a)
            return *a == *b ? 0 : *a < *b ? -1 : 1;
}
int32_t c_strncmp(const unsigned char *a, const unsigned char *b, uint32_t n)
{
    for(; n; n--, a++, b++)
        if(*a != *b || !*a)
            return *a == *b ? 0 : *a < *b ? -1 : 1;
    return 0;
}
char *c_strchr(char *s, int32_t c)
{
    for(;; s++)
    {
        if(*s == (char)c)
            return s;
        if(!*s)
            return nullptr;
    }
}
char *c_strrchr(char *s, int32_t c)
{
    char *found = nullptr;
    for(;; s++)
    {
        if(*s == (char)c)
            found = s;
        if(!*s)
            return found;
    }
}
uint32_t c_strspn(const char *s, const char *set) { return (uint32_t)strspn(s, set); }
uint32_t c_strcspn(const char *s, const char *set) { return (uint32_t)strcspn(s, set); }
char *c_strpbrk(char *s, const char *set) { return (char *)strpbrk(s, set); }
char *c_strstr(char *s, const char *t) { return (char *)strstr(s, t); }
char *c_strtok(char *s, const char *sep)
{
    char *p = s ? s : host<char>(strtokNext);
    if(!p)
        return nullptr;
    p += strspn(p, sep);
    if(!*p)
    {
        strtokNext = 0;
        return nullptr;
    }
    char *end = p + strcspn(p, sep);
    if(*end)
    {
        *end = 0;
        strtokNext = guest(end + 1);
    }
    else
        strtokNext = 0;
    return p;
}
int32_t c_strcoll(const unsigned char *a, const unsigned char *b) { return c_strcmp(a, b); }
uint32_t c_strxfrm(char *d, const char *s, uint32_t n)
{
    uint32_t len = (uint32_t)strlen(s);
    if(n)
        c_strncpy(d, s, std::min(n, len + 1));
    return len;
}

const char *errorText(int e)
{
    switch(e)
    {
        case 0: return "Error 0 - No error";
        case 1: return "Permission denied";
        case 2: return "No such file or directory";
        case 3: return "No such resource";
        case 4: return "Interrupted system service";
        case 5: return "I/O error";
        case 6: return "No such device or address";
        case 7: return "Argument list too long";
        case 8: return "Exec format error";
        case 9: return "Bad file number";
        case 10: return "No child processes";
        case 11: return "Resource temporarily unavailable, try again later";
        case 12: return "Not enough space";
        case 13: return "Permission denied";
        case 14: return "Bad address";
        case 15: return "Block device required";
        case 16: return "Device or resource busy";
        case 17: return "File exists";
        case 18: return "Cross-device link";
        case 19: return "No such device";
        case 20: return "Not a directory";
        case 21: return "Is a directory";
        case 22: return "Invalid argument";
        case 23: return "File table overflow";
        case 24: return "Too many open files";
        case 25: return "Not a character device";
        case 26: return "Text file busy";
        case 27: return "File too large";
        case 28: return "No space left on device";
        case 29: return "Illegal seek";
        case 30: return "Read-only file system";
        case 31: return "Too many links";
        case 32: return "Broken pipe";
        case 33: return "Math arg out of domain of func";
        case 34: return "Math result not representable";
        default: return nullptr;
    }
}

char *c_strerror(int32_t e)
{
    snprintf(strerrorBuf_, sizeof strerrorBuf_, "%s", errorMessage(e).c_str());
    return strerrorBuf_;
}
}

std::string Executor::clib::errorMessage(int e)
{
    if(const char *t = errorText(e))
        return t;
    return "Unknown error";
}

/* ---- ctype.h ------------------------------------------------------------------ */

namespace
{
int32_t ct(int32_t c, uint8_t mask) { return (c >= 0 && c < 256) ? (ctypeTable[c] & mask) : 0; }
int32_t c_isalnum(int32_t c) { return ct(c, UPP_ | LOW_ | DIG_); }
int32_t c_isalpha(int32_t c) { return ct(c, UPP_ | LOW_); }
int32_t c_iscntrl(int32_t c) { return ct(c, CTL_); }
int32_t c_isdigit(int32_t c) { return ct(c, DIG_); }
int32_t c_isgraph(int32_t c) { return ct(c, UPP_ | LOW_ | DIG_ | PUN_); }
int32_t c_islower(int32_t c) { return ct(c, LOW_); }
int32_t c_isprint(int32_t c) { return ct(c, UPP_ | LOW_ | DIG_ | PUN_ | BLA_); }
int32_t c_ispunct(int32_t c) { return ct(c, PUN_); }
int32_t c_isspace(int32_t c) { return ct(c, WSP_); }
int32_t c_isupper(int32_t c) { return ct(c, UPP_); }
int32_t c_isxdigit(int32_t c) { return ct(c, HEX_); }
int32_t c_isascii(int32_t c) { return (uint32_t)c <= 0x7F; }
int32_t c_toascii(int32_t c) { return c & 0x7F; }
int32_t c_tolower(int32_t c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int32_t c_toupper(int32_t c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
}

/* ---- stdlib.h: numbers --------------------------------------------------------- */

namespace
{
/* strtol and strtoul for 32-bit longs. */
uint32_t parseInteger(const char *s, char **end, int base, bool isSigned, bool &overflow,
                      bool &negative)
{
    const char *p = s;
    overflow = negative = false;
    while(cIsSpace((unsigned char)*p))
        p++;
    if(*p == '+' || *p == '-')
        negative = *p++ == '-';
    if((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')
       && isxdigit((unsigned char)p[2]))
    {
        p += 2;
        base = 16;
    }
    else if(base == 0)
        base = p[0] == '0' ? 8 : 10;
    if(base < 2 || base > 36)
    {
        if(end)
            *end = (char *)s;
        return 0;
    }
    uint64_t value = 0;
    const char *digits = p;
    for(;; p++)
    {
        int d;
        unsigned char c = *p;
        if(c >= '0' && c <= '9')
            d = c - '0';
        else if(c >= 'a' && c <= 'z')
            d = c - 'a' + 10;
        else if(c >= 'A' && c <= 'Z')
            d = c - 'A' + 10;
        else
            break;
        if(d >= base)
            break;
        value = value * base + d;
        if(value > 0xFFFFFFFFull)
        {
            overflow = true;
            value = 0x100000000ull;
        }
    }
    if(p == digits)
    {
        if(end)
            *end = (char *)s;
        negative = false;
        return 0;
    }
    if(end)
        *end = (char *)p;
    if(isSigned && !overflow)
    {
        if(negative ? value > 0x80000000ull : value > 0x7FFFFFFFull)
            overflow = true;
    }
    return (uint32_t)value;
}

/* A guest char ** to store the end in, through a host char *. */
struct EndPtr
{
    uint32_t where;
    char *end = nullptr;
    void store() { if(where) write32(where, guest(end)); }
};

int32_t c_strtol(const char *s, uint32_t endp, int32_t base)
{
    EndPtr e{endp};
    bool overflow, negative;
    uint32_t v = parseInteger(s, &e.end, base, true, overflow, negative);
    e.store();
    if(overflow)
    {
        setErrno(34);
        return negative ? INT32_MIN : INT32_MAX;
    }
    return negative ? -(int32_t)v : (int32_t)v;
}

uint32_t c_strtoul(const char *s, uint32_t endp, int32_t base)
{
    EndPtr e{endp};
    bool overflow, negative;
    uint32_t v = parseInteger(s, &e.end, base, false, overflow, negative);
    e.store();
    if(overflow)
    {
        setErrno(34);
        return 0xFFFFFFFF;
    }
    return negative ? (uint32_t)-(int64_t)v : v;
}

int32_t c_atoi(const char *s) { return c_strtol(s, 0, 10); }
int32_t c_atol(const char *s) { return c_strtol(s, 0, 10); }

double cStrtod(const char *s, char **end)
{
    static locale_t c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    locale_t old = uselocale(c);
    const char *p = s;
    while(cIsSpace((unsigned char)*p))
        p++;
    /* Decimal only: hexadecimal floating constants came after C89. */
    const char *q = p + (*p == '+' || *p == '-');
    double d;
    if(q[0] == '0' && (q[1] == 'x' || q[1] == 'X'))
    {
        d = (*p == '-') ? -0.0 : 0.0;
        *end = (char *)q + 1;
    }
    else
    {
        errno = 0;
        d = strtod(p, end);
        if(*end == p)
            *end = (char *)s;
        if(errno == ERANGE)
            setErrno(34);
    }
    uselocale(old);
    return d;
}

double c_strtod(const char *s, uint32_t endp)
{
    EndPtr e{endp};
    double d = cStrtod(s, &e.end);
    e.store();
    return d;
}

double c_atof(const char *s)
{
    char *end;
    return cStrtod(s, &end);
}

int32_t c_abs(int32_t x) { return x < 0 ? -x : x; }
int32_t c_labs(int32_t x) { return x < 0 ? -x : x; }

int32_t c_rand()
{
    randNext = randNext * 1103515245u + 12345u;
    return (int32_t)((randNext >> 16) & 0x7FFF);
}
void c_srand(uint32_t seed) { randNext = seed; }

/* div and ldiv return a structure: the caller passes where in r3. */
uint32_t raw_div(PowerCore &cpu)
{
    int32_t n = (int32_t)cpu.r[4], d = (int32_t)cpu.r[5];
    uint32_t result = cpu.r[3];
    int32_t q = d ? n / d : 0, r = d ? n % d : 0;
    write32(result, (uint32_t)q);
    write32(result + 4, (uint32_t)r);
    return cpu.lr;
}

/* ecvt/fcvt (System V): the digits, the decimal point's place, the sign. */
char *cvt(double x, int32_t ndigit, uint32_t decpt, uint32_t sign, bool fixed)
{
    write32(sign, std::signbit(x) ? 1 : 0);
    x = std::fabs(x);
    if(ndigit < 0)
        ndigit = 0;
    if(ndigit > 350)
        ndigit = 350;
    char buf[400];
    int point;
    if(x == 0)
    {
        int n = fixed ? ndigit : std::max(ndigit, 1);
        memset(ecvtBuf_, '0', n);
        ecvtBuf_[n] = 0;
        write32(decpt, fixed ? 0 : 1);
        return ecvtBuf_;
    }
    if(fixed)
    {
        snprintf(buf, sizeof buf, "%.*f", (int)ndigit, x);
        char *dot = strchr(buf, '.');
        point = dot ? int(dot - buf) : (int)strlen(buf);
        std::string digits(buf, point);
        if(dot)
            digits += dot + 1;
        size_t lead = digits.find_first_not_of('0');
        if(lead == std::string::npos)
        {
            point = 0;
            digits.assign(ndigit, '0');
        }
        else
        {
            point -= (int)lead;
            digits.erase(0, lead);
        }
        snprintf(ecvtBuf_, sizeof ecvtBuf_, "%s", digits.c_str());
    }
    else
    {
        int n = std::max(ndigit, 1);
        snprintf(buf, sizeof buf, "%.*e", n - 1, x);
        char *e = strchr(buf, 'e');
        point = atoi(e + 1) + 1;
        std::string digits(1, buf[0]);
        if(buf[1] == '.')
            digits.append(buf + 2, e);
        snprintf(ecvtBuf_, sizeof ecvtBuf_, "%s", digits.c_str());
    }
    write32(decpt, (uint32_t)point);
    return ecvtBuf_;
}
char *c_ecvt(double x, int32_t n, uint32_t decpt, uint32_t sign) { return cvt(x, n, decpt, sign, false); }
char *c_fcvt(double x, int32_t n, uint32_t decpt, uint32_t sign) { return cvt(x, n, decpt, sign, true); }

/* Multibyte characters: one byte each. wchar_t is a short. */
int32_t c_mblen(const char *s, uint32_t n) { return !s ? 0 : !n ? -1 : *s ? 1 : 0; }
int32_t c_mbtowc(uint32_t pwc, const char *s, uint32_t n)
{
    if(!s)
        return 0;
    if(!n)
        return -1;
    if(pwc)
        write16(pwc, (unsigned char)*s);
    return *s ? 1 : 0;
}
int32_t c_wctomb(char *s, int32_t wc)
{
    if(!s)
        return 0;
    *s = (char)wc;
    return 1;
}
uint32_t c_mbstowcs(uint32_t pwcs, const char *s, uint32_t n)
{
    uint32_t i = 0;
    for(; i < n; i++)
    {
        if(pwcs)
            write16(pwcs + 2 * i, (unsigned char)s[i]);
        if(!s[i])
            return i;
    }
    return i;
}
uint32_t c_wcstombs(char *s, uint32_t pwcs, uint32_t n)
{
    uint32_t i = 0;
    for(; i < n; i++)
    {
        uint16_t w = read16(pwcs + 2 * i);
        if(s)
            s[i] = (char)w;
        if(!w)
            return i;
    }
    return i;
}
}

/* ---- stdlib.h: memory --------------------------------------------------------- */

namespace
{
/* The Memory Manager's application heap, as a Mac C library has it. */
char *c_malloc(uint32_t size)
{
    if(size > 0x7FFFFFF0u)
    {
        setErrno(12);
        return nullptr;
    }
    Ptr p = NewPtr(size);
    if(!p)
        setErrno(12);
    return (char *)p;
}

char *c_calloc(uint32_t n, uint32_t size)
{
    uint64_t total = (uint64_t)n * size;
    if(total > 0x7FFFFFF0u)
    {
        setErrno(12);
        return nullptr;
    }
    Ptr p = NewPtrClear((Size)total);
    if(!p)
        setErrno(12);
    return (char *)p;
}

void c_free(char *p)
{
    if(p)
        DisposePtr((Ptr)p);
}

char *c_realloc(char *p, uint32_t size)
{
    if(!p)
        return c_malloc(size);
    if(!size)
    {
        DisposePtr((Ptr)p);
        return nullptr;
    }
    Size old = GetPtrSize((Ptr)p);
    SetPtrSize((Ptr)p, size);
    if(C_MemError() == noErr)
        return p;
    char *q = c_malloc(size);
    if(!q)
        return nullptr;
    memcpy(q, p, std::min<uint32_t>((uint32_t)old, size));
    DisposePtr((Ptr)p);
    return q;
}
}

/* ---- stdlib.h: sorting --------------------------------------------------------- */

namespace
{
void c_qsort(char *base, uint32_t n, uint32_t size, uint32_t compar)
{
    if(n < 2 || !size)
        return;
    uint32_t b = guest(base);
    std::vector<uint32_t> order(n);
    for(uint32_t i = 0; i < n; i++)
        order[i] = i;
    /* A merge sort: the elements stay where they are while compared, and an
       inconsistent comparison routine can't send it out of bounds. */
    std::stable_sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) {
        return (int32_t)callPPC(compar, { b + x * size, b + y * size }) < 0;
    });
    std::vector<uint8_t> sorted((size_t)n * size);
    for(uint32_t i = 0; i < n; i++)
        memcpy(&sorted[(size_t)i * size], base + (size_t)order[i] * size, size);
    memcpy(base, sorted.data(), sorted.size());
}

char *c_bsearch(const char *key, char *base, uint32_t n, uint32_t size, uint32_t compar)
{
    uint32_t k = guest(key), b = guest(base);
    uint32_t lo = 0, hi = n;
    while(lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;
        int32_t c = (int32_t)callPPC(compar, { k, b + mid * size });
        if(c == 0)
            return base + (size_t)mid * size;
        if(c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return nullptr;
}
}

/* ---- the end of a program -------------------------------------------------------- */

namespace
{
[[noreturn]] void endProcess()
{
    C_ExitToShell();
    std::abort();
}

void runExitProcs()
{
    while(!atexitProcs.empty())
    {
        uint32_t tv = atexitProcs.back();
        atexitProcs.pop_back();
        callPPC(tv, {});
    }
}

int32_t c_atexit(uint32_t func)
{
    if(atexitProcs.size() >= 64)
        return -1;
    atexitProcs.push_back(func);
    return 0;
}

void c_exit(int32_t status)
{
    write32(guest(exitStatus_), (uint32_t)status);
    runExitProcs();
    stdioCloseAll();
    endProcess();
}

void c__exit(int32_t status)
{
    write32(guest(exitStatus_), (uint32_t)status);
    stdioFlushAll();
    endProcess();
}

int32_t c_raise(int32_t sig)
{
    int i = signalIndex(sig);
    if(i < 0)
        return -1;
    uint32_t h = signalHandlers[i];
    if(h == SIG_IGN_)
        return 0;
    if(h == SIG_DFL_ || h == 3 || h == 5)
    {
        /* The default for every signal: the program ends. */
        write32(guest(exitStatus_), 3);
        endProcess();
    }
    signalHandlers[i] = SIG_DFL_;
    callPPC(h, { (uint32_t)sig });
    return 0;
}

/* signal(): every signal whose bit is in `sig` gets the handler (Apple's
   takes a mask); the answer is the lowest one's old handler, SIG_DFL when
   there is none (as Apple's). */
uint32_t c_signal(int32_t sig, uint32_t func)
{
    uint32_t old = SIG_ERR_;
    for(int i = 0; i < 6; i++)
        if(sig & SIGNALS[i])
        {
            if(old == SIG_ERR_)
                old = signalHandlers[i];
            signalHandlers[i] = func;
        }
    return old == SIG_ERR_ ? SIG_DFL_ : old;
}

void c_abort()
{
    c_raise(1);
    endProcess();
}

char *c_getenv(const char *) { return nullptr; }

int32_t c_system(const char *s) { return s ? -1 : 0; }

void c_breakpoint()
{
    fprintf(stderr, "StdCLib: _BreakPoint\n");
}

/* assert(): the message on the error stream, then abort(). */
void c_assertprint(const char *file, int32_t line, const char *expr);
}

/* ---- setjmp.h --------------------------------------------------------------- */

/* A jmp_buf is 64 words: LR, SP, TOC, CR, r13-r31, then f14-f31. */
namespace
{
uint32_t raw_setjmp(PowerCore &cpu)
{
    uint32_t env = cpu.r[3];
    write32(env, cpu.lr);
    write32(env + 4, cpu.r[1]);
    write32(env + 8, cpu.r[2]);
    write32(env + 12, cpu.cr);
    for(int i = 13; i < 32; i++)
        write32(env + 16 + 4 * (i - 13), cpu.r[i]);
    for(int i = 14; i < 32; i++)
        writeDouble(env + 96 + 8 * (i - 14), cpu.f[i]);
    cpu.r[3] = 0;
    return cpu.lr;
}

uint32_t raw_longjmp(PowerCore &cpu)
{
    uint32_t env = cpu.r[3];
    int32_t val = (int32_t)cpu.r[4];
    uint32_t pc = read32(env);
    cpu.lr = pc;
    cpu.r[1] = read32(env + 4);
    cpu.r[2] = read32(env + 8);
    cpu.cr = read32(env + 12);
    for(int i = 13; i < 32; i++)
        cpu.r[i] = read32(env + 16 + 4 * (i - 13));
    for(int i = 14; i < 32; i++)
        cpu.f[i] = readDouble(env + 96 + 8 * (i - 14));
    cpu.r[3] = val ? (uint32_t)val : 1;
    return pc;
}
}

/* ---- time.h -------------------------------------------------------------------- */

/* time_t: seconds since 1 January 1904, local time, as the Mac's clock. */
namespace
{
const char *const DAY_NAMES[] = { "Sunday", "Monday", "Tuesday", "Wednesday",
                                  "Thursday", "Friday", "Saturday" };
const char *const MONTH_NAMES[] = { "January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November",
                                    "December" };

struct TM
{
    int32_t sec, min, hour, mday, mon, year, wday, yday, isdst;
};

TM readTM(uint32_t p)
{
    TM t;
    int32_t *f = &t.sec;
    for(int i = 0; i < 9; i++)
        f[i] = (int32_t)read32(p + 4 * i);
    return t;
}

void writeTM(uint32_t p, const TM &t)
{
    const int32_t *f = &t.sec;
    for(int i = 0; i < 9; i++)
        write32(p + 4 * i, (uint32_t)f[i]);
}

/* Days from 1 January 1970 of a civil date (H. Hinnant's algorithm). */
int64_t daysFromCivil(int64_t y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t z, int64_t &y, int &m, int &d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    d = int(doy - (153 * mp + 2) / 5 + 1);
    m = int(mp < 10 ? mp + 3 : mp - 9);
    y += m <= 2;
}

const int64_t DAYS_1904 = daysFromCivil(1904, 1, 1);

TM tmFromMacSeconds(int64_t secs)
{
    int64_t days = secs / 86400;
    int64_t rem = secs % 86400;
    if(rem < 0)
    {
        rem += 86400;
        days--;
    }
    TM t;
    int64_t y;
    int m, d;
    civilFromDays(days + DAYS_1904, y, m, d);
    t.year = int32_t(y - 1900);
    t.mon = m - 1;
    t.mday = d;
    t.hour = int32_t(rem / 3600);
    t.min = int32_t(rem / 60 % 60);
    t.sec = int32_t(rem % 60);
    t.wday = int32_t(((days + DAYS_1904) % 7 + 11) % 7);   /* 1970-01-01 was a Thursday */
    t.yday = int32_t(days + DAYS_1904 - daysFromCivil(y, 1, 1));
    t.isdst = 0;
    return t;
}

int32_t gmtDelta()
{
    uint32_t v = (uint32_t(XPRAM[0xEC]) << 24) | (XPRAM[0xED] << 16) | (XPRAM[0xEE] << 8) | XPRAM[0xEF];
    int32_t delta = int32_t(v << 8) >> 8;   /* the low 24 bits, signed */
    return delta;
}

uint32_t c_time(uint32_t timer)
{
    GUEST<ULONGINT> now;
    GetDateTime(&now);
    uint32_t t = now;
    if(timer)
        write32(timer, t);
    return t;
}

uint32_t c_clock()
{
    return C_TickCount() - startTicks;
}

double c_difftime(uint32_t t1, uint32_t t0)
{
    return (double)t1 - (double)t0;
}

char *c_localtime(uint32_t timer)
{
    if(!timer)
        return nullptr;
    writeTM(guest(tmBuf_), tmFromMacSeconds(read32(timer)));
    return (char *)tmBuf_;
}

char *c_gmtime(uint32_t timer)
{
    if(!timer)
        return nullptr;
    writeTM(guest(tmBuf_), tmFromMacSeconds((int64_t)read32(timer) - gmtDelta()));
    return (char *)tmBuf_;
}

uint32_t c_mktime(uint32_t p)
{
    TM t = readTM(p);
    /* Normalize month into the year, then count everything in seconds. */
    int64_t year = (int64_t)t.year + 1900 + t.mon / 12;
    int64_t mon = t.mon % 12;
    if(mon < 0)
    {
        mon += 12;
        year--;
    }
    int64_t days = daysFromCivil(year, int(mon + 1), 1) - DAYS_1904 + (t.mday - 1);
    int64_t secs = days * 86400 + (int64_t)t.hour * 3600 + (int64_t)t.min * 60 + t.sec;
    if(secs < 0 || secs > 0xFFFFFFFFll)
        return 0xFFFFFFFF;
    TM n = tmFromMacSeconds(secs);
    n.isdst = 0;   /* no daylight time: the Mac clock is local */
    writeTM(p, n);
    return (uint32_t)secs;
}

std::string asctimeString(const TM &t)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%.3s %.3s%3d %.2d:%.2d:%.2d %d\r",
             DAY_NAMES[((t.wday % 7) + 7) % 7], MONTH_NAMES[((t.mon % 12) + 12) % 12],
             (int)t.mday, (int)t.hour, (int)t.min, (int)t.sec, (int)t.year + 1900);
    return buf;
}

char *c_asctime(uint32_t p)
{
    snprintf(asctimeBuf_, sizeof asctimeBuf_, "%s", asctimeString(readTM(p)).c_str());
    return asctimeBuf_;
}

char *c_ctime(uint32_t timer)
{
    char *tm = c_localtime(timer);
    return tm ? c_asctime(guest(tm)) : nullptr;
}

void time12(std::string &out, const TM &t)
{
    char b[32];
    int h = t.hour % 12 ? t.hour % 12 : 12;
    snprintf(b, sizeof b, "%02d:%02d:%02d %s", h, (int)t.min, (int)t.sec, t.hour < 12 ? "AM" : "PM");
    out += b;
}

void longDate(std::string &out, const TM &t, int wday, int mon)
{
    char b[64];
    snprintf(b, sizeof b, "%s, %s %d, %d", DAY_NAMES[wday], MONTH_NAMES[mon], (int)t.mday,
             (int)t.year + 1900);
    out += b;
}

uint32_t c_strftime(char *s, uint32_t max, const char *format, uint32_t p)
{
    TM t = readTM(p);
    std::string out;
    auto num = [&](int v, int width) {
        char b[16];
        snprintf(b, sizeof b, "%0*d", width, v);
        out += b;
    };
    int wday = ((t.wday % 7) + 7) % 7, mon = ((t.mon % 12) + 12) % 12;
    for(const char *f = format; *f; f++)
    {
        if(*f != '%' || !f[1])
        {
            out += *f;
            continue;
        }
        switch(*++f)
        {
            case 'a': out.append(DAY_NAMES[wday], 3); break;
            case 'A': out += DAY_NAMES[wday]; break;
            case 'b': out.append(MONTH_NAMES[mon], 3); break;
            case 'B': out += MONTH_NAMES[mon]; break;
            /* %c, %x, %X and %p as Apple's: the Mac's long date and 12-hour
               time, " AM"/" PM" (the U.S. international resources). */
            case 'c':
                time12(out, t);
                out += ' ';
                longDate(out, t, wday, mon);
                break;
            case 'd': num(t.mday, 2); break;
            case 'H': num(t.hour, 2); break;
            case 'I': num(t.hour % 12 ? t.hour % 12 : 12, 2); break;
            case 'j': num(t.yday + 1, 3); break;
            case 'm': num(t.mon + 1, 2); break;
            case 'M': num(t.min, 2); break;
            case 'p': out += t.hour < 12 ? " AM" : " PM"; break;
            case 'S': num(t.sec, 2); break;
            case 'U': num((t.yday + 7 - wday) / 7, 2); break;
            case 'w': num(wday, 1); break;
            case 'W': num((t.yday + 7 - (wday + 6) % 7) / 7, 2); break;
            case 'x': longDate(out, t, wday, mon); break;
            case 'X': time12(out, t); break;
            case 'y': num(((t.year % 100) + 100) % 100, 2); break;
            case 'Y': num(t.year + 1900, 1); break;
            case 'Z': break;
            case '%': out += '%'; break;
            default: out += '%'; out += *f; break;
        }
    }
    if(out.size() + 1 > max)
        return 0;
    memcpy(s, out.c_str(), out.size() + 1);
    return (uint32_t)out.size();
}
}

/* ---- locale.h ------------------------------------------------------------------ */

namespace
{
/* Apple's answers name every category's locale at once, "#0#0#1#1#1":
   collate, ctype, monetary, numeric, time; the "C" locale is 0 0 1 1 1, the
   native one ("") 0 0 2 2 2. Only the name changes here: both format
   numbers alike. */
int localeOf[5] = { 0, 0, 1, 1, 1 };

char *c_setlocale(int32_t category, const char *locale)
{
    if(category < 1 || category > 6)
        return nullptr;
    if(locale)
    {
        bool native = !*locale;
        if(!native && strcmp(locale, "C"))
            return nullptr;
        for(int c = 0; c < 5; c++)
            if(category == 1 || category == c + 2)
                localeOf[c] = c < 2 ? 0 : native ? 2 : 1;
    }
    snprintf(localeName_, sizeof localeName_, "#%d#%d#%d#%d#%d", localeOf[0], localeOf[1],
             localeOf[2], localeOf[3], localeOf[4]);
    return localeName_;
}

char *c_localeconv()
{
    return (char *)lconv_;
}

void initLocale()
{
    for(int c = 0; c < 5; c++)
        localeOf[c] = c < 2 ? 0 : 1;
    lconvStrings_[0] = '.';
    lconvStrings_[1] = 0;
    lconvStrings_[2] = 0;
    uint32_t point = guest(lconvStrings_), empty = guest(lconvStrings_ + 2);
    uint32_t l = guest(lconv_);
    write32(l, point);
    for(int i = 1; i < 10; i++)
        write32(l + 4 * i, empty);
    memset(lconv_ + 40, 127, 8);   /* CHAR_MAX */
}
}

/* ---- PLStringFuncs.h ---------------------------------------------------------- */

namespace
{
int16_t c_PLstrcmp(const unsigned char *a, const unsigned char *b)
{
    int n = std::min(a[0], b[0]);
    for(int i = 1; i <= n; i++)
        if(a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return a[0] == b[0] ? 0 : a[0] < b[0] ? -1 : 1;
}
int16_t c_PLstrncmp(const unsigned char *a, const unsigned char *b, int16_t num)
{
    int la = std::min<int>(a[0], num), lb = std::min<int>(b[0], num);
    int n = std::min(la, lb);
    for(int i = 1; i <= n; i++)
        if(a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return la == lb ? 0 : la < lb ? -1 : 1;
}
unsigned char *c_PLstrcpy(unsigned char *d, const unsigned char *s)
{
    memmove(d, s, s[0] + 1);
    return d;
}
unsigned char *c_PLstrncpy(unsigned char *d, const unsigned char *s, int16_t num)
{
    int n = std::min<int>(s[0], std::max<int>(num, 0));
    memmove(d + 1, s + 1, n);
    d[0] = (unsigned char)n;
    return d;
}
unsigned char *c_PLstrcat(unsigned char *d, const unsigned char *s)
{
    int n = std::min(255 - d[0], (int)s[0]);
    memmove(d + 1 + d[0], s + 1, n);
    d[0] += n;
    return d;
}
unsigned char *c_PLstrncat(unsigned char *d, const unsigned char *s, int16_t num)
{
    int n = std::min({ 255 - d[0], (int)s[0], std::max<int>(num, 0) });
    memmove(d + 1 + d[0], s + 1, n);
    d[0] += n;
    return d;
}
unsigned char *c_PLstrchr(unsigned char *s, int16_t ch)
{
    for(int i = 1; i <= s[0]; i++)
        if(s[i] == (unsigned char)ch)
            return s + i;
    return nullptr;
}
unsigned char *c_PLstrrchr(unsigned char *s, int16_t ch)
{
    for(int i = s[0]; i >= 1; i--)
        if(s[i] == (unsigned char)ch)
            return s + i;
    return nullptr;
}
unsigned char *c_PLstrpbrk(unsigned char *s, const unsigned char *set)
{
    for(int i = 1; i <= s[0]; i++)
        for(int j = 1; j <= set[0]; j++)
            if(s[i] == set[j])
                return s + i;
    return nullptr;
}
int16_t c_PLstrspn(const unsigned char *s, const unsigned char *set)
{
    int i = 1;
    for(; i <= s[0]; i++)
    {
        bool in = false;
        for(int j = 1; j <= set[0] && !in; j++)
            in = s[i] == set[j];
        if(!in)
            break;
    }
    return int16_t(i - 1);
}
unsigned char *c_PLstrstr(unsigned char *s, const unsigned char *t)
{
    if(!t[0])
        return s + 1;
    for(int i = 1; i + t[0] - 1 <= s[0]; i++)
        if(!memcmp(s + i, t + 1, t[0]))
            return s + i;
    return nullptr;
}
int16_t c_PLstrlen(const unsigned char *s) { return s[0]; }
/* PLpos(sub, str): where sub is in str, as Pascal's Pos (Apple's order). */
int16_t c_PLpos(const unsigned char *sub, const unsigned char *str)
{
    unsigned char *p = c_PLstrstr(const_cast<unsigned char *>(str), sub);
    return p ? int16_t(p - str) : 0;
}
}

/* ---- IntEnv.h: Mac paths ------------------------------------------------------ */

namespace
{
OSErr c_MakeResolvedFSSpec(int16_t vol, int32_t dir, unsigned char *path, FSSpec *spec,
                           Boolean *isFolder, Boolean *hadAlias, Boolean *leafIsAlias)
{
    OSErr err = C_FSMakeFSSpec(vol, dir, path, spec);
    if(err != noErr && err != fnfErr)
        return err;
    if(hadAlias)
        *hadAlias = false;
    if(leafIsAlias)
        *leafIsAlias = false;
    if(isFolder)
        *isFolder = false;
    if(err == noErr)
    {
        Boolean folder = false, aliased = false;
        OSErr e2 = C_ResolveAliasFile(spec, true, &folder, &aliased);
        if(e2 == noErr)
        {
            if(isFolder)
                *isFolder = folder;
            if(hadAlias)
                *hadAlias = aliased;
            if(leafIsAlias)
                *leafIsAlias = aliased;
        }
    }
    return err;
}

OSErr c_ResolveFolderAliases(int16_t vol, int32_t dir, unsigned char *path, Boolean resolveLeaf,
                             FSSpec *spec, Boolean *isFolder, Boolean *hadAlias,
                             Boolean *leafIsAlias)
{
    OSErr err = C_FSMakeFSSpec(vol, dir, path, spec);
    if(isFolder)
        *isFolder = false;
    if(hadAlias)
        *hadAlias = false;
    if(leafIsAlias)
        *leafIsAlias = false;
    if(err == noErr && resolveLeaf)
    {
        Boolean folder = false, aliased = false;
        if(C_ResolveAliasFile(spec, true, &folder, &aliased) == noErr)
        {
            if(isFolder)
                *isFolder = folder;
            if(hadAlias)
                *hadAlias = aliased;
            if(leafIsAlias)
                *leafIsAlias = aliased;
        }
    }
    return err;
}

/* ResolvePath: a C path, resolved, copied to resolvedPath (as given: no
   aliases are rewritten into it). */
OSErr c_ResolvePath(const char *raw, char *resolved, Boolean *isFolder, Boolean *hadAlias)
{
    size_t n = strlen(raw);
    if(n > 255)
        return bdNamErr;
    Str255 p;
    p[0] = (unsigned char)n;
    memcpy(p + 1, raw, n);
    GUEST<FSSpec> *spec = (GUEST<FSSpec> *)NewPtrClear(sizeof(FSSpec));
    OSErr err = c_MakeResolvedFSSpec(0, 0, p, (FSSpec *)spec, isFolder, hadAlias, nullptr);
    DisposePtr((Ptr)spec);
    if(resolved)
        strcpy(resolved, raw);
    return err;
}

OSErr c_MakeResolvedPath(int16_t vol, int32_t dir, unsigned char *path, Boolean resolveLeaf,
                         char *buffer, Boolean *isFolder, Boolean *hadAlias,
                         Boolean *leafIsAlias)
{
    FSSpec *spec = (FSSpec *)NewPtrClear(sizeof(FSSpec));
    OSErr err = c_ResolveFolderAliases(vol, dir, path, resolveLeaf, spec, isFolder,
                                       hadAlias, leafIsAlias);
    if(buffer)
    {
        memcpy(buffer, path + 1, path[0]);
        buffer[path[0]] = 0;
    }
    DisposePtr((Ptr)spec);
    return err;
}

Boolean c_TrapAvailable(int16_t trapNum)
{
    uint16_t t = (uint16_t)trapNum;
    TrapType type = (t & 0x0800) ? kToolboxTrapType : kOSTrapType;
    return C_NGetTrapAddress(t, type) != C_NGetTrapAddress(0xA89F, kToolboxTrapType);
}

int32_t c_getpid() { return 1; }
}

/* ---- start and end ------------------------------------------------------------- */

void Executor::clib::stdclibStart()
{
    clientZone = guest_cast<uint32_t>(LM(ApplZone));
    memset(errno_, 0, sizeof errno_);
    memset(macOSErr_, 0, sizeof macOSErr_);
    memset(intEnv_, 0, sizeof intEnv_);
    memset(cPhase_, 0, sizeof cPhase_);
    memset(targetForExit_, 0, sizeof targetForExit_);
    memset(exitStatus_, 0, sizeof exitStatus_);
    memset(nubAt3_, 0, sizeof nubAt3_);
    memset(noMoreDebugStr_, 0, sizeof noMoreDebugStr_);
    memset(zeroData_, 0, sizeof zeroData_);
    write32(guest(standAlone_), 1);

    memcpy(ctypeData_, ctypeTable, 256);
    write32(guest(pCType_), guest(ctypeData_));

    auto setDouble = [](uint8_t *p, double d) {
        writeDouble(guest(p), d);
        writeDouble(guest(p + 8), 0.0);
    };
    setDouble(dblEpsilon_, DBL_EPSILON);
    setDouble(dblMax_, DBL_MAX);
    setDouble(dblMin_, DBL_MIN);
    setDouble(ldblEpsilon_, DBL_EPSILON);
    setDouble(ldblMax_, DBL_MAX);
    setDouble(ldblMin_, DBL_MIN);
    writeFloat(guest(fltEpsilon_), FLT_EPSILON);
    writeFloat(guest(fltMax_), FLT_MAX);
    writeFloat(guest(fltMin_), FLT_MIN);

    initLocale();
    strtokNext = 0;
    randNext = 1;
    startTicks = C_TickCount();
    atexitProcs.clear();
    for(auto &h : signalHandlers)
        h = SIG_DFL_;
    stdioStart();
}

void Executor::clib::stdclibProcessExit()
{
    /* Only for the application that has it. */
    if(!clientZone || clientZone != guest_cast<uint32_t>(LM(ApplZone)))
        return;
    clientZone = 0;
    stdioCloseAll();
}

/* ---- the export table ---------------------------------------------------------- */

namespace
{
void c_assertprint(const char *file, int32_t line, const char *expr)
{
    fprintf(stderr, "StdCLib: assertion (%s) failed in \"%s\" on line %d\n", expr, file, (int)line);
    c_abort();
}
}

void Executor::clib::addStdCLib()
{
    /* data */
    exportData(LIB, "errno", errno_);
    exportData(LIB, "MacOSErr", macOSErr_);
    exportData(LIB, "__p_CType", pCType_);
    exportData(LIB, "_IntEnv", intEnv_);
    exportData(LIB, "__C_phase", cPhase_);
    exportData(LIB, "__target_for_exit", targetForExit_);
    exportData(LIB, "_exit_status", exitStatus_);
    exportData(LIB, "__NubAt3", nubAt3_);
    exportData(LIB, "StandAlone", standAlone_);
    exportData(LIB, "NoMoreDebugStr", noMoreDebugStr_);
    exportData(LIB, "_DBL_EPSILON", dblEpsilon_);
    exportData(LIB, "_DBL_MAX", dblMax_);
    exportData(LIB, "_DBL_MIN", dblMin_);
    exportData(LIB, "_FLT_EPSILON", fltEpsilon_);
    exportData(LIB, "_FLT_MAX", fltMax_);
    exportData(LIB, "_FLT_MIN", fltMin_);
    exportData(LIB, "_LDBL_EPSILON", ldblEpsilon_);
    exportData(LIB, "_LDBL_MAX", ldblMax_);
    exportData(LIB, "_LDBL_MIN", ldblMin_);
    const char *internal[] = { "_StdDevs", "__SigEnv", "__loc", "__yd", "_CategoryLoc",
                               "_PublicTimeInfo", "MoneyData", "NumericData" };
    for(int i = 0; i < 8; i++)
        exportData(LIB, internal[i], zeroData_[i]);

    /* string.h */
    exportFn(LIB, "memcpy", &c_memcpy);
    exportFn(LIB, "_memcpy", &c_memcpy);
    exportFn(LIB, "memmove", &c_memmove);
    exportFn(LIB, "_rmemcpy", &c_memmove);
    exportFn(LIB, "memset", &c_memset);
    exportFn(LIB, "memcmp", &c_memcmp);
    exportFn(LIB, "memchr", &c_memchr);
    exportFn(LIB, "_memchr", &c_memchr);
    exportFn(LIB, "memccpy", &c_memccpy);
    exportFn(LIB, "strlen", &c_strlen);
    exportFn(LIB, "strcpy", &c_strcpy);
    exportFn(LIB, "strncpy", &c_strncpy);
    exportFn(LIB, "strcat", &c_strcat);
    exportFn(LIB, "strncat", &c_strncat);
    exportFn(LIB, "strcmp", &c_strcmp);
    exportFn(LIB, "strncmp", &c_strncmp);
    exportFn(LIB, "strchr", &c_strchr);
    exportFn(LIB, "strrchr", &c_strrchr);
    exportFn(LIB, "strspn", &c_strspn);
    exportFn(LIB, "strcspn", &c_strcspn);
    exportFn(LIB, "strpbrk", &c_strpbrk);
    exportFn(LIB, "strstr", &c_strstr);
    exportFn(LIB, "strtok", &c_strtok);
    exportFn(LIB, "strcoll", &c_strcoll);
    exportFn(LIB, "strxfrm", &c_strxfrm);
    exportFn(LIB, "strerror", &c_strerror);

    /* ctype.h */
    exportFn(LIB, "isalnum", &c_isalnum);
    exportFn(LIB, "isalpha", &c_isalpha);
    exportFn(LIB, "iscntrl", &c_iscntrl);
    exportFn(LIB, "isdigit", &c_isdigit);
    exportFn(LIB, "isgraph", &c_isgraph);
    exportFn(LIB, "islower", &c_islower);
    exportFn(LIB, "isprint", &c_isprint);
    exportFn(LIB, "ispunct", &c_ispunct);
    exportFn(LIB, "isspace", &c_isspace);
    exportFn(LIB, "isupper", &c_isupper);
    exportFn(LIB, "isxdigit", &c_isxdigit);
    exportFn(LIB, "isascii", &c_isascii);
    exportFn(LIB, "toascii", &c_toascii);
    exportFn(LIB, "tolower", &c_tolower);
    exportFn(LIB, "toupper", &c_toupper);

    /* stdlib.h */
    exportFn(LIB, "atoi", &c_atoi);
    exportFn(LIB, "atol", &c_atol);
    exportFn(LIB, "atof", &c_atof);
    exportFn(LIB, "strtol", &c_strtol);
    exportFn(LIB, "strtoul", &c_strtoul);
    exportFn(LIB, "strtod", &c_strtod);
    exportFn(LIB, "abs", &c_abs);
    exportFn(LIB, "labs", &c_labs);
    exportRaw(LIB, "div", &raw_div);
    exportRaw(LIB, "ldiv", &raw_div);
    exportFn(LIB, "rand", &c_rand);
    exportFn(LIB, "srand", &c_srand);
    exportFn(LIB, "ecvt", &c_ecvt);
    exportFn(LIB, "fcvt", &c_fcvt);
    exportFn(LIB, "mblen", &c_mblen);
    exportFn(LIB, "mbtowc", &c_mbtowc);
    exportFn(LIB, "wctomb", &c_wctomb);
    exportFn(LIB, "mbstowcs", &c_mbstowcs);
    exportFn(LIB, "wcstombs", &c_wcstombs);
    exportFn(LIB, "malloc", &c_malloc);
    exportFn(LIB, "calloc", &c_calloc);
    exportFn(LIB, "free", &c_free);
    exportFn(LIB, "realloc", &c_realloc);
    exportFn(LIB, "qsort", &c_qsort);
    exportFn(LIB, "bsearch", &c_bsearch);
    exportFn(LIB, "atexit", &c_atexit);
    exportFn(LIB, "exit", &c_exit);
    exportFn(LIB, "_exit", &c__exit);
    exportFn(LIB, "abort", &c_abort);
    exportFn(LIB, "__abort", &c_abort);
    exportFn(LIB, "getenv", &c_getenv);
    exportFn(LIB, "system", &c_system);
    exportFn(LIB, "_BreakPoint", &c_breakpoint);
    exportFn(LIB, "__assertprint", &c_assertprint);

    /* signal.h, setjmp.h */
    exportFn(LIB, "signal", &c_signal);
    exportFn(LIB, "raise", &c_raise);
    exportRaw(LIB, "__setjmp", &raw_setjmp);
    exportRaw(LIB, "longjmp", &raw_longjmp);

    /* time.h */
    exportFn(LIB, "time", &c_time);
    exportFn(LIB, "clock", &c_clock);
    exportFn(LIB, "difftime", &c_difftime);
    exportFn(LIB, "mktime", &c_mktime);
    exportFn(LIB, "localtime", &c_localtime);
    exportFn(LIB, "gmtime", &c_gmtime);
    exportFn(LIB, "asctime", &c_asctime);
    exportFn(LIB, "ctime", &c_ctime);
    exportFn(LIB, "strftime", &c_strftime);

    /* locale.h */
    exportFn(LIB, "setlocale", &c_setlocale);
    exportFn(LIB, "localeconv", &c_localeconv);

    /* PLStringFuncs.h */
    exportFn(LIB, "PLstrcmp", &c_PLstrcmp);
    exportFn(LIB, "PLstrncmp", &c_PLstrncmp);
    exportFn(LIB, "PLstrcpy", &c_PLstrcpy);
    exportFn(LIB, "PLstrncpy", &c_PLstrncpy);
    exportFn(LIB, "PLstrcat", &c_PLstrcat);
    exportFn(LIB, "PLstrncat", &c_PLstrncat);
    exportFn(LIB, "PLstrchr", &c_PLstrchr);
    exportFn(LIB, "PLstrrchr", &c_PLstrrchr);
    exportFn(LIB, "PLstrpbrk", &c_PLstrpbrk);
    exportFn(LIB, "PLstrspn", &c_PLstrspn);
    exportFn(LIB, "PLstrstr", &c_PLstrstr);
    exportFn(LIB, "PLstrlen", &c_PLstrlen);
    exportFn(LIB, "PLpos", &c_PLpos);

    /* IntEnv.h, MacRuntime.h */
    exportFn(LIB, "MakeResolvedFSSpec", &c_MakeResolvedFSSpec);
    exportFn(LIB, "ResolveFolderAliases", &c_ResolveFolderAliases);
    exportFn(LIB, "ResolvePath", &c_ResolvePath);
    exportFn(LIB, "IEResolvePath", &c_ResolvePath);
    exportFn(LIB, "MakeResolvedPath", &c_MakeResolvedPath);
    exportFn(LIB, "TrapAvailable", &c_TrapAvailable);
    exportFn(LIB, "getpid", &c_getpid);

    addStdio();
}
