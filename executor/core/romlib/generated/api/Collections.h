/*****************************************************************************
 *                                                                           *
 *                               Collections.h                               *
 *                               =============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    OSType, OSErr, LONGINT, Boolean, INTEGER, Handle        *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME Collections
#include <base/api-module.h>


namespace Executor {

// The selector in D0 (TWOWORDINLINE(0x70nn, 0xABF6): moveq #nn, D0),
// Pascal arguments on the stack.
DISPATCHER_TRAP(CollectionMgr, 0xABF6, D0<0xFF>);


struct OpaqueCollection;

typedef OpaqueCollection *Collection;

typedef OSType CollectionTag;

enum {kCollectionDontWantTag = 0,
kCollectionDontWantId = 0,
kCollectionDontWantSize = 0,
kCollectionDontWantAttributes = 0,
kCollectionDontWantIndex = 0,
kCollectionDontWantData = 0,
};

enum {kCollectionNoAttributes = 0,
kCollectionAllAttributes = -1,
kCollectionUserAttributes = 65535,
kCollectionDefaultAttributes = 1073741824,
};

enum {kCollectionPersistenceBit = 30,
kCollectionLockBit = 31,
};

enum {kCollectionPersistenceMask = 1073741824,
kCollectionLockMask = -2147483648,
};

enum {collectionItemLockedErr = -5750,
collectionItemNotFoundErr = -5751,
collectionIndexRangeErr = -5752,
collectionVersionErr = -5753,
};

using CollectionFlattenUPP = UPP<OSErr(LONGINT size, void *data, void *refCon)>;


using CollectionExceptionUPP = UPP<OSErr(Collection c, OSErr status)>;


Collection C_NewCollection();PASCAL_SUBTRAP(NewCollection, 0xABF6, 0x0000, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, NewCollection, InterfaceLib);


void C_DisposeCollection(Collection c);PASCAL_SUBTRAP(DisposeCollection, 0xABF6, 0x0001, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, DisposeCollection, InterfaceLib);


Collection C_CloneCollection(Collection c);PASCAL_SUBTRAP(CloneCollection, 0xABF6, 0x0002, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, CloneCollection, InterfaceLib);


LONGINT C_CountCollectionOwners(Collection c);PASCAL_SUBTRAP(CountCollectionOwners, 0xABF6, 0x0003, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, CountCollectionOwners, InterfaceLib);


Collection C_CopyCollection(Collection srcCollection, Collection dstCollection);PASCAL_SUBTRAP(CopyCollection, 0xABF6, 0x0004, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, CopyCollection, InterfaceLib);


LONGINT C_GetCollectionDefaultAttributes(Collection c);PASCAL_SUBTRAP(GetCollectionDefaultAttributes, 0xABF6, 0x0005, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetCollectionDefaultAttributes, InterfaceLib);


void C_SetCollectionDefaultAttributes(Collection c, LONGINT whichAttributes, LONGINT newAttributes);PASCAL_SUBTRAP(SetCollectionDefaultAttributes, 0xABF6, 0x0006, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, SetCollectionDefaultAttributes, InterfaceLib);


LONGINT C_CountCollectionItems(Collection c);PASCAL_SUBTRAP(CountCollectionItems, 0xABF6, 0x0007, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, CountCollectionItems, InterfaceLib);


OSErr C_AddCollectionItem(Collection c, CollectionTag tag, LONGINT id, LONGINT itemSize, const void *itemData);PASCAL_SUBTRAP(AddCollectionItem, 0xABF6, 0x0008, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, AddCollectionItem, InterfaceLib);


OSErr C_GetCollectionItem(Collection c, CollectionTag tag, LONGINT id, GUEST<LONGINT> *itemSize, void *itemData);PASCAL_SUBTRAP(GetCollectionItem, 0xABF6, 0x0009, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetCollectionItem, InterfaceLib);


OSErr C_RemoveCollectionItem(Collection c, CollectionTag tag, LONGINT id);PASCAL_SUBTRAP(RemoveCollectionItem, 0xABF6, 0x000A, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, RemoveCollectionItem, InterfaceLib);


OSErr C_SetCollectionItemInfo(Collection c, CollectionTag tag, LONGINT id, LONGINT whichAttributes, LONGINT newAttributes);PASCAL_SUBTRAP(SetCollectionItemInfo, 0xABF6, 0x000B, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, SetCollectionItemInfo, InterfaceLib);


OSErr C_GetCollectionItemInfo(Collection c, CollectionTag tag, LONGINT id, GUEST<LONGINT> *index, GUEST<LONGINT> *itemSize, GUEST<LONGINT> *attributes);PASCAL_SUBTRAP(GetCollectionItemInfo, 0xABF6, 0x000C, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetCollectionItemInfo, InterfaceLib);


OSErr C_ReplaceIndexedCollectionItem(Collection c, LONGINT index, LONGINT itemSize, const void *itemData);PASCAL_SUBTRAP(ReplaceIndexedCollectionItem, 0xABF6, 0x000D, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, ReplaceIndexedCollectionItem, InterfaceLib);


OSErr C_GetIndexedCollectionItem(Collection c, LONGINT index, GUEST<LONGINT> *itemSize, void *itemData);PASCAL_SUBTRAP(GetIndexedCollectionItem, 0xABF6, 0x000E, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetIndexedCollectionItem, InterfaceLib);


OSErr C_RemoveIndexedCollectionItem(Collection c, LONGINT index);PASCAL_SUBTRAP(RemoveIndexedCollectionItem, 0xABF6, 0x000F, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, RemoveIndexedCollectionItem, InterfaceLib);


OSErr C_SetIndexedCollectionItemInfo(Collection c, LONGINT index, LONGINT whichAttributes, LONGINT newAttributes);PASCAL_SUBTRAP(SetIndexedCollectionItemInfo, 0xABF6, 0x0010, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, SetIndexedCollectionItemInfo, InterfaceLib);


OSErr C_GetIndexedCollectionItemInfo(Collection c, LONGINT index, GUEST<CollectionTag> *tag, GUEST<LONGINT> *id, GUEST<LONGINT> *itemSize, GUEST<LONGINT> *attributes);PASCAL_SUBTRAP(GetIndexedCollectionItemInfo, 0xABF6, 0x0011, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetIndexedCollectionItemInfo, InterfaceLib);


Boolean C_CollectionTagExists(Collection c, CollectionTag tag);PASCAL_SUBTRAP(CollectionTagExists, 0xABF6, 0x0012, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, CollectionTagExists, InterfaceLib);


LONGINT C_CountCollectionTags(Collection c);PASCAL_SUBTRAP(CountCollectionTags, 0xABF6, 0x0013, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, CountCollectionTags, InterfaceLib);


OSErr C_GetIndexedCollectionTag(Collection c, LONGINT tagIndex, GUEST<CollectionTag> *tag);PASCAL_SUBTRAP(GetIndexedCollectionTag, 0xABF6, 0x0014, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetIndexedCollectionTag, InterfaceLib);


LONGINT C_CountTaggedCollectionItems(Collection c, CollectionTag tag);PASCAL_SUBTRAP(CountTaggedCollectionItems, 0xABF6, 0x0015, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, CountTaggedCollectionItems, InterfaceLib);


OSErr C_GetTaggedCollectionItem(Collection c, CollectionTag tag, LONGINT whichItem, GUEST<LONGINT> *itemSize, void *itemData);PASCAL_SUBTRAP(GetTaggedCollectionItem, 0xABF6, 0x0016, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetTaggedCollectionItem, InterfaceLib);


OSErr C_GetTaggedCollectionItemInfo(Collection c, CollectionTag tag, LONGINT whichItem, GUEST<LONGINT> *id, GUEST<LONGINT> *index, GUEST<LONGINT> *itemSize, GUEST<LONGINT> *attributes);PASCAL_SUBTRAP(GetTaggedCollectionItemInfo, 0xABF6, 0x0017, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetTaggedCollectionItemInfo, InterfaceLib);


void C_PurgeCollection(Collection c, LONGINT whichAttributes, LONGINT matchingAttributes);PASCAL_SUBTRAP(PurgeCollection, 0xABF6, 0x0018, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, PurgeCollection, InterfaceLib);


void C_PurgeCollectionTag(Collection c, CollectionTag tag);PASCAL_SUBTRAP(PurgeCollectionTag, 0xABF6, 0x0019, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, PurgeCollectionTag, InterfaceLib);


void C_EmptyCollection(Collection c);PASCAL_SUBTRAP(EmptyCollection, 0xABF6, 0x001A, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, EmptyCollection, InterfaceLib);


OSErr C_FlattenCollection(Collection c, CollectionFlattenUPP flattenProc, void *refCon);PASCAL_SUBTRAP(FlattenCollection, 0xABF6, 0x001B, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, FlattenCollection, InterfaceLib);


OSErr C_FlattenPartialCollection(Collection c, CollectionFlattenUPP flattenProc, void *refCon, LONGINT whichAttributes, LONGINT matchingAttributes);PASCAL_SUBTRAP(FlattenPartialCollection, 0xABF6, 0x001C, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, FlattenPartialCollection, InterfaceLib);


OSErr C_UnflattenCollection(Collection c, CollectionFlattenUPP flattenProc, void *refCon);PASCAL_SUBTRAP(UnflattenCollection, 0xABF6, 0x001D, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, UnflattenCollection, InterfaceLib);


CollectionExceptionUPP C_GetCollectionExceptionProc(Collection c);PASCAL_SUBTRAP(GetCollectionExceptionProc, 0xABF6, 0x001E, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetCollectionExceptionProc, InterfaceLib);


void C_SetCollectionExceptionProc(Collection c, CollectionExceptionUPP exceptionProc);PASCAL_SUBTRAP(SetCollectionExceptionProc, 0xABF6, 0x001F, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, SetCollectionExceptionProc, InterfaceLib);


Collection C_GetNewCollection(INTEGER collectionID);PASCAL_SUBTRAP(GetNewCollection, 0xABF6, 0x0020, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetNewCollection, InterfaceLib);


OSErr C_AddCollectionItemHdl(Collection aCollection, CollectionTag tag, LONGINT id, Handle itemData);PASCAL_SUBTRAP(AddCollectionItemHdl, 0xABF6, 0x0021, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, AddCollectionItemHdl, InterfaceLib);


OSErr C_GetCollectionItemHdl(Collection aCollection, CollectionTag tag, LONGINT id, Handle itemData);PASCAL_SUBTRAP(GetCollectionItemHdl, 0xABF6, 0x0022, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetCollectionItemHdl, InterfaceLib);


OSErr C_ReplaceIndexedCollectionItemHdl(Collection aCollection, LONGINT index, Handle itemData);PASCAL_SUBTRAP(ReplaceIndexedCollectionItemHdl, 0xABF6, 0x0023, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, ReplaceIndexedCollectionItemHdl, InterfaceLib);


OSErr C_GetIndexedCollectionItemHdl(Collection aCollection, LONGINT index, Handle itemData);PASCAL_SUBTRAP(GetIndexedCollectionItemHdl, 0xABF6, 0x0024, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, GetIndexedCollectionItemHdl, InterfaceLib);


OSErr C_FlattenCollectionToHdl(Collection aCollection, Handle flattened);PASCAL_SUBTRAP(FlattenCollectionToHdl, 0xABF6, 0x0025, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, FlattenCollectionToHdl, InterfaceLib);


OSErr C_UnflattenCollectionFromHdl(Collection aCollection, Handle flattened);PASCAL_SUBTRAP(UnflattenCollectionFromHdl, 0xABF6, 0x0026, CollectionMgr);
LIBRARY_ALSO(CollectionsLib, UnflattenCollectionFromHdl, InterfaceLib);


} /* namespace Executor */