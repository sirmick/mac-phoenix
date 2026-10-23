/*****************************************************************************
 *                                                                           *
 *                                 DeskMgr.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     INTEGER, ConstStringPtr, Boolean, LONGINT              *
 *     EventMgr.h     EventRecord                                            *
 *     WindowMgr.h    WindowPtr                                              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "EventMgr.h"
#include "WindowMgr.h"

#define MODULE_NAME DeskMgr
#include <base/api-module.h>


namespace Executor {

enum {undoCmd = 0,
cutCmd = 2,
copyCmd = 3,
pasteCmd = 4,
clearCmd = 5,
};

enum {accEvent = 64,
accRun = 65,
accMenu = 67,
accUndo = 68,
};

// DeskMgr IMI-443 (false);
const LowMemGlobal<Byte > SEvtEnb{ 0x15C };
inline Byte  LMGetSEvtEnb() { return LM(SEvtEnb); }
inline void LMSetSEvtEnb(Byte val) { LM(SEvtEnb) = val; }
NOTRAP_FUNCTION2(LMGetSEvtEnb);
NOTRAP_FUNCTION2(LMSetSEvtEnb);


INTEGER C_OpenDeskAcc(ConstStringPtr acc);PASCAL_TRAP(OpenDeskAcc, 0xA9B6);


void C_CloseDeskAcc(INTEGER rn);PASCAL_TRAP(CloseDeskAcc, 0xA9B7);


void C_SystemClick(EventRecord *evp, WindowPtr wp);PASCAL_TRAP(SystemClick, 0xA9B3);


Boolean C_SystemEdit(INTEGER editcmd);PASCAL_TRAP(SystemEdit, 0xA9C2);


void C_SystemTask();PASCAL_TRAP(SystemTask, 0xA9B4);


Boolean C_SystemEvent(EventRecord *evp);PASCAL_TRAP(SystemEvent, 0xA9B2);


void C_SystemMenu(LONGINT menu);PASCAL_TRAP(SystemMenu, 0xA9B5);


} /* namespace Executor */