/*****************************************************************************
 *                                                                           *
 *                               ToolboxEvent.h                              *
 *                               ==============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     Ptr, Boolean, INTEGER, LONGINT, ULONGINT               *
 *     EventMgr.h     EventRecord                                            *
 *     QuickDraw.h    RgnHandle                                              *
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
#include "QuickDraw.h"

#define MODULE_NAME ToolboxEvent
#include <base/api-module.h>


namespace Executor {

// ToolboxEvent IMI-246 (true);
const LowMemGlobal<INTEGER > KeyThresh{ 0x18E };
inline INTEGER  LMGetKeyThresh() { return LM(KeyThresh); }
inline void LMSetKeyThresh(INTEGER val) { LM(KeyThresh) = val; }
NOTRAP_FUNCTION2(LMGetKeyThresh);
NOTRAP_FUNCTION2(LMSetKeyThresh);


// ToolboxEvent IMI-246 (true);
const LowMemGlobal<INTEGER > KeyRepThresh{ 0x190 };
inline INTEGER  LMGetKeyRepThresh() { return LM(KeyRepThresh); }
inline void LMSetKeyRepThresh(INTEGER val) { LM(KeyRepThresh) = val; }
NOTRAP_FUNCTION2(LMGetKeyRepThresh);
NOTRAP_FUNCTION2(LMSetKeyRepThresh);


// ToolboxEvent IMI-260 (true);
const LowMemGlobal<LONGINT > DoubleTime{ 0x2F0 };
inline LONGINT  LMGetDoubleTime() { return LM(DoubleTime); }
inline void LMSetDoubleTime(LONGINT val) { LM(DoubleTime) = val; }
NOTRAP_FUNCTION2(LMGetDoubleTime);
NOTRAP_FUNCTION2(LMSetDoubleTime);


// ToolboxEvent IMI-260 (true);
const LowMemGlobal<LONGINT > CaretTime{ 0x2F4 };
inline LONGINT  LMGetCaretTime() { return LM(CaretTime); }
inline void LMSetCaretTime(LONGINT val) { LM(CaretTime) = val; }
NOTRAP_FUNCTION2(LMGetCaretTime);
NOTRAP_FUNCTION2(LMSetCaretTime);


// ToolboxEvent IMI-258 (true);
const LowMemGlobal<Byte > ScrDmpEnb{ 0x2F8 };
inline Byte  LMGetScrDmpEnb() { return LM(ScrDmpEnb); }
inline void LMSetScrDmpEnb(Byte val) { LM(ScrDmpEnb) = val; }
NOTRAP_FUNCTION2(LMGetScrDmpEnb);
NOTRAP_FUNCTION2(LMSetScrDmpEnb);


// ToolboxEvent IMI-261 (false);
const LowMemGlobal<INTEGER > JournalFlag{ 0x8DE };
inline INTEGER  LMGetJournalFlag() { return LM(JournalFlag); }
inline void LMSetJournalFlag(INTEGER val) { LM(JournalFlag) = val; }
NOTRAP_FUNCTION2(LMGetJournalFlag);
NOTRAP_FUNCTION2(LMSetJournalFlag);


// ToolboxEvent IMI-261 (false);
const LowMemGlobal<INTEGER > JournalRef{ 0x8E8 };
inline INTEGER  LMGetJournalRef() { return LM(JournalRef); }
inline void LMSetJournalRef(INTEGER val) { LM(JournalRef) = val; }
NOTRAP_FUNCTION2(LMGetJournalRef);
NOTRAP_FUNCTION2(LMSetJournalRef);


uint32_t C_KeyTranslate(Ptr mapp, uint16_t code, GUEST<uint32_t> *state);PASCAL_TRAP(KeyTranslate, 0xA9C3);


Boolean C_GetNextEvent(INTEGER em, EventRecord *evt);PASCAL_TRAP(GetNextEvent, 0xA970);


Boolean C_WaitNextEvent(INTEGER mask, EventRecord *evp, LONGINT sleep, RgnHandle mousergn);PASCAL_TRAP(WaitNextEvent, 0xA860);


Boolean C_EventAvail(INTEGER em, EventRecord *evt);PASCAL_TRAP(EventAvail, 0xA971);


void C_GetMouse(GUEST<Point> *p);PASCAL_TRAP(GetMouse, 0xA972);


Boolean C_Button();PASCAL_TRAP(Button, 0xA974);


Boolean C_StillDown();PASCAL_TRAP(StillDown, 0xA973);


Boolean C_WaitMouseUp();PASCAL_TRAP(WaitMouseUp, 0xA977);


void C_GetKeys(uint8_t *keys);PASCAL_TRAP(GetKeys, 0xA976);


ULONGINT C_TickCount();PASCAL_TRAP(TickCount, 0xA975);


LONGINT GetDblTime();

LONGINT GetCaretTime();

// MacPhoenix: GetNextEvent's filter (Tech Note 85): called after every
// GetNextEvent with A1 at the event record and the Boolean result word
// at 4(SP).  Extensions chain onto it (WindowShade watches title bar
// double-clicks here).  Starts at a stub that just returns.
const LowMemGlobal<ProcPtr > jGNEFilter{ 0x29A };
inline ProcPtr  LMGetjGNEFilter() { return LM(jGNEFilter); }
inline void LMSetjGNEFilter(ProcPtr val) { LM(jGNEFilter) = val; }
NOTRAP_FUNCTION2(LMGetjGNEFilter);
NOTRAP_FUNCTION2(LMSetjGNEFilter);


} /* namespace Executor */