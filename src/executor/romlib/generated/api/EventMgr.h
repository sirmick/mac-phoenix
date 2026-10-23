/*****************************************************************************
 *                                                                           *
 *                                 EventMgr.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    INTEGER, LONGINT                                        *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME EventMgr
#include <base/api-module.h>


namespace Executor {

enum {nullEvent = 0,
mouseDown = 1,
mouseUp = 2,
keyDown = 3,
keyUp = 4,
autoKey = 5,
updateEvt = 6,
diskEvt = 7,
activateEvt = 8,
networkEvt = 10,
driverEvt = 11,
app1Evt = 12,
app2Evt = 13,
app3Evt = 14,
app4Evt = 15,
kHighLevelEvent = 23,
};

enum {charCodeMask = 255,
keyCodeMask = 65280,
};

enum {mDownMask = 2,
mUpMask = 4,
keyDownMask = 8,
keyUpMask = 16,
autoKeyMask = 32,
updateMask = 64,
diskMask = 128,
activMask = 256,
};

// #define networkMask	1024
enum {highLevelEventMask = 1024,
driverMask = 2048,
app1Mask = 4096,
app2Mask = 8192,
app3Mask = 16384,
app4Mask = -32768,
everyEvent = -1,
};

enum {activeFlag = 1,
changeFlag = 2,
btnState = 128,
cmdKey = 256,
shiftKey = 512,
alphaLock = 1024,
optionKey = 2048,
ControlKey = 4096,
};

enum {rightShiftKey = 8192,
rightOptionKey = 16384,
rightControlKey = 32768,
};

struct EventRecord{GUEST_STRUCT;GUEST<INTEGER> what;GUEST<LONGINT> message;GUEST<LONGINT> when;GUEST<Point> where;GUEST<INTEGER> modifiers;};static_assert(sizeof(EventRecord) == 16);void describeStruct(const EventRecord&, std::ostream&);

// was LONGINT KeypadMap[2];
// EventMgr SysEqu.a (true-b);
const LowMemGlobal<uint8_t [16]> KeyMap{ 0x174 };


} /* namespace Executor */