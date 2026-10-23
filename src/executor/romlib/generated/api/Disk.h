/*****************************************************************************
 *                                                                           *
 *                                   Disk.h                                  *
 *                                   ======                                  *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    INTEGER, SignedByte, QElemPtr, OSErr, Ptr               *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME Disk
#include <base/api-module.h>


namespace Executor {

enum {firstDskErr = -84,
sectNFErr = -81,
seekErr = -80,
spdAdjErr = -79,
twoSideErr = -78,
initIWMErr = -77,
tk0BadErr = -76,
cantStepErr = -75,
wrUnderrun = -74,
badDBtSlp = -73,
badDCksum = -72,
noDtaMkErr = -71,
badBtSlpErr = -70,
badCksmErr = -69,
dataVerErr = -68,
noAdrMkErr = -67,
noNybErr = -66,
offLinErr = -65,
noDriveErr = -64,
lastDskErr = -64,
};

struct DrvSts{GUEST_STRUCT;GUEST<INTEGER> track;SignedByte writeProt;SignedByte diskInPlace;SignedByte installed;SignedByte sides;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> dQDrive;GUEST<INTEGER> dQRefNum;GUEST<INTEGER> dQFSID;SignedByte twoSideFmt;SignedByte needsFlush;GUEST<INTEGER> diskErrs;};static_assert(sizeof(DrvSts) == 22);void describeStruct(const DrvSts&, std::ostream&);

OSErr DiskEject(INTEGER rn);

OSErr SetTagBuffer(Ptr bp);

OSErr DriveStatus(INTEGER dn, DrvSts *statp);

} /* namespace Executor */