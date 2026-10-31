/*****************************************************************************
 *                                                                           *
 *                                Components.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     LONGINT, OSType, INTEGER, Handle, OSErr                *
 *     MixedMode.h    UniversalProcPtr                                       *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "MixedMode.h"

#define MODULE_NAME Components
#include <base/api-module.h>


namespace Executor {

struct ComponentRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};static_assert(sizeof(ComponentRecord) == 4);void describeStruct(const ComponentRecord&, std::ostream&);

typedef ComponentRecord *Component;

struct ComponentInstanceRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};static_assert(sizeof(ComponentInstanceRecord) == 4);void describeStruct(const ComponentInstanceRecord&, std::ostream&);

typedef ComponentInstanceRecord *ComponentInstance;

typedef LONGINT ComponentResult;

struct ComponentDescription{GUEST_STRUCT;GUEST<OSType> componentType;GUEST<OSType> componentSubType;GUEST<OSType> componentManufacturer;GUEST<uint32_t> componentFlags;GUEST<uint32_t> componentFlagsMask;};static_assert(sizeof(ComponentDescription) == 20);void describeStruct(const ComponentDescription&, std::ostream&);

struct ResourceSpec{GUEST_STRUCT;GUEST<OSType> resType;GUEST<INTEGER> resID;};static_assert(sizeof(ResourceSpec) == 6);void describeStruct(const ResourceSpec&, std::ostream&);

struct ComponentResource{GUEST_STRUCT;ComponentDescription cd;ResourceSpec component;ResourceSpec componentName;ResourceSpec componentInfo;ResourceSpec componentIcon;};static_assert(sizeof(ComponentResource) == 44);void describeStruct(const ComponentResource&, std::ostream&);

typedef ComponentResource *ComponentResourcePtr;

typedef GUEST<ComponentResourcePtr> *ComponentResourceHandle;

struct ComponentParameters{GUEST_STRUCT;uint8_t flags;uint8_t paramSize;GUEST<INTEGER> what;GUEST<LONGINT> params[1];};static_assert(sizeof(ComponentParameters) == 8);void describeStruct(const ComponentParameters&, std::ostream&);

void *data_CallComponentUPP(); DATA_EXPORT(InterfaceLib, "InterfaceLib", CallComponentUPP, data_CallComponentUPP);


ComponentResult C_CallComponentFunction(ComponentParameters *params, UniversalProcPtr func);LIBRARY_FUNCTION(InterfaceLib, CallComponentFunction, C_CallComponentFunction);


ComponentResult C_CallComponentFunctionWithStorage(Handle storage, ComponentParameters *params, UniversalProcPtr func);LIBRARY_FUNCTION(InterfaceLib, CallComponentFunctionWithStorage, C_CallComponentFunctionWithStorage);


using ComponentRoutineUPP = UPP<ComponentResult(ComponentParameters *cp, Handle componentStorage)>;


enum {kAnyComponentType = 0,
kAnyComponentSubType = 0,
kAnyComponentManufacturer = 0,
kAnyComponentFlagsMask = 0,
cmpWantsRegisterMessage = 2147483648,
registerComponentGlobal = 1,
registerComponentNoDuplicates = 2,
registerComponentAfterExisting = 4,
componentDoAutoVersion = 1,
componentWantsUnregister = 2,
componentAutoVersionIncludeFlags = 4,
componentHasMultiplePlatforms = 8,
defaultComponentIdentical = 0,
defaultComponentAnyFlags = 1,
defaultComponentAnyManufacturer = 2,
defaultComponentAnySubType = 4,
};

enum {kComponentOpenSelect = -1,
kComponentCloseSelect = -2,
kComponentCanDoSelect = -3,
kComponentVersionSelect = -4,
kComponentRegisterSelect = -5,
kComponentTargetSelect = -6,
kComponentUnregisterSelect = -7,
};

enum {invalidComponentID = -3000,
validInstancesExist = -3001,
componentNotCaptured = -3002,
componentDontRegister = -3003,
unresolvedComponentDLLErr = -3004,
};

enum {badComponentInstance = 2147516417,
badComponentSelector = 2147516418,
};

DISPATCHER_TRAP(ComponentDispatch, 0xA82A, D0W);


Component C_RegisterComponent(ComponentDescription *cd, ComponentRoutineUPP componentEntryPoint, INTEGER global, Handle componentName, Handle componentInfo, Handle componentIcon);PASCAL_SUBTRAP(RegisterComponent, 0xA82A, 0x0001, ComponentDispatch);


OSErr C_UnregisterComponent(Component aComponent);PASCAL_SUBTRAP(UnregisterComponent, 0xA82A, 0x0002, ComponentDispatch);


LONGINT C_CountComponents(ComponentDescription *looking);PASCAL_SUBTRAP(CountComponents, 0xA82A, 0x0003, ComponentDispatch);


Component C_FindNextComponent(Component aComponent, ComponentDescription *looking);PASCAL_SUBTRAP(FindNextComponent, 0xA82A, 0x0004, ComponentDispatch);


OSErr C_GetComponentInfo(Component aComponent, ComponentDescription *cd, Handle componentName, Handle componentInfo, Handle componentIcon);PASCAL_SUBTRAP(GetComponentInfo, 0xA82A, 0x0005, ComponentDispatch);


LONGINT C_GetComponentListModSeed();PASCAL_SUBTRAP(GetComponentListModSeed, 0xA82A, 0x0006, ComponentDispatch);


ComponentInstance C_OpenComponent(Component aComponent);PASCAL_SUBTRAP(OpenComponent, 0xA82A, 0x0007, ComponentDispatch);


OSErr C_CloseComponent(ComponentInstance aComponentInstance);PASCAL_SUBTRAP(CloseComponent, 0xA82A, 0x0008, ComponentDispatch);


OSErr C_GetComponentInstanceError(ComponentInstance aComponentInstance);PASCAL_SUBTRAP(GetComponentInstanceError, 0xA82A, 0x000A, ComponentDispatch);


void C_SetComponentInstanceError(ComponentInstance aComponentInstance, OSErr theError);PASCAL_SUBTRAP(SetComponentInstanceError, 0xA82A, 0x000B, ComponentDispatch);


Handle C_GetComponentInstanceStorage(ComponentInstance aComponentInstance);PASCAL_SUBTRAP(GetComponentInstanceStorage, 0xA82A, 0x000C, ComponentDispatch);


void C_SetComponentInstanceStorage(ComponentInstance aComponentInstance, Handle theStorage);PASCAL_SUBTRAP(SetComponentInstanceStorage, 0xA82A, 0x000D, ComponentDispatch);


LONGINT C_GetComponentInstanceA5(ComponentInstance aComponentInstance);PASCAL_SUBTRAP(GetComponentInstanceA5, 0xA82A, 0x000E, ComponentDispatch);


void C_SetComponentInstanceA5(ComponentInstance aComponentInstance, LONGINT theA5);PASCAL_SUBTRAP(SetComponentInstanceA5, 0xA82A, 0x000F, ComponentDispatch);


LONGINT C_GetComponentRefcon(Component aComponent);PASCAL_SUBTRAP(GetComponentRefcon, 0xA82A, 0x0010, ComponentDispatch);


void C_SetComponentRefcon(Component aComponent, LONGINT theRefcon);PASCAL_SUBTRAP(SetComponentRefcon, 0xA82A, 0x0011, ComponentDispatch);


Component C_RegisterComponentResource(ComponentResourceHandle cr, INTEGER global);PASCAL_SUBTRAP(RegisterComponentResource, 0xA82A, 0x0012, ComponentDispatch);


LONGINT C_CountComponentInstances(Component aComponent);PASCAL_SUBTRAP(CountComponentInstances, 0xA82A, 0x0013, ComponentDispatch);


LONGINT C_RegisterComponentResourceFile(INTEGER resRefNum, INTEGER global);PASCAL_SUBTRAP(RegisterComponentResourceFile, 0xA82A, 0x0014, ComponentDispatch);


INTEGER C_OpenComponentResFile(Component aComponent);PASCAL_SUBTRAP(OpenComponentResFile, 0xA82A, 0x0015, ComponentDispatch);


OSErr C_CloseComponentResFile(INTEGER refnum);PASCAL_SUBTRAP(CloseComponentResFile, 0xA82A, 0x0018, ComponentDispatch);


Component C_CaptureComponent(Component capturedComponent, Component capturingComponent);PASCAL_SUBTRAP(CaptureComponent, 0xA82A, 0x001C, ComponentDispatch);


OSErr C_UncaptureComponent(Component aComponent);PASCAL_SUBTRAP(UncaptureComponent, 0xA82A, 0x001D, ComponentDispatch);


OSErr C_SetDefaultComponent(Component aComponent, INTEGER flags);PASCAL_SUBTRAP(SetDefaultComponent, 0xA82A, 0x001E, ComponentDispatch);


Component C_ResolveComponentAlias(Component aComponent);PASCAL_SUBTRAP(ResolveComponentAlias, 0xA82A, 0x0020, ComponentDispatch);


ComponentInstance C_OpenDefaultComponent(OSType componentType, OSType componentSubType);PASCAL_SUBTRAP(OpenDefaultComponent, 0xA82A, 0x0021, ComponentDispatch);


ComponentResult C_DelegateComponentCall(ComponentParameters *originalParams, ComponentInstance ci);PASCAL_SUBTRAP(DelegateComponentCall, 0xA82A, 0x0024, ComponentDispatch);


LONGINT C_GetComponentTypeModSeed(OSType componentType);PASCAL_SUBTRAP(GetComponentTypeModSeed, 0xA82A, 0x002C, ComponentDispatch);


OSErr C_OpenAComponent(Component aComponent, GUEST<ComponentInstance> *ci);PASCAL_SUBTRAP(OpenAComponent, 0xA82A, 0x002D, ComponentDispatch);


OSErr C_OpenADefaultComponent(OSType componentType, OSType componentSubType, GUEST<ComponentInstance> *ci);PASCAL_SUBTRAP(OpenADefaultComponent, 0xA82A, 0x002E, ComponentDispatch);


OSErr C_OpenAComponentResFile(Component aComponent, GUEST<INTEGER> *resRef);PASCAL_SUBTRAP(OpenAComponentResFile, 0xA82A, 0x002F, ComponentDispatch);


OSErr C_GetComponentResource(Component aComponent, OSType resType, INTEGER resID, GUEST<Handle> *theResource);PASCAL_SUBTRAP(GetComponentResource, 0xA82A, 0x0035, ComponentDispatch);


// MacPhoenix: private; Gestalt 'cpnt' on 7.5.5 calls it. Name and value
// guessed (3.0).
LONGINT C_GetComponentMgrVersion();PASCAL_SUBTRAP(GetComponentMgrVersion, 0xA82A, 0x0019, ComponentDispatch);


} /* namespace Executor */