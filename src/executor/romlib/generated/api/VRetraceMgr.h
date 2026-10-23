/*****************************************************************************
 *                                                                           *
 *                               VRetraceMgr.h                               *
 *                               =============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    QElemPtr, INTEGER, OSErr, QHdrPtr                       *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME VRetraceMgr
#include <base/api-module.h>


namespace Executor {

enum {qErr = -1,
vTypErr = -2,
};

struct VBLTask{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<ProcPtr> vblAddr;GUEST<INTEGER> vblCount;GUEST<INTEGER> vblPhase;};static_assert(sizeof(VBLTask) == 14);void describeStruct(const VBLTask&, std::ostream&);

typedef VBLTask *VBLTaskPtr;

// VRetraceMgr IMII-352 (true);
const LowMemGlobal<QHdr > VBLQueue{ 0x160 };
inline void LMGetVBLQueue(QHdr *val) { *val = LM(VBLQueue); }
inline void LMSetVBLQueue(const QHdr *val) { LM(VBLQueue) = *val; }
NOTRAP_FUNCTION2(LMGetVBLQueue);
NOTRAP_FUNCTION2(LMSetVBLQueue);


// VRetraceMgr IMV (false);
const LowMemGlobal<ProcPtr > JVBLTask{ 0xD28 };
inline ProcPtr  LMGetJVBLTask() { return LM(JVBLTask); }
inline void LMSetJVBLTask(ProcPtr val) { LM(JVBLTask) = val; }
NOTRAP_FUNCTION2(LMGetJVBLTask);
NOTRAP_FUNCTION2(LMSetJVBLTask);


OSErr VInstall(VBLTaskPtr vtaskp);REGISTER_TRAP2(VInstall, 0xA033, D0(A0));


OSErr VRemove(VBLTaskPtr vtaskp);REGISTER_TRAP2(VRemove, 0xA034, D0(A0));


QHdrPtr GetVBLQHdr();

OSErr SlotVInstall(VBLTaskPtr vtaskp, INTEGER slot);REGISTER_TRAP2(SlotVInstall, 0xA06F, D0(A0, D0));


OSErr SlotVRemove(VBLTaskPtr vtaskp, INTEGER slot);REGISTER_TRAP2(SlotVRemove, 0xA070, D0(A0, D0));


} /* namespace Executor */