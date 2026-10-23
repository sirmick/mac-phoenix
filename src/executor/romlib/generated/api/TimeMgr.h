/*****************************************************************************
 *                                                                           *
 *                                 TimeMgr.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    QElemPtr, INTEGER, LONGINT                              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME TimeMgr
#include <base/api-module.h>


namespace Executor {

struct TMTask{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<ProcPtr> tmAddr;GUEST<LONGINT> tmCount;GUEST<LONGINT> tmWakeUp;GUEST<LONGINT> tmReserved; // I don't trust IMIV-301
};static_assert(sizeof(TMTask) == 22);void describeStruct(const TMTask&, std::ostream&);

void InsTime(QElemPtr taskp);REGISTER_TRAP2(InsTime, 0xA058, void(A0), ClearD0);


void InsXTime(QElemPtr taskp);

void RmvTime(QElemPtr taskp);REGISTER_TRAP2(RmvTime, 0xA059, void(A0), ClearD0);


void PrimeTime(QElemPtr taskp, LONGINT count);REGISTER_TRAP2(PrimeTime, 0xA05A, void(A0, D0), ClearD0);


} /* namespace Executor */