/*****************************************************************************
 *                                                                           *
 *                                DialogMgr8.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h          SInt16, Boolean, ConstStringPtr, UInt16, Rect,    *
 *                         ConstStr255Param, SInt32, Handle, UInt32,         *
 *                         OSErr                                             *
 *     DialogMgr.h         ModalFilterUPP, DialogRef, DITLMethod             *
 *     WindowMgr.h         WindowRef                                         *
 *     ControlMgr.h        ControlRef                                        *
 *     DriverServices.h    OSStatus                                          *
 *     EventMgr.h          EventMask                                         *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "DialogMgr.h"
#include "WindowMgr.h"
#include "ControlMgr.h"
#include "DriverServices.h"
#include "EventMgr.h"

#define MODULE_NAME DialogMgr8
#include <base/api-module.h>


namespace Executor {

enum {kControlDialogItem = 4,
kButtonDialogItem = kControlDialogItem | 0,
kCheckBoxDialogItem = kControlDialogItem | 1,
kRadioButtonDialogItem = kControlDialogItem | 2,
kResourceControlDialogItem = kControlDialogItem | 3,
kStaticTextDialogItem = 8,
kEditTextDialogItem = 16,
kIconDialogItem = 32,
kPictureDialogItem = 64,
kUserDialogItem = 0,
kHelpDialogItem = 1,
kItemDisableBit = 128,
};

enum {kStdOkItemIndex = 1,
kStdCancelItemIndex = 2,
ok = kStdOkItemIndex,
cancel = kStdCancelItemIndex,
};

enum {kStopIcon = 0,
kNoteIcon = 1,
kCautionIcon = 2,
};

enum {kOkItemIndex = 1,
kCancelItemIndex = 2,
};

typedef SInt16 DialogItemIndexZeroBased;

enum {kAlertStopAlert = 0,
kAlertNoteAlert = 1,
kAlertCautionAlert = 2,
kAlertPlainAlert = 3,
};

typedef SInt16 AlertType;

enum {kAlertDefaultOKText = -1,
kAlertDefaultCancelText = -1,
kAlertDefaultOtherText = -1,
};

enum {kAlertStdAlertOKButton = 1,
kAlertStdAlertCancelButton = 2,
kAlertStdAlertOtherButton = 3,
kAlertStdAlertHelpButton = 4,
};

enum {kDialogFlagsUseThemeBackground = (1 << 0),
kDialogFlagsUseControlHierarchy = (1 << 1),
kDialogFlagsHandleMovableModal = (1 << 2),
kDialogFlagsUseThemeControls = (1 << 3),
};

enum {kAlertFlagsUseThemeBackground = (1 << 0),
kAlertFlagsUseControlHierarchy = (1 << 1),
kAlertFlagsAlertIsMovable = (1 << 2),
kAlertFlagsUseThemeControls = (1 << 3),
};

enum {kDialogFontNoFontStyle = 0,
kDialogFontUseFontMask = 1,
kDialogFontUseFaceMask = 2,
kDialogFontUseSizeMask = 4,
kDialogFontUseForeColorMask = 8,
kDialogFontUseBackColorMask = 16,
kDialogFontUseModeMask = 32,
kDialogFontUseJustMask = 64,
kDialogFontUseAllMask = 255,
kDialogFontAddFontSizeMask = 256,
kDialogFontUseFontNameMask = 512,
kDialogFontAddToMetaFontMask = 1024,
};

enum {kDialogFontUseThemeFontIDMask = 128,
};

struct AlertStdAlertParamRec{GUEST_STRUCT;Boolean movable;Boolean helpButton;GUEST<ModalFilterUPP> filterProc;GUEST<ConstStringPtr> defaultText;GUEST<ConstStringPtr> cancelText;GUEST<ConstStringPtr> otherText;GUEST<SInt16> defaultButton;GUEST<SInt16> cancelButton;GUEST<UInt16> position;};void describeStruct(const AlertStdAlertParamRec&, std::ostream&);

typedef AlertStdAlertParamRec *AlertStdAlertParamPtr;

enum {kHICommandOther = "othr"_4,
};

enum {kStdAlertDoNotDisposeSheet = 1 << 0,
};

EXTERN_DISPATCHER_TRAP(DialogDispatch, 0xAA68, D0<0xFF>);
DialogRef C_NewFeaturesDialog(void *inStorage, const Rect *inBoundsRect, ConstStr255Param inTitle, Boolean inIsVisible, SInt16 inProcID, WindowRef inBehind, Boolean inGoAwayFlag, SInt32 inRefCon, Handle inItemListHandle, UInt32 inFlags);PASCAL_SUBTRAP(NewFeaturesDialog, 0xAA68, 0x110C, DialogDispatch);
LIBRARY_ALSO(AppearanceLib, NewFeaturesDialog, InterfaceLib);


OSErr C_AutoSizeDialog(DialogRef inDialog);PASCAL_SUBTRAP(AutoSizeDialog, 0xAA68, 0x020D, DialogDispatch);
LIBRARY_ALSO(AppearanceLib, AutoSizeDialog, InterfaceLib);


OSErr C_StandardAlert(AlertType inAlertType, ConstStr255Param inError, ConstStr255Param inExplanation, const AlertStdAlertParamRec *inAlertParam, GUEST<SInt16> *outItemHit);PASCAL_SUBTRAP(StandardAlert, 0xAA68, 0x090E, DialogDispatch);
LIBRARY_ALSO(AppearanceLib, StandardAlert, InterfaceLib);


OSErr C_GetDialogItemAsControl(DialogRef inDialog, SInt16 inItemNo, GUEST<ControlRef> *outControl);PASCAL_SUBTRAP(GetDialogItemAsControl, 0xAA68, 0x050F, DialogDispatch);
LIBRARY_ALSO(AppearanceLib, GetDialogItemAsControl, InterfaceLib);


OSErr C_MoveDialogItem(DialogRef inDialog, SInt16 inItemNo, SInt16 inHoriz, SInt16 inVert);PASCAL_SUBTRAP(MoveDialogItem, 0xAA68, 0x0510, DialogDispatch);
LIBRARY_ALSO(AppearanceLib, MoveDialogItem, InterfaceLib);


OSErr C_SizeDialogItem(DialogRef inDialog, SInt16 inItemNo, SInt16 inWidth, SInt16 inHeight);PASCAL_SUBTRAP(SizeDialogItem, 0xAA68, 0x0511, DialogDispatch);
LIBRARY_ALSO(AppearanceLib, SizeDialogItem, InterfaceLib);


OSErr C_AppendDialogItemList(DialogRef dialog, SInt16 ditlID, DITLMethod method);PASCAL_SUBTRAP(AppendDialogItemList, 0xAA68, 0x0412, DialogDispatch);
LIBRARY_ALSO(MacDialogsLib, AppendDialogItemList, InterfaceLib);


OSStatus C_SetDialogTimeout(DialogRef inDialog, SInt16 inButtonToPress, UInt32 inSecondsToWait);LIBRARY_FUNCTION(MacDialogsLib, SetDialogTimeout, C_SetDialogTimeout);


OSStatus C_GetDialogTimeout(DialogRef inDialog, GUEST<SInt16> *outButtonToPress, GUEST<UInt32> *outSecondsToWait, GUEST<UInt32> *outSecondsRemaining);LIBRARY_FUNCTION(MacDialogsLib, GetDialogTimeout, C_GetDialogTimeout);


OSStatus C_SetModalDialogEventMask(DialogRef inDialog, EventMask inMask);LIBRARY_FUNCTION(MacDialogsLib, SetModalDialogEventMask, C_SetModalDialogEventMask);


OSStatus C_GetModalDialogEventMask(DialogRef inDialog, GUEST<EventMask> *outMask);LIBRARY_FUNCTION(MacDialogsLib, GetModalDialogEventMask, C_GetModalDialogEventMask);


} /* namespace Executor */