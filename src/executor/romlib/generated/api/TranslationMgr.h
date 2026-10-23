/*****************************************************************************
 *                                                                           *
 *                              TranslationMgr.h                             *
 *                              ================                             *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    OSErr, Handle, OSType, Ptr                              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME TranslationMgr
#include <base/api-module.h>


namespace Executor {

DISPATCHER_TRAP(TranslationDispatch, 0xABFC, D0W);


// Selector $E, 18 bytes of parameters, word result. kind is -16555
// ('proc' -16555 is Easy Open's) at both of the Drag Manager's call
// sites ('proc' -16513 +$EC2 and +$2CF6, the latter with 'fmts').
// Answers noTranslationPathErr (-3025, our reading of the Translation
// Manager's errors), which the caller takes as "no translation".
OSErr C_GetFlavorTranslation(int16_t kind, Handle data, OSType theType, Ptr result, ProcPtr proc);PASCAL_SUBTRAP(GetFlavorTranslation, 0xABFC, 0x000E, TranslationDispatch);


} /* namespace Executor */