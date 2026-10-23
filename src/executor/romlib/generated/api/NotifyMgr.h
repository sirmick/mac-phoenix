/*****************************************************************************
 *                                                                           *
 *                                NotifyMgr.h                                *
 *                                ===========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    QElemPtr, INTEGER, LONGINT, Handle, StringPtr, OSErr    *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME NotifyMgr
#include <base/api-module.h>


namespace Executor {

enum {nmType = 8,
nmTypErr = -299,
};

struct NMRec{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> nmFlags;GUEST<LONGINT> nmPrivate;GUEST<INTEGER> nmReserved;GUEST<INTEGER> nmMark;GUEST<Handle> nmIcon;GUEST<Handle> nmSound;GUEST<StringPtr> nmStr;GUEST<ProcPtr> nmResp; // void myresponse(NMRecPtr foo)
GUEST<LONGINT> nmRefCon;};static_assert(sizeof(NMRec) == 36);void describeStruct(const NMRec&, std::ostream&);

// value of -1 means remove queue element
//                            automatically
typedef NMRec *NMRecPtr;

OSErr NMInstall(NMRecPtr nmptr);REGISTER_TRAP2(NMInstall, 0xA05E, D0(A0));


OSErr NMRemove(NMRecPtr nmptr);REGISTER_TRAP2(NMRemove, 0xA05F, D0(A0));


} /* namespace Executor */