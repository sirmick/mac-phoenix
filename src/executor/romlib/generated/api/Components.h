/*****************************************************************************
 *                                                                           *
 *                                Components.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    LONGINT, OSType, OSErr                                  *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME Components
#include <base/api-module.h>


namespace Executor {

struct ComponentRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};static_assert(sizeof(ComponentRecord) == 4);void describeStruct(const ComponentRecord&, std::ostream&);

typedef ComponentRecord *Component;

struct ComponentInstanceRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};static_assert(sizeof(ComponentInstanceRecord) == 4);void describeStruct(const ComponentInstanceRecord&, std::ostream&);

typedef ComponentInstanceRecord *ComponentInstance;

ComponentInstance glue_OpenDefaultComponent(OSType type, OSType subType);LIBRARY_FUNCTION(InterfaceLib, OpenDefaultComponent, glue_OpenDefaultComponent);


ComponentInstance glue_OpenComponent(Component c);LIBRARY_FUNCTION(InterfaceLib, OpenComponent, glue_OpenComponent);


OSErr glue_CloseComponent(ComponentInstance ci);LIBRARY_FUNCTION(InterfaceLib, CloseComponent, glue_CloseComponent);


} /* namespace Executor */