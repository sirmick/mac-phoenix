#if !defined(_LOWGLOBALS_H_)
#define _LOWGLOBALS_H_

#include <ExMacTypes.h>
/*
 * Copyright 1986, 1989, 1990, 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *

 */

namespace Executor
{
template<class T>
struct LowMemGlobal
{
    using type = T;
    uint32_t address;
};

template<class T>
inline GUEST<T>& LM(LowMemGlobal<T> lm)
{
    return *(GUEST<T>*)(ROMlib_offset + lm.address);
}

}

#endif
