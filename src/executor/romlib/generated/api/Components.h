/*****************************************************************************
 *                                                                           *
 *                                Components.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    LONGINT                                                 *
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

} /* namespace Executor */