/*****************************************************************************
 *                                                                           *
 *                              BinaryDecimal.h                              *
 *                              ===============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    LONGINT, StringPtr, ConstStringPtr                      *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME BinaryDecimal
#include <base/api-module.h>


namespace Executor {

EXTERN_DISPATCHER_TRAP(Pack7, 0xA9EE, StackW);
void C_NumToString(LONGINT l, StringPtr s);REGISTER_SUBTRAP(NumToString, 0xA9EE, 0x0000, Pack7, void(D0, A0));


void C_StringToNum(ConstStringPtr s, GUEST<LONGINT> *lp);REGISTER_SUBTRAP(StringToNum, 0xA9EE, 0x0001, Pack7, void(A0, Out<LONGINT, D0>));


inline void cglue_numtostring(LONGINT l, char *s) {C_NumToString(l, (StringPtr)s);cglue::p2c(s);}
LIBRARY_FUNCTION(InterfaceLib, numtostring, cglue_numtostring);


inline void cglue_stringtonum(const char *s, GUEST<LONGINT> *lp) {C_StringToNum(cglue::CString(s), lp);}
LIBRARY_FUNCTION(InterfaceLib, stringtonum, cglue_stringtonum);


} /* namespace Executor */