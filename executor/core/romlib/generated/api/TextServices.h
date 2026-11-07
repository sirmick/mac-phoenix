/*****************************************************************************
 *                                                                           *
 *                               TextServices.h                              *
 *                               ==============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     Ptr, INTEGER, OSErr, OSType, LONGINT, Boolean          *
 *     WindowMgr.h    WindowPtr                                              *
 *     EventMgr.h     EventRecord                                            *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "WindowMgr.h"
#include "EventMgr.h"

#define MODULE_NAME TextServices
#include <base/api-module.h>


namespace Executor {

typedef Ptr TSMDocumentID;

// MacPhoenix: 7.5.5's handler is a table of 26 long addresses
// (selectors 0-25: NewTSMDocument, DeleteTSMDocument, ...,
// SetTextServiceLanguage is 14); its globals hang off ExpandMem+$64.
// Only the two private selectors the Help Manager calls while
// balloons are on are here; their names are guesses.
DISPATCHER_TRAP(TextServicesDispatch, 0xAA54, D0W);


// Selector 23: FindWindow among the TSM's floating input windows
// (Apple's switches to the layer OSDispatch $48 answers and calls
// FindWindow there; the Process Manager's OSDispatch $64 is this
// call). The Help Manager treats a hit as "not mine". No such
// windows here: 0 and nil.
INTEGER C_TSMFindWindow(Point pt, GUEST<WindowPtr> *window);PASCAL_SUBTRAP(TSMFindWindow, 0xAA54, 0x0017, TextServicesDispatch);


// Selector 22: operations (1-6) on the floating input window's state
// in the TSM globals. paramErr here; the Help Manager takes that as
// "no input window".
OSErr C_TSMInputWindowOp(Ptr record, INTEGER op);PASCAL_SUBTRAP(TSMInputWindowOp, 0xAA54, 0x0016, TextServicesDispatch);


OSErr C_InitTSMAwareApplication();PASCAL_SUBTRAP(InitTSMAwareApplication, 0xAA54, 0x0014, TextServicesDispatch);


OSErr C_CloseTSMAwareApplication();PASCAL_SUBTRAP(CloseTSMAwareApplication, 0xAA54, 0x0015, TextServicesDispatch);


OSErr C_NewTSMDocument(INTEGER numOfInterface, GUEST<OSType> *supportedInterfaces, GUEST<TSMDocumentID> *idocID, LONGINT refcon);PASCAL_SUBTRAP(NewTSMDocument, 0xAA54, 0x0000, TextServicesDispatch);


OSErr C_DeleteTSMDocument(TSMDocumentID idocID);PASCAL_SUBTRAP(DeleteTSMDocument, 0xAA54, 0x0001, TextServicesDispatch);


OSErr C_ActivateTSMDocument(TSMDocumentID idocID);PASCAL_SUBTRAP(ActivateTSMDocument, 0xAA54, 0x0002, TextServicesDispatch);


OSErr C_DeactivateTSMDocument(TSMDocumentID idocID);PASCAL_SUBTRAP(DeactivateTSMDocument, 0xAA54, 0x0003, TextServicesDispatch);


Boolean C_TSMEvent(EventRecord *event);PASCAL_SUBTRAP(TSMEvent, 0xAA54, 0x0004, TextServicesDispatch);


Boolean C_TSMMenuSelect(LONGINT menuResult);PASCAL_SUBTRAP(TSMMenuSelect, 0xAA54, 0x0005, TextServicesDispatch);


Boolean C_SetTSMCursor(Point mousePos);PASCAL_SUBTRAP(SetTSMCursor, 0xAA54, 0x0006, TextServicesDispatch);


OSErr C_FixTSMDocument(TSMDocumentID idocID);PASCAL_SUBTRAP(FixTSMDocument, 0xAA54, 0x0007, TextServicesDispatch);


OSErr C_UseInputWindow(TSMDocumentID idocID, Boolean useWindow);PASCAL_SUBTRAP(UseInputWindow, 0xAA54, 0x0010, TextServicesDispatch);


} /* namespace Executor */