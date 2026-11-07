/*****************************************************************************
 *                                                                           *
 *                              InputSprocket.h                              *
 *                              ===============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h          OSType, UInt32, Str63, INTEGER, UInt8, Boolean    *
 *     DriverServices.h    AbsoluteTime, OSStatus                            *
 *     EventMgr.h          EventRecord                                       *
 *     SoundMgr.h          NumVersion                                        *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "DriverServices.h"
#include "EventMgr.h"
#include "SoundMgr.h"

#define MODULE_NAME InputSprocket
#include <base/api-module.h>


namespace Executor {

struct OpaqueISpElementReference;

typedef OpaqueISpElementReference *ISpElementReference;

struct OpaqueISpElementListReference;

typedef OpaqueISpElementListReference *ISpElementListReference;

struct OpaqueISpDeviceReference;

typedef OpaqueISpDeviceReference *ISpDeviceReference;

typedef OSType ISpDeviceClass;

typedef OSType ISpElementKind;

typedef OSType ISpElementLabel;

typedef UInt32 ISpNeedFlagBits;

enum {kISpElementKind_Button = "butn"_4,
kISpElementKind_DPad = "dpad"_4,
kISpElementKind_Axis = "axis"_4,
kISpElementKind_Delta = "dlta"_4,
kISpElementKind_Movement = "move"_4,
kISpElementKind_Virtual = "virt"_4,
};

enum {kISpDeviceClass_SpeechRecognition = "talk"_4,
kISpDeviceClass_Mouse = "mous"_4,
kISpDeviceClass_Keyboard = "keyd"_4,
kISpDeviceClass_Joystick = "joys"_4,
kISpDeviceClass_Gamepad = "gmpd"_4,
kISpDeviceClass_Wheel = "whel"_4,
kISpDeviceClass_Pedals = "pedl"_4,
kISpDeviceClass_Levers = "levr"_4,
kISpDeviceClass_Tickle = "tckl"_4,
kISpDeviceClass_Unknown = "????"_4,
};

enum {kISpButtonUp = 0,
kISpButtonDown = 1,
kISpPadIdle = 0,
kISpAxisMinimum = 0,
kISpAxisMiddle = 2147483647,
};

enum {kISpInternalErr = -30420,
kISpSystemListErr = -30421,
kISpBufferToSmallErr = -30422,
kISpElementInListErr = -30423,
kISpElementNotInListErr = -30424,
kISpSystemInactiveErr = -30425,
kISpDeviceInactiveErr = -30426,
kISpSystemActiveErr = -30427,
kISpDeviceActiveErr = -30428,
kISpListBusyErr = -30429,
};

struct ISpNeed{GUEST_STRUCT;GUEST<Str63> name;GUEST<INTEGER> iconSuiteResourceId;UInt8 playerNum;UInt8 group;GUEST<ISpElementKind> theKind;GUEST<ISpElementLabel> theLabel;GUEST<ISpNeedFlagBits> flags;GUEST<UInt32> reserved1;GUEST<UInt32> reserved2;GUEST<UInt32> reserved3;};static_assert(sizeof(ISpNeed) == 92);void describeStruct(const ISpNeed&, std::ostream&);

struct ISpElementEvent{GUEST_STRUCT;AbsoluteTime when;GUEST<ISpElementReference> element;GUEST<UInt32> refCon;GUEST<UInt32> data;};static_assert(sizeof(ISpElementEvent) == 20);void describeStruct(const ISpElementEvent&, std::ostream&);

typedef ISpElementEvent *ISpElementEventPtr;

using ISpEventProcPtr = UPP<Boolean(EventRecord *inEvent), callconv::CCall>;


NumVersion C_ISpGetVersion();LIBRARY_FUNCTION(InputSprocketLib, ISpGetVersion, C_ISpGetVersion);


OSStatus C_ISpStartup();LIBRARY_FUNCTION(InputSprocketLib, ISpStartup, C_ISpStartup);


OSStatus C_ISpShutdown();LIBRARY_FUNCTION(InputSprocketLib, ISpShutdown, C_ISpShutdown);


OSStatus C_ISpTickle();LIBRARY_FUNCTION(InputSprocketLib, ISpTickle, C_ISpTickle);


OSStatus C_ISpInit(UInt32 count, ISpNeed *needs, GUEST<ISpElementReference> *inReferences, OSType appCreatorCode, OSType subCreatorCode, UInt32 flags, INTEGER setListResourceId, UInt32 reserved);LIBRARY_FUNCTION(InputSprocketLib, ISpInit, C_ISpInit);


OSStatus C_ISpConfigure(ISpEventProcPtr inEventProcPtr, void *inReserved);LIBRARY_FUNCTION(InputSprocketLib, ISpConfigure, C_ISpConfigure);


OSStatus C_ISpStop();LIBRARY_FUNCTION(InputSprocketLib, ISpStop, C_ISpStop);


OSStatus C_ISpSuspend();LIBRARY_FUNCTION(InputSprocketLib, ISpSuspend, C_ISpSuspend);


OSStatus C_ISpResume();LIBRARY_FUNCTION(InputSprocketLib, ISpResume, C_ISpResume);


OSStatus C_ISpDevices_Extract(UInt32 inBufferCount, GUEST<UInt32> *outCount, GUEST<ISpDeviceReference> *buffer);LIBRARY_FUNCTION(InputSprocketLib, ISpDevices_Extract, C_ISpDevices_Extract);


OSStatus C_ISpDevices_ExtractByClass(ISpDeviceClass inClass, UInt32 inBufferCount, GUEST<UInt32> *outCount, GUEST<ISpDeviceReference> *buffer);LIBRARY_FUNCTION(InputSprocketLib, ISpDevices_ExtractByClass, C_ISpDevices_ExtractByClass);


OSStatus C_ISpDevices_ActivateClass(ISpDeviceClass inClass);LIBRARY_FUNCTION(InputSprocketLib, ISpDevices_ActivateClass, C_ISpDevices_ActivateClass);


OSStatus C_ISpDevices_DeactivateClass(ISpDeviceClass inClass);LIBRARY_FUNCTION(InputSprocketLib, ISpDevices_DeactivateClass, C_ISpDevices_DeactivateClass);


OSStatus C_ISpDevices_Activate(UInt32 inDeviceCount, GUEST<ISpDeviceReference> *inDevicesToActivate);LIBRARY_FUNCTION(InputSprocketLib, ISpDevices_Activate, C_ISpDevices_Activate);


OSStatus C_ISpDevices_Deactivate(UInt32 inDeviceCount, GUEST<ISpDeviceReference> *inDevicesToDeactivate);LIBRARY_FUNCTION(InputSprocketLib, ISpDevices_Deactivate, C_ISpDevices_Deactivate);


OSStatus C_ISpElement_NewVirtual(UInt32 dataSize, GUEST<ISpElementReference> *outElement, UInt32 flags);LIBRARY_FUNCTION(InputSprocketLib, ISpElement_NewVirtual, C_ISpElement_NewVirtual);


OSStatus C_ISpElement_NewVirtualFromNeeds(UInt32 count, ISpNeed *needs, GUEST<ISpElementReference> *outElements, UInt32 flags);LIBRARY_FUNCTION(InputSprocketLib, ISpElement_NewVirtualFromNeeds, C_ISpElement_NewVirtualFromNeeds);


OSStatus C_ISpElement_DisposeVirtual(UInt32 count, GUEST<ISpElementReference> *inElements);LIBRARY_FUNCTION(InputSprocketLib, ISpElement_DisposeVirtual, C_ISpElement_DisposeVirtual);


OSStatus C_ISpElement_GetSimpleState(ISpElementReference inElement, GUEST<UInt32> *outState);LIBRARY_FUNCTION(InputSprocketLib, ISpElement_GetSimpleState, C_ISpElement_GetSimpleState);


OSStatus C_ISpElement_GetComplexState(ISpElementReference inElement, UInt32 bufferSize, void *outState);LIBRARY_FUNCTION(InputSprocketLib, ISpElement_GetComplexState, C_ISpElement_GetComplexState);


OSStatus C_ISpElement_GetNextEvent(ISpElementReference inElement, UInt32 bufSize, ISpElementEventPtr event, Boolean *wasEvent);LIBRARY_FUNCTION(InputSprocketLib, ISpElement_GetNextEvent, C_ISpElement_GetNextEvent);


OSStatus C_ISpElement_Flush(ISpElementReference inElement);LIBRARY_FUNCTION(InputSprocketLib, ISpElement_Flush, C_ISpElement_Flush);


OSStatus C_ISpElementList_New(UInt32 inCount, GUEST<ISpElementReference> *inElements, GUEST<ISpElementListReference> *outElementList, UInt32 flags);LIBRARY_FUNCTION(InputSprocketLib, ISpElementList_New, C_ISpElementList_New);


OSStatus C_ISpElementList_Dispose(ISpElementListReference inElementList);LIBRARY_FUNCTION(InputSprocketLib, ISpElementList_Dispose, C_ISpElementList_Dispose);


OSStatus C_ISpGetGlobalElementList(GUEST<ISpElementListReference> *outElementList);LIBRARY_FUNCTION(InputSprocketLib, ISpGetGlobalElementList, C_ISpGetGlobalElementList);


OSStatus C_ISpElementList_AddElements(ISpElementListReference inElementList, UInt32 refCon, UInt32 count, GUEST<ISpElementReference> *newElements);LIBRARY_FUNCTION(InputSprocketLib, ISpElementList_AddElements, C_ISpElementList_AddElements);


OSStatus C_ISpElementList_RemoveElements(ISpElementListReference inElementList, UInt32 count, GUEST<ISpElementReference> *oldElement);LIBRARY_FUNCTION(InputSprocketLib, ISpElementList_RemoveElements, C_ISpElementList_RemoveElements);


OSStatus C_ISpElementList_GetNextEvent(ISpElementListReference inElementList, UInt32 bufSize, ISpElementEventPtr event, Boolean *wasEvent);LIBRARY_FUNCTION(InputSprocketLib, ISpElementList_GetNextEvent, C_ISpElementList_GetNextEvent);


OSStatus C_ISpElementList_Flush(ISpElementListReference inElementList);LIBRARY_FUNCTION(InputSprocketLib, ISpElementList_Flush, C_ISpElementList_Flush);


} /* namespace Executor */