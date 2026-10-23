/*****************************************************************************
 *                                                                           *
 *                               ResourceMgr.h                               *
 *                               =============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    Boolean, INTEGER, ResType, Handle, ConstStringPtr,      *
 *                   StringPtr, LONGINT, Byte, SignedByte, Ptr               *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME ResourceMgr
#include <base/api-module.h>


namespace Executor {

// resource attribute masks
enum {resSysHeap = 64,
resPurgeable = 32,
resLocked = 16,
resProtected = 8,
resPreload = 4,
resChanged = 2,
};

enum {resCompressed = 1,
};

// resource manager return codes
enum {CantDecompress = -186,
};

enum {resNotFound = -192,
resFNotFound = -193,
addResFailed = -194,
rmvResFailed = -196,
};

// IMIV
enum {resAttrErr = -198,
mapReadErr = -199,
};

// IMVI
enum {resourceInMemory = -188,
inputOutOfBounds = -190,
};

// resource file attribute masks
enum {mapReadOnly = 128,
mapCompact = 64,
mapChanged = 32,
};

// ResourceMgr IMI-115 (true);
const LowMemGlobal<Handle > TopMapHndl{ 0xA50 };
inline Handle  LMGetTopMapHndl() { return LM(TopMapHndl); }
inline void LMSetTopMapHndl(Handle val) { LM(TopMapHndl) = val; }
NOTRAP_FUNCTION2(LMGetTopMapHndl);
NOTRAP_FUNCTION2(LMSetTopMapHndl);


// ResourceMgr IMI-114 (true);
const LowMemGlobal<Handle > SysMapHndl{ 0xA54 };
inline Handle  LMGetSysMapHndl() { return LM(SysMapHndl); }
inline void LMSetSysMapHndl(Handle val) { LM(SysMapHndl) = val; }
NOTRAP_FUNCTION2(LMGetSysMapHndl);
NOTRAP_FUNCTION2(LMSetSysMapHndl);


// ResourceMgr IMI-114 (true);
const LowMemGlobal<INTEGER > SysMap{ 0xA58 };
inline INTEGER  LMGetSysMap() { return LM(SysMap); }
inline void LMSetSysMap(INTEGER val) { LM(SysMap) = val; }
NOTRAP_FUNCTION2(LMGetSysMap);
NOTRAP_FUNCTION2(LMSetSysMap);


// ResourceMgr IMI-117 (true);
const LowMemGlobal<INTEGER > CurMap{ 0xA5A };
inline INTEGER  LMGetCurMap() { return LM(CurMap); }
inline void LMSetCurMap(INTEGER val) { LM(CurMap) = val; }
NOTRAP_FUNCTION2(LMGetCurMap);
NOTRAP_FUNCTION2(LMSetCurMap);


// ResourceMgr ToolEqu.a (false);
const LowMemGlobal<INTEGER > resreadonly{ 0xA5C };
inline INTEGER  LMGetresreadonly() { return LM(resreadonly); }
inline void LMSetresreadonly(INTEGER val) { LM(resreadonly) = val; }
NOTRAP_FUNCTION2(LMGetresreadonly);
NOTRAP_FUNCTION2(LMSetresreadonly);


// ResourceMgr IMI-118 (true);
const LowMemGlobal<Boolean > ResLoad{ 0xA5E };
inline Boolean  LMGetResLoad() { return LM(ResLoad); }
inline void LMSetResLoad(Boolean val) { LM(ResLoad) = val; }
NOTRAP_FUNCTION2(LMGetResLoad);
NOTRAP_FUNCTION2(LMSetResLoad);


// ResourceMgr IMI-118 (true);
const LowMemGlobal<INTEGER > ResErr{ 0xA60 };
inline INTEGER  LMGetResErr() { return LM(ResErr); }
inline void LMSetResErr(INTEGER val) { LM(ResErr) = val; }
NOTRAP_FUNCTION2(LMGetResErr);
NOTRAP_FUNCTION2(LMSetResErr);


// ResourceMgr IMI-116 (true);
const LowMemGlobal<ProcPtr > ResErrProc{ 0xAF2 };
inline ProcPtr  LMGetResErrProc() { return LM(ResErrProc); }
inline void LMSetResErrProc(ProcPtr val) { LM(ResErrProc) = val; }
NOTRAP_FUNCTION2(LMGetResErrProc);
NOTRAP_FUNCTION2(LMSetResErrProc);


// ResourceMgr IMI-114 (true);
const LowMemGlobal<Byte [20]> SysResName{ 0xAD8 };


// ResourceMgr IMIV-19 (false);
const LowMemGlobal<Byte > RomMapInsert{ 0xB9E };
inline Byte  LMGetRomMapInsert() { return LM(RomMapInsert); }
inline void LMSetRomMapInsert(Byte val) { LM(RomMapInsert) = val; }
NOTRAP_FUNCTION2(LMGetRomMapInsert);
NOTRAP_FUNCTION2(LMSetRomMapInsert);


// ResourceMgr IMIV-19 (false);
const LowMemGlobal<Byte > TmpResLoad{ 0xB9F };
inline Byte  LMGetTmpResLoad() { return LM(TmpResLoad); }
inline void LMSetTmpResLoad(Byte val) { LM(TmpResLoad) = val; }
NOTRAP_FUNCTION2(LMGetTmpResLoad);
NOTRAP_FUNCTION2(LMSetTmpResLoad);


DISPATCHER_TRAP(ResourceDispatch, 0xA822, D0<0xFF>);


BEGIN_EXECUTOR_ONLY
RAW_68K_TRAP(ResourceStub, 0xA0FC); // defined in emustubs.cpp
END_EXECUTOR_ONLY


void C_SetResLoad(Boolean load);PASCAL_TRAP(SetResLoad, 0xA99B);


INTEGER C_CountResources(ResType typ);PASCAL_TRAP(CountResources, 0xA99C);


INTEGER C_Count1Resources(ResType typ);PASCAL_TRAP(Count1Resources, 0xA80D);


Handle C_GetIndResource(ResType typ, INTEGER indx);PASCAL_TRAP(GetIndResource, 0xA99D);


Handle C_Get1IndResource(ResType typ, INTEGER i);PASCAL_TRAP(Get1IndResource, 0xA80E);


Handle C_GetResource(ResType typ, INTEGER id);PASCAL_TRAP(GetResource, 0xA9A0);


Handle C_Get1Resource(ResType typ, INTEGER id);PASCAL_TRAP(Get1Resource, 0xA81F);


Handle C_GetNamedResource(ResType typ, ConstStringPtr nam);PASCAL_TRAP(GetNamedResource, 0xA9A1);


Handle C_Get1NamedResource(ResType typ, ConstStringPtr s);PASCAL_TRAP(Get1NamedResource, 0xA820);


void C_LoadResource(Handle res);PASCAL_TRAP(LoadResource, 0xA9A2);


void C_ReleaseResource(Handle res);PASCAL_TRAP(ReleaseResource, 0xA9A3);


void C_DetachResource(Handle res);PASCAL_TRAP(DetachResource, 0xA992);


INTEGER C_UniqueID(ResType typ);PASCAL_TRAP(UniqueID, 0xA9C1);


INTEGER C_Unique1ID(ResType typ);PASCAL_TRAP(Unique1ID, 0xA810);


void C_GetResInfo(Handle res, GUEST<INTEGER> *id1, GUEST<ResType> *typ, StringPtr name);PASCAL_TRAP(GetResInfo, 0xA9A8);


INTEGER C_GetResAttrs(Handle res);PASCAL_TRAP(GetResAttrs, 0xA9A6);


LONGINT C_GetResourceSizeOnDisk(Handle res);PASCAL_TRAP(GetResourceSizeOnDisk, 0xA9A5);


INTEGER C_CountTypes();PASCAL_TRAP(CountTypes, 0xA99E);


INTEGER C_Count1Types();PASCAL_TRAP(Count1Types, 0xA81C);


void C_GetIndType(GUEST<ResType> *typ, INTEGER indx);PASCAL_TRAP(GetIndType, 0xA99F);


void C_Get1IndType(GUEST<ResType> *typ, INTEGER indx);PASCAL_TRAP(Get1IndType, 0xA80F);


LONGINT C_GetMaxResourceSize(Handle h);PASCAL_TRAP(GetMaxResourceSize, 0xA821);


LONGINT C_RsrcMapEntry(Handle h);PASCAL_TRAP(RsrcMapEntry, 0xA9C5);


Handle C_RGetResource(ResType typ, INTEGER id);PASCAL_TRAP(RGetResource, 0xA80C);


INTEGER C_InitResources();PASCAL_TRAP(InitResources, 0xA995);


void C_RsrcZoneInit();PASCAL_TRAP(RsrcZoneInit, 0xA996);


INTEGER C_ResError();PASCAL_TRAP(ResError, 0xA9AF);


INTEGER C_GetResFileAttrs(INTEGER rn);PASCAL_TRAP(GetResFileAttrs, 0xA9F6);


void C_SetResFileAttrs(INTEGER rn, INTEGER attrs);PASCAL_TRAP(SetResFileAttrs, 0xA9F7);


void C_SetResInfo(Handle res, INTEGER id, ConstStringPtr name);PASCAL_TRAP(SetResInfo, 0xA9A9);


void C_SetResAttrs(Handle res, INTEGER attrs);PASCAL_TRAP(SetResAttrs, 0xA9A7);


void C_ChangedResource(Handle res);PASCAL_TRAP(ChangedResource, 0xA9AA);


void C_AddResource(Handle data, ResType typ, INTEGER id, ConstStringPtr name);PASCAL_TRAP(AddResource, 0xA9AB);


void C_RemoveResource(Handle res);PASCAL_TRAP(RemoveResource, 0xA9AD);


void C_UpdateResFile(INTEGER rn);PASCAL_TRAP(UpdateResFile, 0xA999);


void C_WriteResource(Handle res);PASCAL_TRAP(WriteResource, 0xA9B0);


void C_SetResPurge(Boolean install);PASCAL_TRAP(SetResPurge, 0xA993);


void C_CreateResFile(ConstStringPtr fn);PASCAL_TRAP(CreateResFile, 0xA9B1);


INTEGER C_OpenRFPerm(ConstStringPtr fn, INTEGER vref, Byte perm);PASCAL_TRAP(OpenRFPerm, 0xA9C4);


INTEGER C_OpenResFile(ConstStringPtr fn);PASCAL_TRAP(OpenResFile, 0xA997);


void C_CloseResFile(INTEGER rn);PASCAL_TRAP(CloseResFile, 0xA99A);


INTEGER C_CurResFile();PASCAL_TRAP(CurResFile, 0xA994);


INTEGER C_HomeResFile(Handle res);PASCAL_TRAP(HomeResFile, 0xA9A4);


void C_UseResFile(INTEGER rn);PASCAL_TRAP(UseResFile, 0xA998);


// MacPhoenix. Private in 7.5.5 (selector -1; name is our guess): true of the System file's map and of a map whose file was opened in system mode; ResErr resFNotFound for nil. Apple's Help Manager asks of a balloon resource's home file.
Boolean C_ResourceMapIsSystem(Handle map);PASCAL_SUBTRAP(ResourceMapIsSystem, 0xA822, 0x02FF, ResourceDispatch);


// MacPhoenix. Private in 7.5.5 (name is our guess, tools/macdecode/learned.yaml): the resource map handle of an open resource file, 0 meaning the System file. Apple's version walks the map chain from TopMapHndl and sets ResErr to resFNotFound when the file isn't open. Finder calls it.
Handle C_GetResourceMapHandle(INTEGER refNum);PASCAL_SUBTRAP(GetResourceMapHandle, 0xA822, 0x0000, ResourceDispatch);


// MacPhoenix. Private in 7.5.5 (name is a guess): the map's attribute byte (offset 23) masked with $1E -- the private bits, not read-only/compact/changed; a nil map sets ResErr to nilHandleErr and returns 0. Finder (opening a disk's saved windows) and the System's HOpenResFile/CloseResFile patches call it.
SignedByte C_GetResourceMapPrivateFlags(Handle map);PASCAL_SUBTRAP(GetResourceMapPrivateFlags, 0xA822, 0x0008, ResourceDispatch);


// MacPhoenix. Private in 7.5.5 (name is a guess): the topmost of the override maps (attribute bit 1) stacked directly above a map in the chain from TopMapHndl, or the map itself when none is; nil and ResErr resFNotFound when the map isn't in the chain. The Shared Library Manager calls it on the files it opens.
Handle C_GetOverrideMap(Handle map);PASCAL_SUBTRAP(GetOverrideMap, 0xA822, 0x0006, ResourceDispatch);


void C_ReadPartialResource(Handle resource, int32_t offset, Ptr buffer, int32_t count);PASCAL_SUBTRAP(ReadPartialResource, 0xA822, 0x0001, ResourceDispatch);


void C_WritePartialResource(Handle resource, int32_t offset, Ptr buffer, int32_t count);PASCAL_SUBTRAP(WritePartialResource, 0xA822, 0x0002, ResourceDispatch);


void C_SetResourceSize(Handle resource, int32_t size);PASCAL_SUBTRAP(SetResourceSize, 0xA822, 0x0003, ResourceDispatch);


Handle C_GetNextFOND(Handle fondHandle);PASCAL_SUBTRAP(GetNextFOND, 0xA822, 0x000A, ResourceDispatch);


} /* namespace Executor */