/*****************************************************************************
 *                                                                           *
 *                                 VDriver.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     FileMgr.h     COMMONFSQUEUEDEFS                                       *
 *     MacTypes.h    INTEGER, Ptr, LONGINT, SignedByte                       *
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

} /* namespace Executor */