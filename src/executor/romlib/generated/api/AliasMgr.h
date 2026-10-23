/*****************************************************************************
 *                                                                           *
 *                                 AliasMgr.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    Boolean, Ptr, Handle, OSErr, OSType, StringPtr,         *
 *                   Str32, Str31, Str63                                     *
 *     FileMgr.h     CInfoPBPtr, FSSpecPtr, FSSpecArrayPtr                   *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "FileMgr.h"

#define MODULE_NAME AliasMgr
#include <base/api-module.h>


namespace Executor {

using AliasFilterUPP = UPP<Boolean(CInfoPBPtr , Boolean *, Ptr )>;


typedef Handle AliasHandle;

typedef int16_t AliasTypeInfo;

enum {kSystemFolderType = "macs"_4,
kDesktopFolderType = "desk"_4,
kTrashFolderType = "trsh"_4,
kWhereToEmptyTrashFolderType = "empt"_4,
kPrintMonitorDocsFolderType = "prnt"_4,
kStartupFolderType = "strt"_4,
kAppleMenuFolderType = "amnu"_4,
kControlPanelFolderType = "ctrl"_4,
kExtensionFolderType = "extn"_4,
kPreferencesFolderType = "pref"_4,
kTemporaryFolderType = "temp"_4,
kFontFolderType = "font"_4,
};

DISPATCHER_TRAP(AliasDispatch, 0xA823, D0W);


OSErr C_FindFolder(int16_t vRefNum, OSType folderType, Boolean createFolder, GUEST<int16_t> *foundVRefNum, GUEST<int32_t> *foundDirID);PASCAL_SUBTRAP(FindFolder, 0xA823, 0x0000, AliasDispatch);


// MacPhoenix: private selector, name a guess (learned.yaml). FindFolder
// plus a list of extra folder descriptions; Finder Scripting Extension
// calls it from its FindFolder patch.
OSErr C_FindFolderWithTable(int16_t vRefNum, OSType folderType, Boolean createFolder, GUEST<int16_t> *foundVRefNum, GUEST<int32_t> *foundDirID, Ptr table);PASCAL_SUBTRAP(FindFolderWithTable, 0xA823, 0x000A, AliasDispatch);


// MacPhoenix: private on 7.5.5, name a guess (the later Folder Manager
// has a GetFolderName with these arguments). Finder Scripting Extension
// asks it for the Trash's name.
OSErr C_GetFolderName(int16_t vRefNum, OSType folderType, GUEST<int16_t> *foundVRefNum, StringPtr name);PASCAL_SUBTRAP(GetFolderName, 0xA823, 0x000E, AliasDispatch);


OSErr C_NewAlias(FSSpecPtr fromFile, FSSpecPtr target, GUEST<AliasHandle> *alias);PASCAL_SUBTRAP(NewAlias, 0xA823, 0x0002, AliasDispatch);


OSErr C_NewAliasMinimal(FSSpecPtr target, GUEST<AliasHandle> *alias);PASCAL_SUBTRAP(NewAliasMinimal, 0xA823, 0x0008, AliasDispatch);


OSErr C_NewAliasMinimalFromFullPath(int16_t fullpathLength, Ptr fullpath, Str32 zoneName, Str31 serverName, GUEST<AliasHandle> *alias);PASCAL_SUBTRAP(NewAliasMinimalFromFullPath, 0xA823, 0x0009, AliasDispatch);


OSErr C_UpdateAlias(FSSpecPtr fromFile, FSSpecPtr target, AliasHandle alias, Boolean *wasChanged);PASCAL_SUBTRAP(UpdateAlias, 0xA823, 0x0006, AliasDispatch);


OSErr C_ResolveAlias(FSSpecPtr fromFile, AliasHandle alias, FSSpecPtr target, Boolean *wasAliased);PASCAL_SUBTRAP(ResolveAlias, 0xA823, 0x0003, AliasDispatch);


OSErr C_ResolveAliasFile(FSSpecPtr theSpec, Boolean resolveAliasChains, Boolean *targetIsFolder, Boolean *wasAliased);PASCAL_SUBTRAP(ResolveAliasFile, 0xA823, 0x000C, AliasDispatch);


OSErr C_FollowFinderAlias(FSSpecPtr fromFile, AliasHandle alias, Boolean logon, FSSpecPtr target, Boolean *wasChanged);PASCAL_SUBTRAP(FollowFinderAlias, 0xA823, 0x000F, AliasDispatch);


OSErr C_MatchAlias(FSSpecPtr fromFile, int32_t rulesMask, AliasHandle alias, GUEST<int16_t> *aliasCount, FSSpecArrayPtr aliasList, Boolean *needsUpdate, AliasFilterUPP aliasFilter, Ptr yourDataPtr);PASCAL_SUBTRAP(MatchAlias, 0xA823, 0x0005, AliasDispatch);


OSErr C_GetAliasInfo(AliasHandle alias, AliasTypeInfo index, Str63 theString);PASCAL_SUBTRAP(GetAliasInfo, 0xA823, 0x0007, AliasDispatch);


} /* namespace Executor */