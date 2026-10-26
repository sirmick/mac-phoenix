/*****************************************************************************
 *                                                                           *
 *                               NameRegistry.h                              *
 *                               ==============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h          UInt32, Boolean                                   *
 *     DriverServices.h    OSStatus                                          *
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

#define MODULE_NAME NameRegistry
#include <base/api-module.h>


namespace Executor {

// Identifies one entry in the Name Registry.  Opaque: copy the whole
// structure, and dispose of IDs the Registry hands out with
// RegistryEntryIDDispose.
struct RegEntryID{GUEST_STRUCT;GUEST<UInt32> contents[4];};static_assert(sizeof(RegEntryID) == 16);void describeStruct(const RegEntryID&, std::ostream&);

typedef RegEntryID *RegEntryIDPtr;

// Property names are C strings.
typedef char RegPropertyName;

typedef RegPropertyName *RegPropertyNamePtr;

typedef UInt32 RegPropertyValueSize;

struct RegEntryIter{GUEST_STRUCT;GUEST<void*> opaque;};static_assert(sizeof(RegEntryIter) == 4);void describeStruct(const RegEntryIter&, std::ostream&);

typedef RegEntryIter *RegEntryIterPtr;

typedef UInt32 RegIterationOp;

typedef RegIterationOp RegEntryIterationOp;

enum {kRegIterContinue = 1, // keep going in the same direction
kRegIterRoot = 2,
kRegIterParents = 3,
kRegIterChildren = 4,
kRegIterDescendants = 5, // the whole subtree; a new iterator starts here, at the root
kRegIterSibling = 6,
};

void RegistryEntryIDDispose(RegEntryID *id);

OSStatus RegistryEntryIterateCreate(RegEntryIter *cookie);

void RegistryEntryIterateDispose(RegEntryIter *cookie);

// Each entry returned in foundEntry must be disposed of with
// RegistryEntryIDDispose.
OSStatus RegistryEntryIterate(RegEntryIter *cookie, RegEntryIterationOp relationship, RegEntryID *foundEntry, Boolean *done);

// Fails if the property already exists; use RegistryPropertySet to
// change the value of an existing one.
OSStatus RegistryPropertyCreate(const RegEntryID *entryID, const RegPropertyName *propertyName, const void *propertyValue, RegPropertyValueSize propertySize);

OSStatus RegistryPropertyGetSize(const RegEntryID *entryID, const RegPropertyName *propertyName, GUEST<RegPropertyValueSize> *propertySize);

// propertySize is the buffer size on entry and the value's size on
// return.
OSStatus RegistryPropertyGet(const RegEntryID *entryID, const RegPropertyName *propertyName, void *propertyValue, GUEST<RegPropertyValueSize> *propertySize);

OSStatus RegistryPropertySet(const RegEntryID *entryID, const RegPropertyName *propertyName, const void *propertyValue, RegPropertyValueSize propertySize);

} /* namespace Executor */