/* MacPhoenix: Executor's StdCLib and MathLib for PowerPC code fragments
 * (docs/executor/PPCLibs.md). A Power Mac has both in ROM; Executor answers
 * them in C++, as it does InterfaceLib. Written from the MPW C headers'
 * declarations and the C standard, with Apple's libraries on the Power Mac
 * as the black-box reference (tests/guest/clib).
 *
 * Calls arrive as PowerPC system calls (builtinlibs): the arguments in
 * r3-r10 and f1-f13, the rest in the caller's parameter area at r1 + 24, one
 * 4-byte word per argument word (Mac OS Runtime Architectures, "PowerPC
 * calling conventions"); results in r3 or f1. */
#pragma once

#include <base/common.h>
#include <base/builtinlibs.h>
#include <base/cpu.h>
#include <PowerCore.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

namespace Executor
{
/* mathlib.cpp: SANE's extended80 at a guest address */
long double fromX80(uint32_t p);

namespace clib
{

/* ---- guest memory ---------------------------------------------------------- */

template<typename T = uint8_t>
inline T *host(uint32_t addr)
{
    return addr ? ptr_from_longint<T *>(addr) : nullptr;
}

inline uint32_t guest(const void *p)
{
    return p ? (uint32_t)ptr_to_longint(p) : 0;
}

inline uint32_t read32(uint32_t addr)
{
    uint8_t *p = host(addr);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

inline uint16_t read16(uint32_t addr)
{
    uint8_t *p = host(addr);
    return uint16_t((p[0] << 8) | p[1]);
}

inline void write32(uint32_t addr, uint32_t v)
{
    uint8_t *p = host(addr);
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

inline void write16(uint32_t addr, uint16_t v)
{
    uint8_t *p = host(addr);
    p[0] = v >> 8; p[1] = v;
}

inline double readDouble(uint32_t addr)
{
    uint64_t bits = (uint64_t(read32(addr)) << 32) | read32(addr + 4);
    double d;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}

inline void writeDouble(uint32_t addr, double d)
{
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    write32(addr, uint32_t(bits >> 32));
    write32(addr + 4, uint32_t(bits));
}

inline float readFloat(uint32_t addr)
{
    uint32_t bits = read32(addr);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

inline void writeFloat(uint32_t addr, float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    write32(addr, bits);
}

inline uint32_t wordFromDouble(double d, int half)
{
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    return half ? uint32_t(bits) : uint32_t(bits >> 32);
}

inline double doubleFromWords(uint32_t hi, uint32_t lo)
{
    uint64_t bits = (uint64_t(hi) << 32) | lo;
    double d;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}

/* A C string in guest memory. */
inline std::string cstring(uint32_t addr)
{
    return addr ? std::string(host<char>(addr)) : std::string();
}

/* ---- arguments ------------------------------------------------------------- */

/* The arguments of one call: word i of the parameter area, wherever it is. */
struct PPCArgs
{
    PowerCore &cpu;

    uint32_t word(int i) const
    {
        return i < 8 ? cpu.r[3 + i] : read32(cpu.r[1] + 24 + 4 * i);
    }
};

/* A call's parameter words, from PowerPC code or from CFM-68K code (the
   68k stack: the return address, then the arguments right to left, each in
   4 bytes). */
struct CallArgs
{
    virtual ~CallArgs() = default;
    virtual uint32_t word(int i) const = 0;
    /* where word i is, for a CFM-68K call (its long doubles are read from
       memory); 0 for a PowerPC call */
    virtual uint32_t address(int) const { return 0; }
};

struct PPCCallArgs : CallArgs
{
    PPCArgs args;
    explicit PPCCallArgs(PowerCore &cpu) : args{cpu} {}
    uint32_t word(int i) const override { return args.word(i); }
};

struct CFM68KCallArgs : CallArgs
{
    uint32_t sp;   /* at the return address */
    explicit CFM68KCallArgs(uint32_t sp) : sp(sp) {}
    uint32_t address(int i) const override { return sp + 4 + 4 * i; }
    uint32_t word(int i) const override { return read32(address(i)); }
};

/* Variadic arguments in order, a word at a time: from a call's parameter
   area (printf) or from a va_list, which MrC and SC make a pointer into the
   caller's parameter area (vprintf). A double takes two words, all 4-byte
   aligned. A long double (%Lf) is a double on PowerPC (MrC's default); on
   68k SANE's 10-byte extended padded to 12, which Apple's NuStdCLib reads
   (SC converts some long doubles to double before passing them, a
   variable one for instance; those print wrong there too). */
struct VarArgs
{
    virtual ~VarArgs() = default;
    virtual uint32_t word() = 0;
    /* the next word's address, for 68k arguments; 0 on PowerPC */
    virtual uint32_t address() const { return 0; }

    double dbl()
    {
        uint32_t hi = word();
        return doubleFromWords(hi, word());
    }
    double longDouble()
    {
        uint32_t p = address();
        if(!p)
            return dbl();
        double v = (double)fromX80(p);
        word(), word(), word();
        return v;
    }
};

struct CallVarArgs : VarArgs
{
    const CallArgs &args;
    int next;
    CallVarArgs(const CallArgs &args, int first) : args(args), next(first) {}
    uint32_t word() override { return args.word(next++); }
    uint32_t address() const override { return args.address(next); }
};

struct ListVarArgs : VarArgs
{
    uint32_t p;
    bool m68k;
    explicit ListVarArgs(uint32_t ap, bool m68k = false) : p(ap), m68k(m68k) {}
    uint32_t word() override
    {
        uint32_t w = read32(p);
        p += 4;
        return w;
    }
    uint32_t address() const override { return m68k ? p : 0; }
};

/* ---- exports --------------------------------------------------------------- */

/* The typed routines are registered from the definitions (StdCLib.yaml,
   MathLib.yaml: `library:`). A routine with a raw handler (`raw: true`
   there) is registered here: it reads its arguments, sets r3/f1, returns
   where execution goes on (normally cpu.lr). */
void exportRaw(const char *library, const char *name,
               std::function<uint32_t(PowerCore &)> code);

/* ... and its CFM-68K version: `body` gets the call's arguments and
   answers D0. A variadic routine (printf) leaves its arguments to the
   caller; one with fixed arguments (vprintf) pops `argWords` of them. */
void exportRaw68k(const char *library, const char *name,
                  std::function<uint32_t(const CallArgs &)> body, int argWords = -1);

/* ---- the libraries ---------------------------------------------------------- */

/* Each fills in its exports once. */
void addStdCLib();
void addMathLib();

/* A new application imports StdCLib: its state starts afresh. */
void stdclibStart(bool m68k = false);
/* A fragment imports StdCLib: started afresh (stdclibStart) for an
   application being launched (launchingApplication: its fragment and the
   libraries it imports being prepared) or when StdCLib has no client;
   not for a library another process loads meanwhile. */
extern bool launchingApplication;
void stdclibImported(bool m68k = false);
/* Calls a routine of the client application (qsort's comparison, an atexit
   or signal handler): a PowerPC TV, or a CFM-68K one for a NuStdCLib
   client. */
uint32_t callClient(uint32_t tv, std::initializer_list<uint32_t> args);
/* The application ends (ExitToShell): streams flushed and closed. */
void stdclibProcessExit();

/* stdio.cpp, for stdclib.cpp */
void addStdio();
void stdioStart(bool m68k);
void stdioFlushAll();
void stdioCloseAll();

/* format.cpp: printf and scanf. */
struct CharSource
{
    virtual ~CharSource() = default;
    virtual int get() = 0;          /* -1 at the end */
    virtual void unget(int c) = 0;
};
std::string formatPrintf(const char *format, VarArgs &args);
/* The number of assignments, or -1 at end of input before the first. */
int formatScanf(CharSource &in, const char *format, VarArgs &args);

/* errno (stdclib.cpp) */
void setErrno(int e);
int getErrno();
std::string errorMessage(int e);   /* strerror's text */
void setMacOSErr(int16_t err);
int errnoFromOSErr(int16_t err);

/* The C type table (stdclib.cpp): _UPP 1, _LOW 2, _DIG 4, _WSP 8, _PUN 16,
   _CTL 32, _BLA 64, _HEX 128. */
extern const uint8_t ctypeTable[256];
inline bool cIsSpace(int c) { return c >= 0 && c < 256 && (ctypeTable[c] & 8); }
inline bool cIsDigit(int c) { return c >= '0' && c <= '9'; }

}
}
