#pragma once

#ifdef EXECUTOR
#include <ExMacTypes.h>
#include <MemoryMgr.h>
#include <QuickDraw.h>
using namespace Executor;
extern QDGlobals qd;

inline bool hasDeepGWorlds()
{
    return true;
}

StringPtr PSTR(const char* s);

typedef signed char SInt8;
typedef int32_t SInt32;



#else
#include <MacTypes.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Gestalt.h>

template<typename T> T& out(T& x) { return x; }
template<typename T> T& inout(T& x) { return x; }

template<typename T> using GUEST = T;

inline bool hasDeepGWorlds()
{
    long resp;
    if(Gestalt(gestaltQuickdrawFeatures, &resp))
        return false;
    return resp & (1 << gestaltHasDeepGWorlds);
}

#define PSTR(s) StringPtr("\p" s)

template<typename TT>
TT ptr_from_longint(int32_t l)
{
    return (TT) l;
}
#endif

#define PTR(x) (&inout(x))
#define OPTR(x) (&out(x))

