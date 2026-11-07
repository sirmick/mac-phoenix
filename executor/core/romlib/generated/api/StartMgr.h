/*****************************************************************************
 *                                                                           *
 *                                 StartMgr.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    SignedByte, INTEGER                                     *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME StartMgr
#include <base/api-module.h>


namespace Executor {

union DefStartRec{GUEST_STRUCT;struct{SignedByte sdExtDevID;SignedByte sdPartition;SignedByte sdSlotNum;SignedByte sdSRsrcID;} slotDev;struct{SignedByte sdReserved1;SignedByte sdReserved2;GUEST<INTEGER> sdRefNum;} scsiDev;};static_assert(sizeof(DefStartRec) == 4);void describeStruct(const DefStartRec&, std::ostream&);

typedef DefStartRec *DefStartPtr;

struct DefVideoRec{GUEST_STRUCT;SignedByte sdSlot;SignedByte sdSResource;};static_assert(sizeof(DefVideoRec) == 2);void describeStruct(const DefVideoRec&, std::ostream&);

typedef DefVideoRec *DefVideoPtr;

struct DefOSRec{GUEST_STRUCT;SignedByte sdReserved;SignedByte sdOSType;};static_assert(sizeof(DefOSRec) == 2);void describeStruct(const DefOSRec&, std::ostream&);

typedef DefOSRec *DefOSPtr;

// StartMgr IMV-348 (true-b);
const LowMemGlobal<Byte > CPUFlag{ 0x12F };
inline Byte  LMGetCPUFlag() { return LM(CPUFlag); }
inline void LMSetCPUFlag(Byte val) { LM(CPUFlag) = val; }
NOTRAP_FUNCTION2(LMGetCPUFlag);
NOTRAP_FUNCTION2(LMSetCPUFlag);


// StartMgr IMV (false);
const LowMemGlobal<INTEGER > TimeDBRA{ 0xD00 };
inline INTEGER  LMGetTimeDBRA() { return LM(TimeDBRA); }
inline void LMSetTimeDBRA(INTEGER val) { LM(TimeDBRA) = val; }
NOTRAP_FUNCTION2(LMGetTimeDBRA);
NOTRAP_FUNCTION2(LMSetTimeDBRA);


// StartMgr IMV (false);
const LowMemGlobal<INTEGER > TimeSCCDB{ 0xD02 };
inline INTEGER  LMGetTimeSCCDB() { return LM(TimeSCCDB); }
inline void LMSetTimeSCCDB(INTEGER val) { LM(TimeSCCDB) = val; }
NOTRAP_FUNCTION2(LMGetTimeSCCDB);
NOTRAP_FUNCTION2(LMSetTimeSCCDB);


// StartMgr IMV (false);
const LowMemGlobal<INTEGER > TimeSCSIDB{ 0xDA6 };
inline INTEGER  LMGetTimeSCSIDB() { return LM(TimeSCSIDB); }
inline void LMSetTimeSCSIDB(INTEGER val) { LM(TimeSCSIDB) = val; }
NOTRAP_FUNCTION2(LMGetTimeSCSIDB);
NOTRAP_FUNCTION2(LMSetTimeSCSIDB);


} /* namespace Executor */