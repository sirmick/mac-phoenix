/*****************************************************************************
 *                                                                           *
 *                                AppleTalk.h                                *
 *                                ===========                                *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>

#define MODULE_NAME AppleTalk
#include <base/api-module.h>


namespace Executor {

// AppleTalk IMII-305 (true-b);
const LowMemGlobal<Byte > PortBUse{ 0x291 };
inline Byte  LMGetPortBUse() { return LM(PortBUse); }
inline void LMSetPortBUse(Byte val) { LM(PortBUse) = val; }
NOTRAP_FUNCTION2(LMGetPortBUse);
NOTRAP_FUNCTION2(LMSetPortBUse);


// AppleTalk IMII-328 (false);
const LowMemGlobal<Ptr > ABusVars{ 0x2D8 };
inline Ptr  LMGetABusVars() { return LM(ABusVars); }
inline void LMSetABusVars(Ptr val) { LM(ABusVars) = val; }
NOTRAP_FUNCTION2(LMGetABusVars);
NOTRAP_FUNCTION2(LMSetABusVars);


// AppleTalk MPW (false);
const LowMemGlobal<Ptr > ABusDCE{ 0x2DC };
inline Ptr  LMGetABusDCE() { return LM(ABusDCE); }
inline void LMSetABusDCE(Ptr val) { LM(ABusDCE) = val; }
NOTRAP_FUNCTION2(LMGetABusDCE);
NOTRAP_FUNCTION2(LMSetABusDCE);


} /* namespace Executor */