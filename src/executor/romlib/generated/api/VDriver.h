/*****************************************************************************
 *                                                                           *
 *                                 VDriver.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     FileMgr.h     COMMONFSQUEUEDEFS                                       *
 *     MacTypes.h    INTEGER, Ptr, LONGINT, SignedByte, UInt32, UInt16,      *
 *                   UInt8, Fixed                                            *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "FileMgr.h"
#include "ExMacTypes.h"

#define MODULE_NAME VDriver
#include <base/api-module.h>


namespace Executor {

struct VDParamBlock{GUEST_STRUCT;COMMONFSQUEUEDEFS;GUEST<INTEGER> ioRefNum;GUEST<INTEGER> csCode;GUEST<Ptr> csParam;};void describeStruct(const VDParamBlock&, std::ostream&);

typedef VDParamBlock *VDParamBlockPtr;

struct VDEntryRecord{GUEST_STRUCT;GUEST<Ptr> csTable;GUEST<INTEGER> csStart;GUEST<INTEGER> csCount;};void describeStruct(const VDEntryRecord&, std::ostream&);

typedef VDEntryRecord *VDEntRecPtr;

struct VDGammaRecord{GUEST_STRUCT;GUEST<Ptr> csGTable;};void describeStruct(const VDGammaRecord&, std::ostream&);

typedef VDGammaRecord *VDGamRecPtr;

struct VDPgInfo{GUEST_STRUCT;GUEST<INTEGER> csMode;GUEST<LONGINT> csData;GUEST<INTEGER> csPage;GUEST<Ptr> csBaseAddr;};void describeStruct(const VDPgInfo&, std::ostream&);

typedef VDPgInfo *VDPgInfoPtr;

struct VDFlagRec{GUEST_STRUCT;SignedByte flag;};void describeStruct(const VDFlagRec&, std::ostream&);

typedef VDFlagRec *VDFlagPtr;

struct VDDefModeRec{GUEST_STRUCT;SignedByte spID;};void describeStruct(const VDDefModeRec&, std::ostream&);

typedef VDDefModeRec *VDDefModePtr;

enum {cscReset = 0,
cscKillIO = 1,
cscSetMode = 2,
cscSetEntries = 3,
cscSetGamma = 4,
cscGrayPage = 5,
cscGrayScreen = 5,
cscSetGray = 6,
cscSetInterrupt = 7,
cscDirectSetEntries = 8,
cscSetDefaultMode = 9,
cscSwitchMode = 10,
cscSetSync = 11,
cscSavePreferredConfiguration = 16,
cscSetHardwareCursor = 22,
cscDrawHardwareCursor = 23,
cscSetConvolution = 24,
cscSetPowerState = 25,
cscPrivateControlCall = 26,
cscSetMultiConnect = 28,
cscSetClutBehavior = 29,
cscSetDetailedTiming = 31,
cscDoCommunication = 33,
cscProbeConnection = 34,
cscUnusedCall = 127,
};

enum {cscGetMode = 2,
cscGetEntries = 3,
cscGetPageCnt = 4,
cscGetPages = 4,
cscGetPageBase = 5,
cscGetBaseAddr = 5,
cscGetGray = 6,
cscGetInterrupt = 7,
cscGetGamma = 8,
cscGetDefaultMode = 9,
cscGetCurMode = 10,
cscGetSync = 11,
cscGetConnection = 12,
cscGetModeTiming = 13,
cscGetModeBaseAddress = 14,
cscGetScanProc = 15,
cscGetPreferredConfiguration = 16,
cscGetNextResolution = 17,
cscGetVideoParameters = 18,
cscGetGammaInfoList = 20,
cscRetrieveGammaTable = 21,
cscSupportsHardwareCursor = 22,
cscGetHardwareCursorDrawState = 23,
cscGetConvolution = 24,
cscGetPowerState = 25,
cscPrivateStatusCall = 26,
cscGetDDCBlock = 27,
cscGetMultiConnect = 28,
cscGetClutBehavior = 29,
cscGetTimingRanges = 30,
cscGetDetailedTiming = 31,
cscGetCommunicationInfo = 32,
};

typedef UInt32 DisplayModeID;

typedef UInt16 DepthMode;

struct VDSwitchInfoRec{GUEST_STRUCT;GUEST<UInt16> csMode;GUEST<UInt32> csData;GUEST<UInt16> csPage;GUEST<Ptr> csBaseAddr;GUEST<UInt32> csReserved;};static_assert(sizeof(VDSwitchInfoRec) == 16);void describeStruct(const VDSwitchInfoRec&, std::ostream&);

typedef VDSwitchInfoRec *VDSwitchInfoPtr;

struct VDTimingInfoRec{GUEST_STRUCT;GUEST<UInt32> csTimingMode;GUEST<UInt32> csTimingReserved;GUEST<UInt32> csTimingFormat;GUEST<UInt32> csTimingData;GUEST<UInt32> csTimingFlags;};static_assert(sizeof(VDTimingInfoRec) == 20);void describeStruct(const VDTimingInfoRec&, std::ostream&);

typedef VDTimingInfoRec *VDTimingInfoPtr;

struct VDDisplayConnectInfoRec{GUEST_STRUCT;GUEST<UInt16> csDisplayType;UInt8 csConnectTaggedType;UInt8 csConnectTaggedData;GUEST<UInt32> csConnectFlags;GUEST<UInt32> csDisplayComponent;GUEST<UInt32> csConnectReserved;};static_assert(sizeof(VDDisplayConnectInfoRec) == 16);void describeStruct(const VDDisplayConnectInfoRec&, std::ostream&);

typedef VDDisplayConnectInfoRec *VDDisplayConnectInfoPtr;

struct VDBaseAddressInfoRec{GUEST_STRUCT;GUEST<LONGINT> csDevData;GUEST<LONGINT> csDevBase;GUEST<INTEGER> csModeReserved;GUEST<LONGINT> csModeBase;};static_assert(sizeof(VDBaseAddressInfoRec) == 14);void describeStruct(const VDBaseAddressInfoRec&, std::ostream&);

typedef VDBaseAddressInfoRec *VDBaseAddressInfoPtr;

struct VDResolutionInfoRec{GUEST_STRUCT;GUEST<DisplayModeID> csPreviousDisplayModeID;GUEST<DisplayModeID> csDisplayModeID;GUEST<UInt32> csHorizontalPixels;GUEST<UInt32> csVerticalLines;GUEST<Fixed> csRefreshRate;GUEST<DepthMode> csMaxDepthMode;GUEST<UInt32> csResolutionFlags;GUEST<UInt32> csReserved;};static_assert(sizeof(VDResolutionInfoRec) == 30);void describeStruct(const VDResolutionInfoRec&, std::ostream&);

typedef VDResolutionInfoRec *VDResolutionInfoPtr;

} /* namespace Executor */