/*****************************************************************************
 *                                                                           *
 *                               AppleScript.h                               *
 *                               =============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     OSA.h            OSAError                                             *
 *     Components.h     ComponentInstance                                    *
 *     MacTypes.h       LONGINT                                              *
 *     TextEdit.h       STHandle                                             *
 *     AppleEvents.h    AEDescList                                           *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "OSA.h"
#include "Components.h"
#include "ExMacTypes.h"
#include "TextEdit.h"
#include "AppleEvents.h"

#define MODULE_NAME AppleScript
#include <base/api-module.h>


namespace Executor {

enum {typeAppleScript = "ascr"_4,
kAppleScriptSubtype = typeAppleScript,
typeASStorage = typeAppleScript,
};

enum {kASSelectInit = 4097,
kASSelectSetSourceStyles = 4098,
kASSelectGetSourceStyles = 4099,
kASSelectGetSourceStyleNames = 4100,
};

enum {kASHasOpenHandler = "hsod"_4,
};

COMPONENT_CALL(AppleScriptLib, ASInit, 0x1001, "4444444");


enum {kASDefaultMinStackSize = 4 * 1024,
kASDefaultPreferredStackSize = 16 * 1024,
kASDefaultMaxStackSize = 16 * 1024,
kASDefaultMinHeapSize = 4 * 1024,
kASDefaultPreferredHeapSize = 16 * 1024,
kASDefaultMaxHeapSize = 32 * 1024 * 1024,
};

COMPONENT_CALL(AppleScriptLib, ASSetSourceStyles, 0x1002, "4");


COMPONENT_CALL(AppleScriptLib, ASGetSourceStyles, 0x1003, "4");


COMPONENT_CALL(AppleScriptLib, ASGetSourceStyleNames, 0x1004, "44");


enum {kASSourceStyleUncompiledText = 0,
kASSourceStyleNormalText = 1,
kASSourceStyleLanguageKeyword = 2,
kASSourceStyleApplicationKeyword = 3,
kASSourceStyleComment = 4,
kASSourceStyleLiteral = 5,
kASSourceStyleUserSymbol = 6,
kASSourceStyleObjectSpecifier = 7,
kASNumberOfSourceStyles = 8,
};

} /* namespace Executor */