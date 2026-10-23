/*****************************************************************************
 *                                                                           *
 *                                Navigation.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h       Boolean, ScriptCode, Handle, Str255, OSType,         *
 *                      Rect, OSErr, Size                                    *
 *     AppleEvents.h    AEDescList, AEDesc                                   *
 *     FileMgr.h        FInfo, FXInfo, DInfo, DXInfo                         *
 *     EventMgr.h       EventRecord                                          *
 *     WindowMgr.h      WindowPtr                                            *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "AppleEvents.h"
#include "FileMgr.h"
#include "EventMgr.h"
#include "WindowMgr.h"

#define MODULE_NAME Navigation
#include <base/api-module.h>


namespace Executor {

typedef uint32_t NavActionState;

typedef int32_t NavCustomControlMessage;

typedef uint32_t NavDialogOptionFlags;

typedef uint32_t NavAskDiscardChangesResult;

typedef uint32_t NavEventCallbackMessage;

enum {kNavCBEvent = 0,
kNavCBCustomize,
kNavCBStart,
kNavCBTerminate,
kNavCBAdjustRect,
kNavCBNewLocation,
kNavCBShowDesktop,
kNavCBSelectEntry,
kNavCBPopupMenuSelect,
kNavCBAccept,
kNavCBCancel,
kNavCBAdjustPreview,
kNavCBOpenSelection = 0x80000000,
};

typedef uint16_t NavSortKeyField;

typedef uint16_t NavPopupMenuItem;

typedef uint32_t NavFilterModes;

typedef uint32_t NavAskSaveChangesResult;

typedef uint32_t NavAskSaveChangesAction;

typedef uint16_t NavSortOrder;

typedef uint32_t NavTranslationOptions;

struct __navcontext;

typedef __navcontext *NavContext;

enum {kNavReplyRecordVersion = 0,
};

struct NavReplyRecord{GUEST_STRUCT;GUEST<uint16_t> version;Boolean validRecord;Boolean replacing;Boolean isStationery;Boolean translationNeeded;GUEST<AEDescList> selection;GUEST<ScriptCode> keyScript;GUEST<Handle> fileTranslation;};void describeStruct(const NavReplyRecord&, std::ostream&);

enum {kNavDialogOptionsVersion = 0,
};

struct NavDialogOptions{GUEST_STRUCT;GUEST<uint16_t> version;GUEST<NavDialogOptionFlags> dialogOptionFlags;GUEST<Point> location;GUEST<Str255> clientName;GUEST<Str255> windowTitle;GUEST<Str255> actionButtonLabel;GUEST<Str255> cancelButtonLabel;GUEST<Str255> savedFileName;GUEST<Str255> message;GUEST<uint32_t> preferenceKey;GUEST<Handle> popupExtension;};void describeStruct(const NavDialogOptions&, std::ostream&);

enum {kNavMenuItemSpecVersion = 0,
};

struct NavMenuItemSpec{GUEST_STRUCT;GUEST<uint16_t> version;GUEST<OSType> menuCreator;GUEST<OSType> menuType;GUEST<Str255> menuItemName;};void describeStruct(const NavMenuItemSpec&, std::ostream&);

struct NavFileOrFolderInfo{GUEST_STRUCT;GUEST<uint16_t> version;Boolean isFolder;Boolean visible;GUEST<uint32_t> creationDate;GUEST<uint32_t> modificationDate;union{struct{Boolean locked;Boolean resourceOpen;Boolean dataOpen;Boolean reserved1;GUEST<uint32_t> dataSize;GUEST<uint32_t> resourceSize;FInfo finderInfo;FXInfo finderXInfo;} fileInfo;struct{Boolean shareable;Boolean sharePoint;Boolean mounted;Boolean readable;Boolean writeable;Boolean reserved2;GUEST<uint32_t> numberOfFiles;DInfo finderDInfo;DXInfo finderDXInfo;GUEST<OSType> folderType;GUEST<OSType> folderCreator;char reserved3[206];} folderInfo;} fileAndFolder;};void describeStruct(const NavFileOrFolderInfo&, std::ostream&);

union NavEventDataInfo{GUEST_STRUCT;GUEST<EventRecord*> event;GUEST<void*> param;};void describeStruct(const NavEventDataInfo&, std::ostream&);

struct NavEventData{GUEST_STRUCT;NavEventDataInfo eventDataParms;GUEST<int16_t> itemHit;};void describeStruct(const NavEventData&, std::ostream&);

struct NavCBRec{GUEST_STRUCT;GUEST<uint16_t> version;GUEST<NavContext> context;GUEST<WindowPtr> window;Rect customRect;Rect previewRect;NavEventData eventData;};void describeStruct(const NavCBRec&, std::ostream&);

typedef NavCBRec *NavCBRecPtr;

struct NavTypeList{GUEST_STRUCT;GUEST<OSType> componentSignature;GUEST<int16_t> reserved;GUEST<int16_t> osTypeCount;GUEST<OSType> osType[1];};void describeStruct(const NavTypeList&, std::ostream&);

typedef GUEST<NavTypeList*> *NavTypeListHandle;

using NavEventUPP = UPP<void(NavEventCallbackMessage callBackSelector, NavCBRecPtr callbackParms, void *callBackUD)>;


using NavPreviewUPP = UPP<void(NavCBRecPtr callbackParms, void *callBackUD)>;


using NavObjectFilterUPP = UPP<void(AEDesc *theItem, void *info, void *callBackUD, NavFilterModes filterMode)>;


Boolean NavServicesCanRun();

Boolean NavServicesAvailable();

uint32_t NavLibraryVersion();

OSErr NavLoad();

OSErr NavUnload();

OSErr NavDisposeReply(NavReplyRecord *reply);

OSErr NavGetFile(AEDesc *defaultLocation, NavReplyRecord *reply, NavDialogOptions *dialogOptions, NavEventUPP eventProc, NavPreviewUPP previewProc, NavObjectFilterUPP filterProc, NavTypeListHandle typeList, void *callBackUD);

OSErr NavChooseFile(AEDesc *defaultLocation, NavReplyRecord *reply, NavDialogOptions *dialogOptions, NavEventUPP eventProc, NavPreviewUPP previewProc, NavObjectFilterUPP filterProc, NavTypeListHandle typeList, void *callBackUD);

OSErr NavTranslateFile(NavReplyRecord *translateInfo, NavTranslationOptions howToTranslate);

OSErr NavChooseVolume(AEDesc *defaultLocation, NavReplyRecord *reply, NavDialogOptions *dialogOptions, NavEventUPP eventProc, NavObjectFilterUPP filterProc, void *callBackUD);

OSErr NavChooseFolder(AEDesc *defaultLocation, NavReplyRecord *reply, NavDialogOptions *dialogOptions, NavEventUPP eventProc, NavObjectFilterUPP filterProc, void *callBackUD);

OSErr NavChooseObject(AEDesc *defaultLocation, NavReplyRecord *reply, NavDialogOptions *dialogOptions, NavEventUPP eventProc, NavObjectFilterUPP filterProc, void *callBackUD);

OSErr NavNewFolder(AEDesc *defaultLocation, NavReplyRecord *reply, NavDialogOptions *dialogOptions, NavEventUPP eventProc, void *callBackUD);

OSErr NavPutFile(AEDesc *defaultLocation, NavReplyRecord *reply, NavDialogOptions *dialogOptions, NavEventUPP eventProc, OSType fileType, OSType fileCreator, void *callBackUD);

OSErr NavAskSaveChanges(NavDialogOptions *dialogOptions, NavAskSaveChangesAction action, GUEST<NavAskSaveChangesResult> *reply, NavEventUPP eventProc, void *callBackUD);

OSErr NavCustomAskSaveChanges(NavDialogOptions *dialogOptions, GUEST<NavAskSaveChangesResult> *reply, NavEventUPP eventProc, void *callBackUD);

OSErr NavAskDiscardChanges(NavDialogOptions *dialogOptions, GUEST<NavAskDiscardChangesResult> *reply, NavEventUPP eventProc, void *callBackUD);

OSErr NavCompleteSave(NavReplyRecord *translateInfo, NavTranslationOptions howToTranslate);

OSErr NavCreatePreview(AEDesc *theObject, OSType previewDataType, const void *previewData, Size previewDataSize);

OSErr NavCustomControl(NavContext context, NavCustomControlMessage selector, void *parms);

} /* namespace Executor */