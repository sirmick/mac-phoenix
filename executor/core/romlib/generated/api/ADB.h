/*****************************************************************************
 *                                                                           *
 *                                   ADB.h                                   *
 *                                   =====                                   *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    SignedByte, Ptr, OSErr, INTEGER                         *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME ADB
#include <base/api-module.h>


namespace Executor {

struct ADBDataBlock{GUEST_STRUCT;SignedByte devType;SignedByte origADBAddr;GUEST<ProcPtr> dbServiceRtPtr;GUEST<Ptr> dbDataAreaAddr;};static_assert(sizeof(ADBDataBlock) == 10);void describeStruct(const ADBDataBlock&, std::ostream&);

struct ADBSetInfoBlock{GUEST_STRUCT;GUEST<ProcPtr> siServiceRtPtr;GUEST<Ptr> siDataAreaAddr;};static_assert(sizeof(ADBSetInfoBlock) == 8);void describeStruct(const ADBSetInfoBlock&, std::ostream&);

// QuickDraw IMV-367 (false);
const LowMemGlobal<Byte > KbdLast{ 0x218 };
inline Byte  LMGetKbdLast() { return LM(KbdLast); }
inline void LMSetKbdLast(Byte val) { LM(KbdLast) = val; }
NOTRAP_FUNCTION2(LMGetKbdLast);
NOTRAP_FUNCTION2(LMSetKbdLast);


// QuickDraw IMV-367 (false);
const LowMemGlobal<Byte > KbdType{ 0x21E };
inline Byte  LMGetKbdType() { return LM(KbdType); }
inline void LMSetKbdType(Byte val) { LM(KbdType) = val; }
NOTRAP_FUNCTION2(LMGetKbdType);
NOTRAP_FUNCTION2(LMSetKbdType);


void ADBReInit();REGISTER_TRAP2(ADBReInit, 0xA07B, void());


OSErr ADBOp(Ptr data, ProcPtr procp, Ptr buffer, INTEGER command);

INTEGER CountADBs();REGISTER_TRAP2(CountADBs, 0xA077, D0());


OSErr GetIndADB(ADBDataBlock *adbp, INTEGER index);REGISTER_TRAP2(GetIndADB, 0xA078, D0(A0, D0));


OSErr GetADBInfo(ADBDataBlock *adbp, INTEGER address);REGISTER_TRAP2(GetADBInfo, 0xA079, D0(A0, D0));


OSErr SetADBInfo(ADBSetInfoBlock *adbp, INTEGER address);REGISTER_TRAP2(SetADBInfo, 0xA07A, D0(A0, D0));


} /* namespace Executor */