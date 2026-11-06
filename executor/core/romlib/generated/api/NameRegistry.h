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

typedef UInt32 RegPathNameSize;

typedef char RegCStrPathName;

typedef void *RegPropertyIter;

enum {nrInvalidNodeErr = -2538, // not an entry (an ID from RegistryEntryIDInit, one disposed of)
nrNotFoundErr = -2539, // no such property
nrDataTruncatedErr = -2543,
nrPathNotFound = -2550,
nrPathBufferTooSmall = -2551,
};

// Not an entry yet: compares equal to another such ID.
OSStatus C_RegistryEntryIDInit(RegEntryID *id);LIBRARY_FUNCTION(NameRegistryLib, RegistryEntryIDInit, C_RegistryEntryIDInit);


Boolean C_RegistryEntryIDCompare(const RegEntryID *id1, const RegEntryID *id2);LIBRARY_FUNCTION(NameRegistryLib, RegistryEntryIDCompare, C_RegistryEntryIDCompare);


OSStatus C_RegistryEntryIDCopy(const RegEntryID *src, RegEntryID *dst);LIBRARY_FUNCTION(NameRegistryLib, RegistryEntryIDCopy, C_RegistryEntryIDCopy);


// Clears the ID.
OSStatus C_RegistryEntryIDDispose(RegEntryID *id);LIBRARY_FUNCTION(NameRegistryLib, RegistryEntryIDDispose, C_RegistryEntryIDDispose);


// An iterator at the root.
OSStatus C_RegistryEntryIterateCreate(RegEntryIter *cookie);LIBRARY_FUNCTION(NameRegistryLib, RegistryEntryIterateCreate, C_RegistryEntryIterateCreate);


OSStatus C_RegistryEntryIterateDispose(RegEntryIter *cookie);LIBRARY_FUNCTION(NameRegistryLib, RegistryEntryIterateDispose, C_RegistryEntryIterateDispose);


OSStatus C_RegistryEntryIterateSet(RegEntryIter *cookie, const RegEntryID *startEntryID);LIBRARY_FUNCTION(NameRegistryLib, RegistryEntryIterateSet, C_RegistryEntryIterateSet);


// Each entry returned in foundEntry must be disposed of with
// RegistryEntryIDDispose.  No more entries: done true, noErr.
OSStatus C_RegistryEntryIterate(RegEntryIter *cookie, RegEntryIterationOp relationship, RegEntryID *foundEntry, Boolean *done);LIBRARY_FUNCTION(NameRegistryLib, RegistryEntryIterate, C_RegistryEntryIterate);


// As RegistryEntryIterate, entries with the property only (and that
// value, when propertyValue isn't nil).
OSStatus C_RegistryEntrySearch(RegEntryIter *cookie, RegEntryIterationOp relationship, RegEntryID *foundEntry, Boolean *done, const RegPropertyName *propertyName, const void *propertyValue, RegPropertyValueSize propertySize);LIBRARY_FUNCTION(NameRegistryLib, RegistryEntrySearch, C_RegistryEntrySearch);


// The path's length with its terminating zero.
OSStatus C_RegistryEntryToPathSize(const RegEntryID *entryID, GUEST<RegPathNameSize> *pathSize);LIBRARY_FUNCTION(NameRegistryLib, RegistryEntryToPathSize, C_RegistryEntryToPathSize);


OSStatus C_RegistryCStrEntryToPath(const RegEntryID *entryID, RegCStrPathName *pathName, RegPathNameSize pathSize);LIBRARY_FUNCTION(NameRegistryLib, RegistryCStrEntryToPath, C_RegistryCStrEntryToPath);


// A path of entry names separated by colons, from searchPointID (nil:
// the root).
OSStatus C_RegistryCStrEntryLookup(const RegEntryID *searchPointID, const RegCStrPathName *pathName, RegEntryID *foundEntry);LIBRARY_FUNCTION(NameRegistryLib, RegistryCStrEntryLookup, C_RegistryCStrEntryLookup);


OSStatus C_RegistryPropertyGetSize(const RegEntryID *entryID, const RegPropertyName *propertyName, GUEST<RegPropertyValueSize> *propertySize);LIBRARY_FUNCTION(NameRegistryLib, RegistryPropertyGetSize, C_RegistryPropertyGetSize);


// propertySize is the buffer size on entry and the value's size on
// return.
OSStatus C_RegistryPropertyGet(const RegEntryID *entryID, const RegPropertyName *propertyName, void *propertyValue, GUEST<RegPropertyValueSize> *propertySize);LIBRARY_FUNCTION(NameRegistryLib, RegistryPropertyGet, C_RegistryPropertyGet);


OSStatus C_RegistryPropertyIterateCreate(const RegEntryID *entry, GUEST<RegPropertyIter> *cookie);LIBRARY_FUNCTION(NameRegistryLib, RegistryPropertyIterateCreate, C_RegistryPropertyIterateCreate);


OSStatus C_RegistryPropertyIterateDispose(GUEST<RegPropertyIter> *cookie);LIBRARY_FUNCTION(NameRegistryLib, RegistryPropertyIterateDispose, C_RegistryPropertyIterateDispose);


// foundProperty: a buffer of 32 bytes (a property name's longest, with
// its zero).
OSStatus C_RegistryPropertyIterate(GUEST<RegPropertyIter> *cookie, RegPropertyName *foundProperty, Boolean *done);LIBRARY_FUNCTION(NameRegistryLib, RegistryPropertyIterate, C_RegistryPropertyIterate);


// Fails if the property already exists; use RegistryPropertySet to
// change the value of an existing one.
OSStatus RegistryPropertyCreate(const RegEntryID *entryID, const RegPropertyName *propertyName, const void *propertyValue, RegPropertyValueSize propertySize);

OSStatus RegistryPropertySet(const RegEntryID *entryID, const RegPropertyName *propertyName, const void *propertyValue, RegPropertyValueSize propertySize);

} /* namespace Executor */