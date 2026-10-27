/*****************************************************************************
 *                                                                           *
 *                                   OSA.h                                   *
 *                                   =====                                   *
 *                                                                           *
 * Needs:                                                                    *
 *     Components.h     ComponentResult, ComponentInstance                   *
 *     MacTypes.h       OSType, OSErr, LONGINT, INTEGER                      *
 *     AppleEvents.h    AppleEvent, AESendMode, AESendPriority, IdleUPP,     *
 *                      EventFilterUPP, AEEventClass, AEEventID,             *
 *                      AEAddressDesc, AEDesc, DescType,                     *
 *                      AEEventHandlerUPP                                    *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "Components.h"
#include "ExMacTypes.h"
#include "AppleEvents.h"

#define MODULE_NAME OSA
#include <base/api-module.h>


namespace Executor {

typedef uint32_t OSAID;

typedef ComponentResult OSAError;

typedef OSType ScriptingComponentSelector;

using OSAActiveUPP = UPP<OSErr(LONGINT refCon)>;


using OSASendUPP = UPP<OSErr(const GUEST<AppleEvent> *theAppleEvent, GUEST<AppleEvent> *reply, AESendMode sendMode, AESendPriority sendPriority, LONGINT timeOutInTicks, IdleUPP idleProc, EventFilterUPP filterProc, LONGINT refCon)>;


using OSACreateAppleEventUPP = UPP<OSErr(AEEventClass theAEEventClass, AEEventID theAEEventID, const GUEST<AEAddressDesc> *target, INTEGER returnID, LONGINT transactionID, GUEST<AppleEvent> *result, LONGINT refCon)>;


enum {kOSAComponentType = "osa "_4,
kOSAGenericScriptingComponentSubtype = "scpt"_4,
kOSAFileType = "osas"_4,
kOSASuite = "ascr"_4,
kOSANullScript = 0,
kOSANullMode = 0,
kOSAModeNull = 0,
kOSAModePreventGetSource = 1,
kOSAModeCompileIntoContext = 2,
kOSAModeAugmentContext = 4,
kOSAModeDisplayForHumans = 8,
kOSAModeNeverInteract = 16,
kOSAModeCanInteract = 32,
kOSAModeAlwaysInteract = 48,
kOSAModeCantSwitchLayer = 64,
kOSAModeDontReconnect = 128,
kOSAModeDoRecord = 4096,
kOSAModeDontStoreParent = 65536,
};

enum {errOSASystemError = -1750,
errOSAInvalidID = -1751,
errOSABadStorageType = -1752,
errOSAScriptError = -1753,
errOSABadSelector = -1754,
errOSASourceNotAvailable = -1756,
errOSANoSuchDialect = -1757,
errOSADataFormatObsolete = -1758,
errOSADataFormatTooNew = -1759,
errOSAComponentMismatch = -1761,
errOSACantOpenComponent = -1762,
};



COMPONENT_CALL(AppleScriptLib, OSALoad, 0x0001, "444");


COMPONENT_CALL(AppleScriptLib, OSAStore, 0x0002, "4444");


COMPONENT_CALL(AppleScriptLib, OSAExecute, 0x0003, "4444");


COMPONENT_CALL(AppleScriptLib, OSADisplay, 0x0004, "4444");


COMPONENT_CALL(AppleScriptLib, OSAScriptError, 0x0005, "444");


COMPONENT_CALL(AppleScriptLib, OSADispose, 0x0006, "4");


COMPONENT_CALL(AppleScriptLib, OSASetScriptInfo, 0x0007, "444");


COMPONENT_CALL(AppleScriptLib, OSAGetScriptInfo, 0x0008, "444");


COMPONENT_CALL(AppleScriptLib, OSASetActiveProc, 0x0009, "44");


COMPONENT_CALL(AppleScriptLib, OSAGetActiveProc, 0x000A, "44");


COMPONENT_CALL(AppleScriptLib, OSAScriptingComponentName, 0x0102, "4");


COMPONENT_CALL(AppleScriptLib, OSACompile, 0x0103, "444");


COMPONENT_CALL(AppleScriptLib, OSACopyID, 0x0104, "44");


COMPONENT_CALL(AppleScriptLib, OSAGetSource, 0x0201, "444");


COMPONENT_CALL(AppleScriptLib, OSACoerceFromDesc, 0x0301, "444");


COMPONENT_CALL(AppleScriptLib, OSACoerceToDesc, 0x0302, "4444");


COMPONENT_CALL(AppleScriptLib, OSASetSendProc, 0x0401, "44");


COMPONENT_CALL(AppleScriptLib, OSAGetSendProc, 0x0402, "44");


COMPONENT_CALL(AppleScriptLib, OSASetCreateProc, 0x0403, "44");


COMPONENT_CALL(AppleScriptLib, OSAGetCreateProc, 0x0404, "44");


COMPONENT_CALL(AppleScriptLib, OSASetDefaultTarget, 0x0405, "4");


COMPONENT_CALL(AppleScriptLib, OSAStartRecording, 0x0501, "4");


COMPONENT_CALL(AppleScriptLib, OSAStopRecording, 0x0502, "4");


COMPONENT_CALL(AppleScriptLib, OSALoadExecute, 0x0601, "4444");


COMPONENT_CALL(AppleScriptLib, OSACompileExecute, 0x0602, "4444");


COMPONENT_CALL(AppleScriptLib, OSADoScript, 0x0603, "44444");


COMPONENT_CALL(AppleScriptLib, OSASetCurrentDialect, 0x0701, "2");


COMPONENT_CALL(AppleScriptLib, OSAGetCurrentDialect, 0x0702, "4");


COMPONENT_CALL(AppleScriptLib, OSAAvailableDialects, 0x0703, "4");


COMPONENT_CALL(AppleScriptLib, OSAGetDialectInfo, 0x0704, "244");


COMPONENT_CALL(AppleScriptLib, OSAAvailableDialectCodeList, 0x0705, "4");


COMPONENT_CALL(AppleScriptLib, OSASetResumeDispatchProc, 0x0801, "44");


COMPONENT_CALL(AppleScriptLib, OSAGetResumeDispatchProc, 0x0802, "44");


COMPONENT_CALL(AppleScriptLib, OSAExecuteEvent, 0x0803, "4444");


COMPONENT_CALL(AppleScriptLib, OSADoEvent, 0x0804, "4444");


COMPONENT_CALL(AppleScriptLib, OSAMakeContext, 0x0805, "444");


COMPONENT_CALL(AppleScriptLib, OSAGetDefaultScriptingComponent, 0x1001, "4");


COMPONENT_CALL(AppleScriptLib, OSASetDefaultScriptingComponent, 0x1002, "4");


COMPONENT_CALL(AppleScriptLib, OSAGetScriptingComponent, 0x1003, "44");


COMPONENT_CALL(AppleScriptLib, OSAGetScriptingComponentFromStored, 0x1004, "44");


COMPONENT_CALL(AppleScriptLib, OSAGenericToRealID, 0x1005, "44");


COMPONENT_CALL(AppleScriptLib, OSARealToGenericID, 0x1006, "44");


} /* namespace Executor */