/*****************************************************************************
 *                                                                           *
 *                                   PPC.h                                   *
 *                                   =====                                   *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    Str32, ScriptCode, OSType                               *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME PPC
#include <base/api-module.h>


namespace Executor {

typedef int16_t PPCPortKinds;

typedef int16_t PPCLocationKind;

struct EntityName{GUEST_STRUCT;GUEST<Str32> objStr;GUEST<Str32> typeStr;GUEST<Str32> zoneStr;};void describeStruct(const EntityName&, std::ostream&);

struct PPCXTIAddress{GUEST_STRUCT;GUEST<int16_t> fAddressType;uint8_t fAddress[96];};void describeStruct(const PPCXTIAddress&, std::ostream&);

struct PPCAddrRec{GUEST_STRUCT;uint8_t Reserved[3];uint8_t xtiAddrLen;PPCXTIAddress xtiAddr;};void describeStruct(const PPCAddrRec&, std::ostream&);

struct LocationNameRec{GUEST_STRUCT;GUEST<PPCLocationKind> locationKindSelector;union{EntityName npbEntity;GUEST<Str32> npbType;PPCAddrRec xtiType;} u;};static_assert(sizeof(LocationNameRec) == 104);void describeStruct(const LocationNameRec&, std::ostream&);

struct PPCPortRec{GUEST_STRUCT;GUEST<ScriptCode> nameScript;GUEST<Str32> name;GUEST<PPCPortKinds> portKindsSelector;union{GUEST<Str32> portTypeStr;struct{GUEST<OSType> creator;GUEST<OSType> type;} port;} u;};static_assert(sizeof(PPCPortRec) == 72);void describeStruct(const PPCPortRec&, std::ostream&);

typedef PPCPortRec *PPCPortPtr;

} /* namespace Executor */