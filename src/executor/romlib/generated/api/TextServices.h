/*****************************************************************************
 *                                                                           *
 *                               TextServices.h                              *
 *                               ==============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     INTEGER, OSErr, Ptr                                    *
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
#include "WindowMgr.h"

#define MODULE_NAME TextServices
#include <base/api-module.h>


namespace Executor {

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


} /* namespace Executor */