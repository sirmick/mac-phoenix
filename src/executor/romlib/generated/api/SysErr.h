/*****************************************************************************
 *                                                                           *
 *                                  SysErr.h                                 *
 *                                  ========                                 *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>

#define MODULE_NAME SysErr
#include <base/api-module.h>


namespace Executor {

typedef enum {EXIST_YES = 0,
EXIST_NO = 255,
}exist_enum_t;

// SysErr IMII-359 (true);
const LowMemGlobal<Ptr > DSAlertTab{ 0x2BA };
inline Ptr  LMGetDSAlertTab() { return LM(DSAlertTab); }
inline void LMSetDSAlertTab(Ptr val) { LM(DSAlertTab) = val; }
NOTRAP_FUNCTION2(LMGetDSAlertTab);
NOTRAP_FUNCTION2(LMSetDSAlertTab);


// SysErr IMII-362 (true);
const LowMemGlobal<Rect > DSAlertRect{ 0x3F8 };
inline void LMGetDSAlertRect(Rect *val) { *val = LM(DSAlertRect); }
inline void LMSetDSAlertRect(const Rect *val) { LM(DSAlertRect) = *val; }
NOTRAP_FUNCTION2(LMGetDSAlertRect);
NOTRAP_FUNCTION2(LMSetDSAlertRect);


// SysError SysEqu.a (true);
const LowMemGlobal<Byte > WWExist{ 0x8F2 };
inline Byte  LMGetWWExist() { return LM(WWExist); }
inline void LMSetWWExist(Byte val) { LM(WWExist) = val; }
NOTRAP_FUNCTION2(LMGetWWExist);
NOTRAP_FUNCTION2(LMSetWWExist);


// SysError SysEqu.a (true);
const LowMemGlobal<Byte > QDExist{ 0x8F3 };
inline Byte  LMGetQDExist() { return LM(QDExist); }
inline void LMSetQDExist(Byte val) { LM(QDExist) = val; }
NOTRAP_FUNCTION2(LMGetQDExist);
NOTRAP_FUNCTION2(LMSetQDExist);


BEGIN_EXECUTOR_ONLY
extern char syserr_msg[];
END_EXECUTOR_ONLY


// MacPhoenix: the code comes in D0 (IM II-362: "moveq #n,d0; _SysError"),
// not on the stack; the Speech Manager's INIT showed 0 for its 98.
void C_SysError(int16_t errorcode);REGISTER_TRAP(SysError, 0xA9C9, void(D0));


} /* namespace Executor */