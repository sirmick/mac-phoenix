/*****************************************************************************
 *                                                                           *
 *                                  LowMem.h                                 *
 *                                  ========                                 *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>

#define MODULE_NAME LowMem
#include <base/api-module.h>


namespace Executor {

// Declarations of low memory globals which haven't been put anywhere else
// yet follow. Whenever a low memory global clearly belongs to one manager/module,
// put it should live in the appropriate header.
// There is also a complete list of low memory globals in globals.cpp,
// which is not used but might be nice to have.
// rsys/misc MADEUP (true-b);
const LowMemGlobal<Ptr > nilhandle{ 0x000 };
inline Ptr  LMGetnilhandle() { return LM(nilhandle); }
inline void LMSetnilhandle(Ptr val) { LM(nilhandle) = val; }
NOTRAP_FUNCTION2(LMGetnilhandle);
NOTRAP_FUNCTION2(LMSetnilhandle);


// * NOTE: MacWrite starts writing longwords at location 0x80 for TRAPs
// rsys/misc WHOKNOWS (true-b);
const LowMemGlobal<GUEST<LONGINT> [10]> trapvectors{ 0x080 };


// rsys/misc WHOKNOWS (true-b);
const LowMemGlobal<Ptr > dodusesit{ 0x0E4 };
inline Ptr  LMGetdodusesit() { return LM(dodusesit); }
inline void LMSetdodusesit(Ptr val) { LM(dodusesit) = val; }
NOTRAP_FUNCTION2(LMGetdodusesit);
NOTRAP_FUNCTION2(LMSetdodusesit);


// * Hypercard does a movel to this location.
// rsys/misc WHOKNOWS (true-b);
const LowMemGlobal<LONGINT > hyperlong{ 0x1AA };
inline LONGINT  LMGethyperlong() { return LM(hyperlong); }
inline void LMSethyperlong(LONGINT val) { LM(hyperlong) = val; }
NOTRAP_FUNCTION2(LMGethyperlong);
NOTRAP_FUNCTION2(LMSethyperlong);


// * NOTE: mathones is a LONGINT that Mathematica looks at that contains -1
//  * on a Mac+
// rsys/misc WHOKNOWS (true-b);
const LowMemGlobal<LONGINT > mathones{ 0x282 };
inline LONGINT  LMGetmathones() { return LM(mathones); }
inline void LMSetmathones(LONGINT val) { LM(mathones) = val; }
NOTRAP_FUNCTION2(LMGetmathones);
NOTRAP_FUNCTION2(LMSetmathones);


// * NOTE: Theoretically ROM85 is mentioned in IMV, but I don't know where.
//  * On a Mac+ the value 0x7FFF is stored there.
//  * tim: It is at least on page IMV-328.
// MacTypes IMV-328 (true-b);
const LowMemGlobal<INTEGER > ROM85{ 0x28E };
inline INTEGER  LMGetROM85() { return LM(ROM85); }
inline void LMSetROM85(INTEGER val) { LM(ROM85) = val; }
NOTRAP_FUNCTION2(LMGetROM85);
NOTRAP_FUNCTION2(LMSetROM85);


// DiskDvr IMII-212 (false);
const LowMemGlobal<LONGINT > BufTgFNum{ 0x2FC };
inline LONGINT  LMGetBufTgFNum() { return LM(BufTgFNum); }
inline void LMSetBufTgFNum(LONGINT val) { LM(BufTgFNum) = val; }
NOTRAP_FUNCTION2(LMGetBufTgFNum);
NOTRAP_FUNCTION2(LMSetBufTgFNum);


// DiskDvr IMII-212 (false);
const LowMemGlobal<INTEGER > BufTgFFlg{ 0x300 };
inline INTEGER  LMGetBufTgFFlg() { return LM(BufTgFFlg); }
inline void LMSetBufTgFFlg(INTEGER val) { LM(BufTgFFlg) = val; }
NOTRAP_FUNCTION2(LMGetBufTgFFlg);
NOTRAP_FUNCTION2(LMSetBufTgFFlg);


// DiskDvr IMII-212 (false);
const LowMemGlobal<INTEGER > BufTgFBkNum{ 0x302 };
inline INTEGER  LMGetBufTgFBkNum() { return LM(BufTgFBkNum); }
inline void LMSetBufTgFBkNum(INTEGER val) { LM(BufTgFBkNum) = val; }
NOTRAP_FUNCTION2(LMGetBufTgFBkNum);
NOTRAP_FUNCTION2(LMSetBufTgFBkNum);


// DiskDvr IMII-212 (false);
const LowMemGlobal<LONGINT > BufTgDate{ 0x304 };
inline LONGINT  LMGetBufTgDate() { return LM(BufTgDate); }
inline void LMSetBufTgDate(LONGINT val) { LM(BufTgDate) = val; }
NOTRAP_FUNCTION2(LMGetBufTgDate);
NOTRAP_FUNCTION2(LMSetBufTgDate);


// MacLinkPC badaccess (true-b);
const LowMemGlobal<INTEGER > MCLKPCmiss1{ 0x3A0 };
inline INTEGER  LMGetMCLKPCmiss1() { return LM(MCLKPCmiss1); }
inline void LMSetMCLKPCmiss1(INTEGER val) { LM(MCLKPCmiss1) = val; }
NOTRAP_FUNCTION2(LMGetMCLKPCmiss1);
NOTRAP_FUNCTION2(LMSetMCLKPCmiss1);


// MacLinkPC badaccess (true-b);
const LowMemGlobal<INTEGER > MCLKPCmiss2{ 0x3A6 };
inline INTEGER  LMGetMCLKPCmiss2() { return LM(MCLKPCmiss2); }
inline void LMSetMCLKPCmiss2(INTEGER val) { LM(MCLKPCmiss2) = val; }
NOTRAP_FUNCTION2(LMGetMCLKPCmiss2);
NOTRAP_FUNCTION2(LMSetMCLKPCmiss2);


// * JFLUSH is a guess from disassembling some of Excel 3.0
// idunno guess (true-b);
const LowMemGlobal<VoidUPP > JFLUSH{ 0x6F4 };
inline VoidUPP  LMGetJFLUSH() { return LM(JFLUSH); }
inline void LMSetJFLUSH(VoidUPP val) { LM(JFLUSH) = val; }
NOTRAP_FUNCTION2(LMGetJFLUSH);
NOTRAP_FUNCTION2(LMSetJFLUSH);


// idunno resedit (true-b);
const LowMemGlobal<VoidUPP > JResUnknown1{ 0x700 };
inline VoidUPP  LMGetJResUnknown1() { return LM(JResUnknown1); }
inline void LMSetJResUnknown1(VoidUPP val) { LM(JResUnknown1) = val; }
NOTRAP_FUNCTION2(LMGetJResUnknown1);
NOTRAP_FUNCTION2(LMSetJResUnknown1);


// idunno resedit (true-b);
const LowMemGlobal<VoidUPP > JResUnknown2{ 0x714 };
inline VoidUPP  LMGetJResUnknown2() { return LM(JResUnknown2); }
inline void LMSetJResUnknown2(VoidUPP val) { LM(JResUnknown2) = val; }
NOTRAP_FUNCTION2(LMGetJResUnknown2);
NOTRAP_FUNCTION2(LMSetJResUnknown2);


// * NOTE: The graphing program looks for a -1 in 0x952
// rsys/misc WHOKNOWS (true-b);
const LowMemGlobal<INTEGER > graphlooksat{ 0x952 };
inline INTEGER  LMGetgraphlooksat() { return LM(graphlooksat); }
inline void LMSetgraphlooksat(INTEGER val) { LM(graphlooksat) = val; }
NOTRAP_FUNCTION2(LMGetgraphlooksat);
NOTRAP_FUNCTION2(LMSetgraphlooksat);


// * NOTE: MacWrite stores a copy of the trap address for LoadSeg in 954
// rsys/misc WHOKNOWS (true-b);
const LowMemGlobal<LONGINT > macwritespace{ 0x954 };
inline LONGINT  LMGetmacwritespace() { return LM(macwritespace); }
inline void LMSetmacwritespace(LONGINT val) { LM(macwritespace) = val; }
NOTRAP_FUNCTION2(LMGetmacwritespace);
NOTRAP_FUNCTION2(LMSetmacwritespace);


// MacTypes IMII-362 (true);
const LowMemGlobal<INTEGER > DSErrCode{ 0xAF0 };
inline INTEGER  LMGetDSErrCode() { return LM(DSErrCode); }
inline void LMSetDSErrCode(INTEGER val) { LM(DSErrCode) = val; }
NOTRAP_FUNCTION2(LMGetDSErrCode);
NOTRAP_FUNCTION2(LMSetDSErrCode);


// uknown Private.a (true-b);
const LowMemGlobal<INTEGER > SCSIFlags{ 0xB22 };
inline INTEGER  LMGetSCSIFlags() { return LM(SCSIFlags); }
inline void LMSetSCSIFlags(INTEGER val) { LM(SCSIFlags) = val; }
NOTRAP_FUNCTION2(LMGetSCSIFlags);
NOTRAP_FUNCTION2(LMSetSCSIFlags);


// rsys/misc WHOKNOWS (true-b);
const LowMemGlobal<LONGINT > LastSPExtra{ 0xB4C };
inline LONGINT  LMGetLastSPExtra() { return LM(LastSPExtra); }
inline void LMSetLastSPExtra(LONGINT val) { LM(LastSPExtra) = val; }
NOTRAP_FUNCTION2(LMGetLastSPExtra);
NOTRAP_FUNCTION2(LMSetLastSPExtra);


// rsys/misc MadeUp (true-b);
const LowMemGlobal<LONGINT > lastlowglobal{ 0x2000 };
inline LONGINT  LMGetlastlowglobal() { return LM(lastlowglobal); }
inline void LMSetlastlowglobal(LONGINT val) { LM(lastlowglobal) = val; }
NOTRAP_FUNCTION2(LMGetlastlowglobal);
NOTRAP_FUNCTION2(LMSetlastlowglobal);


} /* namespace Executor */