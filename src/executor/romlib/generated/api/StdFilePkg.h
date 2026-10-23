/*****************************************************************************
 *                                                                           *
 *                                StdFilePkg.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     Boolean, OSType, INTEGER, Str63, ScriptCode,           *
 *                    LONGINT, ConstStringPtr, Ptr                           *
 *     FileMgr.h      FSSpec, CInfoPBPtr                                     *
 *     DialogMgr.h    DialogPtr, ModalFilterUPP, ModalFilterYDUPP            *
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
#include "DialogMgr.h"

#define MODULE_NAME StdFilePkg
#include <base/api-module.h>


namespace Executor {

enum {putDlgID = -3999,
};

enum {putSave = 1,
putCancel = 2,
putEject = 5,
putDrive = 6,
putName = 7,
};

enum {getDlgID = -4000,
};

enum {getOpen = 1,
getCancel = 3,
getEject = 5,
getDrive = 6,
getNmList = 7,
getScroll = 8,
};

struct SFReply{GUEST_STRUCT;Boolean good;Boolean copy;GUEST<OSType> fType;GUEST<INTEGER> vRefNum;GUEST<INTEGER> version;GUEST<Str63> fName;};static_assert(sizeof(SFReply) == 74);void describeStruct(const SFReply&, std::ostream&);

typedef GUEST<OSType> SFTypeList[4];

// StdFilePkg IMIV-72 (true);
const LowMemGlobal<INTEGER > SFSaveDisk{ 0x214 };
inline INTEGER  LMGetSFSaveDisk() { return LM(SFSaveDisk); }
inline void LMSetSFSaveDisk(INTEGER val) { LM(SFSaveDisk) = val; }
NOTRAP_FUNCTION2(LMGetSFSaveDisk);
NOTRAP_FUNCTION2(LMSetSFSaveDisk);


// StdFilePkg IMIV-72 (true);
const LowMemGlobal<LONGINT > CurDirStore{ 0x398 };
inline LONGINT  LMGetCurDirStore() { return LM(CurDirStore); }
inline void LMSetCurDirStore(LONGINT val) { LM(CurDirStore) = val; }
NOTRAP_FUNCTION2(LMGetCurDirStore);
NOTRAP_FUNCTION2(LMSetCurDirStore);


struct StandardFileReply{GUEST_STRUCT;Boolean sfGood;Boolean sfReplacing;GUEST<OSType> sfType;FSSpec sfFile;GUEST<ScriptCode> sfScript;GUEST<INTEGER> sfFlags;Boolean sfIsFolder;Boolean sfIsVolume;GUEST<LONGINT> sfReserved1;GUEST<INTEGER> sfReserved2;};static_assert(sizeof(StandardFileReply) == 88);void describeStruct(const StandardFileReply&, std::ostream&);

using DlgHookUPP = UPP<INTEGER(INTEGER item, DialogPtr theDialog)>;


using FileFilterUPP = UPP<Boolean(CInfoPBPtr pb)>;


using DlgHookYDUPP = UPP<INTEGER(INTEGER item, DialogPtr theDialog, void *yourDataPtr)>;


using FileFilterYDUPP = UPP<Boolean(CInfoPBPtr pb, void *yourDataPtr)>;


using ActivateYDUPP = UPP<void(DialogPtr theDialog, INTEGER itemNo, Boolean activating, void *yourDataPtr)>;


enum {sfItemOpenButton = 1,
sfItemCancelButton = 2,
sfItemBalloonHelp = 3,
sfItemVolumeUser = 4,
sfItemEjectButton = 5,
sfItemDesktopButton = 6,
sfItemFileListUser = 7,
sfItemPopUpMenuUser = 8,
sfItemDividerLinePict = 9,
sfItemFileNameTextEdit = 10, // Put only
sfItemPromptStaticText = 11,
sfItemNewFolderUser = 12,
};

enum {sfHookFirstCall = -1,
sfHookLastCall = -2,
sfHookNullEvent = 100,
sfHookRebuildList = 101,
sfHookFolderPopUp = 102,
sfHookOpenFolder = 103,
sfHookOpenAlias = 104,
sfHookGoToDesktop = 105,
sfHookGoToAliasTarget = 106,
sfHookGoToParent = 107,
sfHookGoToNextDrive = 108,
sfHookGoToPrevDrive = 109,
sfHookChangeSelection = 110,
};

enum {sfMainDialogRefCon = "stdf"_4,
};

enum {sfPutDialogID = -6043,
sfGetDialogID = -6042,
};

DISPATCHER_TRAP(Pack3, 0xA9EA, StackW);


void C_SFPPutFile(Point p, ConstStringPtr prompt, ConstStringPtr name, DlgHookUPP dh, SFReply *rep, INTEGER dig, ModalFilterUPP fp);PASCAL_SUBTRAP(SFPPutFile, 0xA9EA, 0x0003, Pack3);


void C_SFPutFile(Point p, ConstStringPtr prompt, ConstStringPtr name, DlgHookUPP dh, SFReply *rep);PASCAL_SUBTRAP(SFPutFile, 0xA9EA, 0x0001, Pack3);


void C_SFPGetFile(Point p, ConstStringPtr prompt, FileFilterUPP filef, INTEGER numt, SFTypeList tl, DlgHookUPP dh, SFReply *rep, INTEGER dig, ModalFilterUPP fp);PASCAL_SUBTRAP(SFPGetFile, 0xA9EA, 0x0004, Pack3);


void C_SFGetFile(Point p, ConstStringPtr prompt, FileFilterUPP filef, INTEGER numt, SFTypeList tl, DlgHookUPP dh, SFReply *rep);PASCAL_SUBTRAP(SFGetFile, 0xA9EA, 0x0002, Pack3);


void C_StandardGetFile(FileFilterUPP filef, INTEGER numt, SFTypeList tl, StandardFileReply *replyp);PASCAL_SUBTRAP(StandardGetFile, 0xA9EA, 0x0006, Pack3);


void C_StandardPutFile(ConstStringPtr prompt, ConstStringPtr defaultname, StandardFileReply *replyp);PASCAL_SUBTRAP(StandardPutFile, 0xA9EA, 0x0005, Pack3);


void C_CustomPutFile(ConstStringPtr prompt, ConstStringPtr defaultName, StandardFileReply *replyp, INTEGER dlgid, Point where, DlgHookYDUPP dlghook, ModalFilterYDUPP filterproc, Ptr activeList, ActivateYDUPP activateproc, void *yourdatap);PASCAL_SUBTRAP(CustomPutFile, 0xA9EA, 0x0007, Pack3);


void C_CustomGetFile(FileFilterYDUPP filefilter, INTEGER numtypes, SFTypeList typelist, StandardFileReply *replyp, INTEGER dlgid, Point where, DlgHookYDUPP dlghook, ModalFilterYDUPP filterproc, Ptr activeList, ActivateYDUPP activateproc, void *yourdatap);PASCAL_SUBTRAP(CustomGetFile, 0xA9EA, 0x0008, Pack3);


} /* namespace Executor */