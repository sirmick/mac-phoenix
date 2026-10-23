/*****************************************************************************
 *                                                                           *
 *                                DialogMgr.h                                *
 *                                ===========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     WindowMgr.h    WindowRecord, CWindowPtr, WindowPtr                    *
 *     MacTypes.h     Handle, INTEGER, Rect, Boolean, LONGINT, Str255,       *
 *                    ConstStringPtr, StringPtr, OSErr                       *
 *     TextEdit.h     TEHandle                                               *
 *     EventMgr.h     EventRecord                                            *
 *     QuickDraw.h    RgnHandle                                              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "WindowMgr.h"
#include "ExMacTypes.h"
#include "TextEdit.h"
#include "EventMgr.h"
#include "QuickDraw.h"

#define MODULE_NAME DialogMgr
#include <base/api-module.h>


namespace Executor {

enum {ctrlItem = 4,
btnCtrl = 0,
chkCtrl = 1,
radCtrl = 2,
resCtrl = 3,
statText = 8,
editText = 16,
iconItem = 32,
picItem = 64,
userItem = 0,
itemDisable = 128,
};

enum {OK = 1,
Cancel = 2,
};

enum {stopIcon = 0,
noteIcon = 1,
cautionIcon = 2,
};

struct DialogRecord{GUEST_STRUCT;WindowRecord window;GUEST<Handle> items;GUEST<TEHandle> textH;GUEST<INTEGER> editField;GUEST<INTEGER> editOpen;GUEST<INTEGER> aDefItem;};static_assert(sizeof(DialogRecord) == 170);void describeStruct(const DialogRecord&, std::ostream&);

typedef DialogRecord *DialogPeek;

typedef CWindowPtr CDialogPtr;

typedef WindowPtr DialogPtr;

BEGIN_EXECUTOR_ONLY
/* dialog record accessors */
#define DIALOG_WINDOW(dialog) ((WindowPtr) & ((DialogPeek)dialog)->window)
#define DIALOG_ITEMS(dialog) (((DialogPeek)(dialog))->items)
#define DIALOG_TEXTH(dialog) (((DialogPeek)(dialog))->textH)
#define DIALOG_EDIT_FIELD(dialog) (((DialogPeek)(dialog))->editField)
#define DIALOG_EDIT_OPEN(dialog) (((DialogPeek)(dialog))->editOpen)
#define DIALOG_ADEF_ITEM(dialog) (((DialogPeek)(dialog))->aDefItem)
END_EXECUTOR_ONLY


struct DialogTemplate{GUEST_STRUCT;Rect boundsRect;GUEST<INTEGER> procID;Boolean visible;Boolean filler1;Boolean goAwayFlag;Boolean filler2;GUEST<LONGINT> refCon;GUEST<INTEGER> itemsID;GUEST<Str255> title;};static_assert(sizeof(DialogTemplate) == 276);void describeStruct(const DialogTemplate&, std::ostream&);

typedef DialogTemplate *DialogTPtr;

typedef GUEST<DialogTPtr> *DialogTHndl;

typedef int16_t StageList;

struct AlertTemplate{GUEST_STRUCT;Rect boundsRect;GUEST<INTEGER> itemsID;GUEST<StageList> stages;};static_assert(sizeof(AlertTemplate) == 12);void describeStruct(const AlertTemplate&, std::ostream&);

typedef AlertTemplate *AlertTPtr;

typedef GUEST<AlertTPtr> *AlertTHndl;

enum {overlayDITL = 0,
appendDITLRight = 1,
appendDITLBottom = 2,
};

typedef int16_t DITLMethod;

enum {TEdoFont = 1,
TEdoFace = 2,
TEdoSize = 4,
TEdoColor = 8,
TEdoAll = 15,
};

enum {TEaddSize = 16,
};

enum {doBColor = 8192,
doMode = 16384,
doFontName = 32768,
};

using SoundUPP = UPP<void(INTEGER soundNumber)>;


using ModalFilterUPP = UPP<Boolean(DialogPtr theDialog, EventRecord *theEvent, GUEST<INTEGER> *itemHit)>;


using ModalFilterYDUPP = UPP<Boolean(DialogPtr theDialog, EventRecord *theEvent, GUEST<INTEGER> *itemHit, void *yourDataPtr)>;


using UserItemUPP = UPP<void(DialogPtr theDialog, INTEGER itemNo)>;


// DialogMgr IMI-411 (true);
const LowMemGlobal<ProcPtr > ResumeProc{ 0xA8C };
inline ProcPtr  LMGetResumeProc() { return LM(ResumeProc); }
inline void LMSetResumeProc(ProcPtr val) { LM(ResumeProc) = val; }
NOTRAP_FUNCTION2(LMGetResumeProc);
NOTRAP_FUNCTION2(LMSetResumeProc);


// DialogMgr IMI-423 (true);
const LowMemGlobal<INTEGER > ANumber{ 0xA98 };
inline INTEGER  LMGetANumber() { return LM(ANumber); }
inline void LMSetANumber(INTEGER val) { LM(ANumber) = val; }
NOTRAP_FUNCTION2(LMGetANumber);
NOTRAP_FUNCTION2(LMSetANumber);


// DialogMgr IMI-423 (true);
const LowMemGlobal<INTEGER > ACount{ 0xA9A };
inline INTEGER  LMGetACount() { return LM(ACount); }
inline void LMSetACount(INTEGER val) { LM(ACount) = val; }
NOTRAP_FUNCTION2(LMGetACount);
NOTRAP_FUNCTION2(LMSetACount);


// DialogMgr IMI-411 (true);
const LowMemGlobal<SoundUPP > DABeeper{ 0xA9C };
inline SoundUPP  LMGetDABeeper() { return LM(DABeeper); }
inline void LMSetDABeeper(SoundUPP val) { LM(DABeeper) = val; }
NOTRAP_FUNCTION2(LMGetDABeeper);
NOTRAP_FUNCTION2(LMSetDABeeper);


// DialogMgr IMI-421 (true);
const LowMemGlobal<GUEST<Handle> [4]> DAStrings{ 0xAA0 };


// DialogMgr IMI-412 (true);
const LowMemGlobal<INTEGER > DlgFont{ 0xAFA };
inline INTEGER  LMGetDlgFont() { return LM(DlgFont); }
inline void LMSetDlgFont(INTEGER val) { LM(DlgFont) = val; }
NOTRAP_FUNCTION2(LMGetDlgFont);
NOTRAP_FUNCTION2(LMSetDlgFont);


// Like the real 7.5.5 dispatcher (through lowmem $7B0), only D0's low
// byte selects: Finder calls selectors 7 and 8 with moveq.
DISPATCHER_TRAP(DialogDispatch, 0xAA68, D0<0xFF>);


// MacPhoenix: System 7's menu bar setup around a modal dialog (true
// disables the menus but Edit's items, false restores them). Executor's
// ModalDialog leaves the menu bar alone, so this does nothing. Apple's
// Color Picker 2.0 (PACK 12) calls it with false when its dialog closes.
void C_ModalDialogMenuSetup(Boolean setup);PASCAL_TRAP(ModalDialogMenuSetup, 0xAA67);


INTEGER C_Alert(INTEGER id, ModalFilterUPP fp);PASCAL_TRAP(Alert, 0xA985);


INTEGER C_StopAlert(INTEGER id, ModalFilterUPP fp);PASCAL_TRAP(StopAlert, 0xA986);


INTEGER C_NoteAlert(INTEGER id, ModalFilterUPP fp);PASCAL_TRAP(NoteAlert, 0xA987);


INTEGER C_CautionAlert(INTEGER id, ModalFilterUPP fp);PASCAL_TRAP(CautionAlert, 0xA988);


void C_CouldAlert(INTEGER id);PASCAL_TRAP(CouldAlert, 0xA989);


void C_FreeAlert(INTEGER id);PASCAL_TRAP(FreeAlert, 0xA98A);


void C_CouldDialog(INTEGER id);PASCAL_TRAP(CouldDialog, 0xA979);


void C_FreeDialog(INTEGER id);PASCAL_TRAP(FreeDialog, 0xA97A);


DialogPtr C_NewDialog(void *dst, const Rect *r, ConstStringPtr tit, Boolean vis, INTEGER procid, WindowPtr behind, Boolean gaflag, LONGINT rc, Handle items);PASCAL_TRAP(NewDialog, 0xA97D);


DialogPtr C_GetNewDialog(INTEGER id, void *dst, WindowPtr behind);PASCAL_TRAP(GetNewDialog, 0xA97C);


void C_CloseDialog(DialogPtr dp);PASCAL_TRAP(CloseDialog, 0xA982);


void C_DisposeDialog(DialogPtr dp);PASCAL_TRAP(DisposeDialog, 0xA983);


void C_ModalDialog(ModalFilterUPP fp, GUEST<INTEGER> *item);PASCAL_TRAP(ModalDialog, 0xA991);


Boolean C_IsDialogEvent(EventRecord *evt);PASCAL_TRAP(IsDialogEvent, 0xA97F);


void C_DrawDialog(DialogPtr dp);PASCAL_TRAP(DrawDialog, 0xA981);


INTEGER C_FindDialogItem(DialogPtr dp, Point pt);PASCAL_TRAP(FindDialogItem, 0xA984);


void C_UpdateDialog(DialogPtr dp, RgnHandle rgn);PASCAL_TRAP(UpdateDialog, 0xA978);


Boolean C_DialogSelect(EventRecord *evt, GUEST<DialogPtr> *dpp, GUEST<INTEGER> *item);PASCAL_TRAP(DialogSelect, 0xA980);


void DialogCut(DialogPtr dp);

void DialogCopy(DialogPtr dp);

void DialogPaste(DialogPtr dp);

void DialogDelete(DialogPtr dp);

void C_ErrorSound(SoundUPP sp);PASCAL_TRAP(ErrorSound, 0xA98C);


void C_InitDialogs(ProcPtr rp);PASCAL_TRAP(InitDialogs, 0xA97B);


void SetDialogFont(INTEGER i);

void C_ParamText(ConstStringPtr p0, ConstStringPtr p1, ConstStringPtr p2, ConstStringPtr p3);PASCAL_TRAP(ParamText, 0xA98B);


void C_GetDialogItem(DialogPtr dp, INTEGER itemno, GUEST<INTEGER> *itype, GUEST<Handle> *item, Rect *r);PASCAL_TRAP(GetDialogItem, 0xA98D);


void C_SetDialogItem(DialogPtr dp, INTEGER itemno, INTEGER itype, Handle item, const Rect *r);PASCAL_TRAP(SetDialogItem, 0xA98E);


void C_GetDialogItemText(Handle item, StringPtr text);PASCAL_TRAP(GetDialogItemText, 0xA990);


void C_SetDialogItemText(Handle item, ConstStringPtr text);PASCAL_TRAP(SetDialogItemText, 0xA98F);


void C_SelectDialogItemText(DialogPtr dp, INTEGER itemno, INTEGER start, INTEGER stop);PASCAL_TRAP(SelectDialogItemText, 0xA97E);


INTEGER GetAlertStage();

void ResetAlertStage();

void C_HideDialogItem(DialogPtr dp, INTEGER item);PASCAL_TRAP(HideDialogItem, 0xA827);


void C_ShowDialogItem(DialogPtr dp, INTEGER item);PASCAL_TRAP(ShowDialogItem, 0xA828);


DialogPtr C_NewColorDialog(void *, const Rect *, ConstStringPtr , Boolean , INTEGER , WindowPtr , Boolean , LONGINT , Handle );PASCAL_TRAP(NewColorDialog, 0xAA4B);


OSErr C_GetStdFilterProc(GUEST<ProcPtr> *proc);PASCAL_SUBTRAP(GetStdFilterProc, 0xAA68, 0x0203, DialogDispatch);


OSErr C_SetDialogDefaultItem(DialogPtr dialog, int16_t new_item);PASCAL_SUBTRAP(SetDialogDefaultItem, 0xAA68, 0x0304, DialogDispatch);


OSErr C_SetDialogCancelItem(DialogPtr dialog, int16_t new_item);PASCAL_SUBTRAP(SetDialogCancelItem, 0xAA68, 0x0305, DialogDispatch);


OSErr C_SetDialogTracksCursor(DialogPtr dialog, Boolean tracks);PASCAL_SUBTRAP(SetDialogTracksCursor, 0xAA68, 0x0306, DialogDispatch);


// DialogDispatch 7 on 7.5.5 (name is a guess): true for a keyDown that
// is Escape (char $1B, key $35) or Command-period (IsCmdChar '.').
Boolean C_IsCancelEvent(EventRecord *event);PASCAL_SUBTRAP(IsCancelEvent, 0xAA68, 0x0207, DialogDispatch);


// DialogDispatch 8 on 7.5.5. Scans the OS event queue for a cancel event
// (IsCancelEvent); if one is there, removes mouse and key events up to
// and including it (copying it to *event when event is not nil) and
// returns true.
Boolean C_CheckEventQueueForUserCancel(EventRecord *event);PASCAL_SUBTRAP(CheckEventQueueForUserCancel, 0xAA68, 0x0208, DialogDispatch);


void AppendDITL(DialogPtr , Handle , DITLMethod );

void ShortenDITL(DialogPtr , int16_t );

int16_t CountDITL(DialogPtr );

typedef DialogPtr DialogRef;

typedef int16_t DialogItemIndex;

typedef int16_t DialogItemType;

// MacPhoenix (name is a guess): 7.5.5's DialogDispatch 2. The window's
// variant when it is dBoxProc (1) or movableDBoxProc (5) of WDEF 0, else
// 0; Apple's caches a 1 in the window record's spare flag bit 7.
OSErr C_GetWindowModalClass(WindowPtr window, GUEST<INTEGER> *modal_class);PASCAL_SUBTRAP(GetWindowModalClass, 0xAA68, 0x0402, DialogDispatch);


// MacPhoenix (name is a guess): DialogDispatch 1, GetWindowModalClass of
// FrontWindow. The Help Manager takes class 1 as "a modal dialog is up"
// for its balloons.
OSErr C_GetFrontWindowModalClass(GUEST<INTEGER> *modal_class);PASCAL_SUBTRAP(GetFrontWindowModalClass, 0xAA68, 0x0201, DialogDispatch);


// MacPhoenix (name is a guess): DialogDispatch -1, which the Help
// Manager calls with a menu's offset in MenuList pushed, before it
// shows a menu title's balloon; the routine leaves that long on the
// stack and pushes its answer above it (the offset of the menu to use,
// a modal dialog's), and the caller pops both. So no argument here:
// C_DialogMenuState reads the long and makes room (dialDispatch.cpp).
int32_t C_DialogMenuState();PASCAL_SUBTRAP(DialogMenuState, 0xAA68, 0x00FF, DialogDispatch);


// MacPhoenix (name is a guess): DialogDispatch -2, which pops the state
// from DialogMenuState and adjusts the menu's enable flags left on the
// stack under it (a modal dialog's menus). A no-op here.
void C_DialogMenuStateApply(int32_t state);PASCAL_SUBTRAP(DialogMenuStateApply, 0xAA68, 0x02FE, DialogDispatch);


} /* namespace Executor */